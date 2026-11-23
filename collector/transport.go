package main

import (
	"bytes"
	"context"
	"crypto/tls"
	"crypto/x509"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/url"
	"os"
)

type poster struct {
	client   *http.Client
	endpoint string
	token    string
}

func newPoster(cfg config) (*poster, error) {
	endpoint, err := url.Parse(cfg.Endpoint)
	if err != nil || endpoint.Host == "" || endpoint.User != nil || endpoint.RawQuery != "" || endpoint.Fragment != "" {
		return nil, errors.New("invalid Hub URL")
	}
	const snapshotPath = "/api/v2/snapshot"
	if endpoint.Path == "" || endpoint.Path == "/" {
		endpoint.Path = snapshotPath
	}
	if endpoint.Path != snapshotPath {
		return nil, fmt.Errorf("Hub URL path must be %s", snapshotPath)
	}
	if endpoint.Scheme != "https" {
		ip := net.ParseIP(endpoint.Hostname())
		loopback := endpoint.Hostname() == "localhost" || (ip != nil && ip.IsLoopback())
		if endpoint.Scheme != "http" || !cfg.AllowHTTP || !loopback {
			return nil, errors.New("HTTPS required; development HTTP needs allow_http_loopback and a loopback host")
		}
	}
	tlsConfig := &tls.Config{MinVersion: tls.VersionTLS12}
	if cfg.CA != "" {
		pem, err := os.ReadFile(cfg.CA)
		if err != nil {
			return nil, errors.New("cannot read CA certificate")
		}
		roots, err := x509.SystemCertPool()
		if err != nil {
			roots = x509.NewCertPool()
		}
		if !roots.AppendCertsFromPEM(pem) {
			return nil, errors.New("CA file has no PEM certificates")
		}
		tlsConfig.RootCAs = roots
	}
	transport := http.DefaultTransport.(*http.Transport).Clone()
	transport.TLSClientConfig = tlsConfig
	transport.MaxIdleConnsPerHost = 1
	return &poster{
		client: &http.Client{
			Transport: transport,
			Timeout:   cfg.Timeout,
			CheckRedirect: func(*http.Request, []*http.Request) error {
				return errors.New("Hub redirects are disabled")
			},
		},
		endpoint: endpoint.String(), token: cfg.Token,
	}, nil
}

func (p *poster) post(ctx context.Context, data []byte) error {
	request, err := http.NewRequestWithContext(ctx, http.MethodPost, p.endpoint, bytes.NewReader(data))
	if err != nil {
		return errors.New("cannot create Hub request")
	}
	request.Header.Set("Authorization", "Bearer "+p.token)
	request.Header.Set("Content-Type", "application/json")
	response, err := p.client.Do(request)
	if err != nil {
		return errors.New("Hub request failed (check connection, certificate and timeout)")
	}
	defer response.Body.Close()
	if response.StatusCode != http.StatusOK {
		io.Copy(io.Discard, io.LimitReader(response.Body, 4096))
		return fmt.Errorf("Hub rejected snapshot: HTTP %d", response.StatusCode)
	}
	data, err = io.ReadAll(io.LimitReader(response.Body, 4097))
	if err != nil || len(data) > 4096 {
		return errors.New("invalid Hub acknowledgement")
	}
	var ack struct {
		OK bool `json:"ok"`
	}
	if json.Unmarshal(data, &ack) != nil || !ack.OK {
		return errors.New("invalid Hub acknowledgement")
	}
	return nil
}

func (p *poster) close() { p.client.CloseIdleConnections() }

package main

import (
	"context"
	"crypto/x509"
	"encoding/pem"
	"io"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func serverCA(t *testing.T, server *httptest.Server) string {
	t.Helper()
	cert, err := x509.ParseCertificate(server.TLS.Certificates[0].Certificate[0])
	if err != nil {
		t.Fatal(err)
	}
	file := filepath.Join(t.TempDir(), "ca.pem")
	if err := os.WriteFile(file, pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: cert.Raw}), 0600); err != nil {
		t.Fatal(err)
	}
	return file
}

func TestHTTPSAuthenticationAndKeepAlive(t *testing.T) {
	var remote string
	var requests int
	server := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/api/v2/snapshot" || r.Method != "POST" || r.Header.Get("Authorization") != "Bearer test-secret" || r.Header.Get("Content-Type") != "application/json" {
			t.Error("incorrect HTTP contract")
		}
		data, _ := io.ReadAll(r.Body)
		if string(data) != `{"v":2}` {
			t.Errorf("body: %s", data)
		}
		if remote != "" && remote != r.RemoteAddr {
			t.Error("connection was not reused")
		}
		remote = r.RemoteAddr
		requests++
		io.WriteString(w, `{"ok":true}`)
	}))
	defer server.Close()
	cfg := config{Endpoint: server.URL, Token: "test-secret", CA: serverCA(t, server), Timeout: time.Second}
	p, err := newPoster(cfg)
	if err != nil {
		t.Fatal(err)
	}
	defer p.close()
	for i := 0; i < 2; i++ {
		if err := p.post(context.Background(), []byte(`{"v":2}`)); err != nil {
			t.Fatal(err)
		}
	}
	if requests != 2 {
		t.Fatal("missing requests")
	}
	for _, test := range []struct {
		name                             string
		verify, ca, wrongName, wantError bool
	}{
		{name: "default"},
		{name: "strict untrusted", verify: true, wantError: true},
		{name: "strict trusted", verify: true, ca: true},
		{name: "CA enables verification", ca: true},
		{name: "CA rejects hostname mismatch", ca: true, wrongName: true, wantError: true},
	} {
		t.Run(test.name, func(t *testing.T) {
			remote = ""
			cfg.VerifyTLS = test.verify
			cfg.CA = ""
			if test.ca {
				cfg.CA = serverCA(t, server)
			}
			p, err := newPoster(cfg)
			if err != nil {
				t.Fatal(err)
			}
			defer p.close()
			if test.wrongName {
				p.client.Transport.(*http.Transport).TLSClientConfig.ServerName = "wrong.example"
			}
			if err := p.post(context.Background(), []byte(`{"v":2}`)); (err != nil) != test.wantError {
				t.Fatalf("post error = %v, want error %v", err, test.wantError)
			}
		})
	}
}

func TestRejectPlaintextAndBadURLs(t *testing.T) {
	for _, endpoint := range []string{"http://192.0.2.1", "http://localhost", "https://user:pass@example.test", "https://example.test/wrong", "https://example.test?token=secret", "file:///tmp/hub"} {
		if _, err := newPoster(config{Endpoint: endpoint, Timeout: time.Second}); err == nil {
			t.Errorf("accepted %s", endpoint)
		}
	}
	if _, err := newPoster(config{Endpoint: "http://192.0.2.1", AllowHTTP: true}); err == nil {
		t.Fatal("allowed remote HTTP")
	}
	for _, endpoint := range []string{"http://127.0.0.1:8000", "http://[::1]:8000", "http://localhost:8000"} {
		p, err := newPoster(config{Endpoint: endpoint, AllowHTTP: true, Timeout: time.Second})
		if err != nil {
			t.Error(err)
		} else {
			p.close()
		}
	}
}

func TestHubErrorsAndRedirects(t *testing.T) {
	for _, status := range []int{400, 401, 413, 503, 302, 200} {
		t.Run(http.StatusText(status), func(t *testing.T) {
			server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
				w.Header().Set("Location", "http://127.0.0.1:1/private")
				w.WriteHeader(status)
				io.WriteString(w, `{"ok":false,"private":"secret"}`)
			}))
			defer server.Close()
			p, err := newPoster(config{Endpoint: server.URL, Token: "credential", AllowHTTP: true, Timeout: time.Second})
			if err != nil {
				t.Fatal(err)
			}
			defer p.close()
			err = p.post(context.Background(), []byte(`{}`))
			if err == nil {
				t.Fatal("accepted failure")
			}
			if strings.Contains(err.Error(), "secret") || strings.Contains(err.Error(), "credential") {
				t.Fatal("leaked server body or token")
			}
		})
	}
}

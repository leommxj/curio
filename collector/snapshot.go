package main

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"path"
	"sort"
	"strings"
	"unicode/utf8"
)

const maxSnapshotBytes = 16384

type Agent struct {
	ID      string `json:"id"`
	Name    string `json:"name"`
	Brand   string `json:"brand,omitempty"`
	Project string `json:"project"`
	State   string `json:"state"`
}

type nativeSession struct {
	Source string `json:"source"`
	Agent  string `json:"agent"`
	Kind   string `json:"kind"`
	Value  string `json:"value"`
}

type nativeAgent struct {
	TerminalID    string         `json:"terminal_id"`
	Agent         string         `json:"agent"`
	Status        string         `json:"agent_status"`
	CWD           string         `json:"cwd"`
	ForegroundCWD string         `json:"foreground_cwd"`
	PaneID        string         `json:"pane_id"`
	Session       *nativeSession `json:"agent_session"`
	DisplayAgent  string         `json:"display_agent"`
	Name          string         `json:"name"`
	Title         string         `json:"title"`
}

type mapOptions struct {
	Projects    map[string]bool
	Brands      map[string]bool
	Panes       map[string]bool
	Aliases     map[string]string
	ProjectMode string
	AllowTitles bool
}

func parseAgents(data []byte) ([]nativeAgent, error) {
	if !utf8.Valid(data) {
		return nil, errors.New("herdr response is not UTF-8")
	}
	var response struct {
		Result *struct {
			Type   string          `json:"type"`
			Agents json.RawMessage `json:"agents"`
		} `json:"result"`
		Error json.RawMessage `json:"error"`
	}
	if err := json.Unmarshal(data, &response); err != nil {
		return nil, errors.New("invalid herdr JSON response")
	}
	if len(response.Error) > 0 && string(response.Error) != "null" {
		return nil, errors.New("herdr returned an API error")
	}
	if response.Result == nil || response.Result.Type != "agent_list" {
		return nil, errors.New("herdr response is not agent_list")
	}
	if len(response.Result.Agents) == 0 || response.Result.Agents[0] != '[' {
		return nil, errors.New("herdr response has no agents array")
	}
	var agents []nativeAgent
	if err := json.Unmarshal(response.Result.Agents, &agents); err != nil {
		return nil, errors.New("invalid herdr agents")
	}
	for _, agent := range agents {
		if agent.TerminalID == "" || agent.Status == "" {
			return nil, errors.New("herdr agent lacks terminal_id or agent_status")
		}
	}
	return agents, nil
}

func shorten(s string, n int) string {
	if len(s) <= n {
		return s
	}
	s = s[:n]
	for !utf8.ValidString(s) {
		s = s[:len(s)-1]
	}
	return s
}

func projectName(a nativeAgent) string {
	cwd := a.ForegroundCWD
	if cwd == "" {
		cwd = a.CWD
	}
	if cwd == "" {
		return ""
	}
	name := path.Base(strings.ReplaceAll(cwd, "\\", "/"))
	if name == "." || name == "/" || strings.HasSuffix(name, ":") {
		return ""
	}
	return name
}

func agentID(a nativeAgent) string {
	if s := a.Session; s != nil && s.Source != "" && s.Agent != "" && s.Value != "" && (s.Kind == "id" || s.Kind == "path") {
		identity, _ := json.Marshal([]string{a.TerminalID, s.Source, s.Agent, s.Kind, s.Value})
		digest := sha256.Sum256(identity)
		return "session-" + hex.EncodeToString(digest[:])
	}
	if len(a.TerminalID) <= 96 {
		return a.TerminalID
	}
	digest := sha256.Sum256([]byte(a.TerminalID))
	return "terminal-" + hex.EncodeToString(digest[:])
}

func selected(allow map[string]bool, value string) bool {
	return len(allow) == 0 || allow[value]
}

func validWireString(value string, min, max int) bool {
	return len(value) >= min && len(value) <= max && utf8.ValidString(value) && !strings.ContainsRune(value, 0)
}

func mapAgents(native []nativeAgent, opts mapOptions) ([]Agent, error) {
	agents := make([]Agent, 0, len(native))
	ids := make(map[string]bool)
	for _, a := range native {
		brand := a.Agent
		if brand == "" && a.Session != nil {
			brand = a.Session.Agent
		}
		if brand == "" {
			brand = "unknown"
		}
		project := projectName(a)
		if !selected(opts.Projects, project) || !selected(opts.Brands, brand) || !selected(opts.Panes, a.PaneID) {
			continue
		}
		name := brand
		if opts.AllowTitles {
			for _, title := range []string{a.DisplayAgent, a.Name, a.Title} {
				if title != "" {
					name = title
					break
				}
			}
		}
		if alias, ok := opts.Aliases[project]; ok {
			project = alias
		}
		if opts.ProjectMode == "none" {
			project = ""
		}
		state := a.Status
		switch state {
		case "working", "blocked", "idle", "done", "unknown":
		default:
			state = "unknown"
		}
		id := agentID(a)
		if ids[id] {
			return nil, errors.New("duplicate herdr agent identity")
		}
		ids[id] = true
		mapped := Agent{ID: id, Name: shorten(name, 64), Brand: shorten(brand, 32), Project: shorten(project, 48), State: state}
		if !validWireString(mapped.ID, 1, 96) || !validWireString(mapped.Name, 1, 64) || !validWireString(mapped.Brand, 1, 32) || !validWireString(mapped.Project, 0, 48) {
			return nil, errors.New("mapped agent fields must be valid UTF-8 without NUL")
		}
		agents = append(agents, mapped)
	}
	if len(agents) > 16 {
		return nil, fmt.Errorf("%d selected agents exceed capacity 16; set allow_projects, allow_agents or allow_panes", len(agents))
	}
	sort.Slice(agents, func(i, j int) bool { return agents[i].ID < agents[j].ID })
	return agents, nil
}

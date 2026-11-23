package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"os"
	"strings"
	"testing"
	"unicode/utf8"
)

func fixtureAgents(t *testing.T) []nativeAgent {
	t.Helper()
	data, err := os.ReadFile("testdata/agent-list.json")
	if err != nil {
		t.Fatal(err)
	}
	agents, err := parseAgents(data)
	if err != nil {
		t.Fatal(err)
	}
	return agents
}

func TestActualSchemaMappingAndPrivacy(t *testing.T) {
	native := fixtureAgents(t)
	agents, err := mapAgents(native, mapOptions{})
	if err != nil {
		t.Fatal(err)
	}
	if len(agents) != 3 {
		t.Fatalf("agents: %v", agents)
	}
	byName := map[string]Agent{}
	for _, a := range agents {
		byName[a.Name] = a
		if a.Brand != a.Name {
			t.Fatalf("default brand differs: %+v", a)
		}
	}
	if byName["codex"].Project != "curio" || byName["codex"].State != "working" || byName["claude"].Project != "demo" || byName["unknown"].State != "unknown" {
		t.Fatalf("mapping: %+v", agents)
	}
	data, err := json.Marshal(agents)
	if err != nil {
		t.Fatal(err)
	}
	for _, private := range []string{"Private", "example", "native-session", "/home/", "Users"} {
		if bytes.Contains(data, []byte(private)) {
			t.Fatalf("disclosed %q", private)
		}
	}
	id := agentID(native[0])
	native[0].PaneID = "moved-pane"
	if id != agentID(native[0]) {
		t.Fatal("pane movement changed identity")
	}
	native[0].Session.Value = "new-session"
	if id == agentID(native[0]) {
		t.Fatal("new native session reused identity")
	}
	if len(agentID(native[0])) > 96 {
		t.Fatal("identity exceeds byte bound")
	}
}

func TestFilteringAliasesTitlesAndEmpty(t *testing.T) {
	native := fixtureAgents(t)
	agents, err := mapAgents(native, mapOptions{Projects: listSet([]string{"curio"}), Brands: listSet([]string{"codex"}), Panes: listSet([]string{"w1:p1"}), Aliases: map[string]string{"curio": "work"}, AllowTitles: true})
	if err != nil {
		t.Fatal(err)
	}
	if len(agents) != 1 || agents[0].Project != "work" || agents[0].Name != "Private customer title" || agents[0].Brand != "codex" {
		t.Fatalf("mapping: %+v", agents)
	}
	agents, err = mapAgents(native, mapOptions{Panes: listSet([]string{"missing"})})
	if err != nil || agents == nil || len(agents) != 0 {
		t.Fatalf("empty filtered list: %v %v", agents, err)
	}
	agents, err = mapAgents(native, mapOptions{ProjectMode: "none"})
	if err != nil {
		t.Fatal(err)
	}
	for _, a := range agents {
		if a.Project != "" {
			t.Fatal("project disclosure disabled")
		}
	}
	parsed, err := parseAgents([]byte(`{"result":{"type":"agent_list","agents":[]}}`))
	if err != nil || len(parsed) != 0 {
		t.Fatalf("valid empty list: %v", err)
	}
}

func TestMalformedSource(t *testing.T) {
	for _, input := range []string{`{`, `{}`, `{"result":{"type":"snapshot","agents":[]}}`, `{"result":{"type":"agent_list"}}`, `{"result":{"type":"agent_list","agents":null}}`, `{"result":{"type":"agent_list","agents":[{}]}}`, `{"result":{"type":"agent_list","agents":[null]}}`, `{"error":{"code":"offline","message":"private"}}`, "\xff"} {
		if _, err := parseAgents([]byte(input)); err == nil {
			t.Errorf("accepted %q", input)
		}
	}
}

func TestUTF8BoundsAndCapacity(t *testing.T) {
	native := []nativeAgent{{TerminalID: strings.Repeat("界", 50), Agent: strings.Repeat("界", 30), Status: "idle", CWD: "/x/" + strings.Repeat("界", 30)}}
	agents, err := mapAgents(native, mapOptions{})
	if err != nil {
		t.Fatal(err)
	}
	a := agents[0]
	if !utf8.ValidString(a.Name) || !utf8.ValidString(a.Brand) || len(a.Brand) != 30 || !utf8.ValidString(a.Project) || len(a.Name) > 64 || len(a.Project) > 48 || len(a.ID) > 96 {
		t.Fatalf("bounds: %+v", a)
	}
	for i := 0; i < 17; i++ {
		native = append(native, nativeAgent{TerminalID: fmt.Sprint(i), Agent: "codex", Status: "idle"})
	}
	if _, err := mapAgents(native, mapOptions{}); err == nil {
		t.Fatal("silently accepted >16 agents")
	}
	if _, err := mapAgents([]nativeAgent{native[0], native[0]}, mapOptions{}); err == nil {
		t.Fatal("accepted duplicate IDs")
	}
	data, err := json.Marshal(declaration(sourceConfig{Module: "herdr"}))
	if err != nil {
		t.Fatal(err)
	}
	var payload map[string]any
	if json.Unmarshal(data, &payload) != nil || payload["agents"] == nil {
		t.Fatal("empty array serialized as null")
	}
}

func TestRejectNULInMappedFields(t *testing.T) {
	for _, field := range []string{"terminal_id", "agent", "cwd", "display_agent"} {
		t.Run(field, func(t *testing.T) {
			input := map[string]string{"terminal_id": "term", "agent": "codex", "agent_status": "idle", "cwd": "/work/curio"}
			input[field] = "before\x00after"
			agent, err := json.Marshal(input)
			if err != nil {
				t.Fatal(err)
			}
			native, err := parseAgents(append(append([]byte(`{"result":{"type":"agent_list","agents":[`), agent...), []byte(`]}}`)...))
			if err != nil {
				t.Fatal(err)
			}
			if _, err := mapAgents(native, mapOptions{AllowTitles: true}); err == nil {
				t.Fatal("accepted NUL in mapped wire field")
			}
		})
	}
	for _, field := range []string{"ID", "Name", "Project"} {
		t.Run("invalid UTF-8 "+field, func(t *testing.T) {
			a := nativeAgent{TerminalID: "term", Agent: "codex", Status: "idle", CWD: "/work/curio"}
			switch field {
			case "ID":
				a.TerminalID = "\xff"
			case "Name":
				a.Agent = "\xff"
			case "Project":
				a.CWD = "/work/\xff"
			}
			if _, err := mapAgents([]nativeAgent{a}, mapOptions{}); err == nil {
				t.Fatal("accepted invalid UTF-8")
			}
		})
	}
	a := nativeAgent{TerminalID: "term", Agent: "codex", Status: "idle", Session: &nativeSession{Source: "herdr:codex", Agent: "codex", Kind: "id", Value: "session\x00value"}}
	if _, err := mapAgents([]nativeAgent{a}, mapOptions{}); err != nil {
		t.Fatalf("safe digest rejected: %v", err)
	}
}

func TestBrandIndependentOfTitleAndOptionalWireField(t *testing.T) {
	for _, titles := range []bool{false, true} {
		native := []nativeAgent{{TerminalID: "term", Status: "idle", Session: &nativeSession{Agent: "pi"}, DisplayAgent: "Private title", Name: "Private name", Title: "Private fallback", CWD: "/private/path/project"}}
		agents, err := mapAgents(native, mapOptions{AllowTitles: titles, ProjectMode: "none"})
		if err != nil {
			t.Fatal(err)
		}
		if agents[0].Brand != "pi" {
			t.Fatal("session brand missing", agents)
		}
		expected := "pi"
		if titles {
			expected = "Private title"
		}
		if agents[0].Name != expected {
			t.Fatal(agents)
		}
		data, err := json.Marshal(agents)
		if err != nil {
			t.Fatal(err)
		}
		if bytes.Contains(data, []byte("/private/")) || (!titles && bytes.Contains(data, []byte("Private"))) {
			t.Fatal("private data disclosed")
		}
	}
	data, err := json.Marshal(Agent{ID: "old", Name: "codex", State: "idle"})
	if err != nil {
		t.Fatal(err)
	}
	if bytes.Contains(data, []byte(`"brand"`)) {
		t.Fatal("empty optional brand serialized")
	}
	native := []nativeAgent{{TerminalID: "term", Agent: "bad\x00brand", DisplayAgent: "valid title", Status: "idle"}}
	if _, err := mapAgents(native, mapOptions{AllowTitles: true}); err == nil {
		t.Fatal("invalid brand accepted with valid title")
	}
}

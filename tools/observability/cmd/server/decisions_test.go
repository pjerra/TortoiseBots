package main

import (
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"tortoise-observability/internal/auth"
	"tortoise-observability/internal/decisions"
)

// mirrorAuth is the requireAuth gate of main (no session cookie on an /api/
// path -> 401 JSON); the decisions routes are registered behind the real one
// in main, and this test proves the handlers behave behind such a gate: no
// login -> 401 without reaching the handler, login -> the handler's answer.
func mirrorAuth(svc *auth.Service, next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if _, err := svc.GetSessionFromRequest(r); err != nil {
			if strings.HasPrefix(r.URL.Path, "/api/") {
				w.Header().Set("Content-Type", "application/json")
				w.WriteHeader(http.StatusUnauthorized)
				_ = json.NewEncoder(w).Encode(map[string]string{"error": "Unauthorized: GM session required"})
				return
			}
			http.Redirect(w, r, "/login", http.StatusFound)
			return
		}
		next(w, r)
	}
}

func decisionsMux(t *testing.T, s *decisions.Store) (*httptest.Server, *auth.Service) {
	t.Helper()
	svc, err := auth.NewService(auth.Config{SecretKey: "decisions-test-secret"})
	if err != nil {
		t.Fatalf("auth.NewService: %v", err)
	}
	mux := http.NewServeMux()
	mux.HandleFunc(decisions.ListPath, mirrorAuth(svc, s.HandleList))
	mux.HandleFunc(decisions.FilePrefix, mirrorAuth(svc, s.HandleFile))
	srv := httptest.NewServer(mux)
	t.Cleanup(srv.Close)
	return srv, svc
}

func login(t *testing.T, srv *httptest.Server, svc *auth.Service) string {
	t.Helper()
	token := svc.CreateSessionToken(&auth.SessionData{AccountID: 1, Username: "gm", Rank: 3, ExpiresAt: 4102444800})
	req, err := http.NewRequest(http.MethodGet, srv.URL+decisions.ListPath, nil)
	if err != nil {
		t.Fatal(err)
	}
	req.AddCookie(&http.Cookie{Name: "tbm_session", Value: token})
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		t.Fatalf("login probe: %d, want 200", resp.StatusCode)
	}
	return token
}

func getAs(t *testing.T, srv *httptest.Server, token, path string) *http.Response {
	t.Helper()
	req, err := http.NewRequest(http.MethodGet, srv.URL+path, nil)
	if err != nil {
		t.Fatal(err)
	}
	if token != "" {
		req.AddCookie(&http.Cookie{Name: "tbm_session", Value: token})
	}
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	return resp
}

func TestDecisionsNotLoggedInIs401(t *testing.T) {
	dir := t.TempDir()
	id := "20261102T161939Z-zg-Fionaa"
	folder := filepath.Join(dir, id)
	if err := os.MkdirAll(folder, 0o755); err != nil {
		t.Fatal(err)
	}
	body := `{"format":"tb-decision-capture","v":1,"id":"` + id + `","commander":"Fionaa","plan":"zg","map":309,"started":"2026-11-02T16:19:39Z","startedUnixMs":0,"ended":null,"endReason":"","schema":"abcd","segments":[],"dropped":0}`
	if err := os.WriteFile(filepath.Join(folder, "index.json"), []byte(body), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(folder, "seg-0001.jsonl"), []byte("{\"k\":\"hdr\"}\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	s, err := decisions.New(dir)
	if err != nil {
		t.Fatal(err)
	}
	srv, _ := decisionsMux(t, s)
	for _, path := range []string{
		decisions.ListPath,
		decisions.FilePrefix + id + "/index.json",
		decisions.FilePrefix + id + "/seg-0001.jsonl",
	} {
		resp := getAs(t, srv, "", path)
		resp.Body.Close()
		if resp.StatusCode != http.StatusUnauthorized {
			t.Errorf("GET %s without login -> %d, want 401", path, resp.StatusCode)
		}
	}
}

func TestDecisionsLoginSeesListAndFile(t *testing.T) {
	dir := t.TempDir()
	id := "20261102T161939Z-zg-Fionaa"
	folder := filepath.Join(dir, id)
	if err := os.MkdirAll(folder, 0o755); err != nil {
		t.Fatal(err)
	}
	body := `{"format":"tb-decision-capture","v":1,"id":"` + id + `","commander":"Fionaa","plan":"zg","map":309,"started":"2026-11-02T16:19:39Z","startedUnixMs":0,"ended":null,"endReason":"","schema":"abcd","segments":[],"dropped":0}`
	if err := os.WriteFile(filepath.Join(folder, "index.json"), []byte(body), 0o644); err != nil {
		t.Fatal(err)
	}
	seg := "{\"k\":\"hdr\"}\n"
	if err := os.WriteFile(filepath.Join(folder, "seg-0001.jsonl"), []byte(seg), 0o644); err != nil {
		t.Fatal(err)
	}
	s, err := decisions.New(dir)
	if err != nil {
		t.Fatal(err)
	}
	srv, svc := decisionsMux(t, s)
	token := login(t, srv, svc)

	resp := getAs(t, srv, token, decisions.ListPath+"?commander=Fionaa")
	var payload struct {
		Captures []json.RawMessage `json:"captures"`
	}
	if err := json.NewDecoder(resp.Body).Decode(&payload); err != nil {
		resp.Body.Close()
		t.Fatalf("list decode: %v", err)
	}
	resp.Body.Close()
	if resp.StatusCode != http.StatusOK || len(payload.Captures) != 1 {
		t.Fatalf("list -> %d with %d captures, want 200 with 1", resp.StatusCode, len(payload.Captures))
	}

	resp = getAs(t, srv, token, decisions.FilePrefix+id+"/seg-0001.jsonl")
	got, err := io.ReadAll(resp.Body)
	resp.Body.Close()
	if err != nil {
		t.Fatal(err)
	}
	if resp.StatusCode != http.StatusOK || string(got) != seg {
		t.Fatalf("file -> %d %q, want 200 %q", resp.StatusCode, got, seg)
	}
	if got := resp.Header.Get("Content-Length"); got != "12" {
		t.Fatalf("Content-Length = %q, want 12", got)
	}
}

func TestDecisionsDisabledProbe(t *testing.T) {
	srv, svc := decisionsMux(t, nil)
	token := svc.CreateSessionToken(&auth.SessionData{AccountID: 1, Username: "gm", Rank: 3, ExpiresAt: 4102444800})
	for _, path := range []string{
		decisions.ListPath,
		decisions.FilePrefix + "20261102T161939Z-zg-Fionaa/index.json",
	} {
		resp := getAs(t, srv, token, path)
		var payload struct {
			Error string `json:"error"`
		}
		if err := json.NewDecoder(resp.Body).Decode(&payload); err != nil {
			resp.Body.Close()
			t.Fatalf("GET %s decode: %v", path, err)
		}
		resp.Body.Close()
		if resp.StatusCode != http.StatusNotFound || payload.Error != "decision capture folder not configured" {
			t.Errorf("GET %s -> %d %q, want 404 with the disabled JSON", path, resp.StatusCode, payload.Error)
		}
	}
}

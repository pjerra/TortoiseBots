package main

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"os"
	"strings"
	"testing"

	"tortoise-observability/web"
)

// TestUIMetaInjected: the server default reaches the page head before the switch
// script runs, so the first paint already has the right skin; the meta pair is
// only added once.
func TestUIMetaInjected(t *testing.T) {
	page, err := web.FS.ReadFile("index.html")
	if err != nil {
		t.Fatal(err)
	}
	out := string(injectUIMeta(page, "v2", true))
	metaAt := strings.Index(out, `<meta name="tbdash-ui" content="v2">`)
	scriptAt := strings.Index(out, "/static/js/uiv2.js")
	if metaAt < 0 {
		t.Fatal("the v2 default was not injected")
	}
	if scriptAt < 0 || metaAt > scriptAt {
		t.Fatalf("the meta must precede the switch script (meta %d, script %d)", metaAt, scriptAt)
	}
	if !strings.Contains(out, `<meta name="tbdash-ui-3d" content="1">`) {
		t.Error("the 3D flag was not injected")
	}
	if off := string(injectUIMeta(page, "v1", false)); !strings.Contains(off, `<meta name="tbdash-ui-3d" content="0">`) {
		t.Error("an off 3D flag must be written as 0")
	}
	if again := string(injectUIMeta([]byte(out), "v1", false)); again != out {
		t.Error("a second injection must leave the page alone")
	}
	if !strings.Contains(out, "<head>") {
		t.Error("the head tag was lost")
	}
}

func TestUIInfoHandler(t *testing.T) {
	rec := httptest.NewRecorder()
	uiInfoHandler("v2", true)(rec, httptest.NewRequest(http.MethodGet, "/api/v1/ui", nil))
	if ct := rec.Header().Get("Content-Type"); !strings.Contains(ct, "application/json") {
		t.Errorf("Content-Type = %q", ct)
	}
	var got struct {
		Default string `json:"default"`
		Model3D bool   `json:"model3d"`
	}
	if err := json.Unmarshal(rec.Body.Bytes(), &got); err != nil {
		t.Fatal(err)
	}
	if got.Default != "v2" || !got.Model3D {
		t.Fatalf("body = %s", rec.Body.String())
	}
	rec = httptest.NewRecorder()
	uiInfoHandler("v1", false)(rec, httptest.NewRequest(http.MethodGet, "/api/v1/ui", nil))
	if body := rec.Body.String(); !strings.Contains(body, `"default":"v1"`) || !strings.Contains(body, `"model3d":false`) {
		t.Fatalf("v1 body = %s", body)
	}
}

// TestUISwitchWired pins the flag/env pair, the route behind the session gate and
// the injected meta, all of which live in main().
func TestUISwitchWired(t *testing.T) {
	src, err := os.ReadFile("main.go")
	if err != nil {
		t.Fatal(err)
	}
	for _, want := range []string{
		`flag.String("ui", getEnv("TBDASH_UI", "v2")`,
		`flag.Bool("ui-3d", getEnv("TBDASH_UI_3D", "") == "1"`,
		`mux.HandleFunc("GET /api/v1/ui", requireAuth(uiInfoHandler(*uiDefault, *ui3d)))`,
		"injectUIMeta(indexHTML, *uiDefault, *ui3d)",
	} {
		if !strings.Contains(string(src), want) {
			t.Errorf("main.go does not contain %q", want)
		}
	}
	if got := normalizeUI(" V2 "); got != "v2" {
		t.Errorf("normalizeUI = %q", got)
	}
	page, err := web.FS.ReadFile("index.html")
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(page), `id="ui-toggle"`) {
		t.Error("index.html has no UI toggle button")
	}
}

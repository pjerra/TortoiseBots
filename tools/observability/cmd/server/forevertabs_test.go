package main

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"os"
	"strings"
	"testing"
)

func TestCoreInfoPerCore(t *testing.T) {
	cases := []struct {
		core string
		want string
	}{
		{"forever", `{"core":"forever","ported":{"raids":false,"coverage":false,"commander_orders":true,"combat":true}}`},
		{"turtle", `{"core":"turtle","ported":{"raids":true,"coverage":true,"commander_orders":true,"combat":true}}`},
		{"", `{"core":"turtle","ported":{"raids":true,"coverage":true,"commander_orders":true,"combat":true}}`},
	}
	for _, c := range cases {
		rec := httptest.NewRecorder()
		coreHandler(c.core)(rec, httptest.NewRequest(http.MethodGet, "/api/v1/core", nil))
		if rec.Code != http.StatusOK {
			t.Fatalf("%q: status %d", c.core, rec.Code)
		}
		var got, want any
		if err := json.Unmarshal(rec.Body.Bytes(), &got); err != nil {
			t.Fatalf("%q: %v: %s", c.core, err, rec.Body.String())
		}
		_ = json.Unmarshal([]byte(c.want), &want)
		g, _ := json.Marshal(got)
		w, _ := json.Marshal(want)
		if string(g) != string(w) {
			t.Errorf("core %q: got %s, want %s", c.core, g, w)
		}
	}
}

// TestCoreRouteWired guards the route: behind the GM session like every API route, fed the -core flag.
func TestCoreRouteWired(t *testing.T) {
	src, err := os.ReadFile("main.go")
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(src), `mux.HandleFunc("GET /api/v1/core", requireAuth(coreHandler(*core)))`) {
		t.Error("main.go does not serve GET /api/v1/core behind requireAuth with the -core flag")
	}
}

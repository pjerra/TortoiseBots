package main

import (
	"net/http"
	"net/http/httptest"
	"os"
	"strings"
	"testing"
	"time"

	"tortoise-observability/internal/auth"
)

// sessionCookieCleared reports whether any Set-Cookie header on the response
// clears the session cookie (empty value, already expired).
func sessionCookieCleared(resp *http.Response) bool {
	for _, c := range resp.Cookies() {
		expired := c.MaxAge < 0 || (!c.Expires.IsZero() && c.Expires.Before(time.Now()))
		if c.Name == "tbm_session" && c.Value == "" && expired {
			return true
		}
	}
	return false
}

func newLogoutTestServer(t *testing.T) *httptest.Server {
	t.Helper()
	svc, err := auth.NewService(auth.Config{})
	if err != nil {
		t.Fatalf("auth.NewService: %v", err)
	}
	mux := http.NewServeMux()
	mux.HandleFunc("/api/v1/auth/logout", logoutHandler(svc))
	srv := httptest.NewServer(mux)
	t.Cleanup(srv.Close)
	return srv
}

// noRedirect keeps the client from following the 302 so the test sees the raw response.
var noRedirect = &http.Client{
	CheckRedirect: func(*http.Request, []*http.Request) error { return http.ErrUseLastResponse },
}

func TestLogoutRejectsNonPOST(t *testing.T) {
	srv := newLogoutTestServer(t)
	for _, method := range []string{http.MethodGet, http.MethodHead} {
		req, err := http.NewRequest(method, srv.URL+"/api/v1/auth/logout", nil)
		if err != nil {
			t.Fatal(err)
		}
		resp, err := noRedirect.Do(req)
		if err != nil {
			t.Fatalf("%s: %v", method, err)
		}
		resp.Body.Close()
		if resp.StatusCode != http.StatusMethodNotAllowed {
			t.Errorf("%s: status = %d, want 405", method, resp.StatusCode)
		}
		if got := resp.Header.Get("Allow"); got != http.MethodPost {
			t.Errorf("%s: Allow = %q, want %q", method, got, http.MethodPost)
		}
		if sessionCookieCleared(resp) {
			t.Errorf("%s: response clears the session cookie; a non-POST must not log out", method)
		}
	}
}

func TestLogoutPOSTClearsCookieAndReturnsNoContent(t *testing.T) {
	srv := newLogoutTestServer(t)
	req, err := http.NewRequest(http.MethodPost, srv.URL+"/api/v1/auth/logout", nil)
	if err != nil {
		t.Fatal(err)
	}
	resp, err := noRedirect.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	resp.Body.Close()
	if resp.StatusCode != http.StatusNoContent {
		t.Errorf("POST: status = %d, want 204", resp.StatusCode)
	}
	if loc := resp.Header.Get("Location"); loc != "" {
		t.Errorf("POST: Location = %q, want no redirect from the server", loc)
	}
	if !sessionCookieCleared(resp) {
		t.Errorf("POST: response does not clear the session cookie; Set-Cookie = %v", resp.Header.Values("Set-Cookie"))
	}
}

func TestDashboardLogoutIsPOSTButton(t *testing.T) {
	raw, err := os.ReadFile("../../web/index.html")
	if err != nil {
		t.Fatalf("read index.html: %v", err)
	}
	html := string(raw)
	if strings.Contains(html, `href="/api/v1/auth/logout"`) {
		t.Error(`index.html still links to /api/v1/auth/logout with href; a GET must not log out`)
	}
	if !strings.Contains(html, `id="logoutBtn"`) {
		t.Error(`index.html has no id="logoutBtn" button for the POST logout`)
	}
}

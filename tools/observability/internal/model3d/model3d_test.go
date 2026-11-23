package model3d

import (
	"errors"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"
)

type failingTransport struct{}

func (failingTransport) RoundTrip(*http.Request) (*http.Response, error) {
	return nil, errors.New("no route to host")
}

func TestAllowedAsset(t *testing.T) {
	allowed := []string{
		"classicplus/meta/character/1.json",
		"classicplus/meta/character/1011653.json",
		"classicplus/meta/charactercustomization/1.json",
		"classicplus/meta/charactercustomization/18.json",
		"classicplus/meta/charactercustomization2/1_0.json",
		"classicplus/meta/armor/5/35276.json",
		"classicplus/meta/item/31735.json",
		"classicplus/meta/itemvisual/35276.json",
		"classicplus/m2/1011653.m2",
		"classicplus/skin/12345.skin",
		"classicplus/skin/12345_0.skin",
		"classicplus/bone/1011653.bone",
		"classicplus/anim/0.anim",
		"classicplus/phys/1011653.phys",
		"classicplus/skel/1011653.skel",
		"classicplus/textures/152067.webp",
		"classicplus/textures/152067.png",
	}
	for _, p := range allowed {
		if !allowedAsset(p) {
			t.Errorf("allowedAsset(%q) = false, want true", p)
		}
	}

	rejected := []string{
		"",
		"classicplus/meta/character/1.json?x=1",
		"classicplus/meta/character/1.txt",
		"classicplus/meta/character/../1.json",
		"classicplus/../../etc/passwd",
		"classicplus/meta/character/1/1.json",
		"classicplus/m2/abc.m2",
		"classicplus/m2/1011653.exe",
		"classicplus/deployment/viewer/1bf640d/viewer.min.js",
		"classicplus/textures/tile/152067.webp",
		"classicplus/textures/152067.webp.exe",
		"live/meta/character/1.json",
		"https://wow.zamimg.com/modelviewer/classicplus/meta/character/1.json",
		"classicplus\\meta\\character\\1.json",
		"classicplus//meta/character/1.json",
	}
	for _, p := range rejected {
		if allowedAsset(p) {
			t.Errorf("allowedAsset(%q) = true, want false", p)
		}
	}
}

func TestProxyEnabled(t *testing.T) {
	off := New(Config{})
	if off.Enabled() {
		t.Fatal("disabled proxy reports enabled")
	}
	on := New(Config{Enabled: true})
	if !on.Enabled() {
		t.Fatal("enabled proxy reports disabled")
	}
}

func TestContentPath(t *testing.T) {
	if got, want := ContentPath(), "/api/v1/model3d/asset/"; got != want {
		t.Fatalf("ContentPath() = %q, want %q", got, want)
	}
}

func TestAssetFailureIsShort(t *testing.T) {
	p := New(Config{
		Enabled:  true,
		CacheDir: t.TempDir(),
		Client:   &http.Client{Transport: failingTransport{}},
	})
	req := httptest.NewRequest(http.MethodGet, "/api/v1/model3d/asset/classicplus/meta/character/1.json", nil)
	req.SetPathValue("path", "classicplus/meta/character/1.json")
	w := httptest.NewRecorder()
	start := time.Now()
	p.HandleAsset(w, req)
	if w.Code != http.StatusBadGateway {
		t.Fatalf("status = %d, want 502", w.Code)
	}
	if elapsed := time.Since(start); elapsed > time.Second {
		t.Fatalf("dead CDN held the request for %v", elapsed)
	}
}

func TestDisabledHandlers(t *testing.T) {
	p := New(Config{CacheDir: t.TempDir()})
	for name, handle := range map[string]http.HandlerFunc{"asset": p.HandleAsset, "viewer": p.HandleViewer} {
		req := httptest.NewRequest(http.MethodGet, "/", nil)
		req.SetPathValue("path", "classicplus/meta/character/1.json")
		w := httptest.NewRecorder()
		handle(w, req)
		if w.Code != http.StatusNotFound {
			t.Errorf("%s: status = %d, want 404", name, w.Code)
		}
	}
}

func TestFetchDeadline(t *testing.T) {
	cases := map[string]time.Duration{
		"https://host/classicplus/meta/character/1.json":             time.Second * 6,
		"https://host/classicplus/deployment/viewer/1/viewer.min.js": time.Second * 6,
		"https://host/classicplus/textures/1.webp":                   time.Second * 12,
		"https://host/classicplus/m2/1011653.m2":                     time.Second * 45,
	}
	for url, want := range cases {
		if got := fetchDeadline(url); got != want {
			t.Errorf("fetchDeadline(%q) = %v, want %v", url, got, want)
		}
	}
}

// Package model3d proxies the Wowhead model viewer's assets for the dashboard's
// optional 3D character preview. The viewer itself is loaded at runtime from
// Wowhead's public deployment path; the browser cannot fetch its assets
// cross-origin (wow.zamimg.com answers an Origin request with 403 and sends no
// CORS header), so the daemon fetches them same-origin, caches them on disk and
// serves only an allowlist of modelviewer paths.
package model3d

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"path"
	"path/filepath"
	"regexp"
	"strings"
	"sync"
	"time"
)

const (
	// contentEnv is the Wowhead content tree that matches Forever's character
	// customization ids (classic meta uses the old 1.12 option ids, live uses
	// retail item visuals and loses classic weapon appearances).
	contentEnv = "classicplus"

	origin = "https://wow.zamimg.com/modelviewer/"

	maxAssetBytes = 48 << 20

	// viewerVersion is a known-good deployment hash. It is tried first; when
	// Wowhead rotates it the page's modelViewerVersion is read once instead.
	viewerVersion = "1bf640d"
)

// assetPathRe is the complete set of paths the viewer may ask for: one
// character/appearance metadata tree and the model files it references.
var assetPathRe = regexp.MustCompile(`^` + contentEnv + `/(?:` +
	`meta/character/[0-9]{1,10}\.json` +
	`|meta/charactercustomization2?/[0-9]{1,10}(?:_[0-9]{1,10})?\.json` +
	`|meta/armor/[0-9]{1,4}/[0-9]{1,10}\.json` +
	`|meta/(?:item|itemvisual)/[0-9]{1,10}\.json` +
	`|(?:m2|skin|bone|anim|phys|skel)/[0-9]{1,12}(?:_[0-9]{1,8})?\.(?:m2|skin|bone|anim|phys|skel)` +
	`|textures/[0-9]{1,12}\.(?:webp|png|jpg)` +
	`)$`)

var viewerVersionRe = regexp.MustCompile(`"classicplus":"([0-9a-f]{6,40})"`)

// allowedAsset reports whether p is a modelviewer path this daemon may fetch.
func allowedAsset(p string) bool {
	if len(p) > 200 || strings.Contains(p, "..") || strings.Contains(p, `\`) || strings.Contains(p, "//") {
		return false
	}
	return assetPathRe.MatchString(p)
}

// Config configures the proxy. Enabled is the -ui-3d switch; CacheDir empty
// means <os.UserCacheDir()>/tbdash-model3d.
type Config struct {
	Enabled  bool
	CacheDir string
	Client   *http.Client
}

// Proxy serves the allowlisted assets and the viewer script.
type Proxy struct {
	enabled bool
	dir     string
	client  *http.Client

	viewerMu  sync.Mutex
	viewerJS  []byte
	viewerAt  time.Time
	version   string
	versionAt time.Time
}

// New builds the proxy. A nil client gets a 60 s ceiling; each fetch also has
// the shorter per-kind deadline below.
func New(cfg Config) *Proxy {
	dir := cfg.CacheDir
	if dir == "" {
		if base, err := os.UserCacheDir(); err == nil && base != "" {
			dir = filepath.Join(base, "tbdash-model3d")
		} else {
			dir = filepath.Join(os.TempDir(), "tbdash-model3d")
		}
	}
	client := cfg.Client
	if client == nil {
		// Per-request deadlines below are the real limit; this is the ceiling
		// for a transport that ignores its context.
		client = &http.Client{Timeout: 60 * time.Second}
	}
	return &Proxy{enabled: cfg.Enabled, dir: dir, client: client}
}

// Enabled reports whether the proxy is on; the handlers 404 when it is off.
func (p *Proxy) Enabled() bool { return p != nil && p.enabled }

// ContentPath is the public prefix the viewer adds asset paths to.
func ContentPath() string { return "/api/v1/model3d/asset/" }

// HandleAsset serves GET /api/v1/model3d/asset/{path...}.
func (p *Proxy) HandleAsset(w http.ResponseWriter, r *http.Request) {
	if !p.Enabled() {
		http.NotFound(w, r)
		return
	}
	rel := r.PathValue("path")
	if !allowedAsset(rel) {
		http.Error(w, "not a model asset", http.StatusBadRequest)
		return
	}
	ctx := r.Context()
	data, err := p.cached(rel, func() ([]byte, error) {
		return p.fetch(ctx, origin+rel)
	})
	if err != nil {
		p.logFetch(rel, err)
		http.Error(w, "model asset unavailable", http.StatusBadGateway)
		return
	}
	w.Header().Set("Content-Type", contentType(rel))
	w.Header().Set("X-Content-Type-Options", "nosniff")
	w.Header().Set("Cache-Control", "public, max-age=31536000, immutable")
	_, _ = w.Write(data)
}

// HandleViewer serves the Wowhead viewer script for the classicplus content
// tree, so the browser loads it from this origin and no third-party script URL
// has to be pinned in the page.
func (p *Proxy) HandleViewer(w http.ResponseWriter, r *http.Request) {
	if !p.Enabled() {
		http.NotFound(w, r)
		return
	}
	p.viewerMu.Lock()
	cached, at := p.viewerJS, p.viewerAt
	p.viewerMu.Unlock()
	if cached != nil && time.Since(at) < 6*time.Hour {
		serveJS(w, cached)
		return
	}
	data, err := p.fetchViewer(r)
	if err != nil {
		if cached != nil {
			serveJS(w, cached)
			return
		}
		log.Printf("[Model3D] viewer script: %v", err)
		http.Error(w, "model viewer unavailable", http.StatusBadGateway)
		return
	}
	p.viewerMu.Lock()
	p.viewerJS, p.viewerAt = data, time.Now()
	p.viewerMu.Unlock()
	serveJS(w, data)
}

func serveJS(w http.ResponseWriter, data []byte) {
	w.Header().Set("Content-Type", "text/javascript; charset=utf-8")
	w.Header().Set("X-Content-Type-Options", "nosniff")
	w.Header().Set("Cache-Control", "public, max-age=3600")
	_, _ = w.Write(data)
}

func (p *Proxy) fetchViewer(r *http.Request) ([]byte, error) {
	version := p.currentVersion()
	data, err := p.fetch(r.Context(), origin+contentEnv+"/deployment/viewer/"+version+"/viewer.min.js")
	if err == nil {
		return data, nil
	}
	// The pinned/reported hash went stale: read the current one from Wowhead's
	// page meta and retry once.
	next, derr := p.discoverVersion(r.Context())
	if derr != nil || next == version {
		return nil, err
	}
	return p.fetch(r.Context(), origin+contentEnv+"/deployment/viewer/"+next+"/viewer.min.js")
}

func (p *Proxy) currentVersion() string {
	p.viewerMu.Lock()
	defer p.viewerMu.Unlock()
	if p.version != "" && time.Since(p.versionAt) < 6*time.Hour {
		return p.version
	}
	return viewerVersion
}

func (p *Proxy) discoverVersion(ctx context.Context) (string, error) {
	ctx, cancel := context.WithTimeout(ctx, 6*time.Second)
	defer cancel()
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, "https://www.wowhead.com/classic/", nil)
	if err != nil {
		return "", err
	}
	req.Header.Set("User-Agent", "tortoise-observability/1.0 (dashboard 3D preview)")
	resp, err := p.client.Do(req)
	if err != nil {
		return "", err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return "", fmt.Errorf("wowhead page: HTTP %d", resp.StatusCode)
	}
	body, err := io.ReadAll(io.LimitReader(resp.Body, 4<<20))
	if err != nil {
		return "", err
	}
	m := viewerVersionRe.FindSubmatch(body)
	if len(m) != 2 {
		return "", errors.New("model viewer version not found")
	}
	v := string(m[1])
	p.viewerMu.Lock()
	p.version, p.versionAt = v, time.Now()
	p.viewerMu.Unlock()
	return v, nil
}

// fetchDeadline keeps a dead CDN from holding a viewer request open: small
// metadata is answered in seconds, model files get a little longer.
func fetchDeadline(rel string) time.Duration {
	switch path.Ext(rel) {
	case ".json", ".js":
		return 6 * time.Second
	case ".webp", ".png", ".jpg":
		return 12 * time.Second
	default:
		return 45 * time.Second
	}
}

func (p *Proxy) fetch(ctx context.Context, url string) ([]byte, error) {
	ctx, cancel := context.WithTimeout(ctx, fetchDeadline(url))
	defer cancel()
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, url, nil)
	if err != nil {
		return nil, err
	}
	req.Header.Set("User-Agent", "tortoise-observability/1.0 (dashboard 3D preview)")
	resp, err := p.client.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("HTTP %d", resp.StatusCode)
	}
	if resp.ContentLength > maxAssetBytes {
		return nil, fmt.Errorf("asset too large (%d bytes)", resp.ContentLength)
	}
	data, err := io.ReadAll(io.LimitReader(resp.Body, maxAssetBytes+1))
	if err != nil {
		return nil, err
	}
	if len(data) > maxAssetBytes {
		return nil, fmt.Errorf("asset larger than %d bytes", maxAssetBytes)
	}
	return data, nil
}

func (p *Proxy) cached(rel string, load func() ([]byte, error)) ([]byte, error) {
	sum := sha256.Sum256([]byte(rel))
	file := filepath.Join(p.dir, hex.EncodeToString(sum[:8])+path.Ext(rel))
	if data, err := os.ReadFile(file); err == nil {
		return data, nil
	}
	data, err := load()
	if err != nil {
		return nil, err
	}
	if err := os.MkdirAll(p.dir, 0o755); err != nil {
		return data, nil
	}
	tmp := file + ".part"
	if err := os.WriteFile(tmp, data, 0o644); err == nil {
		if err := os.Rename(tmp, file); err != nil {
			_ = os.Remove(tmp)
		}
	}
	return data, nil
}

func (p *Proxy) logFetch(rel string, err error) {
	// One missing appearance file is normal: Wowhead has no metadata for some
	// custom-era items. Log the path, not a retry stack.
	log.Printf("[Model3D] %s: %v", rel, err)
}

func contentType(rel string) string {
	switch path.Ext(rel) {
	case ".json":
		return "application/json"
	case ".png":
		return "image/png"
	case ".jpg":
		return "image/jpeg"
	case ".webp":
		return "image/webp"
	default:
		return "application/octet-stream"
	}
}

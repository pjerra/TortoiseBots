package mapdir

import (
	"encoding/json"
	"math"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"unicode/utf8"
)

// zf is turtle-agent's minimap-extract JSON for Zul'Farrak (map 209), as written.
const zf = `{"map":209,"name":"Zul'Farrak","source":"worldmap","width":1002,"height":668,"px_per_yard":0.7243390938342229,
"north":2052.080078125,"south":1129.1700439453125,"west":1625,"east":241.6699981689453,"tiles":12}`

const sm2f = `{"map":189,"name":"Scarlet Monastery","source":"worldmap","width":1002,"height":668,"px_per_yard":3.12,
"north":307.37,"south":93.91,"west":-162.27,"east":-482.46}`

func write(t *testing.T, dir, name, body string) {
	t.Helper()
	if err := os.WriteFile(filepath.Join(dir, name), []byte(body), 0o644); err != nil {
		t.Fatal(err)
	}
}

// src is one of the fixture metas with another source.
func src(meta, source string) string {
	return strings.Replace(meta, `"source":"worldmap"`, `"source":"`+source+`"`, 1)
}

// pic writes a picture with its metadata.
func pic(t *testing.T, dir, name, meta string) {
	t.Helper()
	write(t, dir, name+".json", meta)
	write(t, dir, name+".png", "\x89PNG-"+name)
}

// zfFolder is the reference folder: Zul'Farrak has all three pictures, and the main <id>.png is a further copy of the world map.
func zfFolder(t *testing.T) (string, *Dir) {
	t.Helper()
	dir := t.TempDir()
	pic(t, dir, "209", zf)
	pic(t, dir, "209.worldmap", zf)
	pic(t, dir, "209.minimap", src(zf, "minimap"))
	pic(t, dir, "209.navmesh", src(zf, "navmesh"))
	write(t, dir, "secret.txt", "not for the web")
	write(t, dir, "209.exe", "x")
	d, err := New(dir)
	if err != nil {
		t.Fatal(err)
	}
	return dir, d
}

// uldamanFolder is an indoor dungeon: the World map and the navmesh, no minimap picture.
func uldamanFolder(t *testing.T) (string, *Dir) {
	t.Helper()
	dir := t.TempDir()
	pic(t, dir, "70", zf)
	pic(t, dir, "70.worldmap", zf)
	pic(t, dir, "70.navmesh", src(zf, "navmesh"))
	d, err := New(dir)
	if err != nil {
		t.Fatal(err)
	}
	return dir, d
}

func TestNewEmptyAndMissing(t *testing.T) {
	if d, err := New(""); d != nil || err != nil {
		t.Fatalf("empty path: %v %v", d, err)
	}
	if d, err := New("   "); d != nil || err != nil {
		t.Fatalf("blank path: %v %v", d, err)
	}
	if _, err := New(filepath.Join(t.TempDir(), "nope")); err == nil {
		t.Fatal("a missing folder must be an error")
	}
	f := filepath.Join(t.TempDir(), "file")
	write(t, filepath.Dir(f), "file", "x")
	if _, err := New(f); err == nil {
		t.Fatal("a file is not a folder")
	}
	var none *Dir
	if none.Slots(209) != nil {
		t.Fatal("a nil Dir has no slots")
	}
}

// The picture lies where the game puts it: the pixel of a world point under the plane transform (x -> -y, y -> -x).
func TestPlaneMatchesPixelTransform(t *testing.T) {
	var m Meta
	if err := json.Unmarshal([]byte(zf), &m); err != nil {
		t.Fatal(err)
	}
	x, y, w, h := Plane(m)
	if math.Abs(w*m.PxPerYard-float64(m.Width)) > 1 || math.Abs(h*m.PxPerYard-float64(m.Height)) > 1 {
		t.Fatalf("plane size %.1f x %.1f yd is not %d x %d px at %.3f px/yd", w, h, m.Width, m.Height, m.PxPerYard)
	}
	// a point 100 yd south and 200 yd east of the north-west corner, in game coordinates (x north, y west)
	gx, gy := m.North-100, m.West-200
	px, py := (-gy-x)*m.PxPerYard, (-gx-y)*m.PxPerYard // plane point -> pixel inside the picture
	if math.Abs(px-200*m.PxPerYard) > 1e-6 || math.Abs(py-100*m.PxPerYard) > 1e-6 {
		t.Fatalf("pixel %.2f,%.2f, want %.2f,%.2f", px, py, 200*m.PxPerYard, 100*m.PxPerYard)
	}
	// the corners: north-west is the picture's top-left, south-east its bottom-right
	if x != -m.West || y != -m.North || x+w != -m.East || y+h != -m.South {
		t.Fatalf("corners wrong: %v %v %v %v", x, y, w, h)
	}
}

// Zul'Farrak: World map, Minimap, Navmesh, in that order, without the main copy listed twice.
func TestSlotsZulFarrak(t *testing.T) {
	_, d := zfFolder(t)
	ls := d.Slots(209)
	if len(ls) != 3 {
		t.Fatalf("map 209 slots = %+v", ls)
	}
	for i, want := range []string{"worldmap", "minimap", "navmesh"} {
		if ls[i].Kind != want || !ls[i].Available {
			t.Fatalf("slot %d = %+v, want an available %s", i, ls[i], want)
		}
		if ls[i].Floor != "" || ls[i].Reason != "" || ls[i].URL == "" {
			t.Fatalf("slot %d = %+v", i, ls[i])
		}
	}
	if ls[0].Label != "World map" || ls[0].Name != "worldmap" || ls[1].Label != "Minimap" || ls[2].Label != "Navmesh" {
		t.Fatalf("labels: %q %q %q", ls[0].Label, ls[1].Label, ls[2].Label)
	}
	// the per-source picture is the one listed, not the main copy of it
	if ls[0].URL != FilePrefix+"209.worldmap.png" {
		t.Fatalf("worldmap url = %q", ls[0].URL)
	}
	seen := map[string]bool{}
	for _, l := range ls {
		if seen[l.URL] {
			t.Fatalf("duplicate url %q in %+v", l.URL, ls)
		}
		seen[l.URL] = true
	}
	if ls[0].X != -1625 || math.Abs(ls[0].W-(1625-241.6699981689453)) > 1e-6 {
		t.Fatalf("rect: %+v", ls[0])
	}
}

// Uldaman: no minimap picture; the slot says so with the default reason instead of disappearing.
func TestSlotsUldamanMissingMinimap(t *testing.T) {
	_, d := uldamanFolder(t)
	ls := d.Slots(70)
	if len(ls) != 3 || ls[0].Kind != "worldmap" || !ls[0].Available || ls[2].Kind != "navmesh" || !ls[2].Available {
		t.Fatalf("map 70 slots = %+v", ls)
	}
	m := ls[1]
	if m.Kind != "minimap" || m.Available || m.URL != "" || m.X != 0 || m.W != 0 || m.H != 0 || m.Floor != "" {
		t.Fatalf("minimap slot = %+v", m)
	}
	if m.Reason != defaultReason["minimap"] || m.Label != "Minimap" {
		t.Fatalf("minimap reason/label = %q / %q", m.Reason, m.Label)
	}
}

// Scarlet Monastery: the world map has floors; the main picture still answers when the folder has no plain world map file. Floors sort
// by their number (10f after 3f), not by their name.
func TestSlotsWithFloors(t *testing.T) {
	dir := t.TempDir()
	pic(t, dir, "189", zf)
	for _, f := range []string{"-2f", "-10f", "-3f"} {
		pic(t, dir, "189.worldmap"+f, sm2f)
	}
	pic(t, dir, "189.navmesh", src(zf, "navmesh"))
	d, err := New(dir)
	if err != nil {
		t.Fatal(err)
	}
	ls := d.Slots(189)
	var names []string
	for _, l := range ls {
		names = append(names, l.Name)
	}
	want := "worldmap|worldmap 2f|worldmap 3f|worldmap 10f|minimap|navmesh"
	if strings.Join(names, "|") != want {
		t.Fatalf("slots = %v, want %s", names, want)
	}
	if !ls[0].Available || ls[0].URL != FilePrefix+"189.png" {
		t.Fatalf("the main picture must stand in for the world map: %+v", ls[0])
	}
	if ls[1].Label != "World map 2f" || ls[3].Label != "World map 10f" || !ls[3].Available {
		t.Fatalf("floor labels: %+v", ls[1:4])
	}
	if ls[4].Available || ls[4].Reason != defaultReason["minimap"] {
		t.Fatalf("minimap slot = %+v", ls[4])
	}
}

// An old folder (only <id>.png/.json): the main picture is the world map, the other two slots are missing.
func TestSlotsOldFolder(t *testing.T) {
	dir := t.TempDir()
	pic(t, dir, "209", zf)
	d, err := New(dir)
	if err != nil {
		t.Fatal(err)
	}
	ls := d.Slots(209)
	if len(ls) != 3 || !ls[0].Available || ls[0].URL != FilePrefix+"209.png" {
		t.Fatalf("slots = %+v", ls)
	}
	if ls[1].Available || ls[2].Available || ls[1].Reason != defaultReason["minimap"] || ls[2].Reason != defaultReason["navmesh"] {
		t.Fatalf("slots = %+v", ls)
	}
}

// A main picture listed twice must not happen: with a world map file of its own, the main copy is dropped.
func TestSlotsMainCopyNeverTwice(t *testing.T) {
	_, d := zfFolder(t)
	for _, l := range d.Slots(209) {
		if strings.HasSuffix(l.URL, "/209.png") {
			t.Fatalf("the main copy is listed as well: %+v", l)
		}
	}
}

// <id>.status.json gives the reason of a missing slot; malformed, oversized, symlinked or empty files fall back to the defaults.
func TestSlotsStatusReasons(t *testing.T) {
	dir, d := uldamanFolder(t)
	write(t, dir, "70.status.json", `{"minimap":{"reason":"  No minimap yet:\n the WMO tiles are not composed  "},"worldmap":{"reason":"ignored"}}`)
	ls := d.Slots(70)
	if ls[1].Reason != "No minimap yet: the WMO tiles are not composed" {
		t.Fatalf("status reason: %+v", ls[1])
	}
	// an available slot never carries a reason, even when the status file names its kind
	if ls[2].Reason != "" {
		t.Fatalf("available navmesh reason = %q", ls[2].Reason)
	}
	// a reason is capped at 160 characters (runes, not bytes)
	write(t, dir, "70.status.json", `{"minimap":{"reason":"`+strings.Repeat("a", 200)+`"}}`)
	if got := d.Slots(70)[1].Reason; utf8.RuneCountInString(got) != maxReason {
		t.Fatalf("capped reason has %d characters, want %d", utf8.RuneCountInString(got), maxReason)
	}
	write(t, dir, "70.status.json", `{"minimap":{"reason":"`+strings.Repeat("é", 200)+`"}}`)
	if got := d.Slots(70)[1].Reason; utf8.RuneCountInString(got) != maxReason || len(got) != 2*maxReason {
		t.Fatalf("capped non-ASCII reason has %d runes / %d bytes, want %d / %d", utf8.RuneCountInString(got), len(got), maxReason, 2*maxReason)
	}
	// control and format characters (NUL, ESC, DEL, the bidi overrides, BEL) become spaces instead of reaching the tooltip
	write(t, dir, "70.status.json", `{"minimap":{"reason":"a\u0000b\u001bc\u007fd\u202ee\u2066f\u0007g"}}`)
	if got := d.Slots(70)[1].Reason; got != "a b c d e f g" {
		t.Fatalf("control characters survived: %q", got)
	}
	// empty reasons fall back per kind
	write(t, dir, "70.status.json", `{"minimap":{"reason":"   "}}`)
	if got := d.Slots(70)[1].Reason; got != defaultReason["minimap"] {
		t.Fatalf("empty reason -> %q", got)
	}
	// malformed, oversized, a directory and an unknown key: the defaults
	for name, body := range map[string]string{
		"malformed": `{"minimap":`,
		"array":     `[1,2,3]`,
		"oversized": `{"minimap":{"reason":"` + strings.Repeat("a", maxStatus) + `"}}`,
		"unknown":   `{"wibble":{"reason":"x"},"minimap":{}}`,
	} {
		write(t, dir, "70.status.json", body)
		if got := d.Slots(70)[1].Reason; got != defaultReason["minimap"] {
			t.Errorf("%s -> %q", name, got)
		}
	}
}

// The status file is read from the folder only: a symlink to a file outside it, or a traversal name, is refused.
func TestStatusFileStaysInTheFolder(t *testing.T) {
	dir, d := uldamanFolder(t)
	outside := filepath.Join(filepath.Dir(dir), "outside-status-"+filepath.Base(dir)+".json")
	write(t, filepath.Dir(dir), filepath.Base(outside), `{"minimap":{"reason":"LEAK"}}`)
	defer os.Remove(outside)
	if err := os.Symlink(outside, filepath.Join(dir, "70.status.json")); err != nil {
		t.Skipf("no symlinks here: %v", err)
	}
	if got := d.Slots(70)[1].Reason; got != defaultReason["minimap"] {
		t.Fatalf("a symlinked status file was read: %q", got)
	}
	for _, n := range []string{"../70.status.json", "sub/70.status.json", "/etc/passwd", "70.status.json.bak", "70.STATUS.json", "70.status.json\x00"} {
		if full := d.file(n, true); full != "" {
			t.Errorf("%q -> %q", n, full)
		}
	}
}

// A folder set but without pictures for the map: 200 and three missing slots. No folder: 404.
func TestHandleLayersAlwaysThreeSlots(t *testing.T) {
	_, d := zfFolder(t)
	rec := get(d.HandleLayers, MapsPrefix+"999")
	if rec.Code != 200 {
		t.Fatalf("empty map -> %d %s", rec.Code, rec.Body)
	}
	var raw []map[string]json.RawMessage
	if err := json.Unmarshal(rec.Body.Bytes(), &raw); err != nil || len(raw) != 3 {
		t.Fatalf("body = %s (%v)", rec.Body, err)
	}
	for _, l := range raw {
		if string(l["available"]) != "false" || len(l["reason"]) == 0 {
			t.Errorf("slot = %s", l)
		}
		for _, k := range []string{"url", "x", "y", "w", "h"} {
			if _, ok := l[k]; ok {
				t.Errorf("missing slot carries %q: %s", k, l)
			}
		}
	}
	for _, p := range []string{"70", "abc", "-1", "209/../189", "209.png", "99999999999999999999", ""} {
		want := 200
		if p != "70" {
			want = 404
		}
		if rec := get(d.HandleLayers, MapsPrefix+p); rec.Code != want {
			t.Errorf("%q -> %d, want %d", p, rec.Code, want)
		}
	}
	var none *Dir
	if rec := get(none.HandleLayers, MapsPrefix+"209"); rec.Code != 404 {
		t.Errorf("no folder -> %d", rec.Code)
	}
}

// An available slot carries its url and rectangle; a missing one only its fields and reason.
func TestHandleLayersFields(t *testing.T) {
	_, d := zfFolder(t)
	rec := get(d.HandleLayers, MapsPrefix+"209")
	var raw []map[string]json.RawMessage
	if err := json.Unmarshal(rec.Body.Bytes(), &raw); err != nil || len(raw) != 3 {
		t.Fatalf("body = %s (%v)", rec.Body, err)
	}
	for _, k := range []string{"kind", "floor", "label", "available", "name", "source", "url", "x", "y", "w", "h"} {
		if _, ok := raw[0][k]; !ok {
			t.Errorf("available slot has no %q: %s", k, raw[0])
		}
	}
	if string(raw[0]["kind"]) != `"worldmap"` || string(raw[1]["kind"]) != `"minimap"` || string(raw[2]["kind"]) != `"navmesh"` {
		t.Errorf("kinds: %s", rec.Body)
	}
}

func TestLayersSkipBrokenMeta(t *testing.T) {
	dir, d := zfFolder(t)
	write(t, dir, "33.json", `{"map":33,"source":"worldmap","width":10,"height":10,"north":1,"south":5,"west":5,"east":1}`) // north below south
	write(t, dir, "33.png", "x")
	write(t, dir, "34.json", "not json")
	write(t, dir, "34.png", "x")
	write(t, dir, "35.json", zf) // metadata without its picture
	for _, id := range []uint32{33, 34, 35} {
		ls := d.Slots(id)
		if len(ls) != 3 {
			t.Fatalf("map %d slots = %+v", id, ls)
		}
		for _, l := range ls {
			if l.Available {
				t.Fatalf("map %d: broken metadata became a picture: %+v", id, l)
			}
		}
	}
}

func get(h http.HandlerFunc, path string) *httptest.ResponseRecorder {
	rec := httptest.NewRecorder()
	h(rec, httptest.NewRequest(http.MethodGet, path, nil))
	return rec
}

func TestHandleFileServesOnlyPngAndJsonOfTheFolder(t *testing.T) {
	dir, d := zfFolder(t)
	if rec := get(d.HandleFile, FilePrefix+"209.png"); rec.Code != 200 || rec.Header().Get("Content-Type") != "image/png" || rec.Body.String() != "\x89PNG-209" {
		t.Fatalf("png: %d %q %q", rec.Code, rec.Header().Get("Content-Type"), rec.Body)
	}
	if rec := get(d.HandleFile, FilePrefix+"209.worldmap.json"); rec.Code != 200 || rec.Header().Get("Content-Type") != "application/json" {
		t.Fatalf("json: %d", rec.Code)
	}
	// the status file is read for its reasons, never served
	write(t, dir, "209.status.json", `{"minimap":{"reason":"x"}}`)
	if rec := get(d.HandleFile, FilePrefix+"209.status.json"); rec.Code != 404 {
		t.Fatalf("status file served: %d %q", rec.Code, rec.Body)
	}
	// a secret one level above the folder, and a symlink to it inside the folder
	outside := filepath.Join(filepath.Dir(dir), "outside-"+filepath.Base(dir))
	if err := os.WriteFile(outside+".png", []byte("secret"), 0o644); err != nil {
		t.Fatal(err)
	}
	defer os.Remove(outside + ".png")
	if err := os.Symlink(outside+".png", filepath.Join(dir, "300.png")); err != nil {
		t.Skipf("no symlinks here: %v", err)
	}
	bad := []string{
		"secret.txt", "209.exe", "209.PNG", "209.png.bak", "../209.png", "..%2f209.png", "%2e%2e/209.png", "..\\209.png", "/etc/passwd",
		"sub/209.png", "209.png/", "209.worldmap.txt", "209.hacked.png", "0209x.png", ".png", "209", "", "300.png", "209.png\x00.txt",
		"12345678901.png", "209.minimap-2.png", "209.worldmap-f.png", "70.png", "209.status.json",
	}
	for _, name := range bad {
		rec := httptest.NewRecorder()
		req := httptest.NewRequest(http.MethodGet, "/", nil)
		req.URL.Path = FilePrefix + name // as the mux hands it over (already decoded)
		d.HandleFile(rec, req)
		if rec.Code != 404 {
			t.Errorf("%q -> %d %q", name, rec.Code, rec.Body)
		}
		if strings.Contains(rec.Body.String(), "secret") || strings.Contains(rec.Body.String(), "not for the web") {
			t.Errorf("%q leaked a file", name)
		}
	}
	// a name that is only encoded traversal in the URL form
	rec := httptest.NewRecorder()
	d.HandleFile(rec, httptest.NewRequest(http.MethodGet, FilePrefix+"..%2f..%2fetc%2fpasswd", nil))
	if rec.Code != 404 {
		t.Errorf("encoded traversal -> %d", rec.Code)
	}
	// only reads
	rec = httptest.NewRecorder()
	d.HandleFile(rec, httptest.NewRequest(http.MethodPost, FilePrefix+"209.png", nil))
	if rec.Code != http.StatusMethodNotAllowed {
		t.Errorf("POST -> %d", rec.Code)
	}
	var none *Dir
	if rec := get(none.HandleFile, FilePrefix+"209.png"); rec.Code != 404 {
		t.Errorf("no folder -> %d", rec.Code)
	}
}

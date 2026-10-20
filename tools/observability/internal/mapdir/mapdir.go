// Package mapdir serves the real game-map pictures behind the Commanders tab maps from an optional local folder.
//
// The pictures are the owner's own extracted game assets (turtle-agent's `minimap-extract` writes them), so they are never part of
// this repository or the embedded web files: the operator points TBDASH_MAP_DIR at a folder and the daemon
// serves from that folder and nothing else. The folder holds, per map id, `<id>.png` + `<id>.json` (the main picture) and
// optionally `<id>.<source>[-<n>f].png` + `.json` (source: minimap, worldmap or navmesh; `-<n>f` a floor). The JSON is
// minimap-extract's: map, name, source, width, height (pixels), px_per_yard and the world coordinates of the picture's edges, north
// and south (world x of the top and bottom edge) and west and east (world y of the left and right edge).
//
// Every map answers with the same three pictures in Zul'Farrak's order, whether or not the folder has them: the World map (one entry
// per floor), the Minimap and the Navmesh. A picture the folder lacks is a slot with available=false and a one-line reason, taken
// from an optional `<id>.status.json` in the folder ({"minimap":{"reason":"..."}}) or the default for its kind, so the page can offer
// all three and say why one is missing instead of silently dropping it.
package mapdir

import (
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"unicode"
)

// maxJSON bounds a metadata file; real ones are a few hundred bytes.
const maxJSON = 1 << 16

// maxStatus bounds a status file (64 KiB) and maxReason a reason line (characters, after whitespace is collapsed).
const (
	maxStatus = 1 << 16
	maxReason = 160
)

// Meta is the JSON beside a picture, as minimap-extract writes it.
type Meta struct {
	Map       uint32  `json:"map"`
	Name      string  `json:"name,omitempty"`
	Source    string  `json:"source"`
	Width     int     `json:"width"`
	Height    int     `json:"height"`
	PxPerYard float64 `json:"px_per_yard"`
	North     float64 `json:"north"`
	South     float64 `json:"south"`
	West      float64 `json:"west"`
	East      float64 `json:"east"`
}

// Layer is one picture slot of a map, ready for the page: the rectangle it covers in the Commanders map's plane (x = -game y,
// y = -game x: north up, west left, one unit a yard) and the URL it is served at. A kind or floor the folder has no picture for is
// still a slot, with Available false, a Reason and no URL or rectangle. The old fields (name, source, url, x, y, w, h) keep their
// names and meaning for an available slot.
type Layer struct {
	Kind      string  `json:"kind"`  // worldmap, minimap or navmesh
	Floor     string  `json:"floor"` // "" for the main picture, else "2f", "3f", ...
	Label     string  `json:"label"` // the picker's text: "World map", "World map 2f", "Minimap", "Navmesh"
	Available bool    `json:"available"`
	Reason    string  `json:"reason,omitempty"` // why a missing slot is missing (one line)
	Name      string  `json:"name"`             // the old slot name: kind, plus " " and the floor when there is one
	Source    string  `json:"source,omitempty"`
	URL       string  `json:"url,omitempty"`
	X         float64 `json:"x,omitempty"`
	Y         float64 `json:"y,omitempty"`
	W         float64 `json:"w,omitempty"`
	H         float64 `json:"h,omitempty"`
}

// MarshalJSON leaves the URL and the rectangle out of a missing slot: there is no picture to point at.
func (l Layer) MarshalJSON() ([]byte, error) {
	type wire struct {
		Kind      string   `json:"kind"`
		Floor     string   `json:"floor"`
		Label     string   `json:"label"`
		Available bool     `json:"available"`
		Reason    string   `json:"reason,omitempty"`
		Name      string   `json:"name"`
		Source    string   `json:"source,omitempty"`
		URL       string   `json:"url,omitempty"`
		X         *float64 `json:"x,omitempty"`
		Y         *float64 `json:"y,omitempty"`
		W         *float64 `json:"w,omitempty"`
		H         *float64 `json:"h,omitempty"`
	}
	out := wire{Kind: l.Kind, Floor: l.Floor, Label: l.Label, Available: l.Available, Reason: l.Reason, Name: l.Name, Source: l.Source}
	if l.Available {
		out.URL = l.URL
		x, y, w, h := l.X, l.Y, l.W, l.H
		out.X, out.Y, out.W, out.H = &x, &y, &w, &h
	}
	return json.Marshal(out)
}

// Dir is a folder of map pictures.
type Dir struct{ root string }

// FilePrefix is where Handler serves the files of the folder from.
const FilePrefix = "/api/v1/mapfiles/"

// MapsPrefix is where Handler answers the layer list of a map id.
const MapsPrefix = "/api/v1/maps/"

// kinds is the order the page offers the pictures in, and the kinds a slot may have.
var kinds = []string{"worldmap", "minimap", "navmesh"}

var kindLabel = map[string]string{"worldmap": "World map", "minimap": "Minimap", "navmesh": "Navmesh"}

// defaultReason is what a missing slot says when the folder has no <id>.status.json entry for its kind.
var defaultReason = map[string]string{
	"worldmap": "No world map picture: the client has no WorldMapArea row for this map",
	"minimap":  "No minimap picture: indoor dungeon (the client draws it from WMO tiles, which minimap-extract does not compose yet)",
	"navmesh":  "No navmesh picture: minimap-extract ran without --mmaps, or the server has no mmaps for this map",
}

var (
	// fileName is every name the daemon will serve: no separator and no dot sequence can match it.
	fileName = regexp.MustCompile(`^(\d{1,9})(?:\.(minimap|worldmap|navmesh)(-\d+f)?)?\.(json|png)$`)
	jsonName = regexp.MustCompile(`^\d{1,9}\.(minimap|worldmap|navmesh)(-\d+f)?\.json$`)
	// statusName is the optional reason file. It is read for its reasons and never served, so it is NOT part of fileName.
	statusName = regexp.MustCompile(`^\d{1,9}\.status\.json$`)
)

// New opens the folder. An empty path gives a nil Dir (no real maps); a path that is not a directory is an error.
func New(root string) (*Dir, error) {
	if strings.TrimSpace(root) == "" {
		return nil, nil
	}
	abs, err := filepath.Abs(root)
	if err != nil {
		return nil, err
	}
	st, err := os.Stat(abs)
	if err != nil {
		return nil, err
	}
	if !st.IsDir() {
		return nil, fmt.Errorf("%s is not a directory", abs)
	}
	return &Dir{root: abs}, nil
}

// Root is the folder.
func (d *Dir) Root() string { return d.root }

// Plane is where a picture with these world edges lies in the Commanders map's plane. Game x is north and game y is west; the map
// draws north up and west left, so the picture's top-left corner (north, west) is the plane point (-west, -north), and it spans
// west-east yards across and north-south yards down.
func Plane(m Meta) (x, y, w, h float64) {
	return -m.West, -m.North, m.West - m.East, m.North - m.South
}

// path is the file of a served name, or "" for a name that is not one: only files directly in the folder, only regular files (a
// symbolic link is refused, so nothing outside the folder can be reached through one).
func (d *Dir) path(name string) string { return d.file(name, false) }

// file is path, widened to <id>.status.json when allowStatus is set (the status file is read, never served).
func (d *Dir) file(name string, allowStatus bool) string {
	if d == nil || !(fileName.MatchString(name) || (allowStatus && statusName.MatchString(name))) {
		return ""
	}
	full := filepath.Join(d.root, name)
	if filepath.Dir(full) != d.root {
		return ""
	}
	st, err := os.Lstat(full)
	if err != nil || !st.Mode().IsRegular() {
		return ""
	}
	return full
}

func (d *Dir) readMeta(name string) (Meta, bool) {
	full := d.path(name)
	if full == "" {
		return Meta{}, false
	}
	if st, err := os.Stat(full); err != nil || st.Size() > maxJSON {
		return Meta{}, false
	}
	b, err := os.ReadFile(full)
	if err != nil {
		return Meta{}, false
	}
	var m Meta
	if json.Unmarshal(b, &m) != nil || m.Width <= 0 || m.Height <= 0 || !(m.West > m.East) || !(m.North > m.South) {
		return Meta{}, false
	}
	return m, true
}

// reasons reads <id>.status.json: {"worldmap":{"reason":"..."},"minimap":...,"navmesh":...}. A malformed or oversized file is no
// file (the defaults answer), and a reason is one line of at most maxReason characters.
func (d *Dir) reasons(base string) map[string]string {
	full := d.file(base+".status.json", true)
	if full == "" {
		return nil
	}
	f, err := os.Open(full)
	if err != nil {
		return nil
	}
	defer f.Close()
	// the name was checked with Lstat, but the file may have been replaced in between: the opened file must still be that regular file
	// (a symlink or another file swapped in is refused), and the read is bounded whatever Stat says.
	st, err := f.Stat()
	if err != nil || !st.Mode().IsRegular() || st.Size() > maxStatus {
		return nil
	}
	lst, err := os.Lstat(full)
	if err != nil || !lst.Mode().IsRegular() || !os.SameFile(st, lst) {
		return nil
	}
	b, err := io.ReadAll(io.LimitReader(f, maxStatus+1))
	if err != nil || len(b) > maxStatus {
		return nil
	}
	var raw map[string]struct {
		Reason string `json:"reason"`
	}
	if json.Unmarshal(b, &raw) != nil {
		return nil
	}
	out := make(map[string]string)
	for _, k := range kinds {
		if r := oneLine(raw[k].Reason); r != "" {
			out[k] = r
		}
	}
	return out
}

// oneLine makes a status reason safe for a tooltip and a line of text: control characters (NUL, escape, DEL, ...) and format
// characters (the bidi overrides U+202A-U+202E and U+2066-U+2069, zero-width joiners) become spaces, whitespace is collapsed and the
// result is capped at maxReason characters (not bytes).
func oneLine(s string) string {
	var b strings.Builder
	for _, r := range s {
		if unicode.IsControl(r) || unicode.Is(unicode.Cf, r) || (r >= 0x202a && r <= 0x202e) || (r >= 0x2066 && r <= 0x2069) {
			b.WriteByte(' ')
			continue
		}
		b.WriteRune(r)
	}
	s = strings.Join(strings.Fields(b.String()), " ")
	if r := []rune(s); len(r) > maxReason {
		s = string(r[:maxReason])
	}
	return s
}

// floorN is the number in "2f" (0 for the main picture).
func floorN(floor string) int {
	n, _ := strconv.Atoi(strings.TrimSuffix(floor, "f"))
	return n
}

// layer is a slot for a picture file ("<id>.json" or "<id>.<kind>[-<floor>].json"), when its picture file is there.
func (d *Dir) layer(file, kind, floor string, m Meta) (Layer, bool) {
	png := strings.TrimSuffix(file, ".json") + ".png"
	if d.path(png) == "" {
		return Layer{}, false
	}
	label, name := kindLabel[kind], kind
	if floor != "" {
		label += " " + floor
		name += " " + floor
	}
	x, y, w, h := Plane(m)
	return Layer{Kind: kind, Floor: floor, Label: label, Name: name, Available: true, Source: m.Source, URL: FilePrefix + png, X: x, Y: y, W: w, H: h}, true
}

// missing is a slot without a picture, with the status file's reason for its kind or the default one.
func missing(kind, floor, reason string) Layer {
	if reason == "" {
		reason = defaultReason[kind]
	}
	label, name := kindLabel[kind], kind
	if floor != "" {
		label += " " + floor
		name += " " + floor
	}
	return Layer{Kind: kind, Floor: floor, Label: label, Name: name, Reason: reason}
}

// Slots lists the picture slots of a map in Zul'Farrak's order: the World map (one entry per floor), the Minimap, the Navmesh. A
// slot whose picture the folder lacks is listed as missing, with a reason. A folder set but without pictures for the map still
// answers three missing slots; a nil Dir answers nothing.
func (d *Dir) Slots(id uint32) []Layer {
	if d == nil {
		return nil
	}
	base := strconv.FormatUint(uint64(id), 10)
	reasons := d.reasons(base)
	pics := make(map[string]Layer) // "<kind>" or "<kind>-<floor>" -> available slot
	ents, _ := os.ReadDir(d.root)
	var names []string
	for _, e := range ents {
		n := e.Name()
		if jsonName.MatchString(n) && strings.HasPrefix(n, base+".") {
			names = append(names, n)
		}
	}
	sort.Strings(names)
	for _, n := range names {
		stem := strings.TrimSuffix(strings.TrimPrefix(n, base+"."), ".json") // "worldmap" or "worldmap-2f"
		kind, floor := stem, ""
		if i := strings.IndexByte(stem, '-'); i >= 0 {
			kind, floor = stem[:i], stem[i+1:]
		}
		if _, ok := kindLabel[kind]; !ok {
			continue
		}
		if kind != "worldmap" && floor != "" {
			continue // only the world map has floors
		}
		if floor != "" && floorN(floor) == 0 {
			continue // "0f" is not a floor
		}
		if m, ok := d.readMeta(n); ok {
			if l, ok := d.layer(n, kind, floor, m); ok {
				if _, dup := pics[stem]; !dup {
					pics[stem] = l
				}
			}
		}
	}
	// The main picture is the whole answer of an old folder: use it only for a kind the folder has no <id>.<kind>.json for (a floor
	// picture does not stand in for the main one), so it is never listed twice.
	if m, ok := d.readMeta(base + ".json"); ok {
		if _, known := kindLabel[m.Source]; known {
			if _, has := pics[m.Source]; !has {
				if l, ok := d.layer(base+".json", m.Source, "", m); ok {
					pics[m.Source] = l
				}
			}
		}
	}
	var floors []string
	for k := range pics {
		if strings.HasPrefix(k, "worldmap-") {
			floors = append(floors, strings.TrimPrefix(k, "worldmap-"))
		}
	}
	sort.Slice(floors, func(i, j int) bool {
		a, b := floorN(floors[i]), floorN(floors[j])
		if a != b {
			return a < b
		}
		return floors[i] < floors[j]
	})
	out := make([]Layer, 0, 2+len(floors)+2)
	add := func(kind, floor string) {
		key := kind
		if floor != "" {
			key += "-" + floor
		}
		if l, ok := pics[key]; ok {
			out = append(out, l)
			return
		}
		out = append(out, missing(kind, floor, reasons[kind]))
	}
	add("worldmap", "")
	for _, f := range floors {
		add("worldmap", f)
	}
	add("minimap", "")
	add("navmesh", "")
	return out
}

// HandleLayers answers GET /api/v1/maps/<map id> with the map's three picture slots: 200 even when the folder has nothing for the
// map (the slots say why), 404 only for a bad id or no folder at all.
func (d *Dir) HandleLayers(w http.ResponseWriter, r *http.Request) {
	id, err := strconv.ParseUint(strings.TrimPrefix(r.URL.Path, MapsPrefix), 10, 32)
	if err != nil || d == nil {
		http.NotFound(w, r)
		return
	}
	b, _ := json.Marshal(d.Slots(uint32(id)))
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Cache-Control", "no-cache")
	_, _ = w.Write(b)
}

// HandleFile serves GET /api/v1/mapfiles/<name>: a .png or .json of the folder with a name minimap-extract writes, and nothing else.
func (d *Dir) HandleFile(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet && r.Method != http.MethodHead {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	name := strings.TrimPrefix(r.URL.Path, FilePrefix)
	full := d.path(name)
	if full == "" {
		http.NotFound(w, r)
		return
	}
	f, err := os.Open(full)
	if err != nil {
		http.NotFound(w, r)
		return
	}
	defer f.Close()
	st, err := f.Stat()
	if err != nil || !st.Mode().IsRegular() {
		http.NotFound(w, r)
		return
	}
	if strings.HasSuffix(name, ".png") {
		w.Header().Set("Content-Type", "image/png")
	} else {
		w.Header().Set("Content-Type", "application/json")
	}
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("X-Content-Type-Options", "nosniff")
	http.ServeContent(w, r, name, st.ModTime(), f)
}

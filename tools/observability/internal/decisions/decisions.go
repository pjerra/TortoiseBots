// Package decisions serves tb-decision-capture folders read-only behind the
// dashboard's usual GM login. The module writes one folder per capture,
// <dir>/<captureId>/{index.json,seg-0001.jsonl,...}; this package lists the
// captures from their index.json files and streams the files back, nothing
// else. Off (empty folder): every route 404s with the JSON probe below, so a
// client learns the feature is off.
//
// A capture id is <UTC yyyymmddThhmmssZ>-<plan>-<commander> (plan and
// commander reduced to [A-Za-z0-9_], cut at 48/24, -2/-3 ... appended on a
// clash). A file is index.json or seg-NNNN.jsonl. Both are validated with the
// contract regexes before any filesystem call, and a resolved path must stay
// inside the folder, so traversal (.. , encoded dots/slashes, absolute paths)
// and symlinks escaping the folder are never served.
package decisions

import (
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"time"
)

const (
	// ListPath answers the capture list (and the feature probe: 404 with the
	// disabled JSON when the folder is off).
	ListPath = "/api/v1/decisions"
	// FilePrefix serves the capture files from.
	FilePrefix = "/api/v1/decisions/"
	// MaxFolders bounds one list pass: at most the first MaxFolders folder
	// entries are considered, the rest ignored.
	MaxFolders = 2000
	// disabledError is the probe answer when no folder is configured.
	disabledError = "decision capture folder not configured"
)

var (
	// captureIDRe is the section 3 id grammar, shared with turtle-agent.
	captureIDRe = regexp.MustCompile(`^[0-9]{8}T[0-9]{6}Z-[A-Za-z0-9_]{1,48}-[A-Za-z0-9_]{1,24}(-[0-9]{1,3})?$`)
	// captureFileRe is the only servable file names: the index or a segment.
	captureFileRe = regexp.MustCompile(`^(index\.json|seg-[0-9]{4}\.jsonl)$`)
	// ErrInvalid marks an id or file name outside the contract regexes; the
	// handlers answer 400 for it, 404 for everything else Open refuses.
	ErrInvalid = errors.New("decisions: invalid capture id or file name")
)

// Store is a folder of decision captures.
type Store struct{ root string }

// New opens the folder. An empty path gives a nil Store (the feature is off
// and every request 404s); a path that is not a directory is an error.
func New(dir string) (*Store, error) {
	if strings.TrimSpace(dir) == "" {
		return nil, nil
	}
	abs, err := filepath.Abs(dir)
	if err != nil {
		return nil, err
	}
	// Resolve the folder once so the containment check below compares
	// canonical paths (the folder itself may be a symlink).
	resolved, err := filepath.EvalSymlinks(abs)
	if err != nil {
		return nil, err
	}
	st, err := os.Stat(resolved)
	if err != nil {
		return nil, err
	}
	if !st.IsDir() {
		return nil, fmt.Errorf("%s is not a directory", abs)
	}
	return &Store{root: resolved}, nil
}

// Root is the folder.
func (s *Store) Root() string {
	if s == nil {
		return ""
	}
	return s.root
}

// rootOK re-resolves the configured folder and requires it to still be the
// canonical directory New found at startup. The stored path alone is not
// proof: if the folder is renamed away and its path replaced by a symlink to
// another directory, every child path would follow that link. Refusing here
// keeps listing and serving to the folder the operator started the daemon
// with.
func (s *Store) rootOK() bool {
	if s == nil {
		return false
	}
	resolved, err := filepath.EvalSymlinks(s.root)
	if err != nil {
		log.Printf("[Decisions] refusing %s: cannot re-resolve the folder: %v", s.root, err)
		return false
	}
	if resolved != s.root {
		log.Printf("[Decisions] refusing %s: the folder now resolves to %s, not the %s it was at startup", s.root, resolved, s.root)
		return false
	}
	st, err := os.Stat(resolved)
	if err != nil || !st.IsDir() {
		log.Printf("[Decisions] refusing %s: it is no longer a directory", s.root)
		return false
	}
	return true
}

// within reports whether path lies inside root (never root itself).
func within(root, path string) bool {
	rel, err := filepath.Rel(root, path)
	if err != nil {
		return false
	}
	return rel != "." && rel != ".." && !strings.HasPrefix(rel, ".."+string(filepath.Separator))
}

// Capture is one capture in the list: its index.json bytes plus the fields
// the list filters and sorts by.
type Capture struct {
	ID        string
	Commander string
	Started   time.Time
	Raw       json.RawMessage
}

// indexFile is the index.json subset the list needs.
type indexFile struct {
	Commander     string `json:"commander"`
	Started       string `json:"started"`
	StartedUnixMs int64  `json:"startedUnixMs"`
}

// startedOf reads the capture start, preferring the RFC3339 started field and
// falling back to the millisecond epoch. ok is false when neither parses.
func startedOf(ix indexFile) (t time.Time, ok bool) {
	if ix.Started != "" {
		if t, err := time.Parse(time.RFC3339, ix.Started); err == nil {
			return t, true
		}
	}
	if ix.StartedUnixMs != 0 {
		return time.UnixMilli(ix.StartedUnixMs).UTC(), true
	}
	return time.Time{}, false
}

// List returns the captures in the folder, newest first (ties by id),
// filtered by commander (exact, case-sensitive; empty matches all) and by
// since <= started <= until (zero times are no bound). A folder entry that is
// not a capture (name outside the id grammar) is ignored; a capture whose
// index.json cannot be read or parsed is skipped and logged once.
func (s *Store) List(commander string, since, until time.Time) ([]Capture, error) {
	out := make([]Capture, 0)
	if s == nil || !s.rootOK() {
		return out, nil
	}
	ents, err := os.ReadDir(s.root)
	if err != nil {
		return out, err
	}
	folders := 0
	for _, e := range ents {
		name := e.Name()
		if !captureIDRe.MatchString(name) {
			continue
		}
		if folders >= MaxFolders {
			break
		}
		folders++
		full := filepath.Join(s.root, name)
		resolved, err := filepath.EvalSymlinks(full)
		if err != nil {
			log.Printf("[Decisions] skipping %s: cannot resolve: %v", name, err)
			continue
		}
		if !within(s.root, resolved) {
			log.Printf("[Decisions] skipping %s: resolves outside the folder", name)
			continue
		}
		st, err := os.Stat(resolved)
		if err != nil || !st.IsDir() {
			log.Printf("[Decisions] skipping %s: not a directory", name)
			continue
		}
		raw, err := os.ReadFile(filepath.Join(resolved, "index.json"))
		if err != nil {
			log.Printf("[Decisions] skipping %s: cannot read index.json: %v", name, err)
			continue
		}
		var ix indexFile
		if err := json.Unmarshal(raw, &ix); err != nil {
			log.Printf("[Decisions] skipping %s: bad index.json: %v", name, err)
			continue
		}
		started, ok := startedOf(ix)
		if !ok {
			log.Printf("[Decisions] skipping %s: index.json has no parsable started time", name)
			continue
		}
		if commander != "" && ix.Commander != commander {
			continue
		}
		if !since.IsZero() && started.Before(since) {
			continue
		}
		if !until.IsZero() && started.After(until) {
			continue
		}
		out = append(out, Capture{ID: name, Commander: ix.Commander, Started: started, Raw: append(json.RawMessage(nil), raw...)})
	}
	sort.Slice(out, func(i, j int) bool {
		if !out[i].Started.Equal(out[j].Started) {
			return out[i].Started.After(out[j].Started)
		}
		return out[i].ID < out[j].ID
	})
	return out, nil
}

// Open returns the capture file for streaming: the caller copies it and
// closes it. The id and file must match the contract regexes (anything else
// is ErrInvalid); the resolved path must stay inside the folder, so symlinks
// escaping it are refused. A missing file is a plain error (the handler
// answers 404).
func (s *Store) Open(id, file string) (*os.File, int64, error) {
	if s == nil || !captureIDRe.MatchString(id) || !captureFileRe.MatchString(file) {
		return nil, 0, ErrInvalid
	}
	if !s.rootOK() {
		return nil, 0, fmt.Errorf("decisions: the folder is unavailable")
	}
	full := filepath.Join(s.root, id, file)
	if filepath.Dir(full) != filepath.Join(s.root, id) {
		return nil, 0, ErrInvalid
	}
	resolved, err := filepath.EvalSymlinks(full)
	if err != nil {
		return nil, 0, fmt.Errorf("decisions: no such capture file %s/%s", id, file)
	}
	if !within(s.root, resolved) {
		return nil, 0, fmt.Errorf("decisions: %s/%s escapes the folder", id, file)
	}
	// The authoritative check is the Lstat immediately before the open: the
	// path must be a regular file, not a symbolic link. The open itself then
	// refuses a link swapped in after this check (O_NOFOLLOW; the descriptor
	// is compared with this Lstat as well), and it is non-blocking so a FIFO
	// swapped in cannot hang the request. The opened descriptor must be that
	// same regular file.
	checked, err := os.Lstat(resolved)
	if err != nil || !checked.Mode().IsRegular() {
		return nil, 0, fmt.Errorf("decisions: no such capture file %s/%s", id, file)
	}
	f, err := openNoFollow(resolved)
	if err != nil {
		return nil, 0, fmt.Errorf("decisions: no such capture file %s/%s", id, file)
	}
	st, err := f.Stat()
	if err != nil || !st.Mode().IsRegular() || !os.SameFile(checked, st) {
		_ = f.Close()
		return nil, 0, fmt.Errorf("decisions: no such capture file %s/%s", id, file)
	}
	return f, st.Size(), nil
}

// writeDisabled answers the feature probe: the routes stay registered when no
// folder is configured so a client gets JSON, not a bare 404.
func writeDisabled(w http.ResponseWriter) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(http.StatusNotFound)
	_ = json.NewEncoder(w).Encode(map[string]string{"error": disabledError})
}

// writeInvalid answers a contract-regex failure.
func writeInvalid(w http.ResponseWriter) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(http.StatusBadRequest)
	_ = json.NewEncoder(w).Encode(map[string]string{"error": "invalid capture id or file name"})
}

// HandleList answers GET /api/v1/decisions?commander=&since=&until=
// (RFC3339) with {"captures":[<index.json objects>]}, newest first. No
// folder: the disabled JSON (which is also how a client learns the feature is
// off).
func (s *Store) HandleList(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet && r.Method != http.MethodHead {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	if r.URL.Path != ListPath {
		http.NotFound(w, r)
		return
	}
	if s == nil {
		writeDisabled(w)
		return
	}
	q := r.URL.Query()
	var since, until time.Time
	if v := q.Get("since"); v != "" {
		t, err := time.Parse(time.RFC3339, v)
		if err != nil {
			http.Error(w, "invalid since (RFC3339)", http.StatusBadRequest)
			return
		}
		since = t
	}
	if v := q.Get("until"); v != "" {
		t, err := time.Parse(time.RFC3339, v)
		if err != nil {
			http.Error(w, "invalid until (RFC3339)", http.StatusBadRequest)
			return
		}
		until = t
	}
	caps, err := s.List(q.Get("commander"), since, until)
	if err != nil {
		http.Error(w, "cannot list the folder", http.StatusInternalServerError)
		return
	}
	raws := make([]json.RawMessage, 0, len(caps))
	for _, c := range caps {
		raws = append(raws, c.Raw)
	}
	b, err := json.Marshal(struct {
		Captures []json.RawMessage `json:"captures"`
	}{Captures: raws})
	if err != nil {
		http.Error(w, "cannot list the folder", http.StatusInternalServerError)
		return
	}
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("X-Content-Type-Options", "nosniff")
	w.Header().Set("Content-Length", strconv.Itoa(len(b)))
	_, _ = w.Write(b)
}

// HandleFile serves GET /api/v1/decisions/<id>/<file>: one index.json or
// seg-NNNN.jsonl of the folder, streamed with io.Copy at the checked length.
// No folder: the disabled JSON. A name outside the contract regexes is 400;
// anything else unservable is 404.
func (s *Store) HandleFile(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet && r.Method != http.MethodHead {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	if !strings.HasPrefix(r.URL.Path, FilePrefix) {
		http.NotFound(w, r)
		return
	}
	if s == nil {
		writeDisabled(w)
		return
	}
	rest := strings.TrimPrefix(r.URL.Path, FilePrefix)
	parts := strings.Split(rest, "/")
	if len(parts) != 2 {
		writeInvalid(w)
		return
	}
	id, file := parts[0], parts[1]
	f, size, err := s.Open(id, file)
	if err != nil {
		if errors.Is(err, ErrInvalid) {
			writeInvalid(w)
			return
		}
		http.NotFound(w, r)
		return
	}
	defer f.Close()
	if file == "index.json" {
		w.Header().Set("Content-Type", "application/json")
	} else {
		w.Header().Set("Content-Type", "application/x-ndjson")
	}
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("X-Content-Type-Options", "nosniff")
	w.Header().Set("Content-Length", strconv.FormatInt(size, 10))
	_, _ = io.Copy(w, f)
}

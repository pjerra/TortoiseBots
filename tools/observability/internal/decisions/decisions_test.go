package decisions

import (
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func writeIndex(t *testing.T, dir, id, commander, started string) {
	t.Helper()
	folder := filepath.Join(dir, id)
	if err := os.MkdirAll(folder, 0o755); err != nil {
		t.Fatal(err)
	}
	body := fmt.Sprintf(`{"format":"tb-decision-capture","v":1,"id":%q,"commander":%q,"plan":"zg","map":309,"started":%q,"startedUnixMs":0,"ended":null,"endReason":"","schema":"abcd","segments":[],"dropped":0}`,
		id, commander, started)
	if err := os.WriteFile(filepath.Join(folder, "index.json"), []byte(body), 0o644); err != nil {
		t.Fatal(err)
	}
}

func fixture(t *testing.T) (string, *Store) {
	t.Helper()
	dir := t.TempDir()
	// Two commanders, three time windows; Fionaa has two captures started
	// minutes apart (the second sorts first).
	writeIndex(t, dir, "20261102T161939Z-zg-Fionaa", "Fionaa", "2026-11-02T16:19:39Z")
	writeIndex(t, dir, "20261102T172200Z-zg-Fionaa", "Fionaa", "2026-11-02T17:22:00Z")
	writeIndex(t, dir, "20261102T165500Z-zg-Betina", "Betina", "2026-11-02T16:55:00Z")
	if err := os.WriteFile(filepath.Join(dir, "20261102T172200Z-zg-Fionaa", "seg-0001.jsonl"), []byte("{\"k\":\"hdr\"}\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	s, err := New(dir)
	if err != nil {
		t.Fatal(err)
	}
	return dir, s
}

func get(h http.HandlerFunc, path string) *httptest.ResponseRecorder {
	rec := httptest.NewRecorder()
	h(rec, httptest.NewRequest(http.MethodGet, path, nil))
	return rec
}

// getPath keeps the raw path (as a mux hands over a decoded URL Path, or a
// caller that did not decode), so encoded traversal reaches the handler raw.
func getPath(h http.HandlerFunc, path string) *httptest.ResponseRecorder {
	rec := httptest.NewRecorder()
	req := httptest.NewRequest(http.MethodGet, "/", nil)
	req.URL.Path = path
	h(rec, req)
	return rec
}

func captureIDs(t *testing.T, rec *httptest.ResponseRecorder) []string {
	t.Helper()
	var payload struct {
		Captures []json.RawMessage `json:"captures"`
	}
	if err := json.Unmarshal(rec.Body.Bytes(), &payload); err != nil {
		t.Fatalf("list body is not JSON: %v: %q", err, rec.Body.String())
	}
	var ids []string
	for _, raw := range payload.Captures {
		var ix struct {
			ID string `json:"id"`
		}
		if err := json.Unmarshal(raw, &ix); err != nil {
			t.Fatalf("capture is not JSON: %v", err)
		}
		ids = append(ids, ix.ID)
	}
	return ids
}

func TestNewEmptyAndMissing(t *testing.T) {
	if s, err := New(""); s != nil || err != nil {
		t.Fatalf("empty path: %v %v", s, err)
	}
	if s, err := New("   "); s != nil || err != nil {
		t.Fatalf("blank path: %v %v", s, err)
	}
	if _, err := New(filepath.Join(t.TempDir(), "nope")); err == nil {
		t.Fatal("a missing folder must be an error")
	}
	f := filepath.Join(t.TempDir(), "file")
	if err := os.WriteFile(f, []byte("x"), 0o644); err != nil {
		t.Fatal(err)
	}
	if _, err := New(f); err == nil {
		t.Fatal("a file is not a folder")
	}
	var none *Store
	if got, err := none.List("", time.Time{}, time.Time{}); len(got) != 0 || err != nil {
		t.Fatalf("a nil Store lists nothing: %v %v", got, err)
	}
	if rec := get(none.HandleList, ListPath); rec.Code != http.StatusNotFound {
		t.Fatalf("no folder list -> %d", rec.Code)
	}
	var payload struct {
		Error string `json:"error"`
	}
	if err := json.Unmarshal(get(none.HandleList, ListPath).Body.Bytes(), &payload); err != nil || payload.Error != "decision capture folder not configured" {
		t.Fatalf("disabled list = %q, err %v", get(none.HandleList, ListPath).Body.String(), err)
	}
	if rec := get(none.HandleFile, FilePrefix+"20261102T161939Z-zg-Fionaa/index.json"); rec.Code != http.StatusNotFound {
		t.Fatalf("no folder file -> %d", rec.Code)
	}
}

func TestListNewestFirst(t *testing.T) {
	_, s := fixture(t)
	got, err := s.List("", time.Time{}, time.Time{})
	if err != nil {
		t.Fatal(err)
	}
	if len(got) != 3 {
		t.Fatalf("list = %d captures, want 3", len(got))
	}
	if got[0].ID != "20261102T172200Z-zg-Fionaa" || got[1].ID != "20261102T165500Z-zg-Betina" || got[2].ID != "20261102T161939Z-zg-Fionaa" {
		t.Fatalf("order = %v, want newest first", []string{got[0].ID, got[1].ID, got[2].ID})
	}
}

func TestListCommanderFilter(t *testing.T) {
	_, s := fixture(t)
	got, err := s.List("Fionaa", time.Time{}, time.Time{})
	if err != nil {
		t.Fatal(err)
	}
	if len(got) != 2 {
		t.Fatalf("Fionaa list = %d captures, want 2", len(got))
	}
	// Exact and case-sensitive: lowercase matches nothing.
	got, err = s.List("fionaa", time.Time{}, time.Time{})
	if err != nil {
		t.Fatal(err)
	}
	if len(got) != 0 {
		t.Fatalf("lowercase list = %d captures, want 0", len(got))
	}
	got, err = s.List("FionaaX", time.Time{}, time.Time{})
	if err != nil {
		t.Fatal(err)
	}
	if len(got) != 0 {
		t.Fatalf("prefix list = %d captures, want 0", len(got))
	}
}

func TestListTimeWindow(t *testing.T) {
	_, s := fixture(t)
	between := func(since, until string) int {
		t.Helper()
		var sT, uT time.Time
		var err error
		if since != "" {
			if sT, err = time.Parse(time.RFC3339, since); err != nil {
				t.Fatal(err)
			}
		}
		if until != "" {
			if uT, err = time.Parse(time.RFC3339, until); err != nil {
				t.Fatal(err)
			}
		}
		got, err := s.List("", sT, uT)
		if err != nil {
			t.Fatal(err)
		}
		return len(got)
	}
	// started is inclusive on both ends.
	if n := between("2026-11-02T16:19:39Z", "2026-11-02T17:22:00Z"); n != 3 {
		t.Fatalf("closed window = %d, want 3", n)
	}
	if n := between("2026-11-02T16:20:00Z", ""); n != 2 {
		t.Fatalf("since-only = %d, want 2", n)
	}
	if n := between("", "2026-11-02T16:19:39Z"); n != 1 {
		t.Fatalf("until-only = %d, want 1", n)
	}
	if n := between("2026-11-02T18:00:00Z", "2026-11-02T19:00:00Z"); n != 0 {
		t.Fatalf("empty window = %d, want 0", n)
	}
}

func TestHandleListQuery(t *testing.T) {
	_, s := fixture(t)
	rec := get(s.HandleList, ListPath+"?commander=Fionaa&since=2026-11-02T17:00:00Z&until=2026-11-02T18:00:00Z")
	if rec.Code != http.StatusOK {
		t.Fatalf("list -> %d %q", rec.Code, rec.Body.String())
	}
	if ids := captureIDs(t, rec); len(ids) != 1 || ids[0] != "20261102T172200Z-zg-Fionaa" {
		t.Fatalf("list = %v, want the late Fionaa capture", ids)
	}
	for _, bad := range []string{
		ListPath + "?since=not-a-time",
		ListPath + "?until=2026-13-45T99:99:99Z",
	} {
		if rec := get(s.HandleList, bad); rec.Code != http.StatusBadRequest {
			t.Errorf("%s -> %d, want 400", bad, rec.Code)
		}
	}
	if rec := getPath(s.HandleList, "/api/v1/other"); rec.Code != http.StatusNotFound {
		t.Errorf("wrong path -> %d, want 404", rec.Code)
	}
	for _, method := range []string{http.MethodPost, http.MethodPut, http.MethodDelete} {
		rec := httptest.NewRecorder()
		s.HandleList(rec, httptest.NewRequest(method, ListPath, nil))
		if rec.Code != http.StatusMethodNotAllowed {
			t.Errorf("%s -> %d, want 405", method, rec.Code)
		}
	}
}

// The list carries the index.json objects untouched, so turtle-agent gets
// segments/firstSeq/bytes without a second fetch per capture.
func TestHandleListCarriesIndexObjects(t *testing.T) {
	dir := t.TempDir()
	id := "20261102T161939Z-zg-Fionaa"
	writeIndex(t, dir, id, "Fionaa", "2026-11-02T16:19:39Z")
	idxPath := filepath.Join(dir, id, "index.json")
	raw, err := os.ReadFile(idxPath)
	if err != nil {
		t.Fatal(err)
	}
	s, err := New(dir)
	if err != nil {
		t.Fatal(err)
	}
	rec := get(s.HandleList, ListPath)
	var payload struct {
		Captures []json.RawMessage `json:"captures"`
	}
	if err := json.Unmarshal(rec.Body.Bytes(), &payload); err != nil {
		t.Fatal(err)
	}
	if len(payload.Captures) != 1 || string(payload.Captures[0]) != string(raw) {
		t.Fatalf("captures = %s, want the index.json bytes %s", payload.Captures, raw)
	}
}

func TestHandleListSkipsBadIndex(t *testing.T) {
	dir := t.TempDir()
	writeIndex(t, dir, "20261102T161939Z-zg-Fionaa", "Fionaa", "2026-11-02T16:19:39Z")
	bad := filepath.Join(dir, "20261102T172200Z-zg-Fionaa")
	if err := os.MkdirAll(bad, 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(bad, "index.json"), []byte("{not json"), 0o644); err != nil {
		t.Fatal(err)
	}
	// A folder name outside the id grammar is ignored, never an error.
	if err := os.Mkdir(filepath.Join(dir, "notes"), 0o755); err != nil {
		t.Fatal(err)
	}
	s, err := New(dir)
	if err != nil {
		t.Fatal(err)
	}
	got, err := s.List("", time.Time{}, time.Time{})
	if err != nil {
		t.Fatal(err)
	}
	if len(got) != 1 || got[0].ID != "20261102T161939Z-zg-Fionaa" {
		t.Fatalf("list = %v, want only the good capture", got)
	}
}

func TestHandleFileRejectsNames(t *testing.T) {
	dir, s := fixture(t)
	outside := filepath.Join(filepath.Dir(dir), "outside-"+filepath.Base(dir))
	if err := os.MkdirAll(outside, 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(outside, "secret.jsonl"), []byte("SECRET"), 0o644); err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(outside)
	id := "20261102T172200Z-zg-Fionaa"

	// Names outside the contract are 400, before any filesystem call: an
	// unknown file name, a free-form .jsonl, traversal, absolute paths and
	// encoded dot/separator sequences.
	bad := []string{
		"notes.txt", "notes.jsonl", "seg-1.jsonl", "seg-00001.jsonl", "seg-abcd.jsonl",
		"index.json.bak", "SEG-0001.jsonl", "INDEX.JSON",
		"..", ".", "/etc/passwd", "/etc/passwd.json", "sub/seg-0001.jsonl",
		"../secret.jsonl", "..%2fsecret.jsonl", "%2e%2e/secret.jsonl", "%2e%2e%2fsecret.jsonl",
		"x%2f..%2fsecret.jsonl", "x%5c..%5csecret.jsonl",
		"../outside-" + filepath.Base(dir) + "/secret.jsonl",
		"seg-0001.jsonl%00.txt", "%00.jsonl",
		id + "%00.json",
	}
	for _, file := range bad {
		rec := getPath(s.HandleFile, FilePrefix+id+"/"+file)
		if rec.Code != http.StatusBadRequest {
			t.Errorf("%q -> %d, want 400", file, rec.Code)
		}
		if strings.Contains(rec.Body.String(), "SECRET") {
			t.Errorf("%q leaked a file", file)
		}
	}
	// Ids outside the contract are 400 too.
	for _, badID := range []string{"..", "../secret", "/etc/passwd", "fionaa", "20261102T161939Z-zg-Fionaa/", "x%2f..%2fsecret"} {
		rec := getPath(s.HandleFile, FilePrefix+badID+"/index.json")
		if rec.Code != http.StatusBadRequest {
			t.Errorf("id %q -> %d, want 400", badID, rec.Code)
		}
	}
	// Encoded traversal as a real URL (net/http decodes it into Path).
	for _, raw := range []string{
		FilePrefix + "..%2f..%2fetc%2fpasswd%2findex.json",
		FilePrefix + id + "%2f..%2f..%2fsecret.jsonl",
	} {
		rec := httptest.NewRecorder()
		s.HandleFile(rec, httptest.NewRequest(http.MethodGet, raw, nil))
		if rec.Code != http.StatusBadRequest && rec.Code != http.StatusNotFound {
			t.Errorf("url %q -> %d", raw, rec.Code)
		}
	}
	// A well-formed name for a missing capture or segment is 404, not 400.
	for _, path := range []string{
		FilePrefix + "20261102T161939Z-zg-Nobody/index.json",
		FilePrefix + id + "/seg-0002.jsonl",
	} {
		if rec := get(s.HandleFile, path); rec.Code != http.StatusNotFound {
			t.Errorf("%s -> %d, want 404", path, rec.Code)
		}
	}
	var none *Store
	if rec := get(none.HandleFile, FilePrefix+id+"/index.json"); rec.Code != http.StatusNotFound {
		t.Errorf("no folder file -> %d, want 404", rec.Code)
	}
	for _, method := range []string{http.MethodPost, http.MethodPut, http.MethodDelete} {
		rec := httptest.NewRecorder()
		s.HandleFile(rec, httptest.NewRequest(method, FilePrefix+id+"/index.json", nil))
		if rec.Code != http.StatusMethodNotAllowed {
			t.Errorf("%s -> %d, want 405", method, rec.Code)
		}
	}
}

// A symlink inside a capture that resolves outside the folder is refused; one
// that resolves to a file of the same capture is served.
func TestHandleFileSymlinks(t *testing.T) {
	dir, s := fixture(t)
	id := "20261102T172200Z-zg-Fionaa"
	outside := filepath.Join(filepath.Dir(dir), "outside-"+filepath.Base(dir))
	if err := os.MkdirAll(outside, 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(outside, "secret.jsonl"), []byte("SECRET"), 0o644); err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(outside)
	if err := os.Symlink(filepath.Join(outside, "secret.jsonl"), filepath.Join(dir, id, "seg-0002.jsonl")); err != nil {
		t.Skipf("no symlinks here: %v", err)
	}
	if rec := get(s.HandleFile, FilePrefix+id+"/seg-0002.jsonl"); rec.Code != http.StatusNotFound || strings.Contains(rec.Body.String(), "SECRET") {
		t.Fatalf("escaping symlink -> %d %q", rec.Code, rec.Body)
	}
	// A symlink to a file of the same capture stays inside the folder.
	if err := os.Symlink(filepath.Join(dir, id, "seg-0001.jsonl"), filepath.Join(dir, id, "seg-0003.jsonl")); err != nil {
		t.Skipf("no symlinks here: %v", err)
	}
	if rec := get(s.HandleFile, FilePrefix+id+"/seg-0003.jsonl"); rec.Code != http.StatusOK {
		t.Fatalf("in-capture symlink -> %d", rec.Code)
	} else {
		rec.Body.Reset()
	}
	if err := os.Remove(filepath.Join(dir, id, "seg-0003.jsonl")); err != nil {
		t.Fatal(err)
	}
	// A directory symlink out of the folder must not serve files through it.
	if err := os.Symlink(outside, filepath.Join(dir, "20261102T180000Z-zg-Linker")); err != nil {
		t.Skipf("no symlinks here: %v", err)
	}
	if _, err := s.List("", time.Time{}, time.Time{}); err != nil {
		t.Fatal(err)
	}
}

// A 40 MB segment streams with the right length and content type.
func TestHandleFileStreamsLarge(t *testing.T) {
	dir := t.TempDir()
	id := "20261102T161939Z-zg-Fionaa"
	writeIndex(t, dir, id, "Fionaa", "2026-11-02T16:19:39Z")
	const size = 40 << 20
	f, err := os.Create(filepath.Join(dir, id, "seg-0001.jsonl"))
	if err != nil {
		t.Fatal(err)
	}
	line := []byte("{\"k\":\"tick\",\"pad\":\"0123456789abcdef\"}\n")
	for written := int64(0); written < size; {
		n, err := f.Write(line)
		if err != nil {
			_ = f.Close()
			t.Fatal(err)
		}
		written += int64(n)
	}
	_ = f.Close()
	s, err := New(dir)
	if err != nil {
		t.Fatal(err)
	}
	rec := get(s.HandleFile, FilePrefix+id+"/seg-0001.jsonl")
	if rec.Code != http.StatusOK {
		t.Fatalf("large file -> %d", rec.Code)
	}
	st, err := os.Stat(filepath.Join(dir, id, "seg-0001.jsonl"))
	if err != nil {
		t.Fatal(err)
	}
	if int64(rec.Body.Len()) != st.Size() {
		t.Fatalf("body = %d bytes, want %d", rec.Body.Len(), st.Size())
	}
	if got := rec.Header().Get("Content-Length"); got != fmt.Sprint(st.Size()) {
		t.Fatalf("Content-Length = %q, want %d", got, st.Size())
	}
	if got := rec.Header().Get("Content-Type"); got != "application/x-ndjson" {
		t.Fatalf("Content-Type = %q, want application/x-ndjson", got)
	}
	rec = get(s.HandleFile, FilePrefix+id+"/index.json")
	if got := rec.Header().Get("Content-Type"); got != "application/json" {
		t.Fatalf("index Content-Type = %q, want application/json", got)
	}
}

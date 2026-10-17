package dungeons

import (
	"log"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"time"
)

const (
	summarySuffix = ".summary.json"
	maxFileBytes  = 1 << 20 // a real summary is a few KiB; anything bigger is not one
	maxFiles      = 5000    // newest summaries kept in memory
	maxCached     = 24      // filtered views kept per index version
)

// Meta describes what the index found, so the page can say why it has nothing to show.
type Meta struct {
	Dir       string `json:"dir"`
	DirFound  bool   `json:"dir_found"`
	Explicit  bool   `json:"dir_configured"` // the operator named the folder (flag or TBDASH_TRACE_DIR) rather than the default search
	Files     int    `json:"files"`          // *.summary.json files in the folder
	Runs      int    `json:"runs"`           // of them, dungeon runs
	Ignored   int    `json:"ignored"`        // valid summaries that are not dungeon runs
	Corrupt   int    `json:"corrupt"`        // files that could not be read as a summary
	ScannedAt int64  `json:"scanned_at"`     // unix seconds of the last scan
}

type fileEntry struct {
	mod, size int64
	run       *Run // nil: corrupt or ignored
	state     int  // stateRun, stateIgnored or stateBad
}

const (
	stateRun = iota
	stateIgnored
	stateBad
)

// Index holds the parsed summaries of one trace folder. Scan reads only files that are new or changed since the last scan (by
// modification time and size), so a periodic scan costs one directory listing plus one stat per file; requests never touch the disk.
type Index struct {
	dirs     []string // candidates, first existing one wins
	explicit bool

	scanMu sync.Mutex            // one scan at a time; guards files and dir
	files  map[string]*fileEntry // by file name
	dir    string

	mu      sync.RWMutex // guards what requests read
	runs    []*Run       // newest first
	meta    Meta
	version uint64
	cache   map[string]cached
}

type cached struct {
	version uint64
	value   []DungeonSummary
}

// NewIndex builds an index over dir. An empty dir means "look in the usual places" (DefaultDirs).
func NewIndex(dir string) *Index {
	ix := &Index{files: map[string]*fileEntry{}, cache: map[string]cached{}}
	if strings.TrimSpace(dir) != "" {
		ix.dirs = []string{dir}
		ix.explicit = true
	} else {
		ix.dirs = DefaultDirs()
	}
	ix.meta = Meta{Dir: ix.dirs[0], Explicit: ix.explicit}
	return ix
}

// DefaultDirs are the places the recorder's folder (`AiPlayerbot.RunTrace.Dir`, default `traces` under the server's LogsDir) usually is,
// relative to where the daemon runs and to its binary.
func DefaultDirs() []string {
	var bases []string
	if wd, err := os.Getwd(); err == nil {
		bases = append(bases, wd)
	}
	if exe, err := os.Executable(); err == nil {
		bases = append(bases, filepath.Dir(exe))
	}
	var out []string
	seen := map[string]bool{}
	for _, b := range bases {
		for _, rel := range []string{"logs/traces", "traces", "../logs/traces"} {
			p := filepath.Clean(filepath.Join(b, rel))
			if !seen[p] {
				seen[p] = true
				out = append(out, p)
			}
		}
	}
	out = append(out, "/opt/turtle/logs/traces")
	return out
}

func (ix *Index) resolve() (string, bool) {
	for _, d := range ix.dirs {
		if st, err := os.Stat(d); err == nil && st.IsDir() {
			return d, true
		}
	}
	return ix.dirs[0], false
}

// Scan reads the folder once and updates the index. It reports whether anything changed.
func (ix *Index) Scan() bool {
	ix.scanMu.Lock()
	defer ix.scanMu.Unlock()

	dir, found := ix.resolve()
	if dir != ix.dir {
		ix.files = map[string]*fileEntry{}
		ix.dir = dir
	}
	type cand struct {
		name      string
		mod, size int64
	}
	var cands []cand
	if found {
		entries, err := os.ReadDir(dir)
		if err != nil {
			log.Printf("[Dungeons] cannot list %s: %v", dir, err)
		}
		for _, e := range entries {
			if e.IsDir() || !strings.HasSuffix(e.Name(), summarySuffix) {
				continue
			}
			info, err := e.Info()
			if err != nil || !info.Mode().IsRegular() {
				continue
			}
			cands = append(cands, cand{e.Name(), info.ModTime().UnixNano(), info.Size()})
		}
	}
	total := len(cands)
	if len(cands) > maxFiles {
		sort.Slice(cands, func(i, j int) bool { return cands[i].mod > cands[j].mod })
		cands = cands[:maxFiles]
	}

	changed := false
	next := make(map[string]*fileEntry, len(cands))
	for _, c := range cands {
		if old, ok := ix.files[c.name]; ok && old.mod == c.mod && old.size == c.size {
			next[c.name] = old
			continue
		}
		changed = true
		fe := &fileEntry{mod: c.mod, size: c.size, state: stateBad}
		if c.size <= maxFileBytes {
			if data, err := os.ReadFile(filepath.Join(dir, c.name)); err == nil {
				if run, ok, perr := Parse(data, c.name); perr != nil {
					log.Printf("[Dungeons] skipping %s: %v", c.name, perr)
				} else if !ok {
					fe.state = stateIgnored
				} else {
					if run.Started == 0 {
						run.Started = c.mod / 1e9
					}
					fe.state, fe.run = stateRun, run
				}
			} else {
				continue // vanished between the listing and the read: the next scan sees it
			}
		} else {
			log.Printf("[Dungeons] skipping %s: %d bytes is too big for a summary", c.name, c.size)
		}
		next[c.name] = fe
	}
	if len(next) != len(ix.files) {
		changed = true
	}
	ix.files = next

	meta := Meta{Dir: dir, DirFound: found, Explicit: ix.explicit, Files: total, ScannedAt: time.Now().Unix()}
	var runs []*Run
	for _, fe := range next {
		switch fe.state {
		case stateRun:
			runs = append(runs, fe.run)
		case stateIgnored:
			meta.Ignored++
		default:
			meta.Corrupt++
		}
	}
	meta.Runs = len(runs)
	SortRuns(runs)

	ix.mu.Lock()
	if changed || meta.DirFound != ix.meta.DirFound {
		ix.runs = runs
		ix.version++
		ix.cache = map[string]cached{}
	}
	ix.meta = meta
	ix.mu.Unlock()
	if changed {
		log.Printf("[Dungeons] indexed %d dungeon runs from %s (%d ignored, %d unreadable)", meta.Runs, dir, meta.Ignored, meta.Corrupt)
	}
	return changed
}

// Loop scans every interval until stop is closed. Call Scan once first so the first request already has data.
func (ix *Index) Loop(interval time.Duration, stop <-chan struct{}) {
	t := time.NewTicker(interval)
	defer t.Stop()
	for {
		select {
		case <-stop:
			return
		case <-t.C:
			ix.Scan()
		}
	}
}

// Meta returns what the last scan found.
func (ix *Index) Meta() Meta {
	ix.mu.RLock()
	defer ix.mu.RUnlock()
	return ix.meta
}

// Dungeons returns the per-dungeon summaries for f.
func (ix *Index) Dungeons(f Filter) []DungeonSummary {
	key := f.Build + "\x00" + f.Driver + "\x00" + f.Mode
	ix.mu.RLock()
	if c, ok := ix.cache[key]; ok && c.version == ix.version {
		ix.mu.RUnlock()
		return c.value
	}
	runs, version := ix.runs, ix.version
	ix.mu.RUnlock()

	v := Summarize(runs, f)

	ix.mu.Lock()
	if version == ix.version {
		if len(ix.cache) >= maxCached {
			ix.cache = map[string]cached{}
		}
		ix.cache[key] = cached{version, v}
	}
	ix.mu.Unlock()
	return v
}

// Facets lists the filter values over all runs.
func (ix *Index) Facets() Facets {
	ix.mu.RLock()
	runs := ix.runs
	ix.mu.RUnlock()
	return FacetsOf(runs)
}

// Runs returns the runs of one map matching f, newest first, at most limit of them, and how many matched.
func (ix *Index) Runs(mapID uint32, f Filter, limit int) ([]*Run, int) {
	ix.mu.RLock()
	runs := ix.runs
	ix.mu.RUnlock()
	var out []*Run
	total := 0
	for _, r := range runs {
		if r.Map != mapID || !f.match(r) {
			continue
		}
		total++
		if limit <= 0 || len(out) < limit {
			out = append(out, r)
		}
	}
	return out, total
}

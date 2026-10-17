package dungeons

import (
	"encoding/json"
	"net/http"
	"strconv"
)

// TraceHint is what the page tells an operator whose folder is empty.
const TraceHint = "Set AiPlayerbot.RunTrace.Enabled = 1, then a GM starts a run with `.bot trace start <run_id>` and ends it with `.bot trace stop`."

// ListView is the answer of GET /api/v1/dungeons.
type ListView struct {
	Meta     Meta             `json:"meta"`
	Hint     string           `json:"hint"`
	Facets   Facets           `json:"facets"`
	Dungeons []DungeonSummary `json:"dungeons"`
}

// RunsView is the answer of GET /api/v1/dungeons/{map}/runs.
type RunsView struct {
	Map    uint32    `json:"map"`
	Name   string    `json:"name"`
	Total  int       `json:"total"`
	Bosses []BossCol `json:"bosses"`
	Runs   []*Run    `json:"runs"`
}

func filterOf(r *http.Request) Filter {
	q := r.URL.Query()
	return Filter{Build: q.Get("build"), Driver: q.Get("driver"), Mode: q.Get("mode")}
}

func writeJSON(w http.ResponseWriter, v any) {
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Cache-Control", "no-cache")
	_ = json.NewEncoder(w).Encode(v)
}

// Register adds the two GET routes to mux. wrap is the dashboard's login check.
func (ix *Index) Register(mux *http.ServeMux, wrap func(http.HandlerFunc) http.HandlerFunc) {
	mux.HandleFunc("GET /api/v1/dungeons", wrap(func(w http.ResponseWriter, r *http.Request) {
		writeJSON(w, ListView{Meta: ix.Meta(), Hint: TraceHint, Facets: ix.Facets(), Dungeons: ix.Dungeons(filterOf(r))})
	}))
	mux.HandleFunc("GET /api/v1/dungeons/{map}/runs", wrap(func(w http.ResponseWriter, r *http.Request) {
		id, err := strconv.ParseUint(r.PathValue("map"), 10, 32)
		if err != nil || id == 0 {
			http.Error(w, "Invalid map id", http.StatusBadRequest)
			return
		}
		limit := 200
		if v, err := strconv.Atoi(r.URL.Query().Get("limit")); err == nil && v > 0 {
			limit = v
		}
		if limit > 1000 {
			limit = 1000
		}
		f := filterOf(r)
		runs, total := ix.Runs(uint32(id), f, limit)
		if total == 0 {
			// the map has no run at all (not just none matching the filter): 404
			if all, n := ix.Runs(uint32(id), Filter{}, 1); n == 0 || len(all) == 0 {
				http.Error(w, "No runs recorded for this map", http.StatusNotFound)
				return
			}
		}
		name := MapName(uint32(id))
		if len(runs) > 0 {
			name = runs[0].Dungeon
		}
		// boss columns over every matching run, not just the page
		all, _ := ix.Runs(uint32(id), f, 0)
		writeJSON(w, RunsView{Map: uint32(id), Name: name, Total: total, Bosses: BossColumns(all), Runs: nonNil(runs)})
	}))
}

func nonNil(r []*Run) []*Run {
	if r == nil {
		return []*Run{}
	}
	return r
}

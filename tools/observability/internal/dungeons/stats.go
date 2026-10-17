package dungeons

import (
	"sort"
	"strconv"
	"strings"
)

// Filter narrows the runs a view is built from. An empty field matches everything.
type Filter struct {
	Build  string
	Driver string // Driver.Key
	Mode   string
}

func (f Filter) match(r *Run) bool {
	return (f.Build == "" || r.Build == f.Build) && (f.Driver == "" || r.Driver.Key == f.Driver) && (f.Mode == "" || r.Mode == f.Mode)
}

// Stat is the headline of a set of runs of one dungeon.
type Stat struct {
	Runs        int      `json:"runs"`
	Cleared     int      `json:"cleared"`
	Partial     int      `json:"partial"`
	WipedOut    int      `json:"wiped_out"`
	Stopped     int      `json:"stopped"`
	Abandoned   int      `json:"abandoned"`
	Running     int      `json:"running"`
	Unknown     int      `json:"unknown"`
	SuccessRate *float64 `json:"success_rate"` // cleared / runs that finished with a known outcome; nil when there are none
	BestS       *float64 `json:"best_s"`       // fastest full clear
	MedianS     *float64 `json:"median_s"`     // median full clear
	Wipes       int      `json:"wipes"`
	Deaths      int      `json:"deaths"`
}

// RunBrief is one run as the glance panel and the group tables show it.
type RunBrief struct {
	Run       string   `json:"run"`
	Outcome   string   `json:"outcome"`
	Started   int64    `json:"started"`
	Ended     int64    `json:"ended,omitempty"`
	ClearS    *float64 `json:"clear_s"`
	DurationS float64  `json:"duration_s,omitempty"`
	Build     string   `json:"build,omitempty"`
	Driver    string   `json:"driver"`
	Furthest  string   `json:"furthest,omitempty"`
	Wipes     int      `json:"wipes"`
	Deaths    int      `json:"deaths"`
}

// Group is the same headline for one build or one driver of a dungeon.
type Group struct {
	Key   string `json:"key"`
	Label string `json:"label"`
	Stat
}

// BossStat is the kill times of one boss across runs.
type BossStat struct {
	Key     string   `json:"key"`
	Entry   uint32   `json:"entry,omitempty"`
	Name    string   `json:"name"`
	Kills   int      `json:"kills"`
	BestS   *float64 `json:"best_s"`
	MedianS *float64 `json:"median_s"`
	BestRun string   `json:"best_run,omitempty"`
}

// DungeonSummary is one dungeon of the Dungeons tab.
type DungeonSummary struct {
	Map  uint32 `json:"map"`
	Name string `json:"name"`
	Stat
	BestRun string     `json:"best_run,omitempty"`
	Last    *RunBrief  `json:"last"`
	DeltaS  *float64   `json:"delta_s"` // the last run's clear time minus the best; nil unless the last run was a full clear
	Bosses  []BossStat `json:"bosses"`
	Builds  []Group    `json:"builds"`
	Drivers []Group    `json:"drivers"`
}

func newF(v float64) *float64 { return &v }

func median(xs []float64) *float64 {
	if len(xs) == 0 {
		return nil
	}
	s := append([]float64(nil), xs...)
	sort.Float64s(s)
	n := len(s)
	if n%2 == 1 {
		return newF(s[n/2])
	}
	return newF((s[n/2-1] + s[n/2]) / 2)
}

// StatOf computes the headline of runs.
func StatOf(runs []*Run) (Stat, *Run) {
	var st Stat
	var clears []float64
	var bestRun *Run
	for _, r := range runs {
		st.Runs++
		st.Wipes += r.WipeCount
		st.Deaths += r.DeathCount
		switch r.Outcome {
		case OutcomeCleared:
			st.Cleared++
		case OutcomePartial:
			st.Partial++
		case OutcomeWipedOut:
			st.WipedOut++
		case OutcomeStopped:
			st.Stopped++
		case OutcomeAbandoned:
			st.Abandoned++
		case OutcomeRunning:
			st.Running++
		default:
			st.Unknown++
		}
		if r.Outcome == OutcomeCleared && r.ClearS != nil {
			clears = append(clears, *r.ClearS)
			if bestRun == nil || *r.ClearS < *bestRun.ClearS {
				bestRun = r
			}
		}
	}
	if finished := st.Runs - st.Running - st.Unknown; finished > 0 {
		st.SuccessRate = newF(float64(st.Cleared) / float64(finished))
	}
	if bestRun != nil {
		st.BestS = newF(*bestRun.ClearS)
	}
	st.MedianS = median(clears)
	return st, bestRun
}

func brief(r *Run) *RunBrief {
	return &RunBrief{Run: r.Run, Outcome: r.Outcome, Started: r.Started, Ended: r.Ended, ClearS: r.ClearS, DurationS: r.DurationS,
		Build: r.Build, Driver: r.Driver.Text, Furthest: r.Furthest, Wipes: r.WipeCount, Deaths: r.DeathCount}
}

func bossKey(b Boss) string {
	if b.Entry != 0 {
		return strconv.FormatUint(uint64(b.Entry), 10)
	}
	return strings.ToLower(b.Name)
}

// BossStats gives the kill times of each boss over runs, in the order bosses appear in the plan (the run with the most bosses sets it).
func BossStats(runs []*Run) []BossStat {
	cols := BossColumns(runs)
	times := map[string][]float64{}
	for _, r := range runs {
		for _, b := range r.Bosses {
			if b.KilledAtS != nil {
				times[bossKey(b)] = append(times[bossKey(b)], *b.KilledAtS)
			}
		}
	}
	out := make([]BossStat, 0, len(cols))
	for _, c := range cols {
		ts := times[c.Key]
		bs := BossStat{Key: c.Key, Entry: c.Entry, Name: c.Name, Kills: len(ts), BestS: c.BestS, BestRun: c.BestRun, MedianS: median(ts)}
		out = append(out, bs)
	}
	return out
}

// BossCol is one column of the boss kill table.
type BossCol struct {
	Key     string   `json:"key"`
	Entry   uint32   `json:"entry,omitempty"`
	Name    string   `json:"name"`
	BestS   *float64 `json:"best_s"`
	BestRun string   `json:"best_run,omitempty"`
}

// BossColumns lists the bosses seen in runs, in plan order, each with its best kill time and the run that set it. runs is read newest
// first; on a tie the older run keeps the record.
func BossColumns(runs []*Run) []BossCol {
	var longest *Run
	for _, r := range runs {
		if longest == nil || len(r.Bosses) > len(longest.Bosses) {
			longest = r
		}
	}
	var cols []BossCol
	idx := map[string]int{}
	add := func(b Boss) {
		k := bossKey(b)
		if _, ok := idx[k]; ok {
			return
		}
		idx[k] = len(cols)
		cols = append(cols, BossCol{Key: k, Entry: b.Entry, Name: b.Name})
	}
	if longest != nil {
		for _, b := range longest.Bosses {
			add(b)
		}
	}
	for i := len(runs) - 1; i >= 0; i-- {
		for _, b := range runs[i].Bosses {
			add(b)
		}
	}
	for i := len(runs) - 1; i >= 0; i-- { // oldest first, so a tie keeps the earlier record
		r := runs[i]
		for _, b := range r.Bosses {
			if b.KilledAtS == nil {
				continue
			}
			c := &cols[idx[bossKey(b)]]
			if c.BestS == nil || *b.KilledAtS < *c.BestS {
				c.BestS = newF(*b.KilledAtS)
				c.BestRun = r.Run
			}
		}
	}
	return cols
}

func groups(runs []*Run, key func(*Run) string) []Group {
	by := map[string][]*Run{}
	var order []string
	for _, r := range runs {
		k := key(r)
		if _, ok := by[k]; !ok {
			order = append(order, k)
		}
		by[k] = append(by[k], r)
	}
	out := make([]Group, 0, len(order))
	for _, k := range order {
		st, _ := StatOf(by[k])
		label := k
		if label == "" {
			label = "unknown"
		}
		out = append(out, Group{Key: k, Label: label, Stat: st})
	}
	// best clear first; groups without a clear last, then most runs
	sort.SliceStable(out, func(i, j int) bool {
		a, b := out[i].BestS, out[j].BestS
		switch {
		case a != nil && b != nil && *a != *b:
			return *a < *b
		case (a == nil) != (b == nil):
			return a != nil
		}
		return out[i].Runs > out[j].Runs
	})
	return out
}

// Summarize builds the Dungeons tab's list from runs (any order), keeping the dungeons that have a run matching f.
func Summarize(runs []*Run, f Filter) []DungeonSummary {
	byMap := map[uint32][]*Run{}
	for _, r := range runs {
		if f.match(r) {
			byMap[r.Map] = append(byMap[r.Map], r)
		}
	}
	out := make([]DungeonSummary, 0, len(byMap))
	for m, rs := range byMap {
		SortRuns(rs)
		st, best := StatOf(rs)
		ds := DungeonSummary{Map: m, Name: rs[0].Dungeon, Stat: st, Bosses: BossStats(rs)}
		if best != nil {
			ds.BestRun = best.Run
		}
		last := rs[0]
		ds.Last = brief(last)
		if last.Outcome == OutcomeCleared && last.ClearS != nil && st.BestS != nil {
			ds.DeltaS = newF(*last.ClearS - *st.BestS)
		}
		ds.Builds = groups(rs, func(r *Run) string { return r.Build })
		ds.Drivers = groups(rs, func(r *Run) string { return r.Driver.Key })
		out = append(out, ds)
	}
	// most recently run first
	sort.Slice(out, func(i, j int) bool {
		if out[i].Last.Started != out[j].Last.Started {
			return out[i].Last.Started > out[j].Last.Started
		}
		return out[i].Map < out[j].Map
	})
	return out
}

// Facets are the values the page offers in its filters, over all runs.
type Facets struct {
	Builds  []string `json:"builds"`
	Drivers []string `json:"drivers"`
	Modes   []string `json:"modes"`
}

// FacetsOf lists the builds, drivers and modes seen in runs, sorted.
func FacetsOf(runs []*Run) Facets {
	b, d, m := map[string]bool{}, map[string]bool{}, map[string]bool{}
	for _, r := range runs {
		if r.Build != "" {
			b[r.Build] = true
		}
		d[r.Driver.Key] = true
		if r.Mode != "" {
			m[r.Mode] = true
		}
	}
	keys := func(s map[string]bool) []string {
		out := make([]string, 0, len(s))
		for k := range s {
			out = append(out, k)
		}
		sort.Strings(out)
		return out
	}
	return Facets{Builds: keys(b), Drivers: keys(d), Modes: keys(m)}
}

package dungeons

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"testing"
	"time"
)

// copyFixtures copies testdata/traces into a fresh temp dir the test may change.
func copyFixtures(t *testing.T) string {
	t.Helper()
	dst := t.TempDir()
	entries, err := os.ReadDir("testdata/traces")
	if err != nil {
		t.Fatal(err)
	}
	for _, e := range entries {
		data, err := os.ReadFile(filepath.Join("testdata/traces", e.Name()))
		if err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(filepath.Join(dst, e.Name()), data, 0o644); err != nil {
			t.Fatal(err)
		}
	}
	return dst
}

func find(t *testing.T, ds []DungeonSummary, id uint32) DungeonSummary {
	t.Helper()
	for _, d := range ds {
		if d.Map == id {
			return d
		}
	}
	t.Fatalf("dungeon %d not in the list", id)
	return DungeonSummary{}
}

func f(v float64) *float64 { return &v }

func eq(t *testing.T, what string, got *float64, want float64) {
	t.Helper()
	if got == nil || *got != want {
		t.Fatalf("%s = %v, want %v", what, got, want)
	}
}

func TestParseIssueSchema(t *testing.T) {
	data, err := os.ReadFile("testdata/traces/zf-ab-1-s2.summary.json")
	if err != nil {
		t.Fatal(err)
	}
	r, ok, err := Parse(data, "zf-ab-1-s2.summary.json")
	if err != nil || !ok {
		t.Fatalf("Parse: ok=%v err=%v", ok, err)
	}
	if r.Map != 209 || r.Dungeon != "Zul'Farrak" || r.Outcome != OutcomeCleared || r.Mode != "commander" {
		t.Fatalf("identity wrong: %+v", r)
	}
	eq(t, "clear_s", r.ClearS, 4260)
	if len(r.Bosses) != 3 || r.Bosses[0].Name != "Antu'sul" || r.Bosses[0].KilledAtS == nil || *r.Bosses[0].KilledAtS != 2280 {
		t.Fatalf("bosses wrong: %+v", r.Bosses)
	}
	if r.WipeCount != 2 || len(r.Wipes) != 2 || r.Wipes[1].X == nil || *r.Wipes[1].X != 1636.1 || r.Wipes[1].PullGroup != "9" || r.Wipes[1].Role != "heal" {
		t.Fatalf("wipes wrong: %+v", r.Wipes)
	}
	if r.DeathCount != 4 || r.GMAssists["revive"] != 8 || len(r.Party) != 5 {
		t.Fatalf("deaths/assists/party wrong: %d %v %d", r.DeathCount, r.GMAssists, len(r.Party))
	}
	if r.Driver.Model != "sonnet" || r.Driver.Effort != "medium" || r.Driver.Label != "zf-ab-1-s2" || r.Driver.Key != "sonnet · medium" {
		t.Fatalf("driver wrong: %+v", r.Driver)
	}
	if r.Build != "19z4 / 9adde948" || r.Plan != "zf-full @4177b48a" || r.Furthest != "leg 8, group 12" {
		t.Fatalf("build/plan/furthest wrong: %q %q %q", r.Build, r.Plan, r.Furthest)
	}
	want := time.Date(2026, 10, 15, 9, 2, 11, 0, time.UTC).Unix()
	if r.Started != want || r.DurationS != 4709 {
		t.Fatalf("times wrong: started %d (want %d) duration %v", r.Started, want, r.DurationS)
	}
}

func TestParseLegacyShapeAndDriverObject(t *testing.T) {
	data, _ := os.ReadFile("testdata/traces/legacy-uld.summary.json")
	r, ok, err := Parse(data, "legacy-uld.summary.json")
	if err != nil || !ok {
		t.Fatalf("legacy: ok=%v err=%v", ok, err)
	}
	// what the recorder writes today: no outcome, no bosses; the page must still list the run
	if r.Map != 70 || r.Dungeon != "Uldaman" || r.Outcome != OutcomeUnknown || r.Driver.Text != DriverUnknown || r.DeathCount != 3 || r.ClearS != nil {
		t.Fatalf("legacy wrong: %+v", r)
	}
	if r.DurationS != 3600 || r.Started != 1760500000 || r.Build != "2026-10-14-v3" {
		t.Fatalf("legacy times/build wrong: %+v", r)
	}
	data, _ = os.ReadFile("testdata/traces/uldaman-clean-1.summary.json")
	r, _, _ = Parse(data, "uldaman-clean-1.summary.json")
	if r.Driver.Key != "sonnet · medium" || r.Driver.Label != "uldaman-clean-1" {
		t.Fatalf("object driver wrong: %+v", r.Driver)
	}
	if d := ParseDriver(json.RawMessage(`"human"`)); d.Key != "human" || d.Effort != "" {
		t.Fatalf("human driver wrong: %+v", d)
	}
	if d := ParseDriver(nil); d.Key != DriverUnknown {
		t.Fatalf("missing driver must be unknown: %+v", d)
	}
}

func TestParseRunningIgnoredAndCorrupt(t *testing.T) {
	data, _ := os.ReadFile("testdata/traces/zf-running.summary.json")
	if r, ok, _ := Parse(data, "zf-running.summary.json"); !ok || r.Outcome != OutcomeRunning {
		t.Fatalf("running run: ok=%v %+v", ok, r)
	}
	data, _ = os.ReadFile("testdata/traces/openworld.summary.json")
	if _, ok, err := Parse(data, "openworld.summary.json"); ok || err != nil {
		t.Fatalf("an open-world trace must be ignored, not an error: ok=%v err=%v", ok, err)
	}
	for _, bad := range []string{`{"run":"x","status":"comp`, `[1,2]`, ``, `null`} {
		if _, _, err := Parse([]byte(bad), "x.summary.json"); err == nil {
			t.Fatalf("%q must be an error", bad)
		}
	}
	// a boss not reached has a null kill time, never 0
	r, _, _ := Parse([]byte(`{"run":"n","map":209,"outcome":"stopped","bosses":[{"entry":1,"name":"A","killed_at_s":null},{"name":"B","killed_at_s":-3}]}`), "n.summary.json")
	if r.Bosses[0].KilledAtS != nil || r.Bosses[1].KilledAtS != nil {
		t.Fatalf("null and negative kill times must read as not reached: %+v", r.Bosses)
	}
}

func TestIndexStats(t *testing.T) {
	ix := NewIndex(copyFixtures(t))
	ix.Scan()
	m := ix.Meta()
	if !m.DirFound || m.Files != 11 || m.Runs != 9 || m.Ignored != 1 || m.Corrupt != 1 {
		t.Fatalf("meta wrong: %+v", m)
	}
	ds := ix.Dungeons(Filter{})
	if len(ds) != 2 {
		t.Fatalf("want 2 dungeons, got %d", len(ds))
	}
	zf := find(t, ds, 209)
	// 6 ZF runs: 4 cleared, 1 stopped, 1 still running
	if zf.Runs != 6 || zf.Cleared != 4 || zf.Stopped != 1 || zf.Running != 1 {
		t.Fatalf("zf counts wrong: %+v", zf.Stat)
	}
	eq(t, "zf success", zf.SuccessRate, 0.8) // 4 cleared of 5 finished
	eq(t, "zf best", zf.BestS, 3420)
	if zf.BestRun != "zf-ab-2-s1" {
		t.Fatalf("best run = %q", zf.BestRun)
	}
	// clears 3420, 3810, 3900, 4260: median is the mean of the middle two
	eq(t, "zf median", zf.MedianS, 3855)
	if zf.Wipes != 4 || zf.Deaths != 4+1+1+3+1 {
		t.Fatalf("zf wipes/deaths wrong: %d %d", zf.Wipes, zf.Deaths)
	}
	if len(zf.Bosses) != 3 || zf.Bosses[0].Name != "Antu'sul" {
		t.Fatalf("zf bosses wrong: %+v", zf.Bosses)
	}
	eq(t, "antu best", zf.Bosses[0].BestS, 1800)
	eq(t, "velratha best", zf.Bosses[2].BestS, 3420)
	if zf.Bosses[0].Kills != 5 || zf.Bosses[2].Kills != 4 {
		t.Fatalf("kill counts wrong: %+v", zf.Bosses)
	}
	// the last ZF run is the running one: no delta; it is the most recently started
	if zf.Last == nil || zf.Last.Run != "zf-running" || zf.DeltaS != nil {
		t.Fatalf("zf last wrong: %+v delta %v", zf.Last, zf.DeltaS)
	}

	uld := find(t, ds, 70)
	if uld.Runs != 3 || uld.Cleared != 1 || uld.Partial != 1 || uld.Unknown != 1 {
		t.Fatalf("uldaman counts wrong: %+v", uld.Stat)
	}
	eq(t, "uld success", uld.SuccessRate, 0.5) // the unknown legacy run is not counted as finished
	eq(t, "uld best", uld.BestS, 4700)
	if uld.Last.Run != "uldaman-clean-1" {
		t.Fatalf("uld last = %q", uld.Last.Run)
	}
	eq(t, "uld delta", uld.DeltaS, 0) // the last run is the best

	// by build and by driver
	var z4 *Group
	for i := range zf.Builds {
		if zf.Builds[i].Key == "19z4 / 9adde948" {
			z4 = &zf.Builds[i]
		}
	}
	if z4 == nil || z4.Runs != 4 || z4.Cleared != 3 {
		t.Fatalf("build group wrong: %+v", zf.Builds)
	}
	if zf.Drivers[0].Key != "haiku · max" { // best clear first
		t.Fatalf("drivers not ordered by best clear: %+v", zf.Drivers)
	}
}

func TestFilterAndFacets(t *testing.T) {
	ix := NewIndex(copyFixtures(t))
	ix.Scan()
	zf := find(t, ix.Dungeons(Filter{Driver: "haiku · max"}), 209)
	if zf.Runs != 2 || zf.Cleared != 1 || zf.Stopped != 1 {
		t.Fatalf("driver filter wrong: %+v", zf.Stat)
	}
	if len(ix.Dungeons(Filter{Mode: "observe"})) != 0 {
		t.Fatal("no run is in observe mode")
	}
	fc := ix.Facets()
	if len(fc.Drivers) < 3 || len(fc.Builds) < 3 || len(fc.Modes) != 1 || fc.Modes[0] != "commander" {
		t.Fatalf("facets wrong: %+v", fc)
	}
}

func TestBossColumnsBestPerBoss(t *testing.T) {
	ix := NewIndex(copyFixtures(t))
	ix.Scan()
	runs, total := ix.Runs(209, Filter{}, 0)
	if total != 6 || len(runs) != 6 {
		t.Fatalf("runs = %d/%d", len(runs), total)
	}
	cols := BossColumns(runs)
	if len(cols) != 3 || cols[1].Name != "Theka the Martyr" {
		t.Fatalf("columns wrong: %+v", cols)
	}
	eq(t, "theka best", cols[1].BestS, 1920)
	if cols[1].BestRun != "zf-ab-2-s1" || cols[0].BestRun != "zf-ab-2-s1" {
		t.Fatalf("best runs wrong: %+v", cols)
	}
	// limit keeps the newest and still reports the total
	few, total := ix.Runs(209, Filter{}, 2)
	if len(few) != 2 || total != 6 || few[0].Run != "zf-running" {
		t.Fatalf("limit wrong: %d %d %s", len(few), total, few[0].Run)
	}
}

func TestRefreshOnNewAndChangedFile(t *testing.T) {
	dir := copyFixtures(t)
	ix := NewIndex(dir)
	if !ix.Scan() {
		t.Fatal("the first scan must report a change")
	}
	if ix.Scan() {
		t.Fatal("a scan with nothing new must report no change")
	}
	v := ix.version
	// a new run appears
	newRun := `{"run":"zf-new","dungeon":{"map":209,"name":"Zul'Farrak"},"started":"2026-10-17T10:00:00Z","outcome":"cleared","bosses":[{"entry":8127,"name":"Antu'sul","killed_at_s":1500}],"clear_s":3000}`
	if err := os.WriteFile(filepath.Join(dir, "zf-new.summary.json"), []byte(newRun), 0o644); err != nil {
		t.Fatal(err)
	}
	if !ix.Scan() || ix.version == v {
		t.Fatal("a new file must be picked up")
	}
	zf := find(t, ix.Dungeons(Filter{}), 209)
	eq(t, "best after the new run", zf.BestS, 3000)
	if zf.Last.Run != "zf-new" {
		t.Fatalf("last = %q", zf.Last.Run)
	}
	// delta of a last run that is not the best: the new run was slower than 3000? make a slower one
	slow := `{"run":"zf-slow","dungeon":{"map":209},"started":"2026-10-17T11:00:00Z","outcome":"cleared","clear_s":3500}`
	_ = os.WriteFile(filepath.Join(dir, "zf-slow.summary.json"), []byte(slow), 0o644)
	ix.Scan()
	zf = find(t, ix.Dungeons(Filter{}), 209)
	eq(t, "delta vs best", zf.DeltaS, 500)
	// the running summary is rewritten when the run ends (same name, new content and size)
	done := `{"run":"zf-running","status":"complete","map":209,"started":"2026-10-17T12:00:00Z","outcome":"wiped_out","wipes":[{"t_s":10,"x":1,"y":2}]}`
	if err := os.WriteFile(filepath.Join(dir, "zf-running.summary.json"), []byte(done), 0o644); err != nil {
		t.Fatal(err)
	}
	ix.Scan()
	zf = find(t, ix.Dungeons(Filter{}), 209)
	if zf.WipedOut != 1 || zf.Running != 0 {
		t.Fatalf("a rewritten summary must be re-read: %+v", zf.Stat)
	}
	// and a deleted file leaves the index
	_ = os.Remove(filepath.Join(dir, "zf-slow.summary.json"))
	ix.Scan()
	if got := find(t, ix.Dungeons(Filter{}), 209).Runs; got != 7 {
		t.Fatalf("runs after delete = %d, want 7", got)
	}
}

func TestCorruptFileSkippedNotRetried(t *testing.T) {
	dir := copyFixtures(t)
	ix := NewIndex(dir)
	ix.Scan()
	if ix.Meta().Corrupt != 1 {
		t.Fatalf("the truncated summary must count as corrupt: %+v", ix.Meta())
	}
	// every other run is still served
	if find(t, ix.Dungeons(Filter{}), 209).Runs != 6 {
		t.Fatal("a corrupt file must not hide the others")
	}
	if ix.Scan() {
		t.Fatal("an unchanged corrupt file must not be re-read on every scan")
	}
	// fixed on disk: picked up
	fixed := `{"run":"broken","map":70,"outcome":"stopped"}`
	_ = os.WriteFile(filepath.Join(dir, "broken.summary.json"), []byte(fixed), 0o644)
	if !ix.Scan() || ix.Meta().Corrupt != 0 || find(t, ix.Dungeons(Filter{}), 70).Runs != 4 {
		t.Fatalf("a repaired file must be indexed: %+v", ix.Meta())
	}
}

func TestEmptyAndMissingFolder(t *testing.T) {
	ix := NewIndex(t.TempDir())
	ix.Scan()
	if m := ix.Meta(); !m.DirFound || m.Runs != 0 || m.Files != 0 {
		t.Fatalf("empty folder meta wrong: %+v", m)
	}
	if ds := ix.Dungeons(Filter{}); ds == nil || len(ds) != 0 {
		t.Fatalf("empty folder must give an empty (non-nil) list, got %v", ds)
	}
	missing := NewIndex(filepath.Join(t.TempDir(), "nope"))
	missing.Scan()
	if m := missing.Meta(); m.DirFound || !m.Explicit || m.Runs != 0 {
		t.Fatalf("missing folder meta wrong: %+v", m)
	}
	// the folder appears later (tracing switched on): the next scan finds it
	dir := filepath.Join(t.TempDir(), "traces")
	late := NewIndex(dir)
	late.Scan()
	_ = os.MkdirAll(dir, 0o755)
	_ = os.WriteFile(filepath.Join(dir, "a.summary.json"), []byte(`{"run":"a","map":70,"outcome":"cleared","clear_s":100}`), 0o644)
	late.Scan()
	if m := late.Meta(); !m.DirFound || m.Runs != 1 {
		t.Fatalf("a folder created later must be found: %+v", m)
	}
}

func TestHTTP(t *testing.T) {
	ix := NewIndex(copyFixtures(t))
	ix.Scan()
	mux := http.NewServeMux()
	ix.Register(mux, func(h http.HandlerFunc) http.HandlerFunc { return h })

	get := func(path string) *httptest.ResponseRecorder {
		rec := httptest.NewRecorder()
		mux.ServeHTTP(rec, httptest.NewRequest(http.MethodGet, path, nil))
		return rec
	}
	rec := get("/api/v1/dungeons")
	if rec.Code != 200 {
		t.Fatalf("list: %d", rec.Code)
	}
	var lv ListView
	if err := json.Unmarshal(rec.Body.Bytes(), &lv); err != nil || len(lv.Dungeons) != 2 || lv.Meta.Runs != 9 || lv.Hint == "" {
		t.Fatalf("list body wrong: %v %+v", err, lv.Meta)
	}
	rec = get("/api/v1/dungeons?driver=" + "haiku%20%C2%B7%20max")
	_ = json.Unmarshal(rec.Body.Bytes(), &lv)
	if len(lv.Dungeons) != 1 || lv.Dungeons[0].Runs != 2 {
		t.Fatalf("filtered list wrong: %+v", lv.Dungeons)
	}

	rec = get("/api/v1/dungeons/209/runs?limit=3")
	var rv RunsView
	if rec.Code != 200 || json.Unmarshal(rec.Body.Bytes(), &rv) != nil || rv.Total != 6 || len(rv.Runs) != 3 || rv.Name != "Zul'Farrak" || len(rv.Bosses) != 3 {
		t.Fatalf("runs wrong: %d %+v", rec.Code, rv)
	}
	if rv.Runs[0].Wipes == nil || rv.Runs[0].Bosses == nil {
		t.Fatal("lists must serialise as [], never null")
	}
	if get("/api/v1/dungeons/abc/runs").Code != 400 || get("/api/v1/dungeons/0/runs").Code != 400 {
		t.Fatal("a bad map id must be 400")
	}
	if get("/api/v1/dungeons/33/runs").Code != 404 {
		t.Fatal("a map with no run must be 404")
	}
	// GET only
	rec = httptest.NewRecorder()
	mux.ServeHTTP(rec, httptest.NewRequest(http.MethodPost, "/api/v1/dungeons", nil))
	if rec.Code != http.StatusMethodNotAllowed {
		t.Fatalf("POST must be refused, got %d", rec.Code)
	}
}

// TestDumpAPIForNode writes the two API answers for the fixtures to $DG_DUMP (the node check of the page reads them from
// internal/commandersjs/testdata). Skipped unless DG_DUMP is set:
//
//	DG_DUMP=../commandersjs/testdata go test ./internal/dungeons -run TestDumpAPIForNode
func TestDumpAPIForNode(t *testing.T) {
	out := os.Getenv("DG_DUMP")
	if out == "" {
		t.Skip("DG_DUMP not set")
	}
	ix := NewIndex(copyFixtures(t))
	ix.Scan()
	mux := http.NewServeMux()
	ix.Register(mux, func(h http.HandlerFunc) http.HandlerFunc { return h })
	for name, path := range map[string]string{"dungeons_list.json": "/api/v1/dungeons", "dungeons_runs_209.json": "/api/v1/dungeons/209/runs"} {
		rec := httptest.NewRecorder()
		mux.ServeHTTP(rec, httptest.NewRequest(http.MethodGet, path, nil))
		var v any
		if err := json.Unmarshal(rec.Body.Bytes(), &v); err != nil {
			t.Fatal(err)
		}
		pretty, _ := json.MarshalIndent(v, "", " ")
		if err := os.WriteFile(filepath.Join(out, name), append(pretty, '\n'), 0o644); err != nil {
			t.Fatal(err)
		}
	}
}

func TestOutcomeMapping(t *testing.T) {
	for given, want := range map[string]string{
		"cleared": OutcomeCleared, "partial": OutcomePartial, "abandoned": OutcomeAbandoned, "wiped_out": OutcomeWipedOut, "stopped": OutcomeStopped,
		"CLEARED": OutcomeCleared, "": OutcomeUnknown,
		// not in the issue's list: not accepted
		"clear": OutcomeUnknown, "complete": OutcomeUnknown, "done": OutcomeUnknown, "wiped": OutcomeUnknown,
	} {
		js := `{"run":"o","map":209,"status":"complete","outcome":"` + given + `"}`
		r, _, err := Parse([]byte(js), "o.summary.json")
		if err != nil || r.Outcome != want {
			t.Fatalf("outcome %q -> %q, want %q (err %v)", given, r.Outcome, want, err)
		}
	}
	// a live run is running even if it carries an outcome or all its bosses are dead; no outcome while running is the recorder's normal shape
	for _, js := range []string{
		`{"run":"o","map":209,"status":"running"}`,
		`{"run":"o","map":209,"status":"running","outcome":"cleared","bosses":[{"name":"A","killed_at_s":5}]}`,
		`{"run":"o","map":209,"status":"RUNNING","outcome":"wiped_out"}`,
	} {
		r, _, _ := Parse([]byte(js), "o.summary.json")
		if r.Outcome != OutcomeRunning || r.ClearS != nil {
			t.Fatalf("%s -> %q clear %v", js, r.Outcome, r.ClearS)
		}
	}
	// build object, ISO times with the unix fallback, stop_reason over end_reason
	r, _, _ := Parse([]byte(`{"run":"b","map":209,"build":{"image":"19z4","module":"2026-10-16-v2","core":"c0ffee0123456789c0ffee0123456789c0ffee01"},"started":"2026-10-15T09:00:00Z","ended":"2026-10-15T09:10:00Z","stop_reason":"plan_done","end_reason":"stop","outcome":"cleared"}`), "b.summary.json")
	if r.Build != "19z4 / 2026-10-16-v2 / core c0ffee01" || r.Started != time.Date(2026, 10, 15, 9, 0, 0, 0, time.UTC).Unix() || r.DurationS != 600 || r.StopReason != "plan_done" {
		t.Fatalf("build/times/stop_reason wrong: %+v", r)
	}
	r, _, _ = Parse([]byte(`{"run":"b","map":209,"start_unix":1760500000,"end_unix":1760500600,"outcome":"stopped"}`), "b.summary.json")
	if r.Started != 1760500000 || r.Ended != 1760500600 || r.DurationS != 600 {
		t.Fatalf("unix fallback wrong: %+v", r)
	}
}

func TestShortSha(t *testing.T) {
	sha := "9adde948cafe0123456789abcdef0123456789ab"
	if got := shortSha(sha); got != "9adde948" {
		t.Fatalf("a 40-hex sha must be cut to 8, got %q", got)
	}
	for _, keep := range []string{"2026-10-16-v2", "9adde948", "9adde948cafe", "zzzzz948cafe0123456789abcdef0123456789ab"} {
		if got := shortSha(keep); got != keep {
			t.Fatalf("%q must be kept whole, got %q", keep, got)
		}
	}
}

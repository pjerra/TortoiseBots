package dungeons

import (
	"os"
	"path/filepath"
	"testing"
)

// The summary file the ForeverCore module writes for a finished run (forever/src/dash/TbDashRunsPure.h SummaryJson; the fixture is printed by
// forever/tools/dash_runs_check.cpp --print-summary): the Dungeon runs tab indexes it, reads its outcome and counts, and shows no boss time
// (the run keeps none yet: killed_at_s is null).
func TestForeverSummaryIsIndexed(t *testing.T) {
	dir := t.TempDir()
	data, err := os.ReadFile("testdata/forever/ragefire_chasm-1763560000-7.summary.json")
	if err != nil {
		t.Fatal(err)
	}
	// a stopped run of the same dungeon, as the module writes one (its stop reason says why)
	stopped := []byte(`{"run":"ragefire_chasm-1763561000-7","mode":"forever-run","plan":"ragefire_chasm","map":389,"dungeon":{"map":389},"build":{"module":"forever-port"},"driver":"human","started":1763561000,"ended":1763561100,"duration_ms":100000,"status":"stopped","outcome":"wiped_out","stop_reason":"the party wiped","bosses":[{"entry":11520,"name":"Taragaman the Hungerer","killed_at_s":null,"killed":false}],"wipes":2,"deaths":9,"party":[{"class":"warrior","role":"tank","level":18}]}`)
	for name, body := range map[string][]byte{"ragefire_chasm-1763560000-7.summary.json": data, "ragefire_chasm-1763561000-7.summary.json": stopped} {
		if err := os.WriteFile(filepath.Join(dir, name), body, 0o644); err != nil {
			t.Fatal(err)
		}
	}
	// the writer's temporary file is never read as a summary
	if err := os.WriteFile(filepath.Join(dir, "ragefire_chasm-1763562000-7.summary.json.tmp"), []byte(`{"run":`), 0o644); err != nil {
		t.Fatal(err)
	}
	ix := NewIndex(dir)
	ix.Scan()
	m := ix.Meta()
	if !m.DirFound || m.Files != 2 || m.Runs != 2 || m.Corrupt != 0 || m.Ignored != 0 {
		t.Fatalf("meta: %+v", m)
	}
	runs, total := ix.Runs(389, Filter{}, 10)
	if total != 2 || len(runs) != 2 {
		t.Fatalf("runs: %d of %d", len(runs), total)
	}
	stoppedRun, cleared := runs[0], runs[1] // newest first
	if cleared.Outcome != OutcomeCleared || cleared.Dungeon != "Ragefire Chasm" || cleared.Plan != "ragefire_chasm" || cleared.Map != 389 {
		t.Fatalf("cleared run: %+v", cleared)
	}
	if cleared.DeathCount != 1 || cleared.WipeCount != 0 || cleared.DurationS != 400 || cleared.Started != 1763560000 || cleared.Status != "done" {
		t.Fatalf("cleared run counts: %+v", cleared)
	}
	if len(cleared.Bosses) != 1 || cleared.Bosses[0].Entry != 11520 || cleared.Bosses[0].Name != "Taragaman the Hungerer" || cleared.Bosses[0].KilledAtS != nil {
		t.Fatalf("bosses must carry no kill time yet: %+v", cleared.Bosses)
	}
	if len(cleared.Party) != 3 || cleared.Party[0].Class != "warrior" || cleared.Party[0].Role != "tank" || cleared.Party[0].Level != 18 {
		t.Fatalf("party: %+v", cleared.Party)
	}
	if cleared.Driver.Key != "human" || cleared.Build == "" {
		t.Fatalf("driver/build: %+v %q", cleared.Driver, cleared.Build)
	}
	if stoppedRun.Outcome != OutcomeWipedOut || stoppedRun.StopReason != "the party wiped" || stoppedRun.WipeCount != 2 || stoppedRun.DeathCount != 9 {
		t.Fatalf("wiped run: %+v", stoppedRun)
	}
	ds := ix.Dungeons(Filter{})
	if len(ds) != 1 || ds[0].Runs != 2 || ds[0].Cleared != 1 {
		t.Fatalf("dungeons: %+v", ds)
	}
}

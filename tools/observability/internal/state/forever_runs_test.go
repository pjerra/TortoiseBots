package state

import (
	"encoding/json"
	"os"
	"strings"
	"testing"

	"tortoise-observability/internal/model"
)

// The fixtures are what the ForeverCore module sends: forever/tools/dash_runs_check.cpp builds forever_commanders.json with the Turtle
// builder (and pins it byte for byte), the DUNGEON_MOBS one is the roster sender's own format (forever/src/TbDashboard.cpp SendDungeonMobs).
func readFixture(t *testing.T, name string) []byte {
	t.Helper()
	data, err := os.ReadFile("testdata/" + name)
	if err != nil {
		t.Fatal(err)
	}
	return data
}

// U1 of the spec: the store takes a COMMANDERS binding with orders:[], no mobs/objects and no COMMANDER_POS, and /api/v1/commanders carries the
// run with its plan_steps and notes. The empty list that follows the last run clears the card at once.
func TestForeverCommandersBinding(t *testing.T) {
	s := newTestStore(newClock())
	var p model.CommandersPayload
	if err := json.Unmarshal(readFixture(t, "forever_commanders.json"), &p); err != nil {
		t.Fatal(err)
	}
	if !s.ApplyCommanders(&p) {
		t.Fatal("the module's COMMANDERS datagram was refused")
	}
	view := s.Commanders()
	if !view.Fresh || len(view.Commanders) != 1 {
		t.Fatalf("view: %+v", view)
	}
	b := view.Commanders[0]
	if b.Commander != "Mcmd" || b.Lead != "Mtank" || b.Group != 3 || !b.Online || len(b.Bots) != 3 || b.BoundAt != 1763560000 {
		t.Fatalf("binding: %+v", b)
	}
	if b.Run == nil || b.Run.Plan != "ragefire_chasm" || b.Run.State != "running" || b.Run.Step != 3 || b.Run.Steps != 6 || b.Run.Leg != "entrance" {
		t.Fatalf("run: %+v", b.Run)
	}
	if len(b.Run.PlanSteps) != 3 || b.Run.PlanSteps[1].Kind != "pull" || b.Run.PlanSteps[1].Entry != 11321 || b.Run.PlanSteps[1].Label != "first pack" {
		t.Fatalf("plan_steps: %+v", b.Run.PlanSteps)
	}
	if len(b.Run.Notes) != 4 || b.Run.Notes[2].Phase != "death" || b.Run.Notes[2].Note != "Mdps" {
		t.Fatalf("notes: %+v", b.Run.Notes)
	}
	if b.Bots[2].Alive || b.Bots[0].Role != "tank" {
		t.Fatalf("bots: %+v", b.Bots)
	}
	out, err := json.Marshal(view)
	if err != nil {
		t.Fatal(err)
	}
	for _, want := range []string{`"plan_steps":[`, `"orders":[]`, `"run":{"plan":"ragefire_chasm"`} {
		if !strings.Contains(string(out), want) {
			t.Errorf("view lacks %s: %s", want, out)
		}
	}
	if b.Mobs != nil || b.Objects != nil || b.Combat != nil {
		t.Errorf("the module reports no mobs, objects or combat window: %+v", b)
	}

	// the empty list after the last run: the card goes at once, not after the stale timer
	var empty model.CommandersPayload
	if err := json.Unmarshal(readFixture(t, "forever_commanders_empty.json"), &empty); err != nil {
		t.Fatal(err)
	}
	if !s.ApplyCommanders(&empty) {
		t.Fatal("the empty list was refused")
	}
	if v := s.Commanders(); !v.Fresh || len(v.Commanders) != 0 {
		t.Fatalf("after the empty list: %+v", v)
	}
}

// The roster sender's DUNGEON_MOBS carries the raid flag: a raid copy with bots in it leaves /api/v1/dungeons for /api/v1/raids (roster only),
// a dungeon copy stays where it is.
func TestForeverRaidCopyMovesToRaids(t *testing.T) {
	s := newTestStore(newClock())
	if ok := s.ApplyBatch(batch(1, 0, 1,
		du(1, "Mtank", false, 409, 13),
		du(2, "Mheal", false, 409, 13),
		du(3, "Mdps", false, 389, 3))); !ok {
		t.Fatal("batch not published")
	}
	var p model.DungeonMobsPayload
	if err := json.Unmarshal(readFixture(t, "forever_dungeon_mobs_raid.json"), &p); err != nil {
		t.Fatal(err)
	}
	s.ApplyDungeonMobs(&p)

	dungeons := s.Dungeons()
	if len(dungeons.Runs) != 1 || dungeons.Runs[0].MapID != 389 || dungeons.Runs[0].Instance != 3 {
		t.Fatalf("dungeons: %+v", dungeons.Runs)
	}
	raids := s.Raids()
	if len(raids.Runs) != 1 || raids.Runs[0].MapID != 409 || raids.Runs[0].Instance != 13 {
		t.Fatalf("raids: %+v", raids.Runs)
	}
	if raids.Live {
		t.Fatalf("no RAIDS datagram came in: the view is roster-only, not live: %+v", raids)
	}
}

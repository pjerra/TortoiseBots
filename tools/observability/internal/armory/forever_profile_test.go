package armory

import (
	"os"
	"path/filepath"
	"testing"

	"tortoise-observability/internal/foreveritems"
)

// testForeverCatalog writes the fixture data files (spells, names, client spell text) and loads them the way the daemon does.
func testForeverCatalog(t *testing.T) *ForeverCatalog {
	t.Helper()
	dataDir, artDir := t.TempDir(), t.TempDir()
	write := func(dir, name, body string) {
		if err := os.WriteFile(filepath.Join(dir, name), []byte(body), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	write(dataDir, "tortoisebots-spells.json", `{"spells":[
		{"id":78,"name":"Heroic Strike","icon":132282,"school":1,"range":5,"level":1,"base_level":1,"power_type":1,"cost":150},
		{"id":12282,"name":"Improved Heroic Strike","icon":132282,"school":1},
		{"id":12286,"name":"Improved Rend","icon":132155,"school":1},
		{"id":12328,"name":"Death Wish","icon":136146,"school":1,"cooldown_ms":180000,"power_type":1,"cost":100,"base_level":40},
		{"id":1459,"name":"Arcane Intellect","icon":135932,"school":64,"duration_ms":1800000}]}`)
	write(dataDir, "tortoisebots-names.json", `{"skills":{"43":{"name":"Swords","category":6,"icon":135274}},"areas":{},"factions":{"72":{"name":"Stormwind","parent":469}}}`)
	write(artDir, "spells-text.json", `{
		"78":{"subtext":"Rank 1","desc":"A strong attack that increases melee damage by $s1.","values":[11]},
		"12282":{"subtext":"Rank 1","desc":"Reduces the cost of your Heroic Strike ability by $s1 rage point.","values":[1],"passive":true}}`)
	write(dataDir, "tortoisebots-talents.json", `{"classes":[{"class":1,"tabs":[
		[{"node":130,"entry":1300,"spell":12282,"name":"Improved Heroic Strike","icon":132282,"max":3,"row":0,"col":0},
		 {"node":127,"entry":1270,"spell":12286,"name":"Improved Rend","icon":132155,"max":3,"row":0,"col":2}],
		[{"node":165,"entry":1650,"spell":12328,"name":"Death Wish","icon":136146,"max":1,"row":4,"col":1}],
		[{"node":138,"entry":1380,"spell":12298,"name":"Shield Specialization","icon":134952,"max":5,"row":0,"col":1}]]}]}`)
	return LoadForeverCatalog(dataDir, artDir, foreveritems.NewLookups(dataDir, artDir))
}

func TestForeverSpellEntry(t *testing.T) {
	svc := &Service{cfg: Config{Core: "forever", ForeverData: testForeverCatalog(t)}}

	hs := svc.foreverSpellEntry(78, 1, 0)
	if hs.Name != "Heroic Strike" || hs.Subtext != "Rank 1" || hs.Icon != "132282" || hs.Cost != 150 || !hs.ClassSpell || hs.Passive {
		t.Fatalf("unexpected Heroic Strike entry: %+v", hs)
	}
	if len(hs.Values) != 1 || hs.Values[0] != 11 {
		t.Fatalf("expected Values=[11], got %+v", hs.Values)
	}

	ihs := svc.foreverSpellEntry(12282, 1, 0)
	if !ihs.Passive {
		t.Fatalf("expected Improved Heroic Strike to be passive: %+v", ihs)
	}
}

func TestBuildForeverTalentsFixture(t *testing.T) {
	cat := testForeverCatalog(t)
	// Ranks by trait node; a rank over the talent's max (stale data) is capped.
	trees := buildForeverTalents(cat, 1, map[uint32]uint32{130: 2, 127: 1, 165: 4})
	if len(trees) != 3 {
		t.Fatalf("want 3 warrior trees, got %d", len(trees))
	}
	if trees[0].Name != "Arms" || trees[0].Points != 3 || trees[0].Talents[0].Name != "Improved Heroic Strike" || trees[0].Talents[0].MaxRank != 3 {
		t.Fatalf("Arms tree mismatch: %+v", trees[0])
	}
	if trees[1].Name != "Fury" || trees[1].Points != 1 {
		t.Fatalf("Fury tree mismatch: %+v", trees[1])
	}
	if trees[2].Name != "Protection" || trees[2].Points != 0 || trees[2].Talents[0].Col != 1 {
		t.Fatalf("Protection tree mismatch: %+v", trees[2])
	}
	if spec := foreverSpecFromTrees(trees); spec != "Arms" {
		t.Fatalf("want spec Arms, got %q", spec)
	}
}

func TestEnrichForeverSkillAndReputationTier(t *testing.T) {
	svc := &Service{cfg: Config{Core: "forever", ForeverData: testForeverCatalog(t)}}
	sk := SkillEntry{Skill: 43, Value: 300, Max: 300}
	svc.enrichForeverSkill(&sk)
	if sk.Name != "Swords" || sk.Category != 6 || sk.Icon != "135274" {
		t.Fatalf("unexpected skill: %+v", sk)
	}

	cases := []struct {
		val  int32
		tier string
	}{
		{-7000, "Hated"},
		{-4000, "Hostile"},
		{-500, "Unfriendly"},
		{0, "Neutral"},
		{3500, "Friendly"},
		{12000, "Honored"},
		{25000, "Revered"},
		{42000, "Exalted"},
	}
	for _, tc := range cases {
		tier, _, _ := reputationTier(tc.val)
		if tier != tc.tier {
			t.Fatalf("standing %d: want %s, got %s", tc.val, tc.tier, tier)
		}
	}
}

func TestLoadAurasForever(t *testing.T) {
	svc := &Service{
		cfg: Config{
			Core:        "forever",
			ForeverData: testForeverCatalog(t),
			LiveAuras: func(guid uint32) [][]int64 {
				return [][]int64{{1459, 1200000, 1, 1}}
			},
		},
	}
	auras := svc.loadAurasForever(3128)
	if len(auras) != 1 || auras[0].Name != "Arcane Intellect" || auras[0].Icon != "135932" || auras[0].RemainingMs != 1200000 || !auras[0].Positive {
		t.Fatalf("unexpected auras: %+v", auras)
	}
}

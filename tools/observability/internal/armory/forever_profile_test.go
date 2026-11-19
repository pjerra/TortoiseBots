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
	cat := LoadForeverCatalog(dataDir, artDir, foreveritems.NewLookups(dataDir, artDir))
	cat.Talents = []ForeverTalent{
		{ID: 130, Tab: 161, Tier: 0, Col: 0, Ranks: []uint32{12282, 12663, 12664}},
		{ID: 127, Tab: 161, Tier: 0, Col: 1, Ranks: []uint32{12286, 12658, 12659}},
		{ID: 165, Tab: 164, Tier: 4, Col: 1, Ranks: []uint32{12328}},
		{ID: 138, Tab: 163, Tier: 0, Col: 1, Ranks: []uint32{12298}},
	}
	return cat
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
	known := map[uint32]bool{12663: true, 12328: true}
	activeTalents := map[uint32]bool{130: true, 127: true, 165: true}

	trees := buildForeverTalents(cat, 1, known, activeTalents)
	if len(trees) != 3 {
		t.Fatalf("want 3 warrior trees, got %d", len(trees))
	}
	if trees[0].Name != "Arms" || trees[0].Points != 3 {
		t.Fatalf("Arms tree mismatch: %+v", trees[0])
	}
	if trees[1].Name != "Fury" || trees[1].Points != 1 {
		t.Fatalf("Fury tree mismatch: %+v", trees[1])
	}
	if trees[2].Name != "Protection" || trees[2].Points != 0 {
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

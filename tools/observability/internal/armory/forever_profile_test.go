package armory

import "testing"

func testForeverCatalog() *ForeverCatalog {
	return &ForeverCatalog{
		Spells: map[uint32]ForeverSpell{
			78: {
				ID:         78,
				Name:       "Heroic Strike",
				Icon:       132282,
				School:     1,
				CastMs:     0,
				Range:      5,
				DurationMs: 0,
				CooldownMs: 0,
				Level:      1,
				BaseLevel:  1,
				PowerType:  1,
				Cost:       150,
			},
			12282: {
				ID:        12282,
				Name:      "Improved Heroic Strike",
				Icon:      132282,
				School:    1,
				BaseLevel: 0,
			},
			12286: {
				ID:        12286,
				Name:      "Improved Rend",
				Icon:      132155,
				School:    1,
				BaseLevel: 0,
			},
			12328: {
				ID:         12328,
				Name:       "Death Wish",
				Icon:       136146,
				School:     1,
				CooldownMs: 180000,
				PowerType:  1,
				Cost:       100,
				BaseLevel:  40,
			},
			1459: {
				ID:         1459,
				Name:       "Arcane Intellect",
				Icon:       135932,
				School:     64,
				DurationMs: 1800000,
			},
		},
		SpellText: map[uint32]ForeverSpellText{
			78: {
				Subtext: "Rank 1",
				Desc:    "A strong attack that increases melee damage by $s1.",
				Values:  []float64{11},
			},
			12282: {
				Subtext: "Rank 1",
				Desc:    "Reduces the cost of your Heroic Strike ability by $s1 rage point.",
				Values:  []float64{1},
				Passive: true,
			},
		},
		Talents: []ForeverTalent{
			{ID: 130, Tab: 161, Tier: 0, Col: 0, Ranks: []uint32{12282, 12663, 12664}},
			{ID: 127, Tab: 161, Tier: 0, Col: 1, Ranks: []uint32{12286, 12658, 12659}},
			{ID: 165, Tab: 164, Tier: 4, Col: 1, Ranks: []uint32{12328}},
			{ID: 138, Tab: 163, Tier: 0, Col: 1, Ranks: []uint32{12298}},
		},
		TalentTabs: defaultForeverTalentTabs(),
		Skills: map[uint32]ForeverSkillMeta{
			43: {Name: "Swords", Category: 6, Icon: 135274},
		},
		Factions: map[uint32]ForeverFactionMeta{
			72: {Name: "Stormwind", Parent: 469},
		},
	}
}

func TestForeverSpellEntry(t *testing.T) {
	svc := &Service{cfg: Config{Core: "forever", ForeverData: testForeverCatalog()}}

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
	cat := testForeverCatalog()
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
	svc := &Service{cfg: Config{Core: "forever", ForeverData: testForeverCatalog()}}
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
			ForeverData: testForeverCatalog(),
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

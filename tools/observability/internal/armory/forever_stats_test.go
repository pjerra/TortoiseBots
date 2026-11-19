package armory

import (
	"encoding/json"
	"testing"
)

func TestLoadStatsForeverPrefersLiveStats(t *testing.T) {
	svc := &Service{
		cfg: Config{
			Core: "forever",
			LiveStats: func(guid uint32) (CharacterStats, bool) {
				if guid == 3125 {
					return CharacterStats{
						Source:           "character_stats", // overwritten to "live"
						MaxHealth:        60,
						MaxPower2:        100,
						Strength:         23,
						ManaRegenCasting: 4,
						Defense:          300,
						DefenseMax:       300,
						BlockValue:       85,
						SpellPenetration: 18,
						Expertise:        1.5,
						Ratings:          map[string]int32{"1": 40, "5": 20},
						RatingBonus:      map[string]float64{"1": 16, "5": 1.3},
					}, true
				}
				return CharacterStats{}, false
			},
		},
	}

	var st CharacterStats
	svc.loadStatsForever(3125, &st)
	if st.Source != "live" || st.MaxHealth != 60 || st.MaxPower2 != 100 || st.Strength != 23 {
		t.Fatalf("unexpected live stats: %+v", st)
	}
	if st.ManaRegenCasting != 4 || st.Defense != 300 || st.DefenseMax != 300 || st.BlockValue != 85 || st.SpellPenetration != 18 || st.Expertise != 1.5 {
		t.Fatalf("unexpected sheet fields: %+v", st)
	}
	if st.Ratings["1"] != 40 || st.RatingBonus["5"] != 1.3 {
		t.Fatalf("unexpected ratings: %+v / %+v", st.Ratings, st.RatingBonus)
	}

	raw, err := json.Marshal(BotProfile{Stats: st})
	if err != nil {
		t.Fatalf("json.Marshal(BotProfile): %v", err)
	}
	var decoded BotProfile
	if err := json.Unmarshal(raw, &decoded); err != nil {
		t.Fatalf("json.Unmarshal(BotProfile): %v", err)
	}
	if decoded.Stats.Defense != 300 || decoded.Stats.Ratings["5"] != 20 || decoded.Stats.RatingBonus["1"] != 16 {
		t.Fatalf("profile JSON round-trip lost fields: %+v", decoded.Stats)
	}
}

func TestForeverPowerOrder(t *testing.T) {
	warrior := CharacterStats{MaxPower1: 1000, MaxPower2: 5}
	foreverPowerOrder(1, &warrior)
	if warrior.MaxPower1 != 0 || warrior.MaxPower2 != 100 {
		t.Fatalf("warrior: mana %d rage %d, want 0 100", warrior.MaxPower1, warrior.MaxPower2)
	}
	rogue := CharacterStats{MaxPower1: 100, MaxPower2: 5}
	foreverPowerOrder(4, &rogue)
	if rogue.MaxPower1 != 0 || rogue.MaxPower4 != 100 {
		t.Fatalf("rogue: mana %d energy %d, want 0 100", rogue.MaxPower1, rogue.MaxPower4)
	}
	mage := CharacterStats{MaxPower1: 165, MaxPower2: 5}
	foreverPowerOrder(8, &mage)
	if mage.MaxPower1 != 165 || mage.MaxPower2 != 0 {
		t.Fatalf("mage: mana %d rage %d, want 165 0", mage.MaxPower1, mage.MaxPower2)
	}
}

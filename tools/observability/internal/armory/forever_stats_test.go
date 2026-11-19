package armory

import "testing"

func TestLoadStatsForeverPrefersLiveStats(t *testing.T) {
	svc := &Service{
		cfg: Config{
			Core: "forever",
			LiveStats: func(guid uint32) (CharacterStats, bool) {
				if guid == 3125 {
					return CharacterStats{
						Source:    "character_stats", // overwritten to "live"
						MaxHealth: 60,
						MaxPower2: 100,
						Strength:  23,
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

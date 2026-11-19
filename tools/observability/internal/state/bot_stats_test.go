package state

import (
	"testing"
	"time"

	"tortoise-observability/internal/model"
)

func TestBotStatsTTLAndPruning(t *testing.T) {
	now := time.Unix(1700000000, 0)
	s := New(Config{RosterTTL: 30 * time.Second}, nil)
	s.SetClock(func() time.Time { return now })

	s.ApplyBotStats(&BotStatsPayload{
		Session: 1,
		Seq:     1,
		Stats: []BotStatsEntry{
			{GUID: 3125, MaxHealth: 60, MaxPower2: 100, Strength: 23},
			{GUID: 3126, MaxHealth: 52, MaxPower1: 160, Intellect: 22},
		},
	})

	if st, ok := s.BotStats(3125); !ok || st.MaxHealth != 60 || st.MaxPower2 != 100 {
		t.Fatalf("got (%+v, %v), want guid 3125", st, ok)
	}

	// Publish a roster with only 3125: 3126 left the roster and must be dropped.
	s.ApplyBatch(&model.BotBatchPayload{
		Session:      1,
		Seq:          2,
		TotalBatches: 1,
		BatchIndex:   0,
		Bots:         []model.BotSnapshot{{GUID: 3125, Name: "Dashwar"}},
	})
	if _, ok := s.BotStats(3126); ok {
		t.Fatal("guid 3126 left the roster and should have been dropped")
	}
	if _, ok := s.BotStats(3125); !ok {
		t.Fatal("guid 3125 stayed on the roster and should remain")
	}

	// Advance 59s: still fresh.
	now = now.Add(59 * time.Second)
	if _, ok := s.BotStats(3125); !ok {
		t.Fatal("guid 3125 should still be fresh at 59s")
	}

	// Advance 1s (total 60s): expired.
	now = now.Add(1 * time.Second)
	if _, ok := s.BotStats(3125); ok {
		t.Fatal("guid 3125 should be expired at 60s")
	}

	// Re-apply and test Evict on RosterTTL.
	s.ApplyBotStats(&BotStatsPayload{
		Session: 1,
		Seq:     3,
		Stats:   []BotStatsEntry{{GUID: 3125, MaxHealth: 60}},
	})
	now = now.Add(31 * time.Second)
	if !s.Evict() {
		t.Fatal("expected Evict to clear stale roster")
	}
	if _, ok := s.BotStats(3125); ok {
		t.Fatal("guid 3125 should be cleared when Evict wipes the roster")
	}
}

func TestBotStatsBoundedAndSessionReset(t *testing.T) {
	s := New(Config{}, nil)

	entries := make([]BotStatsEntry, 0, maxTrackedBotStats+50)
	for i := uint32(1); i <= maxTrackedBotStats+50; i++ {
		entries = append(entries, BotStatsEntry{GUID: i, MaxHealth: 100})
	}
	s.ApplyBotStats(&BotStatsPayload{Session: 10, Seq: 1, Stats: entries})

	if _, ok := s.BotStats(1); !ok {
		t.Fatal("first guid should be stored")
	}
	if _, ok := s.BotStats(maxTrackedBotStats + 1); ok {
		t.Fatal("entries beyond maxTrackedBotStats should be dropped")
	}

	// Session change clears stored stats.
	s.ApplyBotStats(&BotStatsPayload{
		Session: 11,
		Seq:     1,
		Stats:   []BotStatsEntry{{GUID: 9999, MaxHealth: 77}},
	})
	if _, ok := s.BotStats(1); ok {
		t.Fatal("old session stats should be cleared on session change")
	}
	if st, ok := s.BotStats(9999); !ok || st.MaxHealth != 77 {
		t.Fatalf("new session stats = (%+v, %v)", st, ok)
	}
}

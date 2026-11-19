package state

import "time"

const (
	// maxTrackedBotStats bounds the per-bot live stats map; evicted with the roster.
	maxTrackedBotStats = 5000
	// defaultBotStatsTTL is how long a BOT_STATS sample stays fresh for profile views.
	defaultBotStatsTTL = 60 * time.Second
)

// BotStatsEntry holds one bot's live character sheet from a BOT_STATS datagram.
type BotStatsEntry struct {
	GUID              uint32             `json:"guid"`
	Source            string             `json:"source"`
	MaxHealth         uint32             `json:"maxhealth"`
	MaxPower1         uint32             `json:"maxpower1"`
	MaxPower2         uint32             `json:"maxpower2"`
	MaxPower3         uint32             `json:"maxpower3"`
	MaxPower4         uint32             `json:"maxpower4"`
	MaxPower5         uint32             `json:"maxpower5"`
	Strength          float64            `json:"strength"`
	Agility           float64            `json:"agility"`
	Stamina           float64            `json:"stamina"`
	Intellect         float64            `json:"intellect"`
	Spirit            float64            `json:"spirit"`
	Armor             uint32             `json:"armor"`
	ResHoly           uint32             `json:"res_holy"`
	ResFire           uint32             `json:"res_fire"`
	ResNature         uint32             `json:"res_nature"`
	ResFrost          uint32             `json:"res_frost"`
	ResShadow         uint32             `json:"res_shadow"`
	ResArcane         uint32             `json:"res_arcane"`
	BlockPct          float64            `json:"block_pct"`
	DodgePct          float64            `json:"dodge_pct"`
	ParryPct          float64            `json:"parry_pct"`
	MeleeCritPct      float64            `json:"melee_crit_pct"`
	RangedCritPct     float64            `json:"ranged_crit_pct"`
	AttackPower       float64            `json:"attack_power"`
	RangedAttackPower float64            `json:"ranged_attack_power"`
	MeleeDamage       string             `json:"melee_damage"`
	RangedDamage      string             `json:"ranged_damage"`
	MeleeSpeed        float64            `json:"melee_speed"`
	RangedSpeed       float64            `json:"ranged_speed"`
	CastSpeed         float64            `json:"cast_speed"`
	MeleeHit          float64            `json:"melee_hit"`
	RangedHit         float64            `json:"ranged_hit"`
	SpellHit          float64            `json:"spell_hit"`
	SpellDamage       uint32             `json:"spell_damage"`
	HealingPower      uint32             `json:"healing_power"`
	SpellCritPct      float64            `json:"spell_crit_pct"`
	ManaRegen         uint32             `json:"mana_regen"`
	SpellDmgFire      uint32             `json:"spell_dmg_fire"`
	SpellDmgFrost     uint32             `json:"spell_dmg_frost"`
	SpellDmgShadow    uint32             `json:"spell_dmg_shadow"`
	SpellDmgNature    uint32             `json:"spell_dmg_nature"`
	SpellDmgArcane    uint32             `json:"spell_dmg_arcane"`
	SpellDmgHoly      uint32             `json:"spell_dmg_holy"`
	ManaRegenCasting  uint32             `json:"mana_regen_casting,omitempty"`
	Defense           uint32             `json:"defense,omitempty"`
	DefenseMax        uint32             `json:"defense_max,omitempty"`
	BlockValue        int32              `json:"block_value,omitempty"`
	SpellPenetration  int32              `json:"spell_penetration,omitempty"`
	Expertise         float64            `json:"expertise,omitempty"`
	Ratings           map[string]int32   `json:"ratings,omitempty"`
	RatingBonus       map[string]float64 `json:"rating_bonus,omitempty"`
	Auras             [][]int64          `json:"auras,omitempty"`
}

// BotStatsPayload is the UDP BOT_STATS datagram.
type BotStatsPayload struct {
	V       int             `json:"v"`
	Session uint64          `json:"session"`
	Seq     uint64          `json:"seq"`
	TS      int64           `json:"ts"`
	Type    string          `json:"type"`
	Stats   []BotStatsEntry `json:"stats"`
}

type botStatsRecord struct {
	entry BotStatsEntry
	at    time.Time
}

// ApplyBotStats records the latest live stats for each bot in the payload.
func (s *Store) ApplyBotStats(p *BotStatsPayload) {
	if p == nil || len(p.Stats) == 0 {
		return
	}

	s.mu.Lock()
	defer s.mu.Unlock()

	now := s.now()
	s.beginSessionLocked(p.Session)

	for _, st := range p.Stats {
		if st.GUID == 0 {
			continue
		}
		if _, exists := s.botStats[st.GUID]; !exists && len(s.botStats) >= maxTrackedBotStats {
			continue
		}
		s.botStats[st.GUID] = botStatsRecord{entry: st, at: now}
	}
}

// BotStats returns the latest live stats for guid if younger than defaultBotStatsTTL.
func (s *Store) BotStats(guid uint32) (BotStatsEntry, bool) {
	s.mu.RLock()
	defer s.mu.RUnlock()

	rec, ok := s.botStats[guid]
	if !ok || s.now().Sub(rec.at) >= defaultBotStatsTTL {
		return BotStatsEntry{}, false
	}
	return rec.entry, true
}

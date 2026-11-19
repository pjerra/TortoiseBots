package armory

import (
	"database/sql"
	"fmt"

	"tortoise-observability/internal/model"
)

func (s *Service) isForever() bool {
	return s != nil && s.cfg.Core == "forever"
}

func (s *Service) foreverBotFilter(alias string) (string, []interface{}) {
	var guids []uint32
	if s.cfg.LiveGUIDs != nil {
		seen := make(map[uint32]struct{})
		for _, g := range s.cfg.LiveGUIDs() {
			if g == 0 {
				continue
			}
			if _, ok := seen[g]; ok {
				continue
			}
			seen[g] = struct{}{}
			guids = append(guids, g)
		}
	}
	if len(guids) == 0 {
		return alias + ".online <> 0", nil
	}
	ph, args := inList(guids)
	return fmt.Sprintf("(%s.online <> 0 OR %s.guid IN (%s))", alias, alias, ph), args
}

func (s *Service) listBotsForever(query string) ([]BotSummary, error) {
	filter, args := s.foreverBotFilter("c")
	sqlQuery := `
		SELECT c.guid, c.name, c.race, c.class, c.gender, c.level, c.money, c.totaltime, c.online, c.zone, c.map
		FROM characters c
		WHERE c.deleteDate IS NULL AND ` + filter
	if query != "" {
		sqlQuery += " AND c.name LIKE ? ESCAPE '\\\\'"
		args = append(args, "%"+likeEscape(query)+"%")
	}
	sqlQuery += fmt.Sprintf(" ORDER BY c.name LIMIT %d", botListLimit)

	rows, err := s.db.Query(sqlQuery, args...)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	bots := []BotSummary{}
	for rows.Next() {
		var b BotSummary
		if err := rows.Scan(&b.GUID, &b.Name, &b.Race, &b.Class, &b.Gender, &b.Level, &b.Money, &b.TotalTime, &b.Online, &b.Zone, &b.Map); err != nil {
			return nil, err
		}
		b.Kind = "pool"
		bots = append(bots, b)
	}
	if err := rows.Err(); err != nil {
		return nil, err
	}

	s.resolveBotGuilds(bots)
	s.resolveBotProfessions(bots)
	return bots, nil
}

func (s *Service) foreverItemMeta(entry uint32) (name string, quality uint8, ilvl uint32, invType uint32, detail ItemDetail) {
	if s.cfg.Items != nil {
		if it, ok := s.cfg.Items.Get(entry); ok {
			name = it.Name
			if name == "" {
				name = fmt.Sprintf("Item #%d", entry)
			}
			quality = uint8(it.Quality)
			ilvl = it.ItemLvl
			invType = it.InvType
			detail = ItemDetail{
				Class:         uint8(it.Class),
				SubClass:      uint8(it.SubClass),
				RequiredLevel: uint8(it.ReqLevel),
				SellPrice:     it.Sell,
			}
			return
		}
	}
	return fmt.Sprintf("Item #%d", entry), 1, 0, 0, ItemDetail{}
}

func (s *Service) getBotProfileForever(guid uint32) (*BotProfile, error) {
	var profile BotProfile

	summaryQuery := `
		SELECT c.guid, c.name, c.race, c.class, c.gender, c.level, c.money, c.totaltime, c.online, c.zone, c.map
		FROM characters c
		WHERE c.guid = ? AND c.deleteDate IS NULL
	`
	err := s.db.QueryRow(summaryQuery, guid).Scan(
		&profile.Summary.GUID, &profile.Summary.Name, &profile.Summary.Race,
		&profile.Summary.Class, &profile.Summary.Gender, &profile.Summary.Level,
		&profile.Summary.Money, &profile.Summary.TotalTime, &profile.Summary.Online,
		&profile.Summary.Zone, &profile.Summary.Map)
	if err != nil {
		if err == sql.ErrNoRows {
			return nil, fmt.Errorf("bot %d not found", guid)
		}
		return nil, err
	}
	profile.Summary.Kind = "pool"

	_ = s.db.QueryRow(
		"SELECT g.name FROM guild_member gm JOIN guild g ON g.guildid = gm.guildid WHERE gm.guid = ?",
		guid,
	).Scan(&profile.Summary.Guild)

	// Equipment (bag=0, slots 0..18).
	profile.Equipment = []EquippedItem{}
	eqRows, err := s.db.Query(fmt.Sprintf(`
		SELECT ci.slot, ii.itemEntry, COALESCE(ii.`+"`count`"+`, 1)
		FROM character_inventory ci
		JOIN item_instance ii ON ii.guid = ci.item
		WHERE ci.guid = ? AND ci.bag = 0 AND ci.slot >= 0 AND ci.slot < %d
		ORDER BY ci.slot
	`, EquipmentSlotEnd), guid)
	if err != nil {
		return nil, err
	}
	for eqRows.Next() {
		var eq EquippedItem
		if err := eqRows.Scan(&eq.Slot, &eq.ItemTemplate, &eq.Count); err != nil {
			eqRows.Close()
			return nil, err
		}
		eq.Name, eq.Quality, eq.ItemLevel, eq.InventoryType, eq.Detail = s.foreverItemMeta(eq.ItemTemplate)
		profile.Equipment = append(profile.Equipment, eq)
	}
	eqRows.Close()
	if err := eqRows.Err(); err != nil {
		return nil, err
	}

	// Equipped bags (bag=0, slots 19..22).
	profile.Bags = []BagContainer{}
	bagRows, err := s.db.Query(fmt.Sprintf(`
		SELECT ci.slot, ci.item, ii.itemEntry, COALESCE(ii.`+"`count`"+`, 1)
		FROM character_inventory ci
		JOIN item_instance ii ON ii.guid = ci.item
		WHERE ci.guid = ? AND ci.bag = 0 AND ci.slot >= %d AND ci.slot < %d
		ORDER BY ci.slot
	`, BagSlotStart, BagSlotEnd), guid)
	if err != nil {
		return nil, err
	}
	for bagRows.Next() {
		var b BagContainer
		if err := bagRows.Scan(&b.Slot, &b.Item, &b.ItemTemplate, &b.Count); err != nil {
			bagRows.Close()
			return nil, err
		}
		b.Name, b.Quality, _, _, b.Detail = s.foreverItemMeta(b.ItemTemplate)
		b.Items = []BagItem{}
		profile.Bags = append(profile.Bags, b)
	}
	bagRows.Close()
	if err := bagRows.Err(); err != nil {
		return nil, err
	}

	// Bag contents.
	for i := range profile.Bags {
		cRows, err := s.db.Query(`
			SELECT ci.slot, ii.itemEntry, COALESCE(ii.`+"`count`"+`, 1)
			FROM character_inventory ci
			JOIN character_inventory container
			  ON container.guid = ci.guid
			 AND container.bag = 0
			 AND container.slot = ?
			 AND container.item = ci.bag
			JOIN item_instance ii ON ii.guid = ci.item
			WHERE ci.guid = ? AND ci.bag != 0
			ORDER BY ci.slot
		`, profile.Bags[i].Slot, guid)
		if err != nil {
			return nil, err
		}
		for cRows.Next() {
			var bi BagItem
			if err := cRows.Scan(&bi.Slot, &bi.ItemTemplate, &bi.Count); err != nil {
				cRows.Close()
				return nil, err
			}
			bi.Name, bi.Quality, _, _, bi.Detail = s.foreverItemMeta(bi.ItemTemplate)
			profile.Bags[i].Items = append(profile.Bags[i].Items, bi)
		}
		cRows.Close()
		if err := cRows.Err(); err != nil {
			return nil, err
		}
	}

	// Backpack (bag=0, slots 23..38).
	bp, err := s.scanForeverSlotItems(guid, BackpackStart, BackpackEnd)
	if err != nil {
		return nil, err
	}
	profile.Backpack = bp

	// Buyback (bag=0, slots 69..80).
	bb, err := s.scanForeverSlotItems(guid, BuybackStart, BuybackEnd)
	if err != nil {
		return nil, err
	}
	profile.Buyback = bb

	// Stats: character_stats when populated, otherwise live base stats.
	s.loadStatsForever(guid, &profile.Stats)

	// Spells: character_spell + classic_spell_info when present.
	profile.Spells = []SpellEntry{}
	spRows, err := s.db.Query(fmt.Sprintf(`
		SELECT cs.spell, cs.active, cs.disabled,
		       COALESCE(csi.spell_name, ''), COALESCE(csi.duration_ms, 0),
		       COALESCE(csi.range_max, 0), COALESCE(csi.cast_ms, 0)
		FROM character_spell cs
		LEFT JOIN %s.classic_spell_info csi ON csi.spell_id = cs.spell
		WHERE cs.guid = ?
		ORDER BY csi.spell_name, cs.spell
	`, s.cfg.WorldDB), guid)
	if err != nil {
		return nil, err
	}
	defer spRows.Close()
	for spRows.Next() {
		var se SpellEntry
		var rangeMax float64
		if err := spRows.Scan(&se.Spell, &se.Active, &se.Disabled, &se.Name, &se.DurationMs, &rangeMax, &se.CastMs); err != nil {
			return nil, err
		}
		se.RangeYd = int32(rangeMax)
		if se.Name == "" {
			se.Name = fmt.Sprintf("Spell #%d", se.Spell)
		}
		profile.Spells = append(profile.Spells, se)
	}
	if err := spRows.Err(); err != nil {
		return nil, err
	}

	// Skills.
	profile.Skills = []SkillEntry{}
	skRows, err := s.db.Query("SELECT skill, MAX(value), MAX(`max`) FROM character_skills WHERE guid = ? GROUP BY skill ORDER BY skill", guid)
	if err != nil {
		return nil, err
	}
	defer skRows.Close()
	for skRows.Next() {
		var se SkillEntry
		if err := skRows.Scan(&se.Skill, &se.Value, &se.Max); err != nil {
			return nil, err
		}
		profile.Skills = append(profile.Skills, se)
	}
	if err := skRows.Err(); err != nil {
		return nil, err
	}

	// Talents: ForeverCore stores talent definitions in client DB2s, not SQL.
	profile.Talents = []TalentTree{}

	// Pet.
	pet, err := s.loadPetForever(guid)
	if err != nil {
		return nil, err
	}
	profile.Pet = pet

	return &profile, nil
}

func (s *Service) scanForeverSlotItems(guid uint32, startSlot, endSlot int) ([]BagItem, error) {
	rows, err := s.db.Query(fmt.Sprintf(`
		SELECT ci.slot, ii.itemEntry, COALESCE(ii.`+"`count`"+`, 1)
		FROM character_inventory ci
		JOIN item_instance ii ON ii.guid = ci.item
		WHERE ci.guid = ? AND ci.bag = 0 AND ci.slot >= %d AND ci.slot < %d
		ORDER BY ci.slot
	`, startSlot, endSlot), guid)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	items := []BagItem{}
	for rows.Next() {
		var bi BagItem
		if err := rows.Scan(&bi.Slot, &bi.ItemTemplate, &bi.Count); err != nil {
			return nil, err
		}
		bi.Name, bi.Quality, _, _, bi.Detail = s.foreverItemMeta(bi.ItemTemplate)
		items = append(items, bi)
	}
	return items, rows.Err()
}

func (s *Service) loadStatsForever(guid uint32, st *CharacterStats) {
	err := s.db.QueryRow(`
		SELECT maxhealth, maxpower1, maxpower2, maxpower3, maxpower4, maxpower5,
		       strength, agility, stamina, intellect, spirit, armor,
		       resHoly, resFire, resNature, resFrost, resShadow, resArcane,
		       blockPct, dodgePct, parryPct, critPct, rangedCritPct, spellCritPct,
		       attackPower, rangedAttackPower, spellPower
		FROM character_stats
		WHERE guid = ?`, guid).Scan(
		&st.MaxHealth, &st.MaxPower1, &st.MaxPower2, &st.MaxPower3, &st.MaxPower4, &st.MaxPower5,
		&st.Strength, &st.Agility, &st.Stamina, &st.Intellect, &st.Spirit, &st.Armor,
		&st.ResHoly, &st.ResFire, &st.ResNature, &st.ResFrost, &st.ResShadow, &st.ResArcane,
		&st.BlockPct, &st.DodgePct, &st.ParryPct, &st.MeleeCritPct, &st.RangedCritPct, &st.SpellCritPct,
		&st.AttackPower, &st.RangedAttackPower, &st.SpellDamage)
	if err == nil && st.MaxHealth > 0 {
		st.HealingPower = st.SpellDamage
		st.Source = "character_stats"
		return
	}

	var race, class, level uint32
	var liveHP, p1, p2, p3, p4, p5 uint32
	if err := s.db.QueryRow(`
		SELECT race, class, level, health, power1, power2, power3, power4, power5
		FROM characters WHERE guid = ?`, guid).Scan(
		&race, &class, &level, &liveHP, &p1, &p2, &p3, &p4, &p5); err != nil {
		st.Source = "none"
		return
	}
	st.MaxPower1, st.MaxPower2, st.MaxPower3, st.MaxPower4, st.MaxPower5 = p1, p2, p3, p4, p5

	var rStr, rAgi, rSta, rInt, rSpi float64
	_ = s.db.QueryRow(fmt.Sprintf(`
		SELECT str, agi, sta, inte, spi FROM %s.player_racestats WHERE race = ?`, s.cfg.WorldDB), race).
		Scan(&rStr, &rAgi, &rSta, &rInt, &rSpi)

	var cStr, cAgi, cSta, cInt, cSpi float64
	var baseHP, baseMana uint32
	_ = s.db.QueryRow(fmt.Sprintf(`
		SELECT str, agi, sta, inte, spi, basehp, basemana FROM %s.player_classlevelstats WHERE class = ? AND level = ?`,
		s.cfg.WorldDB), class, level).Scan(&cStr, &cAgi, &cSta, &cInt, &cSpi, &baseHP, &baseMana)

	st.Strength = rStr + cStr
	st.Agility = rAgi + cAgi
	st.Stamina = rSta + cSta
	st.Intellect = rInt + cInt
	st.Spirit = rSpi + cSpi
	if baseHP > 0 {
		st.MaxHealth = baseHP
	} else {
		st.MaxHealth = liveHP
	}
	if baseMana > 0 && st.MaxPower1 == 0 {
		st.MaxPower1 = baseMana
	}
	st.Source = "live"
}

func (s *Service) loadPetForever(guid uint32) (*PetInfo, error) {
	var pet PetInfo
	var id uint32
	var petType uint32
	err := s.db.QueryRow(`
		SELECT id, entry, name, PetType, level, curhealth
		FROM character_pet
		WHERE owner = ? AND (slot = 0 OR slot > 4)
		ORDER BY slot
		LIMIT 1`, guid).Scan(&id, &pet.Entry, &pet.Name, &petType, &pet.Level, &pet.Health)
	if err == sql.ErrNoRows {
		return nil, nil
	}
	if err != nil {
		return nil, err
	}
	pet.Hunter = petType == 1

	rows, err := s.db.Query(fmt.Sprintf(`
		SELECT ps.spell, COALESCE(csi.spell_name, ''), ps.active
		FROM pet_spell ps
		LEFT JOIN %s.classic_spell_info csi ON csi.spell_id = ps.spell
		WHERE ps.guid = ?
		ORDER BY ps.spell`, s.cfg.WorldDB), id)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	pet.Spells = []PetSpell{}
	for rows.Next() {
		var ps PetSpell
		var active uint32
		if err := rows.Scan(&ps.Spell, &ps.Name, &active); err != nil {
			return nil, err
		}
		if ps.Name == "" {
			ps.Name = fmt.Sprintf("Spell #%d", ps.Spell)
		}
		ps.Autocast = active == petAutocastOn
		pet.Spells = append(pet.Spells, ps)
	}
	return &pet, rows.Err()
}

func (s *Service) gearRollupForever() (map[uint32]model.BotGear, error) {
	if s.cfg.Items == nil || s.cfg.Items.Len() == 0 {
		return map[uint32]model.BotGear{}, nil
	}
	filter, args := s.foreverBotFilter("c")
	query := fmt.Sprintf(`
		SELECT ci.guid, ii.itemEntry
		FROM character_inventory ci
		JOIN item_instance ii ON ii.guid = ci.item
		JOIN characters c ON c.guid = ci.guid
		WHERE ci.bag = 0 AND ci.slot >= 0 AND ci.slot < %d
		  AND ci.slot NOT IN (%d, %d)
		  AND c.deleteDate IS NULL AND %s
	`, EquipmentSlotEnd, slotShirt, slotTabard, filter)

	rows, err := s.db.Query(query, args...)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	type equipTally struct {
		sumLevel uint64
		pieces   int
		quality  [5]int
	}
	tallies := map[uint32]*equipTally{}
	for rows.Next() {
		var guid, itemEntry uint32
		if err := rows.Scan(&guid, &itemEntry); err != nil {
			return nil, err
		}
		it, ok := s.cfg.Items.Get(itemEntry)
		if !ok {
			continue
		}
		t := tallies[guid]
		if t == nil {
			t = &equipTally{}
			tallies[guid] = t
		}
		t.sumLevel += uint64(it.ItemLvl)
		t.pieces++
		t.quality[min(it.Quality, 4)]++
	}
	if err := rows.Err(); err != nil {
		return nil, err
	}

	out := make(map[uint32]model.BotGear, len(tallies))
	for guid, t := range tallies {
		gear := model.BotGear{
			Pieces: t.pieces,
			Grey:   t.quality[0],
			White:  t.quality[1],
			Green:  t.quality[2],
			Blue:   t.quality[3],
			Epic:   t.quality[4],
		}
		if t.pieces > 0 {
			gear.ItemLevel = float64(t.sumLevel) / float64(t.pieces)
		}
		out[guid] = gear
	}
	return out, nil
}

func (s *Service) completedQuestsRollupForever() (map[uint32]uint64, error) {
	filter, args := s.foreverBotFilter("c")
	query := `
		SELECT c.guid, COUNT(*)
		FROM character_queststatus_rewarded qr
		JOIN characters c ON c.guid = qr.guid
		WHERE c.deleteDate IS NULL AND ` + filter + `
		GROUP BY c.guid`

	rows, err := s.db.Query(query, args...)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	out := make(map[uint32]uint64)
	for rows.Next() {
		var guid uint32
		var count uint64
		if err := rows.Scan(&guid, &count); err != nil {
			return nil, err
		}
		out[guid] = count
	}
	return out, rows.Err()
}

func (s *Service) spellsLearnedRollupForever() (map[uint32]uint64, error) {
	filter, args := s.foreverBotFilter("c")
	query := `
		SELECT c.guid, COUNT(*)
		FROM character_spell cs
		JOIN characters c ON c.guid = cs.guid
		WHERE c.deleteDate IS NULL AND cs.active = 1 AND ` + filter + `
		GROUP BY c.guid`

	rows, err := s.db.Query(query, args...)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	out := make(map[uint32]uint64)
	for rows.Next() {
		var guid uint32
		var count uint64
		if err := rows.Scan(&guid, &count); err != nil {
			return nil, err
		}
		out[guid] = count
	}
	return out, rows.Err()
}

func (s *Service) openQuestsRollupForever() (map[uint32][]uint32, error) {
	filter, args := s.foreverBotFilter("c")
	query := `
		SELECT c.guid, qs.quest
		FROM character_queststatus qs
		JOIN characters c ON c.guid = qs.guid
		WHERE c.deleteDate IS NULL AND qs.status IN (1, 3) AND ` + filter

	rows, err := s.db.Query(query, args...)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	out := make(map[uint32][]uint32)
	for rows.Next() {
		var guid, quest uint32
		if err := rows.Scan(&guid, &quest); err != nil {
			return nil, err
		}
		out[guid] = append(out[guid], quest)
	}
	return out, rows.Err()
}

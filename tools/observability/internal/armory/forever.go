package armory

import (
	"database/sql"
	"fmt"
	"math"
	"sort"
	"strings"

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
	s.resolveForeverProfessions(bots)
	s.resolveForeverSpecs(bots)
	return bots, nil
}

func (s *Service) foreverItemMeta(entry uint32, curDurability *uint32) (name string, quality uint8, ilvl uint32, invType uint32, icon string, detail ItemDetail) {
	if s.cfg.Items != nil {
		if it, ok := s.cfg.Items.Get(entry); ok {
			name = it.Name
			if name == "" {
				name = fmt.Sprintf("Item #%d", entry)
			}
			quality = uint8(it.Quality)
			ilvl = it.ItemLvl
			invType = it.InvType
			if it.Icon > 0 {
				icon = fmt.Sprintf("%d", it.Icon)
			}
			detail = ItemDetail{
				Class:             uint8(it.Class),
				SubClass:          uint8(it.SubClass),
				Description:       it.Desc,
				Bonding:           uint8(it.Bonding),
				RequiredLevel:     uint8(it.ReqLevel),
				RequiredSkill:     it.ReqSkill,
				RequiredSkillRank: it.ReqSkillRank,
				AllowableClass:    it.Classes,
				Delay:             it.Delay,
				DmgMin1:           it.DmgMin,
				DmgMax1:           it.DmgMax,
				Armor:             it.Armor,
				MaxDurability:     it.Durability,
				SellPrice:         it.Sell,
			}
			if it.Durability > 0 && curDurability != nil {
				d := *curDurability
				detail.Durability = &d
			}
			if it.ReqSkill > 0 && s.cfg.Lookups != nil {
				detail.RequiredSkillName = s.cfg.Lookups.GetSkillName(it.ReqSkill)
			}
			if len(it.Stats) > 0 {
				detail.StatTypes = make([]uint32, 0, len(it.Stats))
				detail.StatValues = make([]int32, 0, len(it.Stats))
				for _, st := range it.Stats {
					if st[0] < 0 || st[1] == 0 {
						continue
					}
					detail.StatTypes = append(detail.StatTypes, uint32(st[0]))
					detail.StatValues = append(detail.StatValues, st[1])
				}
			}
			if len(it.Spells) > 0 {
				detail.SpellIDs = make([]uint32, 0, len(it.Spells))
				detail.SpellTriggers = make([]uint32, 0, len(it.Spells))
				detail.SpellNames = make([]string, 0, len(it.Spells))
				detail.SpellDescs = make([]string, 0, len(it.Spells))
				for _, sp := range it.Spells {
					if sp[0] <= 0 {
						continue
					}
					sid := uint32(sp[0])
					trig := uint32(max(sp[1], 0))
					spName, spDesc := "", ""
					if s.cfg.Lookups != nil {
						if spell, ok := s.cfg.Lookups.GetSpell(sid); ok {
							spName = spell.Name
						}
						spDesc = s.cfg.Lookups.ResolveSpellDesc(sid)
					}
					detail.SpellIDs = append(detail.SpellIDs, sid)
					detail.SpellTriggers = append(detail.SpellTriggers, trig)
					detail.SpellNames = append(detail.SpellNames, spName)
					detail.SpellDescs = append(detail.SpellDescs, spDesc)
				}
			}
			if it.Set > 0 && s.cfg.Lookups != nil {
				if set, ok := s.cfg.Lookups.GetItemSet(it.Set); ok {
					info := &ItemSetInfo{ID: it.Set, Name: set.Name}
					for _, pieceID := range set.Items {
						pName := fmt.Sprintf("Item #%d", pieceID)
						if p, ok := s.cfg.Items.Get(pieceID); ok && p.Name != "" {
							pName = p.Name
						}
						info.Items = append(info.Items, ItemSetPieceInfo{ID: pieceID, Name: pName})
					}
					for _, b := range set.Bonuses {
						if b[0] <= 0 || b[1] <= 0 {
							continue
						}
						sid := uint32(b[1])
						bName, bDesc := "", ""
						if sp, ok := s.cfg.Lookups.GetSpell(sid); ok {
							bName = sp.Name
						}
						bDesc = s.cfg.Lookups.ResolveSpellDesc(sid)
						info.Bonuses = append(info.Bonuses, ItemSetBonusInfo{
							Pieces: uint32(b[0]),
							Spell:  sid,
							Name:   bName,
							Desc:   bDesc,
						})
					}
					detail.ItemSet = info
				}
			}
			return
		}
	}
	return fmt.Sprintf("Item #%d", entry), 1, 0, 0, "", ItemDetail{}
}

func (s *Service) getBotProfileForever(guid uint32) (*BotProfile, error) {
	var profile BotProfile

	var activeTalentGroup uint8
	summaryQuery := `
		SELECT c.guid, c.name, c.race, c.class, c.gender, c.level, c.money, c.totaltime, c.online, c.zone, c.map,
		       c.leveltime, c.xp, c.rest_bonus, c.restState, c.honor, c.honorLevel, c.totalKills, c.todayKills, c.yesterdayKills, c.activeTalentGroup
		FROM characters c
		WHERE c.guid = ? AND c.deleteDate IS NULL
	`
	err := s.db.QueryRow(summaryQuery, guid).Scan(
		&profile.Summary.GUID, &profile.Summary.Name, &profile.Summary.Race,
		&profile.Summary.Class, &profile.Summary.Gender, &profile.Summary.Level,
		&profile.Summary.Money, &profile.Summary.TotalTime, &profile.Summary.Online,
		&profile.Summary.Zone, &profile.Summary.Map,
		&profile.Summary.LevelTime, &profile.Summary.XP, &profile.Summary.RestBonus,
		&profile.Summary.RestState, &profile.Summary.Honor, &profile.Summary.HonorLevel,
		&profile.Summary.TotalKills, &profile.Summary.TodayKills, &profile.Summary.YesterdayKills, &activeTalentGroup)
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
		SELECT ci.slot, ii.itemEntry, COALESCE(ii.`+"`count`"+`, 1),
		       COALESCE(ii.durability, 0), COALESCE(ii.enchantments, '')
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
		var durability uint32
		var rawEnchants string
		if err := eqRows.Scan(&eq.Slot, &eq.ItemTemplate, &eq.Count, &durability, &rawEnchants); err != nil {
			eqRows.Close()
			return nil, err
		}
		eq.Name, eq.Quality, eq.ItemLevel, eq.InventoryType, eq.Icon, eq.Detail = s.foreverItemMeta(eq.ItemTemplate, &durability)
		eq.Enchantments = s.parseEnchantments(rawEnchants)
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
		b.Name, b.Quality, _, _, b.Icon, b.Detail = s.foreverItemMeta(b.ItemTemplate, nil)
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
			SELECT ci.slot, ii.itemEntry, COALESCE(ii.`+"`count`"+`, 1),
			       COALESCE(ii.durability, 0), COALESCE(ii.enchantments, '')
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
			var durability uint32
			var rawEnchants string
			if err := cRows.Scan(&bi.Slot, &bi.ItemTemplate, &bi.Count, &durability, &rawEnchants); err != nil {
				cRows.Close()
				return nil, err
			}
			bi.Name, bi.Quality, _, _, bi.Icon, bi.Detail = s.foreverItemMeta(bi.ItemTemplate, &durability)
			bi.Enchantments = s.parseEnchantments(rawEnchants)
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

	// Spells: character_spell + ForeverCore spells catalog (+ spells-text when present).
	profile.Spells = []SpellEntry{}
	knownSpells := map[uint32]bool{}
	spRows, err := s.db.Query("SELECT spell, active, disabled FROM character_spell WHERE guid = ? ORDER BY spell", guid)
	if err != nil {
		return nil, err
	}
	defer spRows.Close()
	for spRows.Next() {
		var spellID uint32
		var active, disabled uint8
		if err := spRows.Scan(&spellID, &active, &disabled); err != nil {
			return nil, err
		}
		knownSpells[spellID] = true
		profile.Spells = append(profile.Spells, s.foreverSpellEntry(spellID, active, disabled))
	}
	if err := spRows.Err(); err != nil {
		return nil, err
	}
	sort.Slice(profile.Spells, func(i, j int) bool {
		if profile.Spells[i].Name != profile.Spells[j].Name {
			return profile.Spells[i].Name < profile.Spells[j].Name
		}
		return profile.Spells[i].Spell < profile.Spells[j].Spell
	})

	// Skills: character_skills + ForeverCore names catalog.
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
		s.enrichForeverSkill(&se)
		profile.Skills = append(profile.Skills, se)
	}
	if err := skRows.Err(); err != nil {
		return nil, err
	}

	// Talents: character_talent + ForeverCore talents catalog + talent-tabs.
	talents, err := s.loadTalentsForever(guid, profile.Summary.Class, activeTalentGroup, knownSpells)
	if err != nil {
		return nil, err
	}
	profile.Talents = talents
	if spec := foreverSpecFromTrees(talents); spec != "" {
		profile.Summary.Spec = spec
	}

	// Reputation, Quest log, and live Auras.
	rep, err := s.loadReputationForever(guid)
	if err != nil {
		return nil, err
	}
	profile.Reputation = rep

	quests, completedCount, err := s.loadQuestsForever(guid)
	if err != nil {
		return nil, err
	}
	profile.Quests = quests
	profile.CompletedQuests = completedCount

	profile.Auras = s.loadAurasForever(guid)

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
		SELECT ci.slot, ii.itemEntry, COALESCE(ii.`+"`count`"+`, 1),
		       COALESCE(ii.durability, 0), COALESCE(ii.enchantments, '')
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
		var durability uint32
		var rawEnchants string
		if err := rows.Scan(&bi.Slot, &bi.ItemTemplate, &bi.Count, &durability, &rawEnchants); err != nil {
			return nil, err
		}
		bi.Name, bi.Quality, _, _, bi.Icon, bi.Detail = s.foreverItemMeta(bi.ItemTemplate, &durability)
		bi.Enchantments = s.parseEnchantments(rawEnchants)
		items = append(items, bi)
	}
	return items, rows.Err()
}

func (s *Service) loadStatsForever(guid uint32, st *CharacterStats) {
	if s.cfg.LiveStats != nil {
		if live, ok := s.cfg.LiveStats(guid); ok {
			*st = live
			st.Source = "live"
			return
		}
	}

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
		var class uint32
		if s.db.QueryRow(`SELECT class FROM characters WHERE guid = ?`, guid).Scan(&class) == nil {
			foreverPowerOrder(class, st)
		}
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
	foreverPowerOrder(class, st)

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
	// Pet family (wolf, cat, ...) from the creature template, as the page names it.
	_ = s.db.QueryRow(fmt.Sprintf(`SELECT family FROM %s.creature_template WHERE entry = ?`, s.cfg.WorldDB), pet.Entry).Scan(&pet.Family)

	rows, err := s.db.Query("SELECT ps.spell, ps.active FROM pet_spell ps WHERE ps.guid = ? ORDER BY ps.spell", id)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	pet.Spells = []PetSpell{}
	for rows.Next() {
		var spellID uint32
		var active uint32
		if err := rows.Scan(&spellID, &active); err != nil {
			return nil, err
		}
		se := s.foreverSpellEntry(spellID, 1, 0)
		ps := PetSpell{
			Spell:    spellID,
			Name:     se.Name,
			Subtext:  se.Subtext,
			Icon:     se.Icon,
			Passive:  se.Passive,
			Autocast: !se.Passive && active == petAutocastOn,
		}
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

// foreverClassPowerSlots lists, per class, the power type held in each of the core's power slots (the class's power types in
// ascending order: 0 mana, 1 rage, 2 focus, 3 energy); character_stats.maxpowerN and characters.powerN are slot-ordered.
var foreverClassPowerSlots = map[uint32][]int{
	1: {1}, 2: {0}, 3: {0, 2}, 4: {3}, 5: {0}, 7: {0}, 8: {0}, 9: {0}, 11: {0, 1, 3},
}

// foreverPowerOrder turns the slot-ordered values into the page's order (maxpower1..5 = mana, rage, focus, energy, happiness),
// rage shown as the client shows it (the core keeps it times ten).
func foreverPowerOrder(class uint32, st *CharacterStats) {
	slots := []uint32{st.MaxPower1, st.MaxPower2, st.MaxPower3, st.MaxPower4, st.MaxPower5}
	var byType [4]uint32
	for i, typ := range foreverClassPowerSlots[class] {
		v := slots[i]
		if typ == 1 {
			v /= 10
		}
		byType[typ] = v
	}
	st.MaxPower1, st.MaxPower2, st.MaxPower3, st.MaxPower4, st.MaxPower5 = byType[0], byType[1], byType[2], byType[3], 0
}

func (s *Service) foreverSpellEntry(spellID uint32, active, disabled uint8) SpellEntry {
	se := SpellEntry{
		Spell:    spellID,
		Name:     fmt.Sprintf("Spell #%d", spellID),
		Active:   active,
		Disabled: disabled,
	}
	cat := s.cfg.ForeverData
	if cat == nil {
		return se
	}
	if sp, ok := cat.Spells[spellID]; ok {
		if sp.Name != "" {
			se.Name = sp.Name
		}
		se.School = schoolFromMask(sp.School)
		se.IconID = sp.Icon
		if sp.Icon > 0 {
			se.Icon = fmt.Sprintf("%d", sp.Icon)
		}
		se.CastMs = sp.CastMs
		se.RangeYd = int32(sp.Range)
		se.DurationMs = sp.DurationMs
		se.CooldownMs = sp.CooldownMs
		se.PowerType = sp.PowerType
		se.Cost = sp.Cost
		se.CostPct = sp.CostPct
		se.ClassSpell = sp.BaseLevel > 0
		if strings.HasSuffix(sp.Name, " Passive") {
			se.Passive = true
		}
	}
	if st, ok := cat.SpellText[spellID]; ok {
		se.Subtext = st.Subtext
		if st.Desc != "" {
			se.Description = st.Desc
		} else if st.Aura != "" {
			se.Description = st.Aura
		}
		if len(st.Values) > 0 {
			se.Values = clampInt32s(st.Values)
		}
		if len(st.Radius) > 0 {
			se.Misc = clampInt32s(st.Radius)
		}
		if len(st.Periods) > 0 {
			se.Triggers = make([]uint32, len(st.Periods))
			for i, p := range st.Periods {
				if p > 0 {
					se.Triggers[i] = uint32(p)
				}
			}
		}
		if st.Passive || strings.EqualFold(st.Subtext, "Passive") {
			se.Passive = true
		}
	}
	return se
}

func (s *Service) enrichForeverSkill(se *SkillEntry) {
	if s.cfg.ForeverData == nil || se == nil {
		return
	}
	if meta, ok := s.cfg.ForeverData.Skills[se.Skill]; ok {
		se.Name = meta.Name
		se.Category = meta.Category
		if meta.Icon > 0 {
			se.Icon = fmt.Sprintf("%d", meta.Icon)
		}
	}
}

func (s *Service) resolveForeverProfessions(bots []BotSummary) {
	s.resolveBotProfessions(bots)
	if s.cfg.ForeverData == nil {
		return
	}
	for i := range bots {
		for j := range bots[i].Professions {
			s.enrichForeverSkill(&bots[i].Professions[j])
		}
	}
}

func (s *Service) loadTalentsForever(guid uint32, classID uint8, activeGroup uint8, knownSpells map[uint32]bool) ([]TalentTree, error) {
	if s.cfg.ForeverData == nil || len(s.cfg.ForeverData.Talents) == 0 {
		return []TalentTree{}, nil
	}
	activeTalents := map[uint32]bool{}
	rows, err := s.db.Query("SELECT talentId, talentGroup FROM character_talent WHERE guid = ?", guid)
	if err != nil {
		if isMissingTable(err) {
			return []TalentTree{}, nil
		}
		return nil, err
	}
	defer rows.Close()
	for rows.Next() {
		var talentID uint32
		var group uint8
		if err := rows.Scan(&talentID, &group); err != nil {
			return nil, err
		}
		if group == activeGroup {
			activeTalents[talentID] = true
		}
	}
	if err := rows.Err(); err != nil {
		return nil, err
	}
	return buildForeverTalents(s.cfg.ForeverData, classID, knownSpells, activeTalents), nil
}

func buildForeverTalents(cat *ForeverCatalog, classID uint8, knownSpells map[uint32]bool, activeTalents map[uint32]bool) []TalentTree {
	if cat == nil || classID == 0 {
		return []TalentTree{}
	}
	classMask := uint32(1) << (classID - 1)
	var tabs []ForeverTalentTab
	for _, tab := range cat.TalentTabs {
		if (tab.ClassMask & classMask) != 0 {
			tabs = append(tabs, tab)
		}
	}
	if len(tabs) == 0 {
		return []TalentTree{}
	}
	sort.Slice(tabs, func(i, j int) bool {
		if tabs[i].Order != tabs[j].Order {
			return tabs[i].Order < tabs[j].Order
		}
		return tabs[i].ID < tabs[j].ID
	})

	trees := make([]TalentTree, len(tabs))
	byTab := make(map[uint32]int, len(tabs))
	for i, tab := range tabs {
		trees[i] = TalentTree{
			TabID:   tab.ID,
			Name:    tab.Name,
			Page:    tab.Order,
			Talents: []TalentNode{},
		}
		byTab[tab.ID] = i
	}

	for _, t := range cat.Talents {
		idx, ok := byTab[t.Tab]
		if !ok || len(t.Ranks) == 0 {
			continue
		}
		// Talent.db2 keeps rows whose spells this client no longer has (no spell name anywhere): the client shows no such talent.
		if _, exists := cat.Spells[t.Ranks[0]]; !exists && len(cat.Spells) > 0 {
			continue
		}
		maxRank := uint32(len(t.Ranks))
		rank := uint32(0)
		spellID := t.Ranks[0]
		for i, rs := range t.Ranks {
			if rs != 0 && knownSpells[rs] {
				rank = uint32(i + 1)
				spellID = rs
			}
		}
		if rank == 0 && activeTalents[t.ID] {
			rank = 1
		}
		name := fmt.Sprintf("Talent #%d", t.ID)
		icon := ""
		if sp, ok := cat.Spells[spellID]; ok {
			if sp.Name != "" {
				name = sp.Name
			}
			if sp.Icon > 0 {
				icon = fmt.Sprintf("%d", sp.Icon)
			}
		}
		desc := ""
		if st, ok := cat.SpellText[spellID]; ok {
			desc = st.Desc
		}
		trees[idx].Talents = append(trees[idx].Talents, TalentNode{
			TalentID:    t.ID,
			Row:         t.Tier,
			Col:         t.Col,
			Rank:        rank,
			MaxRank:     maxRank,
			SpellID:     spellID,
			Name:        name,
			Icon:        icon,
			Description: desc,
		})
		trees[idx].Points += rank
	}

	for i := range trees {
		sort.Slice(trees[i].Talents, func(a, b int) bool {
			if trees[i].Talents[a].Row != trees[i].Talents[b].Row {
				return trees[i].Talents[a].Row < trees[i].Talents[b].Row
			}
			if trees[i].Talents[a].Col != trees[i].Talents[b].Col {
				return trees[i].Talents[a].Col < trees[i].Talents[b].Col
			}
			return trees[i].Talents[a].TalentID < trees[i].Talents[b].TalentID
		})
	}
	return trees
}

func foreverSpecFromTrees(trees []TalentTree) string {
	var bestName string
	var bestPts uint32
	var total uint32
	for _, t := range trees {
		total += t.Points
		if t.Points > bestPts {
			bestPts = t.Points
			bestName = t.Name
		}
	}
	if total == 0 {
		return ""
	}
	return bestName
}

func (s *Service) resolveForeverSpecs(bots []BotSummary) {
	if len(bots) == 0 || s.cfg.ForeverData == nil {
		return
	}
	guids := make([]uint32, len(bots))
	byGUID := make(map[uint32]int, len(bots))
	for i, b := range bots {
		guids[i] = b.GUID
		byGUID[b.GUID] = i
	}
	ph, args := inList(guids)
	rows, err := s.db.Query(fmt.Sprintf(`
		SELECT ct.guid, ct.talentId
		FROM character_talent ct
		JOIN characters c ON c.guid = ct.guid
		WHERE ct.guid IN (%s) AND ct.talentGroup = c.activeTalentGroup
	`, ph), args...)
	if err != nil {
		return
	}
	defer rows.Close()

	talentTab := make(map[uint32]uint32, len(s.cfg.ForeverData.Talents))
	for _, t := range s.cfg.ForeverData.Talents {
		talentTab[t.ID] = t.Tab
	}
	ptsByBot := map[uint32]map[uint32]uint32{}
	for rows.Next() {
		var guid, talentID uint32
		if err := rows.Scan(&guid, &talentID); err != nil {
			return
		}
		tab := talentTab[talentID]
		if tab == 0 {
			continue
		}
		if ptsByBot[guid] == nil {
			ptsByBot[guid] = map[uint32]uint32{}
		}
		ptsByBot[guid][tab]++
	}
	// Bots without character_talent rows: the talent rank spells they know (the profile reads them the same way).
	rankOf := map[uint32][2]uint32{} // rank spell -> {talent tab, points at that rank}
	for _, t := range s.cfg.ForeverData.Talents {
		for i, rs := range t.Ranks {
			if rs != 0 {
				rankOf[rs] = [2]uint32{t.Tab, uint32(i + 1)}
			}
		}
	}
	var missing []uint32
	for _, g := range guids {
		if ptsByBot[g] == nil {
			missing = append(missing, g)
		}
	}
	if len(missing) > 0 && len(rankOf) > 0 {
		mph, margs := inList(missing)
		if srows, err := s.db.Query(fmt.Sprintf(`SELECT guid, spell FROM character_spell WHERE guid IN (%s)`, mph), margs...); err == nil {
			best := map[uint32]map[uint32]uint32{} // guid -> first rank spell of a talent -> highest points known
			first := map[uint32]uint32{}
			for _, t := range s.cfg.ForeverData.Talents {
				for _, rs := range t.Ranks {
					if rs != 0 && len(t.Ranks) > 0 {
						first[rs] = t.Ranks[0]
					}
				}
			}
			for srows.Next() {
				var guid, spell uint32
				if srows.Scan(&guid, &spell) != nil {
					break
				}
				r, ok := rankOf[spell]
				if !ok {
					continue
				}
				if best[guid] == nil {
					best[guid] = map[uint32]uint32{}
				}
				if r[1] > best[guid][first[spell]] {
					best[guid][first[spell]] = r[1]
				}
			}
			srows.Close()
			for guid, talents := range best {
				for firstRank, pts := range talents {
					tab := rankOf[firstRank][0]
					if ptsByBot[guid] == nil {
						ptsByBot[guid] = map[uint32]uint32{}
					}
					ptsByBot[guid][tab] += pts
				}
			}
		}
	}
	for guid, tabPts := range ptsByBot {
		idx, ok := byGUID[guid]
		if !ok {
			continue
		}
		// Ties go to the tree shown first (the profile walks the trees in that order).
		var bestTab uint32
		var bestPts uint32
		for tab, pts := range tabPts {
			if pts > bestPts || (pts == bestPts && pts > 0 && s.cfg.ForeverData.TalentTabs[tab].Order < s.cfg.ForeverData.TalentTabs[bestTab].Order) {
				bestPts = pts
				bestTab = tab
			}
		}
		if bestTab != 0 {
			if meta, ok := s.cfg.ForeverData.TalentTabs[bestTab]; ok {
				bots[idx].Spec = meta.Name
			}
		}
	}
}

func reputationTier(standing int32) (string, int32, int32) {
	switch {
	case standing < -6000:
		return "Hated", -42000, -6000
	case standing < -3000:
		return "Hostile", -6000, -3000
	case standing < 0:
		return "Unfriendly", -3000, 0
	case standing < 3000:
		return "Neutral", 0, 3000
	case standing < 9000:
		return "Friendly", 3000, 9000
	case standing < 21000:
		return "Honored", 9000, 21000
	case standing < 42000:
		return "Revered", 21000, 42000
	default:
		return "Exalted", 42000, 42999
	}
}

func (s *Service) loadReputationForever(guid uint32) ([]ReputationEntry, error) {
	rows, err := s.db.Query(`
		SELECT faction, standing, flags
		FROM character_reputation
		WHERE guid = ? AND (flags & 4) = 0 AND ((flags & 1) != 0 OR standing != 0)
		ORDER BY faction
	`, guid)
	if err != nil {
		if isMissingTable(err) {
			return []ReputationEntry{}, nil
		}
		return nil, err
	}
	defer rows.Close()

	out := []ReputationEntry{}
	for rows.Next() {
		var r ReputationEntry
		if err := rows.Scan(&r.Faction, &r.Standing, &r.Flags); err != nil {
			return nil, err
		}
		r.Name = fmt.Sprintf("Faction #%d", r.Faction)
		if s.cfg.ForeverData != nil {
			if meta, ok := s.cfg.ForeverData.Factions[r.Faction]; ok && meta.Name != "" {
				r.Name = meta.Name
			}
		}
		r.Tier, r.Min, r.Max = reputationTier(r.Standing)
		out = append(out, r)
	}
	if err := rows.Err(); err != nil {
		return nil, err
	}
	sort.Slice(out, func(i, j int) bool {
		if out[i].Name != out[j].Name {
			return out[i].Name < out[j].Name
		}
		return out[i].Faction < out[j].Faction
	})
	return out, nil
}

func (s *Service) loadQuestsForever(guid uint32) ([]QuestEntry, uint32, error) {
	var completed uint32
	_ = s.db.QueryRow("SELECT COUNT(*) FROM character_queststatus_rewarded WHERE guid = ?", guid).Scan(&completed)

	rows, err := s.db.Query(fmt.Sprintf(`
		SELECT cq.quest, COALESCE(qt.LogTitle, ''), cq.status, cq.explored
		FROM character_queststatus cq
		LEFT JOIN %s.quest_template qt ON qt.ID = cq.quest
		WHERE cq.guid = ?
		ORDER BY cq.quest
	`, s.cfg.WorldDB), guid)
	if err != nil {
		if isMissingTable(err) {
			return []QuestEntry{}, completed, nil
		}
		return nil, completed, err
	}
	defer rows.Close()

	quests := []QuestEntry{}
	byID := map[uint32]int{}
	for rows.Next() {
		var q QuestEntry
		var explored uint8
		if err := rows.Scan(&q.Quest, &q.Title, &q.Status, &explored); err != nil {
			return nil, completed, err
		}
		q.Explored = explored != 0
		if q.Title == "" {
			q.Title = fmt.Sprintf("Quest #%d", q.Quest)
		}
		byID[q.Quest] = len(quests)
		quests = append(quests, q)
	}
	if err := rows.Err(); err != nil {
		return nil, completed, err
	}

	if len(quests) > 0 {
		objRows, err := s.db.Query(`
			SELECT quest, objective, data
			FROM character_queststatus_objectives
			WHERE guid = ?
			ORDER BY quest, objective
		`, guid)
		if err == nil {
			defer objRows.Close()
			for objRows.Next() {
				var qid uint32
				var idx uint8
				var cnt int32
				if err := objRows.Scan(&qid, &idx, &cnt); err != nil {
					break
				}
				if pos, ok := byID[qid]; ok {
					obj := QuestObjective{Index: idx, Count: cnt}
					s.describeQuestObjective(qid, &obj)
					quests[pos].Objectives = append(quests[pos].Objectives, obj)
				}
			}
		}
	}
	return quests, completed, nil
}

func (s *Service) loadAurasForever(guid uint32) []AuraEntry {
	if s.cfg.LiveAuras == nil {
		return nil
	}
	raw := s.cfg.LiveAuras(guid)
	if len(raw) == 0 {
		return nil
	}
	out := make([]AuraEntry, 0, len(raw))
	for _, row := range raw {
		if len(row) < 4 || row[0] <= 0 {
			continue
		}
		spellID := uint32(row[0])
		au := AuraEntry{
			Spell:       spellID,
			Name:        fmt.Sprintf("Spell #%d", spellID),
			RemainingMs: row[1],
			Stacks:      int32(row[2]),
			Positive:    row[3] != 0,
		}
		if s.cfg.ForeverData != nil {
			if sp, ok := s.cfg.ForeverData.Spells[spellID]; ok {
				if sp.Name != "" {
					au.Name = sp.Name
				}
				if sp.Icon > 0 {
					au.Icon = fmt.Sprintf("%d", sp.Icon)
				}
			}
		}
		out = append(out, au)
	}
	return out
}

// schoolFromMask turns the core's school mask (1 physical, 2 holy, 4 fire, ...) into the page's school index (0 physical, 1 holy, 2 fire, ...).
func schoolFromMask(mask uint32) uint32 {
	for i := uint32(0); i < 7; i++ {
		if mask&(1<<i) != 0 {
			return i
		}
	}
	return 0
}

// clampInt32s rounds client values into the page's int32 fields (a few client values exceed 32 bits).
func clampInt32s(in []float64) []int32 {
	out := make([]int32, len(in))
	for i, v := range in {
		switch {
		case v > math.MaxInt32:
			out[i] = math.MaxInt32
		case v < math.MinInt32:
			out[i] = math.MinInt32
		default:
			out[i] = int32(math.Round(v))
		}
	}
	return out
}

// describeQuestObjective names a quest objective the way the quest log does: its own text, else the creature (kill,
// type 0) or item (collect, type 1) it counts, and the amount needed.
func (s *Service) describeQuestObjective(quest uint32, obj *QuestObjective) {
	var typ, objectID uint32
	var desc string
	err := s.db.QueryRow(fmt.Sprintf(`SELECT Type, ObjectID, Amount, Description FROM %s.quest_objectives WHERE QuestID = ? AND StorageIndex = ? LIMIT 1`, s.cfg.WorldDB),
		quest, obj.Index).Scan(&typ, &objectID, &obj.Amount, &desc)
	if err != nil {
		return
	}
	obj.Name = desc
	if obj.Name != "" {
		return
	}
	switch typ {
	case 0:
		_ = s.db.QueryRow(fmt.Sprintf(`SELECT name FROM %s.creature_template WHERE entry = ?`, s.cfg.WorldDB), objectID).Scan(&obj.Name)
		if obj.Name != "" {
			obj.Name += " slain"
		}
	case 1:
		if it, ok := s.cfg.Items.Get(objectID); ok {
			obj.Name = it.Name
		}
	}
}

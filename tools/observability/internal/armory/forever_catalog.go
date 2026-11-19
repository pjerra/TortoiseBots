package armory

import (
	"encoding/json"
	"log"
	"os"
	"path/filepath"
	"strconv"
)

type ForeverSpell struct {
	ID         uint32  `json:"id"`
	Name       string  `json:"name"`
	Icon       uint32  `json:"icon"`
	School     uint32  `json:"school"`
	CastMs     int32   `json:"cast_ms"`
	Range      float64 `json:"range"`
	DurationMs int32   `json:"duration_ms"`
	CooldownMs int32   `json:"cooldown_ms"`
	Level      uint32  `json:"level"`
	BaseLevel  uint32  `json:"base_level"`
	PowerType  int32   `json:"power_type"`
	Cost       int32   `json:"cost"`
	CostPct    float64 `json:"cost_pct"`
}

type ForeverSpellText struct {
	Subtext string `json:"subtext"`
	Desc    string `json:"desc"`
	Aura    string `json:"aura"`
	// float64: some client values do not fit 32 bits and some radii are fractional.
	Values  []float64 `json:"values"`
	Periods []float64 `json:"periods"`
	Radius  []float64 `json:"radius"`
	Passive bool      `json:"passive"`
}

type ForeverTalent struct {
	ID    uint32   `json:"id"`
	Class uint32   `json:"class"`
	Tab   uint32   `json:"tab"`
	Tier  uint32   `json:"tier"`
	Col   uint32   `json:"col"`
	Spell uint32   `json:"spell"`
	Ranks []uint32 `json:"ranks"`
}

type ForeverTalentTab struct {
	ID         uint32 `json:"id"`
	Name       string `json:"name"`
	ClassMask  uint32 `json:"class_mask"`
	Order      uint32 `json:"order"`
	Icon       uint32 `json:"icon"`
	Background string `json:"background"`
}

type ForeverSkillMeta struct {
	Name     string `json:"name"`
	Category uint32 `json:"category"`
	Icon     uint32 `json:"icon"`
}

type ForeverFactionMeta struct {
	Name   string `json:"name"`
	Parent uint32 `json:"parent"`
}

type ForeverCatalog struct {
	Spells     map[uint32]ForeverSpell
	SpellText  map[uint32]ForeverSpellText
	Talents    []ForeverTalent
	TalentTabs map[uint32]ForeverTalentTab
	Skills     map[uint32]ForeverSkillMeta
	Factions   map[uint32]ForeverFactionMeta
}

func LoadForeverCatalog(dataDir, artDir string) *ForeverCatalog {
	cat := &ForeverCatalog{
		Spells:     map[uint32]ForeverSpell{},
		SpellText:  map[uint32]ForeverSpellText{},
		TalentTabs: defaultForeverTalentTabs(),
		Skills:     map[uint32]ForeverSkillMeta{},
		Factions:   map[uint32]ForeverFactionMeta{},
	}
	if dataDir != "" {
		cat.loadSpells(filepath.Join(dataDir, "tortoisebots-spells.json"))
		cat.loadTalents(filepath.Join(dataDir, "tortoisebots-talents.json"))
		cat.loadNames(filepath.Join(dataDir, "tortoisebots-names.json"))
	}
	if artDir != "" {
		cat.loadSpellText(filepath.Join(artDir, "spells-text.json"))
		cat.loadTalentTabs(filepath.Join(artDir, "talent-tabs.json"))
	}
	return cat
}

func (c *ForeverCatalog) loadSpells(path string) {
	raw, err := os.ReadFile(path)
	if err != nil {
		if !os.IsNotExist(err) {
			log.Printf("[Armory] forever spells read %s: %v", path, err)
		}
		return
	}
	var doc struct {
		Spells []ForeverSpell `json:"spells"`
	}
	if err := json.Unmarshal(raw, &doc); err != nil {
		log.Printf("[Armory] forever spells parse %s: %v", path, err)
		return
	}
	for _, sp := range doc.Spells {
		if sp.ID != 0 {
			c.Spells[sp.ID] = sp
		}
	}
}

func (c *ForeverCatalog) loadTalents(path string) {
	raw, err := os.ReadFile(path)
	if err != nil {
		if !os.IsNotExist(err) {
			log.Printf("[Armory] forever talents read %s: %v", path, err)
		}
		return
	}
	var doc struct {
		Talents []ForeverTalent `json:"talents"`
	}
	if err := json.Unmarshal(raw, &doc); err != nil {
		log.Printf("[Armory] forever talents parse %s: %v", path, err)
		return
	}
	c.Talents = doc.Talents
}

func (c *ForeverCatalog) loadNames(path string) {
	raw, err := os.ReadFile(path)
	if err != nil {
		if !os.IsNotExist(err) {
			log.Printf("[Armory] forever names read %s: %v", path, err)
		}
		return
	}
	var doc struct {
		Skills   map[string]ForeverSkillMeta   `json:"skills"`
		Factions map[string]ForeverFactionMeta `json:"factions"`
	}
	if err := json.Unmarshal(raw, &doc); err != nil {
		log.Printf("[Armory] forever names parse %s: %v", path, err)
		return
	}
	for k, v := range doc.Skills {
		if id, err := strconv.ParseUint(k, 10, 32); err == nil {
			c.Skills[uint32(id)] = v
		}
	}
	for k, v := range doc.Factions {
		if id, err := strconv.ParseUint(k, 10, 32); err == nil {
			c.Factions[uint32(id)] = v
		}
	}
}

func (c *ForeverCatalog) loadSpellText(path string) {
	raw, err := os.ReadFile(path)
	if err != nil {
		if !os.IsNotExist(err) {
			log.Printf("[Armory] forever spell-text read %s: %v", path, err)
		}
		return
	}
	var doc map[string]ForeverSpellText
	if err := json.Unmarshal(raw, &doc); err != nil {
		log.Printf("[Armory] forever spell-text parse %s: %v", path, err)
		return
	}
	for k, v := range doc {
		if id, err := strconv.ParseUint(k, 10, 32); err == nil {
			c.SpellText[uint32(id)] = v
		}
	}
}

func (c *ForeverCatalog) loadTalentTabs(path string) {
	raw, err := os.ReadFile(path)
	if err != nil {
		if !os.IsNotExist(err) {
			log.Printf("[Armory] forever talent-tabs read %s: %v", path, err)
		}
		return
	}
	var doc map[string]ForeverTalentTab
	if err := json.Unmarshal(raw, &doc); err != nil {
		log.Printf("[Armory] forever talent-tabs parse %s: %v", path, err)
		return
	}
	if len(doc) == 0 {
		return
	}
	tabs := make(map[uint32]ForeverTalentTab, len(doc))
	for k, v := range doc {
		if id, err := strconv.ParseUint(k, 10, 32); err == nil {
			v.ID = uint32(id)
			tabs[uint32(id)] = v
		}
	}
	if len(tabs) > 0 {
		c.TalentTabs = tabs
	}
}

func defaultForeverTalentTabs() map[uint32]ForeverTalentTab {
	return map[uint32]ForeverTalentTab{
		161: {ID: 161, Name: "Arms", ClassMask: 1 << 0, Order: 0},
		164: {ID: 164, Name: "Fury", ClassMask: 1 << 0, Order: 1},
		163: {ID: 163, Name: "Protection", ClassMask: 1 << 0, Order: 2},
		382: {ID: 382, Name: "Holy", ClassMask: 1 << 1, Order: 0},
		383: {ID: 383, Name: "Protection", ClassMask: 1 << 1, Order: 1},
		381: {ID: 381, Name: "Retribution", ClassMask: 1 << 1, Order: 2},
		361: {ID: 361, Name: "Beast Mastery", ClassMask: 1 << 2, Order: 0},
		363: {ID: 363, Name: "Marksmanship", ClassMask: 1 << 2, Order: 1},
		362: {ID: 362, Name: "Survival", ClassMask: 1 << 2, Order: 2},
		182: {ID: 182, Name: "Assassination", ClassMask: 1 << 3, Order: 0},
		181: {ID: 181, Name: "Combat", ClassMask: 1 << 3, Order: 1},
		183: {ID: 183, Name: "Subtlety", ClassMask: 1 << 3, Order: 2},
		201: {ID: 201, Name: "Discipline", ClassMask: 1 << 4, Order: 0},
		202: {ID: 202, Name: "Holy", ClassMask: 1 << 4, Order: 1},
		203: {ID: 203, Name: "Shadow", ClassMask: 1 << 4, Order: 2},
		261: {ID: 261, Name: "Elemental", ClassMask: 1 << 6, Order: 0},
		263: {ID: 263, Name: "Enhancement", ClassMask: 1 << 6, Order: 1},
		262: {ID: 262, Name: "Restoration", ClassMask: 1 << 6, Order: 2},
		81:  {ID: 81, Name: "Arcane", ClassMask: 1 << 7, Order: 0},
		41:  {ID: 41, Name: "Fire", ClassMask: 1 << 7, Order: 1},
		61:  {ID: 61, Name: "Frost", ClassMask: 1 << 7, Order: 2},
		302: {ID: 302, Name: "Affliction", ClassMask: 1 << 8, Order: 0},
		303: {ID: 303, Name: "Demonology", ClassMask: 1 << 8, Order: 1},
		301: {ID: 301, Name: "Destruction", ClassMask: 1 << 8, Order: 2},
		283: {ID: 283, Name: "Balance", ClassMask: 1 << 10, Order: 0},
		281: {ID: 281, Name: "Feral Combat", ClassMask: 1 << 10, Order: 1},
		282: {ID: 282, Name: "Restoration", ClassMask: 1 << 10, Order: 2},
	}
}

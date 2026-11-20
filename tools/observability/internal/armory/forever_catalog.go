package armory

import (
	"encoding/json"
	"log"
	"os"
	"path/filepath"
	"strconv"

	"tortoise-observability/internal/foreveritems"
)

// ForeverTalent is one talent of a class tab as the Forever client draws it (the module's trait-data layout): the trait
// node and node entry a character's character_trait_entry rows name, its spell, name, icon, max rank and place.
type ForeverTalent struct {
	Node  uint32 `json:"node"`
	Entry uint32 `json:"entry"`
	Spell uint32 `json:"spell"`
	Name  string `json:"name"`
	Icon  uint32 `json:"icon"`
	Max   uint32 `json:"max"`
	Row   uint32 `json:"row"`
	Col   uint32 `json:"col"`
}

type ForeverTalentTab struct {
	ID         uint32 `json:"id"`
	Name       string `json:"name"`
	ClassMask  uint32 `json:"class_mask"`
	Order      uint32 `json:"order"`
	Icon       uint32 `json:"icon"`
	Background string `json:"background"`
}

// ForeverCatalog is the profile's talent data; spells, spell text, skills and factions come from Lookups, the one
// reader of the module's data files and the client's spell text.
type ForeverCatalog struct {
	Talents    map[uint32][][]ForeverTalent // class -> its tabs left to right -> talents
	TalentTabs map[uint32]ForeverTalentTab
	Lookups    *foreveritems.Lookups
}

func LoadForeverCatalog(dataDir, artDir string, lookups *foreveritems.Lookups) *ForeverCatalog {
	cat := &ForeverCatalog{TalentTabs: defaultForeverTalentTabs(), Lookups: lookups}
	if dataDir != "" {
		cat.loadTalents(filepath.Join(dataDir, "tortoisebots-talents.json"))
	}
	if artDir != "" {
		cat.loadTalentTabs(filepath.Join(artDir, "talent-tabs.json"))
	}
	return cat
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
		Classes []struct {
			Class uint32            `json:"class"`
			Tabs  [][]ForeverTalent `json:"tabs"`
		} `json:"classes"`
	}
	if err := json.Unmarshal(raw, &doc); err != nil {
		log.Printf("[Armory] forever talents parse %s: %v", path, err)
		return
	}
	c.Talents = make(map[uint32][][]ForeverTalent, len(doc.Classes))
	for _, cl := range doc.Classes {
		c.Talents[cl.Class] = cl.Tabs
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

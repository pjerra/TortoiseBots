package foreveritems

import (
	"encoding/json"
	"fmt"
	"log"
	"math"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"sync"
	"time"
)

type Spell struct {
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

type SpellText struct {
	Subtext string `json:"subtext"`
	Desc    string `json:"desc"`
	Aura    string `json:"aura"`
	// float64: some client values do not fit 32 bits.
	Values  []float64 `json:"values"`
	Periods []float64 `json:"periods"`
	Radius  []float64 `json:"radius"`
	Passive bool      `json:"passive,omitempty"`
}

type NamedEntry struct {
	Name     string `json:"name"`
	Category uint32 `json:"category,omitempty"`
	Icon     uint32 `json:"icon,omitempty"`
	Map      uint32 `json:"map,omitempty"`
	Parent   uint32 `json:"parent,omitempty"`
}

type ItemSet struct {
	Name    string     `json:"name"`
	Items   []uint32   `json:"items"`
	Bonuses [][2]int32 `json:"bonuses"` // [pieces, spellId]
}

type Names struct {
	Skills   map[uint32]NamedEntry `json:"skills"`
	Areas    map[uint32]NamedEntry `json:"areas"`
	Factions map[uint32]NamedEntry `json:"factions"`
	ItemSets map[uint32]ItemSet    `json:"item_sets"`
	Enchants map[uint32]string     `json:"enchants"`
}

// Lookups holds ForeverCore spell, spell-text, and name catalogs with automatic
// reload when any backing JSON file's mtime changes.
type Lookups struct {
	dataDir string
	artDir  string

	mu           sync.RWMutex
	spells       map[uint32]Spell
	spellsMod    time.Time
	spellText    map[uint32]SpellText
	spellTextMod time.Time
	names        Names
	namesMod     time.Time
}

func NewLookups(dataDir, artDir string) *Lookups {
	return &Lookups{dataDir: dataDir, artDir: artDir}
}

func (l *Lookups) GetSpell(id uint32) (Spell, bool) {
	if l == nil {
		return Spell{}, false
	}
	l.refreshSpells()
	l.mu.RLock()
	defer l.mu.RUnlock()
	sp, ok := l.spells[id]
	return sp, ok
}

func (l *Lookups) GetSpellText(id uint32) (SpellText, bool) {
	if l == nil {
		return SpellText{}, false
	}
	l.refreshSpellText()
	l.mu.RLock()
	defer l.mu.RUnlock()
	st, ok := l.spellText[id]
	return st, ok
}

// GetSkill is a SkillLine's name, category and icon.
func (l *Lookups) GetSkill(id uint32) (NamedEntry, bool) {
	if l == nil {
		return NamedEntry{}, false
	}
	l.refreshNames()
	l.mu.RLock()
	defer l.mu.RUnlock()
	e, ok := l.names.Skills[id]
	return e, ok
}

// GetFaction is a faction's name and parent.
func (l *Lookups) GetFaction(id uint32) (NamedEntry, bool) {
	if l == nil {
		return NamedEntry{}, false
	}
	l.refreshNames()
	l.mu.RLock()
	defer l.mu.RUnlock()
	e, ok := l.names.Factions[id]
	return e, ok
}

// HasSpells reports whether the spell file is loaded (an absent file must not hide every talent).
func (l *Lookups) HasSpells() bool {
	if l == nil {
		return false
	}
	l.refreshSpells()
	l.mu.RLock()
	defer l.mu.RUnlock()
	return len(l.spells) > 0
}

func (l *Lookups) GetSkillName(id uint32) string {
	if l == nil || id == 0 {
		return ""
	}
	l.refreshNames()
	l.mu.RLock()
	defer l.mu.RUnlock()
	return l.names.Skills[id].Name
}

func (l *Lookups) GetItemSet(id uint32) (ItemSet, bool) {
	if l == nil || id == 0 {
		return ItemSet{}, false
	}
	l.refreshNames()
	l.mu.RLock()
	defer l.mu.RUnlock()
	set, ok := l.names.ItemSets[id]
	return set, ok
}

// GetEnchant is the name of a SpellItemEnchantment (the enchant line of an item tooltip), "" when unknown.
func (l *Lookups) GetEnchant(id uint32) string {
	if l == nil {
		return ""
	}
	l.refreshNames()
	l.mu.RLock()
	defer l.mu.RUnlock()
	return l.names.Enchants[id]
}

func (l *Lookups) Areas() map[uint32]NamedEntry {
	if l == nil {
		return map[uint32]NamedEntry{}
	}
	l.refreshNames()
	l.mu.RLock()
	defer l.mu.RUnlock()
	out := make(map[uint32]NamedEntry, len(l.names.Areas))
	for k, v := range l.names.Areas {
		out[k] = v
	}
	return out
}

// ResolveSpellDesc formats a spell's description from spells-text.json using
// values/periods/radius and duration/range/cast from tortoisebots-spells.json.
func (l *Lookups) ResolveSpellDesc(id uint32) string {
	st, ok := l.GetSpellText(id)
	if !ok || strings.TrimSpace(st.Desc) == "" {
		return ""
	}
	sp, _ := l.GetSpell(id)
	return FormatSpellDesc(st, sp)
}

var (
	reDivVal = regexp.MustCompile(`\$\/(\d+);([sSoO])(\d)`)
	reToken  = regexp.MustCompile(`\$([sSoOaAtT])(\d)`)
)

func FormatSpellDesc(st SpellText, sp Spell) string {
	raw := strings.Join(strings.Fields(st.Desc), " ")
	if raw == "" {
		return ""
	}
	raw = reDivVal.ReplaceAllStringFunc(raw, func(m string) string {
		sub := reDivVal.FindStringSubmatch(m)
		if len(sub) != 4 {
			return m
		}
		div, _ := strconv.Atoi(sub[1])
		idx, _ := strconv.Atoi(sub[3])
		if div <= 0 || idx <= 0 || idx > len(st.Values) {
			return m
		}
		q := float64(st.Values[idx-1]) / float64(div)
		rounded := math.Round(q*10) / 10
		if rounded == math.Trunc(rounded) {
			return strconv.Itoa(int(rounded))
		}
		return fmt.Sprintf("%.1f", rounded)
	})
	raw = reToken.ReplaceAllStringFunc(raw, func(m string) string {
		sub := reToken.FindStringSubmatch(m)
		if len(sub) != 3 {
			return m
		}
		kind := strings.ToLower(sub[1])
		idx, _ := strconv.Atoi(sub[2])
		if idx <= 0 {
			return m
		}
		switch kind {
		case "s", "o":
			if idx <= len(st.Values) {
				return strconv.Itoa(int(st.Values[idx-1]))
			}
		case "a":
			if idx <= len(st.Radius) && st.Radius[idx-1] != 0 {
				r := st.Radius[idx-1]
				if r == math.Trunc(r) {
					return strconv.Itoa(int(r))
				}
				return fmt.Sprintf("%.1f", r)
			}
			if idx <= len(st.Values) {
				return strconv.Itoa(int(st.Values[idx-1]))
			}
		case "t":
			if idx <= len(st.Periods) && st.Periods[idx-1] > 0 {
				sec := float64(st.Periods[idx-1]) / 1000.0
				if sec == math.Trunc(sec) {
					return strconv.Itoa(int(sec))
				}
				return fmt.Sprintf("%.1f", sec)
			}
			if idx <= len(st.Values) {
				return strconv.Itoa(int(st.Values[idx-1]))
			}
		}
		return m
	})
	if sp.DurationMs != 0 {
		raw = strings.ReplaceAll(raw, "$d", fmtDurMs(sp.DurationMs))
	}
	if sp.Range > 0 {
		raw = strings.ReplaceAll(raw, "$r", fmt.Sprintf("%g yd", sp.Range))
	}
	if sp.CastMs > 0 {
		raw = strings.ReplaceAll(raw, "$c", fmtDurMs(sp.CastMs))
	}
	return raw
}

func fmtDurMs(ms int32) string {
	if ms < 0 {
		return "permanent"
	}
	if ms < 1000 {
		return fmt.Sprintf("%d ms", ms)
	}
	s := float64(ms) / 1000.0
	if s < 60 {
		rounded := math.Round(s*10) / 10
		if rounded == math.Trunc(rounded) {
			return fmt.Sprintf("%d sec", int(rounded))
		}
		return fmt.Sprintf("%.1f sec", rounded)
	}
	m := int(s) / 60
	if m >= 60 {
		return fmt.Sprintf("%dh %dm", m/60, m%60)
	}
	return fmt.Sprintf("%d min", m)
}

func (l *Lookups) refreshSpells() {
	if l.dataDir == "" {
		return
	}
	path := filepath.Join(l.dataDir, "tortoisebots-spells.json")
	st, err := os.Stat(path)
	if err != nil {
		return
	}
	l.mu.RLock()
	same := st.ModTime().Equal(l.spellsMod)
	l.mu.RUnlock()
	if same {
		return
	}
	raw, err := os.ReadFile(path)
	if err != nil {
		return
	}
	var doc struct {
		Spells []Spell `json:"spells"`
	}
	if err := json.Unmarshal(raw, &doc); err != nil {
		log.Printf("[Lookups] parse %s: %v", path, err)
		l.mu.Lock()
		l.spellsMod = st.ModTime()
		l.mu.Unlock()
		return
	}
	out := make(map[uint32]Spell, len(doc.Spells))
	for _, sp := range doc.Spells {
		out[sp.ID] = sp
	}
	l.mu.Lock()
	l.spells, l.spellsMod = out, st.ModTime()
	l.mu.Unlock()
}

func (l *Lookups) refreshSpellText() {
	if l.artDir == "" {
		return
	}
	path := filepath.Join(l.artDir, "spells-text.json")
	st, err := os.Stat(path)
	if err != nil {
		return
	}
	l.mu.RLock()
	same := st.ModTime().Equal(l.spellTextMod)
	l.mu.RUnlock()
	if same {
		return
	}
	raw, err := os.ReadFile(path)
	if err != nil {
		return
	}
	var rawMap map[string]SpellText
	if err := json.Unmarshal(raw, &rawMap); err != nil {
		log.Printf("[Lookups] parse %s: %v", path, err)
		l.mu.Lock()
		l.spellTextMod = st.ModTime()
		l.mu.Unlock()
		return
	}
	out := make(map[uint32]SpellText, len(rawMap))
	for k, v := range rawMap {
		id, err := strconv.ParseUint(k, 10, 32)
		if err != nil {
			continue
		}
		out[uint32(id)] = v
	}
	l.mu.Lock()
	l.spellText, l.spellTextMod = out, st.ModTime()
	l.mu.Unlock()
}

func (l *Lookups) refreshNames() {
	if l.dataDir == "" {
		return
	}
	path := filepath.Join(l.dataDir, "tortoisebots-names.json")
	st, err := os.Stat(path)
	if err != nil {
		return
	}
	l.mu.RLock()
	same := st.ModTime().Equal(l.namesMod)
	l.mu.RUnlock()
	if same {
		return
	}
	raw, err := os.ReadFile(path)
	if err != nil {
		return
	}
	var doc struct {
		Skills   map[string]NamedEntry `json:"skills"`
		Areas    map[string]NamedEntry `json:"areas"`
		Factions map[string]NamedEntry `json:"factions"`
		ItemSets map[string]ItemSet    `json:"item_sets"`
		Enchants map[string]string     `json:"enchants"`
	}
	if err := json.Unmarshal(raw, &doc); err != nil {
		log.Printf("[Lookups] parse %s: %v", path, err)
		l.mu.Lock()
		l.namesMod = st.ModTime()
		l.mu.Unlock()
		return
	}
	n := Names{
		Skills:   parseNamedMap(doc.Skills),
		Areas:    parseNamedMap(doc.Areas),
		Factions: parseNamedMap(doc.Factions),
		ItemSets: make(map[uint32]ItemSet, len(doc.ItemSets)),
		Enchants: make(map[uint32]string, len(doc.Enchants)),
	}
	for k, v := range doc.Enchants {
		if id, err := strconv.ParseUint(k, 10, 32); err == nil {
			n.Enchants[uint32(id)] = v
		}
	}
	for k, v := range doc.ItemSets {
		id, err := strconv.ParseUint(k, 10, 32)
		if err != nil {
			continue
		}
		n.ItemSets[uint32(id)] = v
	}
	l.mu.Lock()
	l.names, l.namesMod = n, st.ModTime()
	l.mu.Unlock()
}

func parseNamedMap(in map[string]NamedEntry) map[uint32]NamedEntry {
	out := make(map[uint32]NamedEntry, len(in))
	for k, v := range in {
		id, err := strconv.ParseUint(k, 10, 32)
		if err != nil {
			continue
		}
		out[uint32(id)] = v
	}
	return out
}

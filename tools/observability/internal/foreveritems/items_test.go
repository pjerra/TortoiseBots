package foreveritems

import (
	"os"
	"path/filepath"
	"testing"
)

func TestStoreExtendedFields(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "tortoisebots-items.json")
	data := `{"items":[
		{"id":19169,"name":"Nightfall","quality":4,"ilvl":70,"req_level":60,"class":2,"subclass":1,"inv_type":17,"sell":129112,"buy":645562,"icon":132403,"stats":[],"dmg_min":187,"dmg_max":282,"delay":3500,"bonding":2,"set":0,"durability":120,"req_skill":0,"req_skill_rank":0,"classes":-1,"desc":"","spells":[[23605,2]]},
		{"id":22669,"name":"Icebane Breastplate","quality":4,"ilvl":80,"req_level":60,"class":4,"subclass":4,"inv_type":5,"sell":68390,"buy":341952,"icon":132633,"stats":[[7,24],[4,12],[52,42],[12,8]],"armor":899,"bonding":2,"set":0,"durability":165,"req_skill":0,"req_skill_rank":0,"classes":32767,"desc":"","spells":[]},
		{"id":21273,"name":"Blessed Qiraji Acolyte Staff","quality":4,"ilvl":79,"req_level":60,"class":2,"subclass":10,"inv_type":17,"sell":219003,"buy":1095018,"icon":135152,"stats":[[5,33],[7,32],[31,20],[32,14]],"dmg_min":189,"dmg_max":284,"delay":3000,"bonding":1,"set":0,"durability":120,"req_skill":0,"req_skill_rank":0,"classes":32767,"desc":"The weapon is infused and reinforced with Elementium.","spells":[]}
	]}`
	if err := os.WriteFile(path, []byte(data), 0o600); err != nil {
		t.Fatal(err)
	}
	s := New(path)
	if got := s.Len(); got != 3 {
		t.Fatalf("Len() = %d, want 3", got)
	}
	nf, ok := s.Get(19169)
	if !ok || nf.DmgMin != 187 || nf.DmgMax != 282 || nf.Delay != 3500 || nf.Durability != 120 || len(nf.Spells) != 1 || nf.Spells[0] != [2]int32{23605, 2} {
		t.Fatalf("unexpected Nightfall: %+v ok=%v", nf, ok)
	}
	ib, ok := s.Get(22669)
	if !ok || ib.Armor != 899 || len(ib.Stats) != 4 || ib.Stats[2] != [2]int32{52, 42} {
		t.Fatalf("unexpected Icebane Breastplate: %+v ok=%v", ib, ok)
	}
	st, ok := s.Get(21273)
	if !ok || st.Bonding != 1 || st.Desc == "" || len(st.Stats) != 4 {
		t.Fatalf("unexpected Blessed Qiraji Acolyte Staff: %+v ok=%v", st, ok)
	}
}

func TestLookupsAndFormatSpellDesc(t *testing.T) {
	dataDir := t.TempDir()
	artDir := t.TempDir()

	if err := os.WriteFile(filepath.Join(dataDir, "tortoisebots-spells.json"), []byte(`{"spells":[
		{"id":23605,"name":"Spell Vulnerability","icon":136116,"school":1,"cast_ms":0,"range":0,"duration_ms":5000,"cooldown_ms":0,"level":0,"base_level":0,"power_type":0,"cost":0,"cost_pct":0}
	]}`), 0o600); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dataDir, "tortoisebots-names.json"), []byte(`{
		"skills":{"164":{"name":"Blacksmithing","category":11,"icon":136241}},
		"areas":{"12":{"name":"Elwynn Forest","map":0,"parent":0}},
		"factions":{},
		"item_sets":{"217":{"name":"Judgement Armor","items":[16955,16958],"bonuses":[[3,23564]]}}
	}`), 0o600); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(artDir, "spells-text.json"), []byte(`{
		"23605":{"subtext":"","desc":"Spell damage taken by target increased by $s1% for $d.","aura":"","values":[15],"periods":[],"radius":[]}
	}`), 0o600); err != nil {
		t.Fatal(err)
	}

	l := NewLookups(dataDir, artDir)
	if got := l.GetSkillName(164); got != "Blacksmithing" {
		t.Fatalf("GetSkillName(164) = %q, want Blacksmithing", got)
	}
	if areas := l.Areas(); areas[12].Name != "Elwynn Forest" {
		t.Fatalf("Areas[12] = %+v, want Elwynn Forest", areas[12])
	}
	if set, ok := l.GetItemSet(217); !ok || set.Name != "Judgement Armor" || len(set.Bonuses) != 1 {
		t.Fatalf("GetItemSet(217) = %+v ok=%v", set, ok)
	}
	if desc := l.ResolveSpellDesc(23605); desc != "Spell damage taken by target increased by 15% for 5 sec." {
		t.Fatalf("ResolveSpellDesc(23605) = %q", desc)
	}
}

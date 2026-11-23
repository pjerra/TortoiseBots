package armory

import (
	"errors"
	"fmt"
)

// ModelCustomization is one saved appearance option for the 3D viewer.
type ModelCustomization struct {
	OptionID uint32 `json:"optionId"`
	ChoiceID uint32 `json:"choiceId"`
}

// ModelAppearance is what the optional 3D character preview needs: the race's
// viewer model id, the saved customization choices and the equipped items as
// [inventory type, item display id] pairs (the viewer's own item format).
type ModelAppearance struct {
	Race           uint8                `json:"race"`
	Gender         uint8                `json:"gender"`
	Class          uint8                `json:"class"`
	Model          uint32               `json:"model"`
	Approximate    bool                 `json:"approximate"`
	Customizations []ModelCustomization `json:"customizations"`
	Items          [][2]uint32          `json:"items"`
}

// ErrModelUnsupported marks cores whose appearance data the viewer cannot use.
var ErrModelUnsupported = errors.New("3D model preview is only available on ForeverCore")

// modelEquipSlots maps equipment slots to the viewer's inventory types.
// Neck, fingers and trinkets have no visual slot.
var modelEquipSlots = map[uint8]uint32{
	0: 1, 2: 3, 3: 4, 4: 5, 5: 6, 6: 7, 7: 8, 8: 9, 9: 10,
	14: 16, 15: 21, 16: 22, 17: 26, 18: 19,
}

// modelIDForRace maps a race/gender to Wowhead's ChrModelId. Forever's custom
// races 95/96 have no third-party model; they use the race-10 body and the
// preview says it is approximate.
func modelIDForRace(race, gender uint8) (uint32, bool, bool) {
	g := uint32(gender & 1)
	switch {
	case race >= 1 && race <= 9:
		return uint32(race-1)*2 + g + 1, false, true
	case race == 10:
		return 19 + g, false, true
	case race == 11:
		return 21 + g, false, true
	case race == 95 || race == 96:
		return 19 + g, true, true
	}
	return 0, false, false
}

// ModelAppearance returns the appearance payload for guid. It is nil when the
// character does not exist and ErrModelUnsupported on other cores.
func (s *Service) ModelAppearance(guid uint32) (*ModelAppearance, error) {
	if !s.isForever() {
		return nil, ErrModelUnsupported
	}
	var out ModelAppearance
	err := s.db.QueryRow(
		`SELECT race, gender, class FROM characters WHERE guid = ? AND deleteDate IS NULL`, guid,
	).Scan(&out.Race, &out.Gender, &out.Class)
	if err != nil {
		return nil, fmt.Errorf("bot %d not found", guid)
	}
	model, approximate, ok := modelIDForRace(out.Race, out.Gender)
	if !ok {
		return nil, fmt.Errorf("race %d has no model", out.Race)
	}
	out.Model, out.Approximate = model, approximate
	out.Customizations = []ModelCustomization{}
	out.Items = [][2]uint32{}

	// The race-10 fallback body has different option ids, so the custom race's
	// saved choices would all fall back to defaults anyway; send the model's own
	// defaults instead of a half-matching set.
	if !approximate {
		rows, err := s.db.Query(
			`SELECT chrCustomizationOptionID, chrCustomizationChoiceID
			   FROM character_customizations WHERE guid = ? ORDER BY chrCustomizationOptionID`, guid)
		if err != nil {
			return nil, err
		}
		defer rows.Close()
		for rows.Next() {
			var c ModelCustomization
			if err := rows.Scan(&c.OptionID, &c.ChoiceID); err != nil {
				return nil, err
			}
			out.Customizations = append(out.Customizations, c)
		}
		if err := rows.Err(); err != nil {
			return nil, err
		}
	}

	if s.cfg.Items == nil {
		return &out, nil
	}
	rows, err := s.db.Query(fmt.Sprintf(
		`SELECT ci.slot, ii.itemEntry
		   FROM character_inventory ci JOIN item_instance ii ON ii.guid = ci.item
		  WHERE ci.guid = ? AND ci.bag = 0 AND ci.slot >= 0 AND ci.slot < %d
		  ORDER BY ci.slot`, EquipmentSlotEnd), guid)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	seen := map[uint32]bool{}
	for rows.Next() {
		var slot uint8
		var entry uint32
		if err := rows.Scan(&slot, &entry); err != nil {
			return nil, err
		}
		invType, ok := modelEquipSlots[slot]
		if !ok {
			continue
		}
		item, ok := s.cfg.Items.Get(entry)
		if !ok || item.DisplayID == 0 {
			continue
		}
		if item.InvType == 20 { // robes replace the chest visual
			invType = 20
		}
		if seen[invType] {
			continue
		}
		seen[invType] = true
		out.Items = append(out.Items, [2]uint32{invType, item.DisplayID})
	}
	if err := rows.Err(); err != nil {
		return nil, err
	}
	return &out, nil
}

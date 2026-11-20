// ForeverCore side of the both-core helpers of slice 10 (druid, paladin, shaman).
// Owned by the slice-10 K worker: no other package edits this file. Turtle side: ai/cmangos-compat-s10.h. Notes: forever/docs/SLICE10-K-NOTES.md
// (each helper below with the file:line of the core call it makes). Helpers K10 did NOT need to add are reused: PlayerHiddenGmCompat
// (TbCompat_Unported.h:342), CreatureRankCompat (TbCompat_Unit.h:506), ForEachAuraOfTypeCompat (TbCompat_Unit.h:461), PetIsMiniPetCompat
// (TbCompat_V.h:208), FORM_MOONKIN (TbCompat_W2.h:196).
#pragma once

// Stop a unit's melee swing. T `Unit::AttackStop(bool targetSwitch, bool includingCast, bool includingCombo)`; F `Unit::AttackStop()` (Unit.h:739,
// Unit.cpp:6262): no arguments. T's `targetSwitch = true` marks "a target change, not a real combat stop" and only skips the creature-only reset
// at the end of the call; F's body does that reset for creatures unconditionally (Unit.cpp:6281-6284). The only caller is a Player (a balance
// druid leaving its melee kit), where both sides do the same: drop the victim, clear the target, interrupt the melee swing, send the stop packet.
// "Different answer": none for a Player. Returns F's result (false when nothing was being attacked).
inline bool AttackStopCompat(Unit* unit, bool /*targetSwitch*/) { return unit->AttackStop(); }

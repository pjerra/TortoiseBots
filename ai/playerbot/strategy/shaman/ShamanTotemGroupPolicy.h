#pragma once
#include <cstdint>
#include <string>

// Pure policy for subgroup-aware shaman totem selection (new automation, no
// donor equivalent: mod-playerbots picks totems manually per fight through
// per-slot strategies).
//
// Why: totems only affect the shaman's own party (subgroup) in 1.12, but the
// per-spec totem rows were fixed per spec (every spec dropped Windfury on
// Air; Restoration dropped Healing Stream on Water). A restoration shaman
// sitting in a caster/healer subgroup therefore buffed nobody: melee totems
// for a group with no melee, a trickle heal for a group that needs mana.
// The caller walks the shaman's own subgroup once per tick (SameSubGroup +
// map/range gate, the ImbueAction pattern) and feeds the counts in; this
// header turns them into the totem each slot should hold. No DB, no world
// scan: plain integers in, plain strings out.

namespace ai
{
// One subgroup member, as the selection rule needs to see it.
struct TotemGroupMember
{
    // True for the shaman itself (never counted as a beneficiary).
    bool self = false;
    // Melee that swings for Windfury / Strength of Earth: warrior, rogue,
    // melee-role others. Ranged, healers, tanks-with-mana all want mana.
    bool meleeAttacker = false;
    // Has a mana bar at all (warriors/rogues never want Mana Spring).
    bool hasMana = false;
    // Can cast the totem spell in question (prevents queueing untrained).
    bool unused = false;
};

// Caller-side census over the shaman's own subgroup (self excluded).
struct TotemSubgroupCensus
{
    uint8_t meleeAttackers = 0;
    uint8_t manaUsers = 0;
    // 0 when solo: keep the spec default, never stand down for lack of data.
    uint8_t members = 0;
};

inline TotemSubgroupCensus CountTotemSubgroup(TotemGroupMember const* members, uint8_t count)
{
    TotemSubgroupCensus census;
    for (uint8_t i = 0; i < count; ++i)
    {
        if (members[i].self)
            continue;
        ++census.members;
        if (members[i].meleeAttacker)
            ++census.meleeAttackers;
        if (members[i].hasMana)
            ++census.manaUsers;
    }
    return census;
}

// Earth slot: Strength of Earth only pays when the subgroup swings it —
// warriors, rogues, feral druids, melee shamans. Caster/healer groups get no
// automatic Strength of Earth (live raid 2026-10-14: SoE on 10 people in
// mage/warlock groups buffed nobody); the slot stays empty unless the
// player orders one. Stoneskin (damage reduction) is the fallback caller
// action when it wants one — this picks "" for caster groups, never it.
inline std::string BestEarthTotemForSubgroup(TotemSubgroupCensus const& census,
    bool strengthKnown)
{
    if (census.meleeAttackers == 0)
        return "";
    return strengthKnown ? "strength of earth totem" : "";
}

// Air slot: Windfury when the subgroup has melee to buff, otherwise the
// agility for casters/healers (Grace of Air when trained, Tranquil Air when
// trained and aggro matters — Turtle custom 25908 — else no air totem).
// Manual `totem air ...` orders always win (caller checks HasStrategy).
inline std::string BestAirTotemForSubgroup(TotemSubgroupCensus const& census,
    bool windfuryKnown, bool graceKnown, bool tranquilKnown)
{
    if (census.meleeAttackers > 0)
        return windfuryKnown ? "windfury totem" : "";
    if (tranquilKnown)
        return "tranquil air totem";
    if (graceKnown)
        return "grace of air totem";
    return "";
}

// Water slot: Mana Spring when anyone in the subgroup has mana to restore,
// else Healing Stream. Restoration in a caster group therefore springs;
// a pure melee group still gets the heal trickle.
inline std::string BestWaterTotemForSubgroup(TotemSubgroupCensus const& census,
    bool manaSpringKnown, bool healingStreamKnown)
{
    if (census.manaUsers > 0)
        return manaSpringKnown ? "mana spring totem" : (healingStreamKnown ? "healing stream totem" : "");
    return healingStreamKnown ? "healing stream totem" : "";
}

// Mana Tide fires for the subgroup's mana, not just the shaman's own bar:
// any healer or caster in the subgroup below the low-mana line arms it
// (the shaman's own low mana is row zero of the same check). Cooldown and
// existing-tide guards live in the trigger/action, not here.
inline bool ManaTideShouldFire(bool selfLowMana, bool subgroupHealerLowMana, bool subgroupCasterLowMana)
{
    return selfLowMana || subgroupHealerLowMana || subgroupCasterLowMana;
}
}

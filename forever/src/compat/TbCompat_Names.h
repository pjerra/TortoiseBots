// Name-level entries for the headers of the slice (slice 1, round R3). Included from forever-compat-shim.h.
//
// Only entries that need no change in a shared file: a constant, a type, a member name or a call shape a macro can carry.
// Every entry says which question it answers and where both cores define it (T = the Turtle core
// ~/playerbots-references/penqle-tortoise-wow-modsys/src/game, F = the pinned ForeverCore src/server/game).
// Where the two cores answer the same question with different numbers the entry says so.
#pragma once

// ---------------------------------------------------------------------------------------------------------------
// Unit state flags. The Turtle names (UNIT_STAT_*) are single bits of one persistent-state word (T Objects/UnitDefines.h
// :308-379); ForeverCore keeps the same facts as UNIT_STATE_* bits (F Entities/Unit/Unit.h:277-323). The numbers differ, so
// each Turtle bit is the ForeverCore bit of the same fact and every Turtle mask is the same union of those facts. The bot
// only hands them to HasUnitState / ClearUnitState, which take the core's own words.
//   ROOT, STUNNED, CONFUSED, FLEEING, CHASE, FOLLOW, POSSESSED: the same state (a root/stun aura, a confused/flee/chase/
//   follow movement generator active, a possession aura)
//   TAXI_FLIGHT <-> IN_FLIGHT (F: "player is in flight mode", T: "player is in flight mode")
//   FEIGN_DEATH <-> DIED (F: "player has fake death aura", T: Unit::SetFeignDeath)
// ---------------------------------------------------------------------------------------------------------------
enum TbLegacyUnitState : uint32
{
    UNIT_STAT_ROOT                          = UNIT_STATE_ROOT,
    UNIT_STAT_STUNNED                       = UNIT_STATE_STUNNED,
    UNIT_STAT_TAXI_FLIGHT                   = UNIT_STATE_IN_FLIGHT,
    UNIT_STAT_CONFUSED                      = UNIT_STATE_CONFUSED,
    UNIT_STAT_FLEEING                       = UNIT_STATE_FLEEING,
    UNIT_STAT_CHASE                         = UNIT_STATE_CHASE,
    UNIT_STAT_FOLLOW                        = UNIT_STATE_FOLLOW,
    UNIT_STAT_POSSESSED                     = UNIT_STATE_POSSESSED,
    UNIT_STAT_FEIGN_DEATH                   = UNIT_STATE_DIED,
    // T :367-370: ROOT | STUNNED | TAXI_FLIGHT | CONFUSED | FLEEING
    UNIT_STAT_NO_FREE_MOVE                  = UNIT_STATE_ROOT | UNIT_STATE_STUNNED | UNIT_STATE_IN_FLIGHT | UNIT_STATE_CONFUSED | UNIT_STATE_FLEEING,
    // T :372-379: (STUNNED | FEIGN_DEATH | CONFUSED | FLEEING) | (FLEEING | POSSESSED)
    UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL = UNIT_STATE_STUNNED | UNIT_STATE_DIED | UNIT_STATE_CONFUSED | UNIT_STATE_FLEEING | UNIT_STATE_POSSESSED,
};

// Movement: the "is moving in space" mask of the movement info. T Objects/Object.h:242-245 (forward, backward, strafes,
// pitch, jumping, falling far, spline elevation) and F Entities/Object/MovementInfo.h:93-95 (forward, backward, strafes,
// falling, ascending, descending) ask the same question; the member sets differ because the clients' flag sets differ.
#define MOVEFLAG_MASK_MOVING MOVEMENTFLAG_MASK_MOVING

// ---------------------------------------------------------------------------------------------------------------
// Spell targets: the client's numbering is the same on both cores (the same Targets table), ForeverCore names the
// entries by shape. T Spells/SpellDefines.h:31-61 against F Miscellaneous/SharedDefines.h:2973-3000, number for number.
// ---------------------------------------------------------------------------------------------------------------
#define TARGET_UNIT_ENEMY                           TARGET_UNIT_TARGET_ENEMY     // 6
#define TARGET_ENUM_UNITS_ENEMY_AOE_AT_SRC_LOC      TARGET_UNIT_SRC_AREA_ENEMY   // 15
#define TARGET_ENUM_UNITS_ENEMY_AOE_AT_DEST_LOC     TARGET_UNIT_DEST_AREA_ENEMY  // 16
#define TARGET_LOCATION_CASTER_DEST                 TARGET_DEST_CASTER           // 18
#define TARGET_LOCATION_CASTER_SRC                  TARGET_SRC_CASTER            // 22
#define TARGET_ENUM_UNITS_ENEMY_IN_CONE_24          TARGET_UNIT_CONE_ENEMY_24    // 24
#define TARGET_ENUM_UNITS_ENEMY_AOE_AT_DYNOBJ_LOC   TARGET_DEST_DYNOBJ_ENEMY     // 28
#define TARGET_ENUM_UNITS_ENEMY_WITHIN_CASTER_RANGE TARGET_DEST_CASTER_UNK_36    // 36

// Team 0, "not known / not set" (T SharedDefines.h:228); ForeverCore's name for the same 0 is TEAM_OTHER (F SharedDefines.h:1313).
#define TEAM_NONE TEAM_OTHER

// Equipment slots: the same nineteen indices 0..18 and the same end marker 19 (T SharedDefines.h:179-200, F Player.h:738-758).
#define SLOT_HEAD       EQUIPMENT_SLOT_HEAD
#define SLOT_NECK       EQUIPMENT_SLOT_NECK
#define SLOT_SHOULDERS  EQUIPMENT_SLOT_SHOULDERS
#define SLOT_SHIRT      EQUIPMENT_SLOT_BODY
#define SLOT_CHEST      EQUIPMENT_SLOT_CHEST
#define SLOT_WAIST      EQUIPMENT_SLOT_WAIST
#define SLOT_LEGS       EQUIPMENT_SLOT_LEGS
#define SLOT_FEET       EQUIPMENT_SLOT_FEET
#define SLOT_WRISTS     EQUIPMENT_SLOT_WRISTS
#define SLOT_HANDS      EQUIPMENT_SLOT_HANDS
#define SLOT_FINGER1    EQUIPMENT_SLOT_FINGER1
#define SLOT_FINGER2    EQUIPMENT_SLOT_FINGER2
#define SLOT_TRINKET1   EQUIPMENT_SLOT_TRINKET1
#define SLOT_TRINKET2   EQUIPMENT_SLOT_TRINKET2
#define SLOT_BACK       EQUIPMENT_SLOT_BACK
#define SLOT_MAIN_HAND  EQUIPMENT_SLOT_MAINHAND
#define SLOT_OFF_HAND   EQUIPMENT_SLOT_OFFHAND
#define SLOT_RANGED     EQUIPMENT_SLOT_RANGED
#define SLOT_TABARD     EQUIPMENT_SLOT_TABARD
#define SLOT_EMPTY      EQUIPMENT_SLOT_END   // the one-past-the-end marker, 19 on both

// Counts the bot uses as array sizes (T SharedDefines.h:55 "max+1 for player race", :1245). The arrays are indexed by the
// vanilla ids the configuration and the class tables name; ForeverCore has no constant of either name.
#define MAX_RACES 11
#define MAX_SKILL_TYPE 763

// ---------------------------------------------------------------------------------------------------------------
// Member names and call shapes a macro can carry. Each is used by the bot only as `x->Name(...)` and no bot class defines a
// member of the same name (checked with grep over ai/ runtime/ host/ commands/), so renaming the token cannot touch a bot
// declaration. A name a bot class DOES define (IsInRoots and IsWithinLOSInMap in ServerFacade) is not done here but by a
// both-core helper (TbCompat_Unit.h).
// ---------------------------------------------------------------------------------------------------------------
// "Is this unit rooted": T Unit.h:701 reads the root movement flag; the same flag, through the same member, here.
#define IsRooted() m_movementInfo.HasMovementFlag(MOVEMENTFLAG_ROOT)
// "Is this unit on a taxi flight": T Unit.h:632 is the taxi-flight unit state, F Unit.h:1044 IsInFlight() the in-flight unit state.
#define IsTaxiFlying() IsInFlight()
// The set of units attacking this one (T Unit.h:1236, F Unit.h:741); both are std::set<Unit*> (AttackerSet).
#define GetAttackers getAttackers
// "Is this a world boss": T Creature.h:648 (rank WORLDBOSS, never a pet), F Creature.cpp:2637 (the boss type flag, never a pet).
#define IsWorldBoss isWorldBoss
// "Has the core given up reaching its target": T Creature.h:953 (target unreachable for > 3 s), F Creature.h:392 (cannot-reach flag).
// The timers differ; the answer is each core's own.
#define IsEvadeBecauseTargetNotReachable CanNotReachTarget
// "Can this unit's auto attack reach that unit now": T Unit.cpp:11056 (combat reaches + leeway, z-reach), F Unit.cpp:734
// (the core's melee range between the two, same map and phase). The core's own answer on each; the numbers differ by design.
#define CanReachWithMeleeAutoAttack IsWithinMeleeRange

// The terrain manager: T `sTerrainMgr` (a singleton by reference), F TerrainMgr::Instance() (TerrainMgr.h:142). The calls on it
// differ (a phase shift, the map id first); the bot's own uses go through the helpers in TbCompat_Unit.h.
#define sTerrainMgr (TerrainMgr::Instance())

// "How long does this aura still last, in ms" (-1 = no end). T Spells/SpellAuras.h:188,475 (Aura asks its holder, the holder's
// duration), F Aura::GetDuration (the remaining duration, -1 when it never ends). No bot class defines a member of this name.
#define GetAuraDuration GetDuration

// Assertion: T `MANGOS_ASSERT(x)` aborts when x is false (shared/Errors.h:62); F's ASSERT(x) does the same.
#define MANGOS_ASSERT(...) ASSERT(__VA_ARGS__)

// The mask of a dispel type: T SpellEntry.h:417 (DISPEL_ALL gives DISPEL_ALL_MASK, any other 1 << type), F SpellInfo.h:561.
#define GetDispellMask SpellInfo::GetDispelMask

// The creature template's NPC flag word: T `npc_flags` (uint32), F `npcflag` (uint64, CreatureData.h). The classic bits the
// bot tests (gossip, quest giver, trainer, vendor, ...) are the same low bits on both. No bot class has a member of this name.
#define npc_flags npcflag

// The list of loot items: T LootMgr.h:156 a vector of LootItem values.
using LootItemList = std::vector<LootItem>;

// cmangos's console progress bar. The Turtle shim has it as a class that does nothing (ai/cmangos-compat-shim.h:175-185,
// there is no bar on that core either); same here.
class BarGoLink
{
public:
    BarGoLink() {}
    template<typename T> BarGoLink(T /*total*/) {}
    void step() {}
    void Step() {}
    static void SetOutputState(bool /*state*/) {}
};

// The classes a quest is limited to, as a class mask (0 = every class): T QuestDef.h:231 GetRequiredClasses, F Quest::GetAllowableClasses.
#define GetRequiredClasses GetAllowableClasses

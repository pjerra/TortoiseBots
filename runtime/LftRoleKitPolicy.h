#pragma once

// Issue #966: the LFT fill pick proves the bot's talent spec can carry the
// queued slot (LftBotFillService::RoleMismatchReason), but the installed
// combat kit is a separate decision: druid Feral masks Tank|DPS while the kit
// default is Cat ("Bear is opt-in via role, never a talent guess",
// AiFactory::AddDefaultCombatStrategies). A feral druid was therefore queued
// into the tank slot with its own cat kit still installed and fought as cat
// mid-dungeon. PlayerbotAI::m_forcedRole exists for exactly this ("role
// handed out by the dungeon finder"), and PlayerbotAI::ResetStrategies
// applies it when the bot adopts the group leader: the fill only needs to
// hand the queued role over when the spec covers the slot but the installed
// kit would not play it. Single-role specs (holy priest heal pick,
// protection warrior tank pick, retribution paladin dps pick) already run a
// kit that matches their slot and skip the assignment.
//
// Pure policy: no core/engine headers, unit-tested standalone
// (tools/test_lft_role_kit_policy.cpp).

#include <cstdint>

namespace TortoiseBots
{

// ai::BotRoles bits (tank 1, healer 2, dps 4) == the LFT_ROLE_* values the
// queue hands out (0x01/0x02/0x04), so one mask type serves both.
inline constexpr uint8_t kLftRoleTank = 0x01;
inline constexpr uint8_t kLftRoleHealer = 0x02;
inline constexpr uint8_t kLftRoleDamage = 0x04;

// Role bits of a bot's installed combat kit, derived from the kit's named
// strategies. The caller owns the name->bit mapping (mirror of the
// BotPlayerAdapter::GetBotRoles named-spec tables; ambient helpers such as
// "dps assist" must not be mapped).
struct LftRoleKitInputs
{
    uint8_t queuedRole;     // LFT_ROLE_* bit the fill queued the bot for
    uint8_t talentRoleMask; // ai::BotRoles mask at pick time (spec covers the slot?)
    uint8_t kitRoleMask;    // role bits of the installed combat kit
};

// True when the queued slot is achievable by the bot's own spec, yet its
// installed kit would not play it, so the fill must hand the role to the bot
// (SetForcedRole + snapshot invalidation + auto talents).
inline bool LftKitNeedsRoleAssignment(LftRoleKitInputs const& in)
{
    if (!in.queuedRole)
        return false;
    // Not a natural pick: the eligibility gate already rejected it, and the
    // (opt-in) borrow handoff runs independently of this check.
    if ((in.talentRoleMask & in.queuedRole) == 0)
        return false;
    return (in.kitRoleMask & in.queuedRole) == 0;
}

} // namespace TortoiseBots

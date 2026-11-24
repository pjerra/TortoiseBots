// Ticket #160 (the fill half): the decisions of the dungeon-finder fill, with no core type and no engine header, so
// forever/tools/test_lfg_fill_policy.cpp runs them standalone.
//
// The fill takes free (masterless) host bots one at a time and groups them with the player who queued, until the party
// has the composition the core's own finder asks for: 1 tank, 1 healer, 3 damage (DungeonFinding/LFG.h:28-33). The
// player's declared role takes one of those slots. The eligibility list is TortoiseBots' LFT fill (runtime/
// LftBotFillService.cpp:125-146): in the world, a free bot, ungrouped, not in a battleground or its queue, not
// teleporting, not on a taxi, not in combat, alive, level 10 or more. `IsFreeBot` is the hold against taking a bot that
// is already commanded or grouped (BotMgr::BindMaster clears free, TbBotMgr.cpp:493-506).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace TortoiseBots
{
namespace lfgfill
{
// The engine's role bits (ai/playerbot/ChatHelper.h:16-19 BOT_ROLE_*): what `.tb role` stores and the kit builder reads.
enum RoleBit : std::uint8_t
{
    RoleNone = 0x00,
    RoleTank = 0x01,
    RoleHealer = 0x02,
    RoleDps = 0x04,
};

// tank, healer (heal) or dps (damage); RoleNone for anything else.
inline std::uint8_t RoleOf(std::string_view word)
{
    if (word == "tank")
        return RoleTank;
    if (word == "healer" || word == "heal")
        return RoleHealer;
    if (word == "dps" || word == "damage")
        return RoleDps;
    return RoleNone;
}

inline char const* RoleName(std::uint8_t role)
{
    switch (role)
    {
        case RoleTank: return "tank";
        case RoleHealer: return "healer";
        case RoleDps: return "dps";
        default: return "none";
    }
}

// The core's group role bits (DungeonFinding/LFG.h:39-45), which the engine's role resolver reads through
// Group::GetLfgRoles (forever/src/slice/SliceAiFactory.cpp:239-250). The two layers use different numbers.
inline constexpr std::uint8_t CoreRoleLeader = 0x01;
inline constexpr std::uint8_t CoreRoleTank = 0x02;
inline constexpr std::uint8_t CoreRoleHealer = 0x04;
inline constexpr std::uint8_t CoreRoleDamage = 0x08;

inline std::uint8_t CoreRoleBits(std::uint8_t engineRole)
{
    switch (engineRole)
    {
        case RoleTank: return CoreRoleTank;
        case RoleHealer: return CoreRoleHealer;
        case RoleDps: return CoreRoleDamage;
        default: return 0;
    }
}

inline std::uint8_t EngineRoleOfCore(std::uint8_t coreRole)
{
    if (coreRole & CoreRoleTank)
        return RoleTank;
    if (coreRole & CoreRoleHealer)
        return RoleHealer;
    if (coreRole & CoreRoleDamage)
        return RoleDps;
    return RoleNone;
}

// The classes a role can be forced on. Tank: the classes of the slice's tank kits (slice/SliceHybridPolicy.h:30-33:
// warrior, druid bear, protection paladin). Healer: the classes of its healer kits (SliceAiFactory.cpp ClassCanHeal:
// priest, shaman, paladin, druid). Damage: every class.
inline bool ClassTakesTankKit(unsigned classId)
{
    return classId == 1 /*warrior*/ || classId == 2 /*paladin*/ || classId == 11 /*druid*/;
}

inline bool ClassTakesHealKit(unsigned classId)
{
    return classId == 2 /*paladin*/ || classId == 5 /*priest*/ || classId == 7 /*shaman*/ || classId == 11 /*druid*/;
}

inline bool ClassFillsRole(unsigned classId, std::uint8_t role)
{
    switch (role)
    {
        case RoleTank: return ClassTakesTankKit(classId);
        case RoleHealer: return ClassTakesHealKit(classId);
        case RoleDps: return true;
        default: return false;
    }
}

// The roles still to be filled, tank first, then healer, then damage. `playerRole` is the queuing player's own slot;
// `filled` are the engine role bits of the bots already in the party for this player. A solo dps player gets
// {tank, healer, dps, dps}.
inline std::vector<std::uint8_t> MissingSlots(std::uint8_t playerRole, std::vector<std::uint8_t> const& filled)
{
    std::size_t tank = 1, healer = 1, dps = 3;   // LFG_TANKS_NEEDED, LFG_HEALERS_NEEDED, LFG_DPS_NEEDED
    auto const take = [](std::size_t& count, std::uint8_t role)
    {
        if (role != RoleNone && count)
            --count;
    };
    take(tank, playerRole == RoleTank ? RoleTank : RoleNone);
    take(healer, playerRole == RoleHealer ? RoleHealer : RoleNone);
    take(dps, playerRole == RoleDps ? RoleDps : RoleNone);
    for (std::uint8_t role : filled)
    {
        take(tank, role == RoleTank ? RoleTank : RoleNone);
        take(healer, role == RoleHealer ? RoleHealer : RoleNone);
        take(dps, role == RoleDps ? RoleDps : RoleNone);
    }
    std::vector<std::uint8_t> missing;
    missing.reserve(tank + healer + dps);
    for (std::size_t i = 0; i < tank; ++i)
        missing.push_back(RoleTank);
    for (std::size_t i = 0; i < healer; ++i)
        missing.push_back(RoleHealer);
    for (std::size_t i = 0; i < dps; ++i)
        missing.push_back(RoleDps);
    return missing;
}

// One bot as the fill reads it. `naturalRoles` is ai::BotRoles from AiFactory::GetPlayerRoles (ChatHelper.h:16-19, the
// bot's own spec); `classId` is the core class id. Nothing here is a core or engine type.
struct CandidateFacts
{
    bool inWorld = false;
    bool free = false;               // BotMgr::IsFreeBot: masterless and ungrouped
    bool grouped = false;
    bool hasMaster = false;
    bool hasBrain = false;           // the engine brain exists (BotMgr::FollowStatus)
    bool inBattleground = false;
    bool inBattlegroundQueue = false;
    bool teleporting = false;
    bool flying = false;
    bool inCombat = false;
    bool alive = false;
    bool claimed = false;            // another queued player's fill already took it
    std::uint32_t level = 0;
    std::uint32_t team = 0;
    std::uint8_t naturalRoles = 0;
    unsigned classId = 0;
};

enum class Refusal : std::uint8_t
{
    None,
    NotInWorld,
    NotFree,
    Grouped,
    NoBrain,
    InBattleground,
    Teleporting,
    Flying,
    InCombat,
    Dead,
    LowLevel,
    OtherTeam,
    Claimed,
    WrongRole,      // the bot's spec does not cover the slot and borrowing is off
    NoKit,          // borrowing is on but the class has no kit for the slot
};

inline char const* RefusalName(Refusal refusal)
{
    switch (refusal)
    {
        case Refusal::None: return "none";
        case Refusal::NotInWorld: return "not-in-world";
        case Refusal::NotFree: return "not-free";
        case Refusal::Grouped: return "grouped";
        case Refusal::NoBrain: return "no-engine";
        case Refusal::InBattleground: return "in-battleground";
        case Refusal::Teleporting: return "teleporting";
        case Refusal::Flying: return "in-flight";
        case Refusal::InCombat: return "in-combat";
        case Refusal::Dead: return "dead";
        case Refusal::LowLevel: return "low-level";
        case Refusal::OtherTeam: return "other-team";
        case Refusal::Claimed: return "claimed";
        case Refusal::WrongRole: return "wrong-role";
        case Refusal::NoKit: return "no-kit";
    }
    return "unknown";
}

// Why this bot cannot fill `role` for a player of `playerTeam`; Refusal::None means it can. With borrowing off a bot
// fills only a role its own spec covers (TortoiseBots' RoleMismatchReason); with the key on, any class with a kit for
// the slot may borrow it (AiPlayerbot.RandomBotLftAllowRoleBorrow).
inline Refusal CandidateRefusal(CandidateFacts const& bot, std::uint8_t role, std::uint32_t playerTeam, bool allowBorrow)
{
    if (role != RoleTank && role != RoleHealer && role != RoleDps)
        return Refusal::WrongRole;
    if (!bot.inWorld)
        return Refusal::NotInWorld;
    if (!bot.free || bot.hasMaster)
        return Refusal::NotFree;
    if (bot.grouped)
        return Refusal::Grouped;
    if (bot.inBattleground || bot.inBattlegroundQueue)
        return Refusal::InBattleground;
    if (bot.teleporting)
        return Refusal::Teleporting;
    if (bot.flying)
        return Refusal::Flying;
    if (bot.inCombat)
        return Refusal::InCombat;
    if (!bot.alive)
        return Refusal::Dead;
    if (bot.level < 10)
        return Refusal::LowLevel;
    if (!bot.hasBrain)
        return Refusal::NoBrain;
    if (bot.team != playerTeam)
        return Refusal::OtherTeam;
    if (bot.claimed)
        return Refusal::Claimed;
    if ((bot.naturalRoles & role) == 0)
    {
        if (!allowBorrow)
            return Refusal::WrongRole;
        if (!ClassFillsRole(bot.classId, role))
            return Refusal::NoKit;
    }
    return Refusal::None;
}

inline bool CandidateEligible(CandidateFacts const& bot, std::uint8_t role, std::uint32_t playerTeam, bool allowBorrow)
{
    return CandidateRefusal(bot, role, playerTeam, allowBorrow) == Refusal::None;
}

struct Choice
{
    long index = -1;        // index into the facts vector, -1 = none
    bool borrowed = false;  // eligible only through borrowing
};

// The best candidate for one slot: a bot whose own spec covers the role first, then the smallest level gap to the
// player, then the lowest index (the caller passes the bots in guid order). A borrowed bot never beats a natural one.
inline Choice PickCandidate(std::vector<CandidateFacts> const& bots, std::uint8_t role, std::uint32_t playerLevel,
    std::uint32_t playerTeam, bool allowBorrow)
{
    Choice best;
    std::uint32_t bestGap = 0;
    bool bestNatural = false;
    for (std::size_t i = 0; i < bots.size(); ++i)
    {
        if (!CandidateEligible(bots[i], role, playerTeam, allowBorrow))
            continue;
        bool const natural = (bots[i].naturalRoles & role) != 0;
        std::uint32_t const gap = bots[i].level > playerLevel ? bots[i].level - playerLevel : playerLevel - bots[i].level;
        bool const better = best.index < 0 ||
            (natural && !bestNatural) ||
            (natural == bestNatural && gap < bestGap);
        if (better)
        {
            best.index = long(i);
            best.borrowed = !natural;
            bestGap = gap;
            bestNatural = natural;
        }
    }
    return best;
}

// The live facts of the player whose party the fill has been topping up.
struct PlayerFacts
{
    bool online = false;
    bool inWorld = false;
    bool inGroup = false;
};

// The fill lets go of every bot it grouped once the player is gone or out of the party.
inline bool ShouldRelease(PlayerFacts const& player)
{
    return !player.online || !player.inWorld || !player.inGroup;
}

// A party holds five (MAX_GROUP_SIZE): the player and four bots.
inline bool PartyComplete(std::size_t members)
{
    return members >= 5;
}

} // namespace lfgfill
} // namespace TortoiseBots

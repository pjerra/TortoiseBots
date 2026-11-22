// The addon roster reply without the core: the TBM wire shapes of TB's
// `roster`/`list`/`alts` answers (TortoiseBots/commands/Info.cpp, Party.cpp
// SendBotStateLine) and the handshake trailer, with the TBF-only TBM:VERBS
// line. No core header and no legacy type; tested standalone by
// forever/tools/test_addon_roster.cpp. The live half that reads BotMgr,
// party state and the owned-alt store is host/TbAddonRoster.cpp.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "host/TbAddonPure.h"
#include "host/player/TbPlayerAuthPure.h"

namespace TortoiseBots
{
namespace roster
{
// Addon protocol levels (TortoiseBots/host/ModuleVersion.h:77-78). TBFM
// accepts them while serverMinAddonProtocol (1) <= C.PROTOCOL (1) and
// serverProtocol (1) >= C.MIN_MODULE_PROTOCOL (0) (Core.lua CompatProblem).
inline constexpr unsigned kAddonProtocol = 1;
inline constexpr unsigned kMinAddonProtocol = 1;

// The capability tokens this server earns today: only verbs that exist
// natively (player::Dispatch plus the f-actions `action` intents). The rest of
// TB's set (inventory, behavior, loot-log, downtime, professions, blessing,
// blessing-player, choice, focus-mark, use-object, guild-orders,
// guild-order-steps, guild-order-nobuy, dungeon-run) stays dark until its verb
// lands: the addon would otherwise light buttons whose sends come back
// ACTION_ERR.
// "tb-gm" rides along when the requester holds the .tb permission
// (rbac::RBAC_PERM_COMMAND_GM, 371, the permission every .tb row asks for):
// the addon uses it to decide whether its GM-only .tb bridge may be used.
inline std::vector<std::string> CapsFor(bool hasTbPermission)
{
    // TB-relative order (Info.cpp CAPS line). pull-seconds/flee/engage ride on
    // f-actions (action pull/pullback [seconds] [ranged|body], action flee,
    // action engage [seconds]); TB advertises claimed-roster unconditionally
    // and the stream below is live (empty until the guild-bot stores land).
    if (hasTbPermission)
        return { "pull-seconds", "flee", "claimed-roster", "engage", "tb-gm" };
    return { "pull-seconds", "flee", "claimed-roster", "engage" };
}

// One roster row: the same bots `.bot who` shows (see RosterVisible).
struct RosterRow
{
    std::uint32_t guidLow = 0;
    std::string name;
    std::uint32_t classId = 0;
    std::string state;
    bool grouped = false;
    std::string location;
    std::string className;
};

// One guild-tab row (TB's TBM:CLAIMED shape). TBF fills name/class/level from
// the live bot and "-" for what it has no store for (profs, gear lock,
// alt autonomy); the stream stays well-formed so the addon commits it.
struct ClaimedRow
{
    std::uint32_t guidLow = 0;
    std::string name;
    std::uint32_t classId = 0;
    std::uint32_t level = 0;
    std::string state;
    bool grouped = false;
    std::string location;
    std::uint32_t gearLocked = 0;
    std::string profs = "-";
    std::uint32_t isAlt = 0;
    std::string className;
};

struct AltSumLine
{
    std::string name;
    std::string text;
};

struct BotStateLine
{
    std::string name;
    std::string fields;
};

inline std::string JoinTokens(std::vector<std::string> const& tokens)
{
    std::string out;
    for (std::string const& token : tokens)
    {
        if (!out.empty())
            out += ',';
        out += token;
    }
    return out;
}

inline std::string RosterBegin(std::size_t count)
{
    return "TBM:ROSTER_BEGIN|" + std::to_string(count);
}

inline std::string RosterLine(RosterRow const& row)
{
    // TB's field order exactly (Info.cpp HandleRoster); older addons read the
    // first six fields and ignore the class-name tail.
    return "TBM:ROSTER|" + std::to_string(row.guidLow) + "|" + addon::pure::Safe(row.name) + "|" +
        std::to_string(row.classId) + "|" + addon::pure::Safe(row.state) + "|" + (row.grouped ? "1" : "0") + "|" +
        addon::pure::Safe(row.location) + "|" + addon::pure::Safe(row.className);
}

inline std::string AltBotsLine(std::vector<std::string> const& names)
{
    // TB's #872 line: the autonomous rows, comma-joined, empty when none.
    return "TBM:ALTBOTS|" + JoinTokens(names);
}

inline std::string AltSumLineText(AltSumLine const& row)
{
    return "TBM:ALTSUM|" + addon::pure::Safe(row.name) + "|" + addon::pure::Safe(row.text);
}

inline std::string AltSumEnd(std::size_t count)
{
    return "TBM:ALTSUM_END|" + std::to_string(count);
}

inline std::string ClaimedBegin(std::size_t count)
{
    return "TBM:CLAIMED_BEGIN|" + std::to_string(count);
}

inline std::string ClaimedLine(ClaimedRow const& row)
{
    return "TBM:CLAIMED|" + std::to_string(row.guidLow) + "|" + addon::pure::Safe(row.name) + "|" +
        std::to_string(row.classId) + "|" + std::to_string(row.level) + "|" + addon::pure::Safe(row.state) + "|" +
        (row.grouped ? "1" : "0") + "|" + addon::pure::Safe(row.location) + "|" + std::to_string(row.gearLocked) +
        "|" + addon::pure::Safe(row.profs) + "|" + std::to_string(row.isAlt) + "|" +
        addon::pure::Safe(row.className);
}

inline std::string BotStateBegin(std::size_t count)
{
    return "TBM:BOTSTATE_BEGIN|" + std::to_string(count);
}

inline std::string BotStateText(BotStateLine const& row)
{
    return "TBM:BOTSTATE|" + addon::pure::Safe(row.name) + "|" + row.fields;
}

inline std::string VersionLine(std::string_view version)
{
    return "TBM:VERSION|" + addon::pure::Safe(version);
}

inline std::string ProtoLine()
{
    return "TBM:PROTO|" + std::to_string(kAddonProtocol) + "|" + std::to_string(kMinAddonProtocol);
}

// No update feed on TBF yet; the addon reads "-" as nil and shows no popup.
inline std::string LatestLine()
{
    return "TBM:LATEST|-|-";
}

// Forever-only line, ignored by older addons: the exact verbs this server
// answers natively, so the addon picks native vs bridge per command.
inline std::string VerbsLine(std::vector<std::string> const& verbs)
{
    return "TBM:VERBS|" + JoinTokens(verbs);
}

inline std::string CapsLine(std::vector<std::string> const& caps)
{
    return "TBM:CAPS|" + JoinTokens(caps);
}

// TB's AddonCommandChannel verdict (Info.cpp:382-397): the addon sends its
// next commands over the channel only while this says "party".
inline std::string TransportVerdict(bool addonChannelOn, bool grouped, bool bgGroup)
{
    if (!addonChannelOn || !grouped || bgGroup)
        return "none";
    return "party";
}

inline std::string TransportLine(std::string_view channel)
{
    return "TBM:TRANSPORT|" + std::string(channel);
}

// In-world on this core: online. TBF keeps no durable owned-character rows,
// so offline alts never reach a row; "starting"/"removing" have no TBF
// equivalent and are never sent.
inline std::string RosterStateOf(bool inWorld)
{
    return inWorld ? "online" : "offline";
}

// The BOTSTATE move= word from what the bot was last told. TB's vocabulary
// is follow/stay/guard/free/custom, plus flee for a passive hold; TBF keeps
// the four it can produce.
inline std::string BotMoveWord(bool isFree, std::string_view lastOrderVerb)
{
    if (isFree)
        return "free";
    if (lastOrderVerb == "stay")
        return "stay";
    if (lastOrderVerb == "flee")
        return "flee";
    return "follow";
}

// Who the addon roster shows: exactly the four faces `.bot who` lists
// (TbPlayerCmd.cpp Visible) - same-account, master, commanded, or real-player
// leader. No GM widening (`who all` is a chat-only GM view; the addon never
// dumps the realm-wide pool) and no run gate (a run refuses orders, it hides
// nothing).
inline bool RosterVisible(player::Caller const& caller, player::BotFacts const& bot)
{
    if (!bot.isBot)
        return false;
    if (caller.account != 0 && bot.account != 0 && caller.account == bot.account)
        return true;
    if (bot.master != 0 && bot.master == caller.guid)
        return true;
    if (bot.commandedByCaller)
        return true;
    if (bot.groupLeader != 0 && bot.groupLeader == caller.guid && !bot.leaderIsBot)
        return true;
    return false;
}
} // namespace roster
} // namespace TortoiseBots

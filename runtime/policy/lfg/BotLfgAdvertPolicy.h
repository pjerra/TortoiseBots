// How a waiting bot LFG group advertises in the LookingForGroup channel and
// answers whispers (post texts, cadence, the "inv" grammar, the player role
// check, the whisper decision and the reply texts). The glue is
// runtime/BotLfgService.cpp.
// Plain data and the standard library only.

#pragma once

#include "BotLfgSelectionPolicy.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace TortoiseBots {
namespace BotLfgAdvert {

using BotLfg::Role;

constexpr char const* kChannel = "LookingForGroup";
constexpr uint32_t kPostIntervalMs = 180000;
constexpr uint32_t kChannelGapMs = 30000;
constexpr uint32_t kInviteHoldMs = 60000;
constexpr uint32_t kReplyGapMs = 10000;
constexpr uint32_t kHintGapMs = 300000;
constexpr uint32_t kTalentLeanMinPoints = 10;
constexpr size_t kMaxInviteWords = 6;
constexpr uint32_t kMaxInvitesPerGroup = 2;

inline std::map<std::string, std::string> const& DungeonNameTable()
{
    static std::map<std::string, std::string> const table = {
        {"ragefire_chasm", "Ragefire Chasm"},
        {"wailing_caverns", "Wailing Caverns"},
        {"deadmines", "Deadmines"},
        {"shadowfang_keep", "Shadowfang Keep"},
        {"stormwind_stockade", "Stockade"},
        {"blackfathom_deeps", "Blackfathom Deeps"},
        {"gnomeregan", "Gnomeregan"},
        {"razorfen_kraul", "Razorfen Kraul"},
        {"crescent_grove", "Crescent Grove"},
        {"scarlet_library", "SM Library"},
        {"scarlet_armory", "SM Armory"},
        {"scarlet_cathedral", "SM Cathedral"},
        {"razorfen_downs", "Razorfen Downs"},
        {"uldaman", "Uldaman"},
        {"gilneas_city", "Gilneas City"},
        {"zulfarrak", "Zul'Farrak"},
        {"maraudon_orange", "Maraudon Orange"},
        {"maraudon_purple", "Maraudon Purple"},
        {"sunken_temple", "Sunken Temple"},
        {"hateforge_quarry", "Hateforge Quarry"},
        {"blackrock_depths", "BRD"},
        {"lower_blackrock_spire", "LBRS"},
        {"dire_maul_east", "DM East"},
        {"dire_maul_north", "DM North"},
        {"dire_maul_west", "DM West"},
        {"scholomance", "Scholomance"},
        {"stratholme_live", "Strat Live"},
        {"stratholme_undead", "Strat UD"},
        {"karazhan_crypt", "Karazhan Crypt"},
        {"black_morass", "Black Morass"},
        {"stormwind_vault", "Stormwind Vault"},
    };
    return table;
}

inline std::string DungeonName(std::string const& plan)
{
    auto const& table = DungeonNameTable();
    auto found = table.find(plan);
    if (found != table.end())
        return found->second;
    std::string out;
    bool upper = true;
    for (char c : plan)
    {
        if (c == '_')
        {
            out.push_back(' ');
            upper = true;
            continue;
        }
        out.push_back(upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c);
        upper = false;
    }
    return out;
}

inline char const* ChatRoleWord(Role role)
{
    switch (role)
    {
        case Role::Tank:
            return "tank";
        case Role::Healer:
            return "healer";
        case Role::Damage:
            return "dps";
        default:
            return "";
    }
}

inline std::string PostText(uint32_t variant, std::string const& dungeon, Role open, uint8_t minLevel,
    uint8_t maxLevel)
{
    std::string band = std::to_string(static_cast<unsigned>(minLevel)) + "-" +
        std::to_string(static_cast<unsigned>(maxLevel));
    std::string role = ChatRoleWord(open);
    switch (variant % 3)
    {
        case 0:
            return "LF1M " + role + " " + dungeon + " " + band + ", w me inv";
        case 1:
            return dungeon + " need " + role + " (" + band + "), /w inv";
        default:
            return "LF " + role + " for " + dungeon + ", lvl " + band + ", whisper inv";
    }
}

inline bool PostDue(uint32_t nowMs, bool groupPosted, uint32_t groupLastMs, bool channelPosted,
    uint32_t channelLastMs)
{
    return (!groupPosted || nowMs - groupLastMs >= kPostIntervalMs) &&
        (!channelPosted || nowMs - channelLastMs >= kChannelGapMs);
}

inline bool IsInviteRequest(std::string const& text)
{
    static char const* const kWords[] = {"inv", "invv", "invi", "invit", "invite", "inviet", "join", "+"};
    std::vector<std::string> words;
    std::string current;
    for (char c : text)
    {
        char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        bool keep = (lower >= 'a' && lower <= 'z') || (lower >= '0' && lower <= '9') || lower == '+';
        if (keep)
            current.push_back(lower);
        else if (!current.empty())
        {
            words.push_back(current);
            current.clear();
        }
    }
    if (!current.empty())
        words.push_back(current);
    if (words.empty() || words.size() > kMaxInviteWords)
        return false;
    for (std::string const& word : words)
        for (char const* key : kWords)
            if (word == key)
                return true;
    return false;
}

// The core's LFTManager::AllowedRoleMask table, by class id.
inline uint8_t ClassRoleMask(uint8_t classId)
{
    switch (classId)
    {
        case 1:
            return 5;
        case 2:
            return 7;
        case 3:
            return 4;
        case 4:
            return 4;
        case 5:
            return 6;
        case 7:
            return 7;
        case 8:
            return 4;
        case 9:
            return 4;
        case 11:
            return 7;
        default:
            return 0;
    }
}

inline uint8_t RoleBit(Role role)
{
    switch (role)
    {
        case Role::Tank:
            return 1;
        case Role::Healer:
            return 2;
        case Role::Damage:
            return 4;
        default:
            return 0;
    }
}

inline bool PlayerFitsRole(uint8_t classId, uint8_t talentRoleMask, uint32_t talentPoints, Role open)
{
    uint8_t bit = RoleBit(open);
    if (bit == 0)
        return false;
    if ((ClassRoleMask(classId) & bit) == 0)
        return false;
    if (open == Role::Damage)
        return true;
    return talentPoints < kTalentLeanMinPoints || (talentRoleMask & bit);
}

inline bool InvitePending(bool invited, bool stillInvitedByGroup, uint32_t nowMs, uint32_t invitedAtMs)
{
    return invited && stillInvitedByGroup && nowMs - invitedAtMs < kInviteHoldMs;
}

enum class Answer : uint8_t
{
    Ignore,
    Invite,
    Hint,
    Full,
    AlreadyInvited,
    Busy,
    Hardcore,
    InGroup,
    WrongLevel,
    WrongRole,
    InviteFailed
};

inline char const* AnswerCode(Answer a)
{
    switch (a)
    {
        case Answer::Ignore:
            return "ignore";
        case Answer::Invite:
            return "invite";
        case Answer::Hint:
            return "hint";
        case Answer::Full:
            return "full";
        case Answer::AlreadyInvited:
            return "already-invited";
        case Answer::Busy:
            return "busy";
        case Answer::Hardcore:
            return "hardcore";
        case Answer::InGroup:
            return "in-group";
        case Answer::WrongLevel:
            return "wrong-level";
        case Answer::WrongRole:
            return "wrong-role";
        case Answer::InviteFailed:
            return "invite-failed";
    }
    return "ignore";
}

struct WhisperFacts
{
    bool sameTeam = false;
    bool repliedRecently = false;
    bool hintedRecently = false;
    bool inviteRequest = false;
    bool waiting = false;
    bool senderInvited = false;
    bool invitePending = false;
    bool senderRefusedResend = false;
    bool hardcore = false;
    bool grouped = false;
    uint8_t level = 0;
    uint8_t minLevel = 0;
    uint8_t maxLevel = 0;
    bool fitsRole = false;
};

inline Answer DecideWhisper(WhisperFacts const& f)
{
    if (!f.sameTeam)
        return Answer::Ignore;
    if (f.repliedRecently)
        return Answer::Ignore;
    if (!f.inviteRequest)
    {
        if (f.waiting && !f.hintedRecently)
            return Answer::Hint;
        return Answer::Ignore;
    }
    if (!f.waiting)
        return Answer::Full;
    if (f.senderInvited && f.invitePending)
        return Answer::AlreadyInvited;
    if (f.senderRefusedResend)
        return Answer::Busy;
    if (f.invitePending)
        return Answer::Busy;
    if (f.hardcore)
        return Answer::Hardcore;
    if (f.grouped)
        return Answer::InGroup;
    if (!BotLfg::InBand(f.level, f.minLevel, f.maxLevel))
        return Answer::WrongLevel;
    if (!f.fitsRole)
        return Answer::WrongRole;
    return Answer::Invite;
}

inline std::string ReplyText(Answer a, std::string const& dungeon, Role open, uint8_t minLevel, uint8_t maxLevel)
{
    std::string band = std::to_string(static_cast<unsigned>(minLevel)) + "-" +
        std::to_string(static_cast<unsigned>(maxLevel));
    switch (a)
    {
        case Answer::Ignore:
            return "";
        case Answer::Invite:
            return "inv sent";
        case Answer::Hint:
            return "need " + std::string(ChatRoleWord(open)) + " for " + dungeon + " (" + band +
                "), w me inv to join";
        case Answer::Full:
            return "sorry, we're full now";
        case Answer::AlreadyInvited:
            return "already sent you an inv";
        case Answer::Busy:
            return "someone's joining atm, w me again in a min";
        case Answer::Hardcore:
            return "sorry, can't group with hardcore chars";
        case Answer::InGroup:
            return "leave your group and w me inv again";
        case Answer::WrongLevel:
            return "sorry, looking for " + band;
        case Answer::WrongRole:
            return "sorry, need a " + std::string(ChatRoleWord(open));
        case Answer::InviteFailed:
            return "couldn't inv you, got another invite open?";
    }
    return "";
}

} // namespace BotLfgAdvert
} // namespace TortoiseBots

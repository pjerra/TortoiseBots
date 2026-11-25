// The decisions of a hire without the core: the gate order, the fee and refund choice, the mustering login wait,
// and the `.bot hire` token grammar. The live half (TbHireProvision.cpp) gathers the facts, maps a HireVerdict
// onto hire::Status and does the work. Unit-tested by forever/tools/test_hire_provision_pure.cpp.
#pragma once

#include "runtime/HireCost.h"
#include "runtime/HireFeePolicy.h"
#include "runtime/HireLevelPolicy.h"
#include "runtime/HireSpecPolicy.h"
#include "runtime/HireTierRequestPolicy.h"
#include "slice/SliceSpecPolicy.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace TortoiseBots
{
namespace hire
{

// The contract's Status without TbHire.h (which includes the core's Define.h). Ok plus one value per
// refusal; the live half switches this onto hire::Status.
enum class HireVerdict
{
    Ok,
    Failed,
    Disabled,
    NoPermission,
    RecruiterRequired,
    GroupFull,
    CapReached,
    Poor,
    TierLocked
};

struct HireGateFacts
{
    bool realPlayer = true;          // in the world with a session
    bool enabled = true;             // AiPlayerbot.HireEnabled
    bool accountAllowed = true;      // security >= HireMinAccountSecurity
    bool pvpAllowedNow = true;       // MayHireForPvpState(...)
    bool hasRecruiter = false;       // the hire came from a <Mercenary Hire> gossip
    bool hireAnywhere = false;       // AiPlayerbot.HireAnywhere
    bool tiersEnabled = true;        // AiPlayerbot.HireGearTiersEnabled
    uint8_t requestedTier = 0;
    uint8_t highestUnlockedTier = 0;
    uint32_t hireLevel = 0;          // the resolved level (HireLevelPolicy.h)
    uint32_t activeHires = 0;        // this master's live plus mustering hires
    uint32_t maxHires = 4;           // AiPlayerbot.HireMaxBotsPerPlayer
    uint32_t groupMembers = 0;       // the hirer's party, the hirer included
    bool groupIsRaid = false;
    bool canAfford = true;
};

struct HireGateResult
{
    HireVerdict verdict = HireVerdict::Ok;
    std::string message;             // player-facing, plain text
};

// The order the refusals are checked in. Cheapest and most fundamental first: an in-world player, the
// server switch, the account, the PvP rule, the recruiter, the per-player cap, room in the party, the
// tier unlock, the money. A tier is only meaningful at level 60 with tiers on; otherwise it is ignored
// (the hire keeps the levelled kit), as in TortoiseBots.
inline HireGateResult DecideHireGates(HireGateFacts const& facts)
{
    HireGateResult out;
    if (!facts.realPlayer)
    {
        out.verdict = HireVerdict::Failed;
        out.message = "You must be in-game to hire a companion.";
        return out;
    }
    if (!facts.enabled)
    {
        out.verdict = HireVerdict::Disabled;
        out.message = "Companion hiring is disabled on this server.";
        return out;
    }
    if (!facts.accountAllowed)
    {
        out.verdict = HireVerdict::NoPermission;
        out.message = "Your account may not hire companions.";
        return out;
    }
    if (!facts.pvpAllowedNow)
    {
        out.verdict = HireVerdict::Failed;
        out.message = "Hired companions are PvE-only and cannot join you in a battleground. Leave the queue to hire.";
        return out;
    }
    if (!facts.hasRecruiter && !facts.hireAnywhere)
    {
        out.verdict = HireVerdict::RecruiterRequired;
        out.message = "Companions are hired at a <Mercenary Hire> recruiter. Look for one in an inn.";
        return out;
    }
    if (facts.activeHires >= facts.maxHires)
    {
        out.verdict = HireVerdict::CapReached;
        out.message = "You already command the maximum number of companions.";
        return out;
    }
    if (facts.groupIsRaid)
    {
        out.verdict = HireVerdict::GroupFull;
        out.message = "You are in a raid: raid hiring is not available yet.";
        return out;
    }
    // The hire is one more member of the hirer's party; a party holds five.
    if (facts.groupMembers >= 5)
    {
        out.verdict = HireVerdict::GroupFull;
        out.message = "Your party is full. Leave room for the companion.";
        return out;
    }
    if (facts.tiersEnabled && facts.hireLevel == kHireGearHireLevel && facts.requestedTier > 0 &&
        facts.requestedTier > facts.highestUnlockedTier)
    {
        out.verdict = HireVerdict::TierLocked;
        out.message = "That equipment is not unlocked yet. Your highest available tier is " +
            HireTierKitName(facts.highestUnlockedTier) + " (tier " +
            std::to_string(facts.highestUnlockedTier) + ").";
        return out;
    }
    if (!facts.canAfford)
    {
        out.verdict = HireVerdict::Poor;
        out.message = "You cannot afford that companion.";
        return out;
    }
    out.verdict = HireVerdict::Ok;
    return out;
}

// The tier that is actually provisioned: a request only counts at level 60 with tiers on, otherwise the
// hire keeps the levelled kit (tier 0).
inline uint8_t EffectiveGearTier(bool tiersEnabled, uint32_t hireLevel, uint8_t requestedTier)
{
    if (!tiersEnabled || hireLevel != kHireGearHireLevel)
        return 0;
    return requestedTier > kHireGearTierMax ? kHireGearTierMax : requestedTier;
}

struct HireFeeConfig
{
    HireCost::CostConfig cost;
    float tierMult[kHireGearTierMax + 1];

    HireFeeConfig()
    {
        for (uint8_t i = 0; i <= kHireGearTierMax; ++i)
            tierMult[i] = kHireTierCostMultDefault[i];
    }
};

// The one formula: HireCost.h's party escalation and level scaling, then the tier multiplier.
inline uint32_t QuoteHireFee(HireFeeConfig const& config, uint32_t activeHires, uint32_t level, uint8_t gearTier)
{
    return HireCostAtTier(HireCost::ForNextHire(config.cost, activeHires, level), gearTier, config.tierMult);
}

// AiPlayerbot.HireFreeMinSecurity (0 = off): the amount actually taken.
inline uint32_t ChargeableHireFee(uint32_t quoted, int accountSecurity, uint32_t freeMinSecurity)
{
    return HireFeeCharged(quoted, accountSecurity, freeMinSecurity);
}

// A hire that fails after its character was made is always undone; only the road back differs.
// RefundAndDismiss: the ledger row exists, so the lifecycle's guarded deletion removes the character.
// RefundAndDeleteUnclaimed: no ledger row yet, so this is the module's own just-created character (the
// account and guid it made) and the caller deletes it directly; the refund is zero when nothing was taken.
enum class HireRollback
{
    RefundAndDeleteUnclaimed,
    RefundAndDismiss
};

inline HireRollback DecideHireRollback(bool ledgerClaimed)
{
    return ledgerClaimed ? HireRollback::RefundAndDismiss : HireRollback::RefundAndDeleteUnclaimed;
}

// The login step of a claimed hire. BotMgr::FindBot only answers for a bot already in the world, so a null player
// is not "the login did not start": a pool login can spend several ticks loading. The live session (BotMgr::IsBot)
// is what tells a login still on its way from one that never started or already ended.
enum class LoginWait
{
    InWorld,    // the player object exists and is in the world: provision it
    Loading,    // the session exists, the player is not in the world yet: wait
    Failed,     // no session: the login did not start, or it ended without entering the world
    TimedOut    // still loading past the bound: undo the hire
};

inline LoginWait DecideLoginWait(bool playerInWorld, bool sessionExists, bool timedOut)
{
    if (playerInWorld)
        return LoginWait::InWorld;
    if (!sessionExists)
        return LoginWait::Failed;
    return timedOut ? LoginWait::TimedOut : LoginWait::Loading;
}

// ---- `.bot hire <class> [race] [gender] [role|spec] [tier] [level=<n>]` ----

struct HireCommandParse
{
    bool ok = false;
    uint8_t classId = 0;
    uint8_t race = 0;
    uint8_t gender = 0;
    bool genderGiven = false;   // gender 0 is "male", so absence needs its own bit
    uint8_t role = 0;
    uint8_t gearTier = 0;
    uint32_t requestedLevel = 0;
    int specIndex = -1;      // a row of HireSpecOptions (the recruiter menu's order)
    int specTab = -1;        // the slice's talent tab for the same build
    std::string errorCode;   // usage | bad-level | bad-druid | bad-shaman
    std::string message;
};

namespace detail
{

inline std::string Lower(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (char c : text)
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

inline std::string_view Trim(std::string_view text)
{
    std::size_t first = 0;
    while (first < text.size() && std::isspace(static_cast<unsigned char>(text[first])))
        ++first;
    std::size_t last = text.size();
    while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1])))
        --last;
    return text.substr(first, last - first);
}

inline std::vector<std::string> Words(std::string_view text)
{
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < text.size())
    {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
            ++i;
        std::size_t const begin = i;
        while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i])))
            ++i;
        if (i > begin)
            out.push_back(Lower(text.substr(begin, i - begin)));
    }
    return out;
}

inline bool ParseNumber(std::string const& word, uint32_t& out)
{
    if (word.empty() || word.size() > 2)
        return false;
    uint32_t value = 0;
    for (char c : word)
    {
        if (c < '0' || c > '9')
            return false;
        value = value * 10 + static_cast<uint32_t>(c - '0');
    }
    out = value;
    return true;
}

} // namespace detail

// Core class ids (SharedDefines.h) happen to equal the slice's ClassId enum; HireSpecPolicy repeats them.
inline uint8_t ParseClassWord(std::string const& word)
{
    static char const* const names[] = { "", "warrior", "paladin", "hunter", "rogue", "priest", "", "shaman",
        "mage", "warlock", "", "druid" };
    for (uint8_t id = 1; id <= 11; ++id)
        if (*names[id] && word == names[id])
            return id;
    uint32_t number = 0;
    if (detail::ParseNumber(word, number) && number >= 1 && number <= 11 && *names[number])
        return static_cast<uint8_t>(number);
    return 0;
}

inline uint8_t ParseRaceWord(std::string const& word)
{
    struct Name { char const* text; uint8_t race; };
    static Name const names[] = {
        { "human", 1 }, { "orc", 2 }, { "dwarf", 3 }, { "nightelf", 4 }, { "night", 4 },
        { "undead", 5 }, { "scourge", 5 }, { "tauren", 6 }, { "gnome", 7 }, { "troll", 8 },
        { "goblin", 9 }, { "bloodelf", 10 }, { "draenei", 11 } };
    for (Name const& name : names)
        if (word == name.text)
            return name.race;
    uint32_t number = 0;
    if (detail::ParseNumber(word, number) && number >= 1 && number <= 11)
        return static_cast<uint8_t>(number);
    return 0;
}

inline uint8_t ParseGenderWord(std::string const& word)
{
    if (word == "male")
        return 0;
    if (word == "female")
        return 1;
    return 0;
}

inline uint8_t ParseRoleWord(std::string const& word)
{
    if (word == "tank")
        return kHireRoleTank;
    if (word == "healer" || word == "heal")
        return kHireRoleHealer;
    if (word == "dps")
        return kHireRoleDps;
    return 0;
}

inline uint8_t DefaultRoleForClass(uint8_t classId)
{
    switch (classId)
    {
        case kHireClassPriest:
        case kHireClassShaman:
        case kHireClassDruid:
        case kHireClassPaladin:
            return kHireRoleHealer;
        default:
            return kHireRoleDps;
    }
}

inline bool ClassCanRole(uint8_t classId, uint8_t role)
{
    if (role == kHireRoleTank)
        return classId == kHireClassWarrior || classId == kHireClassPaladin ||
            classId == kHireClassDruid || classId == kHireClassShaman;
    if (role == kHireRoleHealer)
        return classId == kHireClassPriest || classId == kHireClassPaladin ||
            classId == kHireClassShaman || classId == kHireClassDruid;
    if (role == kHireRoleDps)
        return classId != 0;
    return false;
}

// The recruiter menu's order (HireSpecOptions) for a talent tab and role. Druid bear and cat share the
// feral tab and split on the role; the shaman tank row is tab 1 with the tank role.
inline int HireSpecIndexForTab(uint8_t classId, int tab, uint8_t role)
{
    if (tab < 0 || tab > 2)
        return -1;
    switch (classId)
    {
        case kHireClassWarrior: return tab == 2 ? 0 : (tab == 0 ? 1 : 2);
        case kHireClassPaladin: return tab == 0 ? 1 : (tab == 1 ? 0 : 2);
        case kHireClassHunter:
        case kHireClassRogue:
        case kHireClassWarlock: return tab;
        case kHireClassPriest: return tab == 0 ? 1 : (tab == 1 ? 0 : 2);
        case kHireClassShaman: return tab == 0 ? 1 : (tab == 1 ? (role == kHireRoleTank ? 3 : 2) : 0);
        case kHireClassMage: return tab == 0 ? 2 : (tab == 1 ? 1 : 0);
        case kHireClassDruid:
            if (tab == 0)
                return 2;
            if (tab == 1)
                return role == kHireRoleTank ? 0 : 3;
            return 1;
        default: return -1;
    }
}

// The inverse: the tab a recruiter menu row asks the talent sync for.
inline int SpecTabForIndex(uint8_t classId, int specIndex)
{
    uint32_t count = 0;
    HireSpecOption const* options = HireSpecOptions(classId, count);
    if (!options || specIndex < 0 || static_cast<uint32_t>(specIndex) >= count)
        return -1;
    switch (classId)
    {
        case kHireClassWarrior: return specIndex == 0 ? 2 : (specIndex == 1 ? 0 : 1);
        case kHireClassPaladin: return specIndex == 0 ? 1 : (specIndex == 1 ? 0 : 2);
        case kHireClassHunter:
        case kHireClassRogue:
        case kHireClassWarlock: return specIndex;
        case kHireClassPriest: return specIndex == 0 ? 1 : (specIndex == 1 ? 0 : 2);
        case kHireClassShaman: return specIndex == 0 ? 2 : (specIndex == 1 ? 0 : 1);
        case kHireClassMage: return specIndex == 0 ? 2 : (specIndex == 1 ? 1 : 0);
        case kHireClassDruid: return specIndex == 0 ? 1 : (specIndex == 1 ? 2 : (specIndex == 2 ? 0 : 1));
        default: return -1;
    }
}

// The role a talent tab fights in; `word` decides the druid's feral split (bear/feral tank, cat dps).
inline uint8_t RoleForSpecTab(uint8_t classId, int tab, std::string const& word)
{
    switch (classId)
    {
        case kHireClassWarrior: return tab == 2 ? kHireRoleTank : kHireRoleDps;
        case kHireClassPaladin: return tab == 1 ? kHireRoleTank : (tab == 0 ? kHireRoleHealer : kHireRoleDps);
        case kHireClassPriest: return tab == 2 ? kHireRoleDps : kHireRoleHealer;
        case kHireClassShaman: return tab == 2 ? kHireRoleHealer : kHireRoleDps;
        case kHireClassDruid:
            if (tab == 2)
                return kHireRoleHealer;
            if (tab == 0)
                return kHireRoleDps;
            return word.find("cat") != std::string::npos ? kHireRoleDps : kHireRoleTank;
        default:
            return kHireRoleDps;
    }
}

// The spec word the gear builder's CanonicalSpecName accepts for a tab (its weight scales are named
// beast/marks/surv, prot/retrib, feraldps/feraltank...). Empty = let the bot's own active spec decide.
inline std::string GearSpecWord(uint8_t classId, int tab, uint8_t role)
{
    if (tab < 0 || tab > 2)
        return std::string();
    switch (classId)
    {
        case kHireClassWarrior: return tab == 0 ? "arms" : (tab == 1 ? "fury" : "prot");
        case kHireClassPaladin: return tab == 0 ? "holy" : (tab == 1 ? "prot" : "retrib");
        case kHireClassHunter: return tab == 0 ? "beast" : (tab == 1 ? "marks" : "surv");
        case kHireClassRogue: return tab == 0 ? "assas" : (tab == 1 ? "combat" : "subtle");
        case kHireClassPriest: return tab == 0 ? "disc" : (tab == 1 ? "holy" : "shadow");
        case kHireClassShaman: return tab == 0 ? "elem" : (tab == 1 ? "enhance" : "resto");
        case kHireClassMage: return tab == 0 ? "arcane" : (tab == 1 ? "fire" : "frost");
        case kHireClassWarlock: return tab == 0 ? "afflic" : (tab == 1 ? "demo" : "destro");
        case kHireClassDruid:
            return tab == 0 ? "balance" : (tab == 1 ? (role == kHireRoleTank ? "bear" : "cat") : "resto");
        default: return std::string();
    }
}

// One token of `.bot hire`: class, race and gender in any order, one role word, one spec word, one tier
// word, one level. A bad or misplaced number refuses with the reason; an unknown word is ignored, as in
// TortoiseBots, so the command stays forgiving about extra words.
inline HireCommandParse ParseHireCommand(std::string_view args, uint32_t hirerLevel, bool shamanTankEnabled)
{
    HireCommandParse out;
    std::vector<std::string> const tokens = detail::Words(args);
    if (tokens.empty())
    {
        out.errorCode = "usage";
        out.message = "Usage: .bot hire <class> [race] [male/female] [tank/healer/dps|spec] [tier] [level=<1-60>]";
        return out;
    }

    std::vector<std::string> specWords;
    bool roleWordGiven = false;
    for (std::size_t index = 0; index < tokens.size(); ++index)
    {
        std::string const& token = tokens[index];
        uint32_t levelWord = 0;
        HireLevelWord levelKind = ParseHireLevelWord(token, levelWord);
        if (levelKind == HireLevelWord::Bad)
        {
            out.errorCode = "bad-level";
            out.message = "Bad level \"" + token + "\": use level=<1-60> or level <1-60>, for example .bot hire mage level=42.";
            return out;
        }
        if (levelKind == HireLevelWord::Ok)
        {
            if (out.requestedLevel)
            {
                out.errorCode = "usage";
                out.message = "Give only one level.";
                return out;
            }
            out.requestedLevel = levelWord;
            continue;
        }
        if (levelKind == HireLevelWord::Prefix)
        {
            if (index + 1 >= tokens.size() || !ParseHireLevelNumber(tokens[index + 1], levelWord))
            {
                out.errorCode = "bad-level";
                out.message = "Bad level: use level=<1-60> or level <1-60>, for example .bot hire mage level=42.";
                return out;
            }
            if (out.requestedLevel)
            {
                out.errorCode = "usage";
                out.message = "Give only one level.";
                return out;
            }
            out.requestedLevel = levelWord;
            ++index;
            continue;
        }
        if (ParseBareHireLevel(token, levelWord))
        {
            if (index + 1 != tokens.size() || index == 0 || !out.classId)
            {
                out.errorCode = "usage";
                out.message = "Unclear number \"" + token + "\": put the level last, or write level=<1-60>, for example .bot hire mage level=42.";
                return out;
            }
            if (out.requestedLevel)
            {
                out.errorCode = "usage";
                out.message = "Give only one level.";
                return out;
            }
            out.requestedLevel = levelWord;
            continue;
        }
        if (!out.classId)
        {
            if (uint8_t const parsed = ParseClassWord(token))
            {
                out.classId = parsed;
                continue;
            }
        }
        if (!out.race)
        {
            if (uint8_t const parsed = ParseRaceWord(token))
            {
                out.race = parsed;
                continue;
            }
        }
        if (!out.genderGiven)
        {
            if (uint8_t const parsed = ParseGenderWord(token))
            {
                out.gender = parsed;
                out.genderGiven = true;
                continue;
            }
        }
        uint8_t tierWord = 0;
        if (ParseHireTierWord(token, tierWord))
        {
            out.gearTier = tierWord;
            continue;
        }
        if (!roleWordGiven)
        {
            if (uint8_t const parsed = ParseRoleWord(token))
            {
                out.role = parsed;
                roleWordGiven = true;
                continue;
            }
        }
        specWords.push_back(token);
    }

    if (!out.classId)
    {
        out.errorCode = "usage";
        out.message = "Usage: .bot hire <class> [race] [male/female] [tank/healer/dps|spec] [tier] [level=<1-60>]";
        return out;
    }

    for (std::string const& word : specWords)
    {
        int tab = -1;
        if (!slice::spec::ParseSpecArg(out.classId, word, tab) || tab < 0)
            continue;
        out.specTab = tab;
        if (!out.role)
            out.role = RoleForSpecTab(out.classId, tab, word);
        break;
    }
    if (!out.role)
        out.role = DefaultRoleForClass(out.classId);
    if (!ClassCanRole(out.classId, out.role))
    {
        out.errorCode = "bad-choice";
        out.message = "That class cannot fill that role.";
        return out;
    }
    if (out.classId == kHireClassShaman && out.role == kHireRoleTank)
    {
        if (!shamanTankEnabled)
        {
            out.errorCode = "bad-shaman";
            out.message = "The experimental shaman tank is turned off on this server.";
            return out;
        }
        if (hirerLevel < 10)
        {
            out.errorCode = "bad-shaman";
            out.message = "Enhancement (Tank) needs level 10 (Earthshaker Slam taunt). Level up and hire again.";
            return out;
        }
        out.specTab = 1;
        out.specIndex = 3;
    }
    if (out.specIndex < 0 && out.specTab >= 0)
        out.specIndex = HireSpecIndexForTab(out.classId, out.specTab, out.role);
    // TortoiseBots' role-only shapes: a druid DPS is the cat (feral) row, a druid tank the bear row.
    if (out.classId == kHireClassDruid && out.specIndex < 0)
    {
        if (out.role == kHireRoleDps)
        {
            out.specIndex = 3;
            out.specTab = 1;
        }
        else if (out.role == kHireRoleTank)
        {
            out.specIndex = 0;
            out.specTab = 1;
        }
    }
    out.ok = true;
    return out;
}

} // namespace hire
} // namespace TortoiseBots

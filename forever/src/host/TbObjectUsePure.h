// The decisions of issue #14 (multi-user objects, `use <object>`, `ritual <name>`, `soulstone [name]`), with no core type and no
// engine header, so forever/tools/test_object_use_policy.cpp runs them standalone.
//
// The fill answers one question: how many more users a ritual object needs, and which of the initiator's bots walk to it. The
// counts come from two worlds at once, because the same object type behaves differently: a plain type-18 ritual counts its users
// in the core (`GameObject::GetUniqueUseCount`, GameObject.cpp:3184-3198), while a scripted altar (UBRS Blackrock Altar, 175706)
// never reaches that counter (its GO AI returns true in OnGossipHello, GameObject.cpp:2709) and remembers its users itself, within
// 8 yd (classic_instance_blackrock_spire.cpp:1543-1590). The fill therefore believes the larger of the two counts.
//
// `ParseObjectWord`, `NameMatches`, `JudgeRitual` and `JudgeSoulstone` are the chat rules of round 2; they live here with the fill
// because one header and one test cover the whole feature.
#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace TortoiseBots
{
namespace objuse
{
// A scripted altar keeps a user only inside this distance (classic_instance_blackrock_spire.cpp: ALTAR_RANGE 8).
inline constexpr float kScriptedUserYd = 8.0f;
// TortoiseBots.RitualFill.Range's default.
inline constexpr float kDefaultRangeYd = 30.0f;
// Soulstone use range (the item spell's own range) and the ritual's `already-here` distance (the issue's "within 40 yd").
inline constexpr float kSoulstoneYd = 30.0f;
inline constexpr float kRitualAlreadyHereYd = 40.0f;
// How long a target with no bot held keeps its per-object `used` record (the HoldSeconds ceiling): a bot released by timeout is
// not picked again for the same object while it still fills, and a permanent altar does not remember bots forever.
inline constexpr std::uint64_t kTargetMemoryMs = 600000ull;

// -------------------------------------------------------------------------------------------------------------------------------
// The click queue
// -------------------------------------------------------------------------------------------------------------------------------
// Recorded only while the fill is on; switching it off drops what is queued, so switching it back on cannot replay stale uses.
template <typename Click>
struct ClickQueue
{
    bool enabled = true;
    std::vector<Click> items;

    void SetEnabled(bool on)
    {
        enabled = on;
        if (!on)
            items.clear();
    }

    bool Record(Click click)
    {
        if (!enabled)
            return false;
        items.push_back(std::move(click));
        return true;
    }

    std::vector<Click> Drain()
    {
        std::vector<Click> taken;
        taken.swap(items);
        return taken;
    }
};

// A target nobody is held on is remembered (its `used` set included) while the object still fills: present, ready and seen
// within the memory window. Otherwise it is dropped, so a new ritual on the same object starts a new record.
inline bool RemembersTarget(bool objectPresent, bool objectReady, std::uint64_t sinceLastSeenMs, std::uint64_t memoryMs)
{
    return objectPresent && objectReady && sinceLastSeenMs <= memoryMs;
}

// -------------------------------------------------------------------------------------------------------------------------------
// Counting the users of one object
// -------------------------------------------------------------------------------------------------------------------------------
struct FillFacts
{
    bool ownedRitual = false;        // the object was summoned by a spell: its owner must channel it (GameObject.cpp:3169-3170)
    bool ownerChanneling = false;    // the owner's channel runs and owns this object
    std::uint32_t casters = 0;       // the template's ritual.casters (GameObjectData.h:379-385)
    std::uint32_t uniqueUses = 0;    // GameObject::GetUniqueUseCount (GameObject.h:345)
    std::uint32_t recordedUsers = 0; // real players who clicked it and are still within kScriptedUserYd
    std::uint32_t heldBotsUsed = 0;  // held bots whose use has gone in
    std::uint32_t walking = 0;       // held bots still walking or standing and not yet used
};

// The users the object has. The core's counter wins where it counts (a plain ritual); the recorded clicks win on a scripted altar,
// where the counter never moves, and there they also cover the bots this fill already sent.
inline std::uint32_t Users(FillFacts const& facts)
{
    return std::max(facts.uniqueUses, facts.recordedUsers + facts.heldBotsUsed);
}

// How many more users the object needs, the bots already walking not counted twice. An owned ritual whose owner stopped channeling
// is not filled at all: every use would be refused until the channel is back (GameObject.cpp:3169-3170), and the fill is not the
// thing that restarts it.
inline std::uint32_t MissingUsers(FillFacts const& facts)
{
    if (facts.casters == 0)
        return 0;
    if (facts.ownedRitual && !facts.ownerChanneling)
        return 0;
    std::uint32_t const taken = Users(facts) + facts.walking;
    return taken >= facts.casters ? 0 : facts.casters - taken;
}

// -------------------------------------------------------------------------------------------------------------------------------
// Picking the bots that walk
// -------------------------------------------------------------------------------------------------------------------------------
struct Candidate
{
    std::uint64_t guid = 0;          // the bot's guid key, the tie-breaker after distance
    bool alive = false;
    bool inCombat = false;
    bool held = false;               // this fill, a commander order, or any other hold
    bool inRun = false;              // a dungeon run drives the bot (run::BotInRunningRun)
    bool inBattleground = false;     // a battleground or arena (Map::PlayerCannotEnter refuses both)
    bool authorized = false;         // chatin::StillAuthorized(bot, initiator)
    bool used = false;               // already used this object: once per GO per bot
    bool isTank = false;
    bool isHealer = false;
    float distance = 0.0f;           // to the object, yards
};

// Alive, out of combat, free to be sent, allowed to answer this initiator, within the fill's range.
inline bool CandidateUsable(Candidate const& bot, float range)
{
    return bot.alive && !bot.inCombat && !bot.held && !bot.inRun && !bot.inBattleground &&
        bot.authorized && !bot.used && bot.distance <= range;
}

// damage first, then healers, tanks last (the issue's rule: the pull's tank and healer keep their places); among equals the
// nearest first, then the lower guid.
inline int RoleRank(Candidate const& bot)
{
    if (bot.isTank)
        return 2;
    if (bot.isHealer)
        return 1;
    return 0;
}

// The `missing` bots to send, in the order they are held. Fewer than `missing` when not enough candidates qualify.
inline std::vector<std::uint64_t> PickFillers(std::vector<Candidate> const& bots, std::uint32_t missing, float range)
{
    std::vector<Candidate const*> usable;
    usable.reserve(bots.size());
    for (Candidate const& bot : bots)
        if (CandidateUsable(bot, range))
            usable.push_back(&bot);
    std::stable_sort(usable.begin(), usable.end(), [](Candidate const* a, Candidate const* b)
    {
        if (RoleRank(*a) != RoleRank(*b))
            return RoleRank(*a) < RoleRank(*b);
        if (a->distance != b->distance)
            return a->distance < b->distance;
        return a->guid < b->guid;
    });
    std::vector<std::uint64_t> picked;
    picked.reserve(std::min<std::size_t>(usable.size(), missing));
    for (std::size_t i = 0; i < usable.size() && picked.size() < missing; ++i)
        picked.push_back(usable[i]->guid);
    return picked;
}

// -------------------------------------------------------------------------------------------------------------------------------
// Holding a bot at the object
// -------------------------------------------------------------------------------------------------------------------------------
struct HoldFacts
{
    bool named = false;              // held by `use <object>` (round 2): released only on an order, combat, death or a map change
    bool objectPresent = true;       // still in the world and spawned
    bool objectReady = true;         // GO_STATE_READY: a finished non-persistent ritual is not ready any more
    bool ownedRitual = false;
    bool ownerChanneling = true;
    bool ritualComplete = false;     // GetUniqueUseCount reached casters since this bot used it
    bool ritualReset = false;        // ... and the count fell back afterwards (the object reset for another round)
    bool inCombat = false;
    bool dead = false;
    bool offMap = false;             // another map than the object's (or teleporting)
    bool holdElapsed = false;        // TortoiseBots.RitualFill.HoldSeconds since the assignment
    bool releasedByCommand = false;  // follow/stay/attack from an authorized sender (named holds only)
};

struct HoldVerdict
{
    bool release = false;
    char const* code = "held";
};

inline HoldVerdict JudgeHold(HoldFacts const& facts)
{
    if (facts.named)
    {
        if (facts.releasedByCommand)
            return { true, "ordered" };
        if (facts.inCombat)
            return { true, "combat" };
        if (facts.dead)
            return { true, "dead" };
        if (facts.offMap)
            return { true, "off-map" };
        return { false, "held" };
    }
    if (!facts.objectPresent)
        return { true, "object-gone" };
    if (!facts.objectReady)
        return { true, "object-not-ready" };
    if (facts.ownedRitual && !facts.ownerChanneling)
        return { true, "channel-ended" };
    if (facts.ritualComplete)
        return { true, "ritual-complete" };
    if (facts.ritualReset)
        return { true, "ritual-reset" };
    if (facts.dead)
        return { true, "dead" };
    if (facts.offMap)
        return { true, "off-map" };
    if (facts.inCombat)
        return { true, "combat" };
    if (facts.holdElapsed)
        return { true, "timeout" };
    return { false, "held" };
}

// -------------------------------------------------------------------------------------------------------------------------------
// The chat words
// -------------------------------------------------------------------------------------------------------------------------------
inline std::string_view Trim(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);
    return text;
}

inline bool IsDigits(std::string_view word)
{
    if (word.empty())
        return false;
    for (char c : word)
        if (c < '0' || c > '9')
            return false;
    return true;
}

inline char LowerAscii(char c)
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

inline bool EqualsNoCase(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (LowerAscii(a[i]) != LowerAscii(b[i]))
            return false;
    return true;
}

// The object word of a chat line, before the inbox sees it:
//   whisper  `use <name|entry>`                        claimed by the receiving bot
//   party    `<BotName> use <name|entry>` / `all use <name|entry>`   claimed by that bot / every bot
//   party    bare `use <name|entry>`                   NOT claimed: that is the engine's item use (and always was)
// `botName` is the receiving bot's own name.
struct UseWord
{
    bool claimed = false;
    bool party = false;      // a party/raid line
    bool everyone = false;   // `all use ...`
    std::string what;        // the words after `use`
};

inline UseWord ParseObjectWord(bool party, std::string_view text, std::string_view botName)
{
    UseWord out;
    out.party = party;
    std::string_view rest = Trim(text);
    if (rest.empty())
        return out;
    std::string_view const first = rest.substr(0, rest.find(' '));
    std::string_view afterFirst = Trim(rest.substr(first.size()));

    auto takeUse = [&out](std::string_view afterVerb) -> bool
    {
        std::string_view const verb = afterVerb.substr(0, afterVerb.find(' '));
        if (!EqualsNoCase(verb, "use"))
            return false;
        std::string_view const what = Trim(afterVerb.substr(verb.size()));
        if (what.empty())
            return false;
        out.claimed = true;
        out.what = std::string(what);
        return true;
    };

    if (EqualsNoCase(first, "use"))
    {
        if (party)                    // bare party `use x` belongs to the engine
            return out;
        takeUse(rest);
        return out;
    }
    if (!party)                       // a whisper's other verbs are not ours
        return out;
    if (EqualsNoCase(first, "all"))
    {
        if (takeUse(afterFirst))
            out.everyone = true;
        return out;
    }
    if (EqualsNoCase(first, botName))
        takeUse(afterFirst);
    return out;
}

// `word` names this object when it is all digits and equals the entry, or when it is a case-insensitive piece of the template name.
inline bool NameMatches(std::string_view name, std::uint32_t entry, std::string_view word)
{
    word = Trim(word);
    if (word.empty())
        return false;
    if (IsDigits(word))
    {
        std::uint64_t value = 0;
        for (char c : word)
        {
            value = value * 10 + std::uint64_t(c - '0');
            if (value > 0xFFFFFFFFull)
                return false;
        }
        return std::uint32_t(value) == entry;
    }
    std::string haystack(name);
    std::string needle(word);
    for (char& c : haystack)
        c = LowerAscii(c);
    for (char& c : needle)
        c = LowerAscii(c);
    return haystack.find(needle) != std::string::npos;
}

// -------------------------------------------------------------------------------------------------------------------------------
// `ritual <name>` (a warlock bot summons a group member)
// -------------------------------------------------------------------------------------------------------------------------------
struct RitualFacts
{
    bool authorized = false;     // the bot's master or its commander binding (chatin::StillAuthorized)
    bool warlock = false;
    bool known = false;          // the engine value `spell id` names "ritual of summoning"
    bool inCombat = false;
    bool battleground = false;
    bool targetInGroup = false;
    bool targetIsSelf = false;   // the bot itself, or the sender naming the bot
    bool sameMapWithin40 = false;
    bool canEnter = true;        // Map::PlayerCannotEnter(bot's map, target) is TRANSFER_ABORT_NONE
    std::uint32_t casters = 0;   // the ritual template's casters
};

struct RitualVerdict
{
    bool ok = false;
    char const* code = "ok";
    std::uint32_t more = 0;      // casters - 1: the clickers the cast still needs
};

// The refusals in the order the issue lists them; `not-yours` stands before them, because an unauthorized sender is told nothing
// about the bot's class or spells.
inline RitualVerdict JudgeRitual(RitualFacts const& facts)
{
    if (!facts.authorized)
        return { false, "not-yours", 0 };
    if (!facts.warlock)
        return { false, "not-warlock", 0 };
    if (!facts.known)
        return { false, "not-known", 0 };
    if (facts.inCombat)
        return { false, "in-combat", 0 };
    if (facts.battleground)
        return { false, "battleground", 0 };
    if (!facts.targetInGroup || facts.targetIsSelf)
        return { false, "no-target", 0 };
    if (facts.sameMapWithin40)
        return { false, "already-here", 0 };
    if (!facts.canEnter)
        return { false, "cannot-enter", 0 };
    return { true, "ok", facts.casters > 1 ? facts.casters - 1 : 0 };
}

// -------------------------------------------------------------------------------------------------------------------------------
// `soulstone [name]` (a warlock bot stones one group member)
// -------------------------------------------------------------------------------------------------------------------------------
struct SoulstoneFacts
{
    bool authorized = false;
    bool warlock = false;
    bool hasStone = false;       // PickSoulstoneItem found a rank in the bags (runtime/policy/class/WarlockSoulstonePolicy.h:76)
    bool inCombat = false;
    bool targetExists = false;   // the sender's target, else the sender
    bool targetAlive = false;
    bool targetInGroup = false;
    bool inRange = false;        // kSoulstoneYd
    bool spellReady = true;      // the stone's use spell is off cooldown
};

struct SoulstoneVerdict
{
    bool ok = false;
    char const* code = "ok";
};

inline SoulstoneVerdict JudgeSoulstone(SoulstoneFacts const& facts)
{
    if (!facts.authorized)
        return { false, "not-yours" };
    if (!facts.warlock)
        return { false, "not-warlock" };
    if (!facts.hasStone)
        return { false, "no-stone" };
    if (facts.inCombat)
        return { false, "in-combat" };
    if (!facts.targetExists)
        return { false, "no-target" };
    if (!facts.targetAlive)
        return { false, "dead" };
    if (!facts.targetInGroup)
        return { false, "not-grouped" };
    if (!facts.inRange)
        return { false, "out-of-range" };
    if (!facts.spellReady)
        return { false, "on-cooldown" };
    return { true, "ok" };
}

// The character a bare `soulstone` stones: the sender's own target when it has one, else the sender.
inline std::uint64_t BareSoulstoneTarget(std::uint64_t sender, std::uint64_t sendersTarget)
{
    return sendersTarget != 0 ? sendersTarget : sender;
}

} // namespace objuse
} // namespace TortoiseBots

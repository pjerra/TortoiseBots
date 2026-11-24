#pragma once

// Core-free gates of the rogue thievery host services: which bag lockbox a
// rogue opens itself, which creature is worth a pickpocket, and which guild
// rogue a lockbox is mailed to. Core facts (lock store, loot templates,
// skills) stay in the host; this header decides. Standalone-tested by
// forever/tools/test_thievery_policy.cpp.

#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace TortoiseBots
{
namespace prof
{

inline constexpr std::uint32_t kPickLockSpellId = 1804;
inline constexpr std::uint32_t kStealthSpellId = 1784;
inline constexpr std::uint32_t kPickPocketSpellId = 921;

// Classic lockpicking skill line (SKILL_LOCKPICKING, SharedDefines.h).
inline constexpr std::uint32_t kLockpickingSkillId = 633;

enum class LockboxGate : std::uint8_t
{
    Ok,              // locked and openable at the bot's Lockpicking
    Unlocked,        // already unlocked: open it and take the loot, no cast
    NoLock,          // no lock id: nothing to pick
    NotAContainer,   // locked, but no loot to take out of it
    NotLockpicking,  // the lock's skill case is not Lockpicking
    NoPickLockSpell, // the bot never learned Pick Lock
    NoSkill,         // the bot has no Lockpicking skill line
    LowSkill,        // Lockpicking below the lock's requirement
};

inline char const* LockboxGateToken(LockboxGate gate)
{
    switch (gate)
    {
        case LockboxGate::Ok:               return "ok";
        case LockboxGate::Unlocked:         return "unlocked";
        case LockboxGate::NoLock:           return "no-lock";
        case LockboxGate::NotAContainer:    return "not-a-container";
        case LockboxGate::NotLockpicking:   return "not-lockpicking";
        case LockboxGate::NoPickLockSpell:  return "no-pick-lock";
        case LockboxGate::NoSkill:          return "no-skill";
        case LockboxGate::LowSkill:         return "low-skill";
    }
    return "no-lock";
}

struct LockboxFacts
{
    bool hasLock = false;
    bool locked = false;
    bool hasLoot = false;
    bool lockpickingLock = false;
    bool hasPickLockSpell = false;
    bool hasLockpicking = false;
    std::uint32_t botSkill = 0;
    std::uint32_t requiredSkill = 0;
};

inline LockboxGate EvaluateLockbox(LockboxFacts const& f)
{
    if (!f.hasLock)
        return LockboxGate::NoLock;
    if (!f.hasLoot)
        return LockboxGate::NotAContainer;
    if (!f.locked)
        return LockboxGate::Unlocked;
    if (!f.lockpickingLock)
        return LockboxGate::NotLockpicking;
    if (!f.hasPickLockSpell)
        return LockboxGate::NoPickLockSpell;
    if (!f.hasLockpicking)
        return LockboxGate::NoSkill;
    if (f.botSkill < f.requiredSkill)
        return LockboxGate::LowSkill;
    return LockboxGate::Ok;
}

// A box this bot may work on now: locked with enough skill, or already
// unlocked with its loot still inside.
inline bool LockboxOpenable(LockboxGate gate)
{
    return gate == LockboxGate::Ok || gate == LockboxGate::Unlocked;
}

// A box another rogue may open: this bot is a rogue with the spell and the
// skill line and is only short of Lockpicking points.
inline bool LockboxNeedsABetterRogue(LockboxGate gate)
{
    return gate == LockboxGate::LowSkill;
}

// A box no rogue can take with the skill it has (this bot is not a rogue,
// or never trained Pick Lock): mail it to a guild rogue.
inline bool LockboxNeedsARogue(LockboxGate gate)
{
    return gate == LockboxGate::NoPickLockSpell || gate == LockboxGate::NoSkill;
}

enum class PickpocketBotGate : std::uint8_t
{
    Ready,
    NotRogue,
    NoSpell,   // Stealth or Pick Pocket not learned
    InCombat,
    Busy,      // another errand owns the walk, or a cast is running
    NotAlive,
};

inline char const* PickpocketBotGateToken(PickpocketBotGate gate)
{
    switch (gate)
    {
        case PickpocketBotGate::Ready:     return "ok";
        case PickpocketBotGate::NotRogue:  return "not-rogue";
        case PickpocketBotGate::NoSpell:   return "no-spell";
        case PickpocketBotGate::InCombat:  return "combat";
        case PickpocketBotGate::Busy:      return "busy";
        case PickpocketBotGate::NotAlive:  return "dead";
    }
    return "busy";
}

struct PickpocketBotFacts
{
    bool isRogue = false;
    bool hasStealthSpell = false;
    bool hasPickPocketSpell = false;
    bool inCombat = false;
    bool busy = false;
    bool alive = false;
    bool inWorld = false;
};

inline PickpocketBotGate EvaluatePickpocketBot(PickpocketBotFacts const& f)
{
    if (!f.isRogue)
        return PickpocketBotGate::NotRogue;
    if (!f.hasStealthSpell || !f.hasPickPocketSpell)
        return PickpocketBotGate::NoSpell;
    if (!f.alive || !f.inWorld)
        return PickpocketBotGate::NotAlive;
    if (f.inCombat)
        return PickpocketBotGate::InCombat;
    if (f.busy)
        return PickpocketBotGate::Busy;
    return PickpocketBotGate::Ready;
}

enum class PickpocketTargetGate : std::uint8_t
{
    Ok,
    Dead,
    InCombat,
    NotHumanoid,
    NoPockets,     // no pickpocket loot id, no template, or the creature cannot be looted
    PocketsLooted, // the refill timer has not run out: do not retry it
    TooFar,
};

inline char const* PickpocketTargetGateToken(PickpocketTargetGate gate)
{
    switch (gate)
    {
        case PickpocketTargetGate::Ok:            return "ok";
        case PickpocketTargetGate::Dead:          return "dead";
        case PickpocketTargetGate::InCombat:      return "in-combat";
        case PickpocketTargetGate::NotHumanoid:   return "not-humanoid";
        case PickpocketTargetGate::NoPockets:     return "no-pockets";
        case PickpocketTargetGate::PocketsLooted: return "pockets-looted";
        case PickpocketTargetGate::TooFar:        return "too-far";
    }
    return "no-pockets";
}

struct PickpocketTargetFacts
{
    bool alive = false;
    bool inCombat = false;
    bool humanoid = false;
    bool canHaveLoot = false;
    bool hasPockets = false;      // PickPocketLootID set and a pickpocketing template exists
    bool pocketsReady = false;    // Creature::CanGeneratePickPocketLoot
    float distanceYd = 0.0f;
    float maxDistanceYd = 0.0f;
};

inline PickpocketTargetGate EvaluatePickpocketTarget(PickpocketTargetFacts const& f)
{
    if (!f.alive)
        return PickpocketTargetGate::Dead;
    if (f.inCombat)
        return PickpocketTargetGate::InCombat;
    if (!f.humanoid)
        return PickpocketTargetGate::NotHumanoid;
    if (!f.canHaveLoot || !f.hasPockets)
        return PickpocketTargetGate::NoPockets;
    if (!f.pocketsReady)
        return PickpocketTargetGate::PocketsLooted;
    if (f.distanceYd > f.maxDistanceYd)
        return PickpocketTargetGate::TooFar;
    return PickpocketTargetGate::Ok;
}

// The core's postage: 30 copper a mail with no item, 30 copper per attached
// item (MailHandler.cpp:171).
inline constexpr std::uint64_t kMailPostagePerItemCopper = 30;

// A mail carries at most 12 client attachments (MAX_CLIENT_MAIL_ITEMS).
inline constexpr std::uint32_t kLockboxMailMaxItems = 12;

inline std::uint64_t MailPostageCopper(std::uint32_t attachments)
{
    return kMailPostagePerItemCopper * (attachments ? attachments : 1);
}

// The box's own coins pay the return postage; a box that yielded less than
// the postage returns none and the rogue covers the difference, so the
// service never mints money for the box's owner.
inline std::uint64_t LockboxReturnCopper(std::uint64_t boxCopper, std::uint64_t postage)
{
    return boxCopper > postage ? boxCopper - postage : 0;
}

inline std::string TrimLockboxSubject(std::string const& subject)
{
    std::size_t begin = 0;
    std::size_t end = subject.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(subject[begin])))
        ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(subject[end - 1])))
        --end;
    std::string out = subject.substr(begin, end - begin);
    for (char& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

inline bool IsLockboxServiceSubject(std::string const& subject)
{
    return TrimLockboxSubject(subject) == "lockbox";
}

inline constexpr char const* kLockboxServiceSubject = "lockbox";

struct LockboxRogueCandidate
{
    std::uint32_t guidLow = 0;
    std::string name;
    bool isBot = false;
    bool isRogue = false;
    bool online = false;
    std::uint32_t lockpickingSkill = 0;
};

// The best rogue: highest Lockpicking, then one that is online (the box is
// opened now rather than at its next login), then the lowest guid so two
// equal candidates always resolve the same way.
inline LockboxRogueCandidate const* PickLockboxRogue(
    std::uint32_t senderGuidLow, std::uint32_t requiredSkill,
    std::vector<LockboxRogueCandidate> const& candidates)
{
    LockboxRogueCandidate const* best = nullptr;
    for (LockboxRogueCandidate const& c : candidates)
    {
        if (!c.isBot || !c.isRogue || c.guidLow == 0 || c.guidLow == senderGuidLow)
            continue;
        if (c.lockpickingSkill < requiredSkill)
            continue;
        if (!best)
        {
            best = &c;
            continue;
        }
        if (c.lockpickingSkill != best->lockpickingSkill)
        {
            if (c.lockpickingSkill > best->lockpickingSkill)
                best = &c;
            continue;
        }
        if (c.online != best->online)
        {
            if (c.online)
                best = &c;
            continue;
        }
        if (c.guidLow < best->guidLow)
            best = &c;
    }
    return best;
}

} // namespace prof
} // namespace TortoiseBots

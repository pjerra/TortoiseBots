// The class spells a real player gets from its class quests, as a reviewed allow-list. Core-free, so a unit test proves the policy
// (forever/tools/test_class_quest_spells.cpp) and the host half (TbClassQuestSpells.cpp) only reads the world.
//
// The table never teaches anything else, and nothing in it is taught without a non-repeatable quest of the running
// world DB that teaches the spell, either class-masked or with the class implied by the spell it teaches (the host
// builds the index from sObjectMgr->GetQuestTemplates()).
#pragma once

#include <cstdint>
#include <vector>

namespace TortoiseBots
{
namespace questspells
{

enum class RaceRule : uint8_t
{
    // Any quest of the bot's class counts; a quest its race can take decides the level when one exists.
    ClassWide,
    // Only a quest the bot's race can take counts (the priest racials).
    QuestRace,
};

struct AllowedSpell
{
    uint8_t classId;
    uint32_t spellId;
    char const* name;
    uint32_t hint;      // the quest's RewardSpell/SourceSpell this row expects (validation only)
    uint8_t level;      // fallback level when the quest carries no classic min level
    RaceRule race;
};

// Read from the pinned core's quest tables and reviewed by hand: nothing else is ever taught.
inline constexpr AllowedSpell kAllowed[] =
{
    { 1, 71, "Defensive Stance", 8121, 10, RaceRule::ClassWide },
    { 1, 7386, "Sunder Armor", 8121, 10, RaceRule::ClassWide },
    { 1, 355, "Taunt", 8121, 10, RaceRule::ClassWide },
    { 1, 2458, "Berserker Stance", 8616, 30, RaceRule::ClassWide },
    { 1, 20252, "Intercept", 8616, 30, RaceRule::ClassWide },
    { 2, 7328, "Redemption", 7329, 12, RaceRule::ClassWide },
    { 2, 5502, "Sense Undead", 5503, 20, RaceRule::ClassWide },
    { 3, 1515, "Tame Beast", 1579, 10, RaceRule::ClassWide },
    { 3, 883, "Call Pet", 1579, 10, RaceRule::ClassWide },
    { 3, 2641, "Dismiss Pet", 1579, 10, RaceRule::ClassWide },
    { 3, 5149, "Beast Training", 5300, 10, RaceRule::ClassWide },
    { 3, 6991, "Feed Pet", 5300, 10, RaceRule::ClassWide },
    { 3, 982, "Revive Pet", 5300, 10, RaceRule::ClassWide },
    { 4, 2842, "Poisons", 2995, 20, RaceRule::ClassWide },
    { 5, 2652, "Touch of Weakness", 19318, 10, RaceRule::QuestRace },
    { 5, 9035, "Hex of Weakness", 19325, 10, RaceRule::QuestRace },
    { 5, 13908, "Desperate Prayer", 19338, 10, RaceRule::QuestRace },
    { 5, 10797, "Starshards", 19350, 10, RaceRule::QuestRace },
    { 5, 6346, "Fear Ward", 19337, 20, RaceRule::QuestRace },
    { 5, 18137, "Shadowguard", 19331, 20, RaceRule::QuestRace },
    { 5, 2944, "Devouring Plague", 2946, 20, RaceRule::QuestRace },
    { 5, 2651, "Elune's Grace", 19357, 20, RaceRule::QuestRace },
    { 5, 13896, "Feedback", 19345, 20, RaceRule::QuestRace },
    { 7, 8071, "Stoneskin Totem", 8073, 4, RaceRule::ClassWide },
    { 7, 3599, "Searing Totem", 2075, 10, RaceRule::ClassWide },
    { 7, 5394, "Healing Stream Totem", 5396, 20, RaceRule::ClassWide },
    { 9, 688, "Summon Imp", 7763, 1, RaceRule::ClassWide },
    { 9, 697, "Summon Voidwalker", 11520, 10, RaceRule::ClassWide },
    { 9, 712, "Summon Succubus", 11519, 20, RaceRule::ClassWide },
    { 9, 691, "Summon Felhunter", 1373, 30, RaceRule::ClassWide },
    { 9, 1122, "Inferno", 1413, 50, RaceRule::ClassWide },
    { 9, 18540, "Ritual of Doom", 20700, 60, RaceRule::ClassWide },
    { 11, 5487, "Bear Form", 19179, 10, RaceRule::ClassWide },
    { 11, 6795, "Growl", 19179, 10, RaceRule::ClassWide },
    { 11, 6807, "Maul", 19179, 10, RaceRule::ClassWide },
    { 11, 18960, "Teleport: Moonglade", 19027, 10, RaceRule::ClassWide },
    { 11, 8946, "Cure Poison", 8947, 14, RaceRule::ClassWide },
    { 11, 1066, "Aquatic Form", 1446, 16, RaceRule::ClassWide },
    { 11, 768, "Cat Form", 1309050, 20, RaceRule::ClassWide },
};

inline constexpr size_t kAllowedCount = sizeof(kAllowed) / sizeof(kAllowed[0]);

inline std::vector<AllowedSpell const*> RowsForClass(uint8_t classId)
{
    std::vector<AllowedSpell const*> rows;
    for (AllowedSpell const& row : kAllowed)
        if (row.classId == classId)
            rows.push_back(&row);
    return rows;
}

inline AllowedSpell const* FindRow(uint8_t classId, uint32_t spellId)
{
    for (AllowedSpell const& row : kAllowed)
        if (row.classId == classId && row.spellId == spellId)
            return &row;
    return nullptr;
}

// One quest template as the host reads it from the core; rewLearns/srcLearns hold the SPELL_EFFECT_LEARN_SPELL trigger spells.
struct QuestRaw
{
    uint32_t questId = 0;
    uint32_t classMask = 0;
    bool repeatable = false;
    int32_t classicMinLevel = 0;
    int32_t rewSpell = 0;
    std::vector<uint32_t> rewLearns;
    std::vector<uint32_t> displays;
    uint32_t srcSpell = 0;
    std::vector<uint32_t> srcLearns;
};

// The allow-listed spells this quest teaches its class. What a quest shows it gives (its display spells) counts too, and so does
// the spell cast on accept; only ids in the allow-list for the class are kept. A quest with no class mask counts when the spell it
// teaches has a row for the class: the spell implies the class (Forever's class-spell quests carry no mask).
inline std::vector<uint32_t> CollectTaught(QuestRaw const& q, uint8_t classId)
{
    std::vector<uint32_t> taught;
    if (!classId || classId > 32 || q.repeatable)
        return taught;
    if (q.classMask && !(q.classMask & (1u << (classId - 1))))
        return taught;

    auto add = [&](uint32_t spellId)
    {
        if (!spellId || !FindRow(classId, spellId))
            return;
        for (uint32_t known : taught)
            if (known == spellId)
                return;
        taught.push_back(spellId);
    };

    if (q.rewSpell > 0)
    {
        add(uint32_t(q.rewSpell));
        for (uint32_t spellId : q.rewLearns)
            add(spellId);
    }
    for (uint32_t spellId : q.displays)
        add(spellId);
    if (q.srcSpell)
    {
        add(q.srcSpell);
        for (uint32_t spellId : q.srcLearns)
            add(spellId);
    }
    return taught;
}

// One indexed quest for a row: whether the bot's race can take it and the level its gate resolves to.
struct Candidate
{
    uint32_t questId = 0;
    bool raceOk = false;
    uint8_t minLevel = 0;
};

enum class Verdict : uint8_t
{
    Teach,
    Known,
    BelowLevel,
    NoQuest,
    NoRaceQuest,
    Unfit,
};

struct Decision
{
    uint32_t spellId = 0;
    uint32_t questId = 0;
    uint8_t gate = 0;
    Verdict verdict = Verdict::NoQuest;
};

// What to do with one row for one bot: the race rule, then the quest's own gate, then what the bot already knows.
inline Decision Decide(AllowedSpell const& row, uint8_t botLevel, std::vector<Candidate> const& cands, bool known, bool fits)
{
    Decision d;
    d.spellId = row.spellId;
    if (cands.empty())
    {
        d.verdict = Verdict::NoQuest;
        return d;
    }

    size_t raceOkCount = 0;
    for (Candidate const& cand : cands)
        if (cand.raceOk)
            ++raceOkCount;
    if (row.race == RaceRule::QuestRace && !raceOkCount)
    {
        d.verdict = Verdict::NoRaceQuest;
        return d;
    }

    // A race-fitting quest decides the level; a class-wide row falls back to all of the class's quests when none fits the race.
    bool const countedRaceOk = raceOkCount > 0;
    bool have = false;
    for (Candidate const& cand : cands)
    {
        if (countedRaceOk && !cand.raceOk)
            continue;
        if (!have || cand.minLevel < d.gate || (cand.minLevel == d.gate && cand.questId < d.questId))
        {
            d.gate = cand.minLevel;
            d.questId = cand.questId;
            have = true;
        }
    }

    if (botLevel < d.gate)
        d.verdict = Verdict::BelowLevel;
    else if (known)
        d.verdict = Verdict::Known;
    else if (!fits)
        d.verdict = Verdict::Unfit;
    else
        d.verdict = Verdict::Teach;
    return d;
}

inline char const* VerdictName(Verdict verdict)
{
    switch (verdict)
    {
        case Verdict::Teach: return "teach";
        case Verdict::Known: return "known";
        case Verdict::BelowLevel: return "below-level";
        case Verdict::NoQuest: return "no-quest";
        case Verdict::NoRaceQuest: return "no-race-quest";
        case Verdict::Unfit: return "unfit";
    }
    return "unknown";
}

} // namespace questspells
} // namespace TortoiseBots

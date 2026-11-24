#pragma once

// Which of the same-named spells a bot knows is the one to cast.
//
// The spell store maps a name to every spell that carries it, and that is
// more than the ranked line: a proc a buff triggers (Touch of Weakness 2943,
// Shadowguard 28377) has the same name, sits in the same class skill line and
// is in no rank chain. The bot learns those with its class spells, and a rank
// it cannot rank must not shadow the ranked one: picking the hostile proc
// made the self-buff action fail its cast check every tick (a friendly target
// refuses a negative spell) and the buff aura never landed.
//
// Ranked spells always win, by rank and then by id; a name with no rank at all
// falls back to its highest id.

#include <cstdint>
#include <string>
#include <vector>

namespace ai
{

struct KnownSpellRank
{
    uint32_t id = 0;
    uint32_t rank = 0;  // 0: the spell is in no proved rank chain
};

// The rank number a rank text holds ("Rank 6" -> 6, "" -> 0).
inline uint32_t SpellRankNumber(std::string const& rankText)
{
    size_t i = 0;
    while (i < rankText.size() && (rankText[i] < '0' || rankText[i] > '9'))
        ++i;
    uint32_t rank = 0;
    for (; i < rankText.size() && rankText[i] >= '0' && rankText[i] <= '9'; ++i)
        rank = rank * 10 + uint32_t(rankText[i] - '0');
    return rank;
}

inline uint32_t PickCastSpellId(std::vector<KnownSpellRank> const& known)
{
    uint32_t ranked = 0;
    uint32_t rankedRank = 0;
    uint32_t unranked = 0;
    for (KnownSpellRank const& spell : known)
    {
        if (!spell.rank)
        {
            if (spell.id > unranked)
                unranked = spell.id;
            continue;
        }
        if (spell.rank > rankedRank || (spell.rank == rankedRank && spell.id > ranked))
        {
            ranked = spell.id;
            rankedRank = spell.rank;
        }
    }
    return ranked ? ranked : unranked;
}

} // namespace ai

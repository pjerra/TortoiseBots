// The pure rules of `.bot behavior` and `.bot totem` (#133 round 1): the behaviour toggle table ported from old TB
// (commands/Party.cpp:1268-1281), the scope word and the tokenizer of a choice line. No core header; tested standalone by
// forever/tools/test_choice_cmd.cpp. The tier of a choice line is the player-command pure header's `TierOfChoice`
// (TbPlayerAuthPure.h), re-exported here so a caller of the table needs one include.
#pragma once

#include "host/player/TbPlayerAuthPure.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace TortoiseBots
{
namespace choice
{
using player::Tier;
using player::TierOf;
using player::TierOfChoice;

// The bot state a toggle's strategy lives in. The host maps it to BotState; the pure header names its own so a standalone
// build never needs the engine's header.
enum class ToggleState
{
    NonCombat,
    Combat,
    Reaction,
};

// One row of the ported table: the key the player types, the strategy it sets, the state it lives in, whether the strategy
// on means the key is off (`aoe` holds "no aoe"), and whether only an off-spec class has it.
struct Toggle
{
    char const* key;
    char const* strategy;
    ToggleState state;
    bool inverted = false;
    bool offSpec = false;
};

// The table in old TB's order, with old TB's keys, strategies and states.
inline Toggle const* ToggleTable(std::size_t& count)
{
    static Toggle const table[] = {
        { "loot", "loot", ToggleState::NonCombat },
        { "aoe", "no aoe", ToggleState::Combat, true },
        { "autocc", "auto cc", ToggleState::Combat },
        { "savemana", "conserve mana", ToggleState::Combat },
        { "boost", "boost", ToggleState::Combat },
        { "threat", "threat", ToggleState::Combat },
        { "potions", "potions", ToggleState::Reaction },
        { "mimic", "mimic consumables", ToggleState::NonCombat },
        { "spread", "spread", ToggleState::Combat },
        { "hold", "hold position", ToggleState::Combat },
        { "noblink", "no blink instance", ToggleState::Combat },
        { "offheal", "offheal", ToggleState::Combat, false, true },
        { "offdps", "offdps", ToggleState::Combat, false, true },
    };
    count = sizeof(table) / sizeof(table[0]);
    return table;
}

// The row of one key, or nullptr. Keys are lowercase; the host lowers the player's word first.
inline Toggle const* FindToggle(std::string_view key)
{
    std::size_t count = 0;
    Toggle const* table = ToggleTable(count);
    for (std::size_t i = 0; i < count; ++i)
        if (key == table[i].key)
            return &table[i];
    return nullptr;
}

// The value the bot shows, read through the row's inversion: `hasStrategy` is HasStrategy on the row's state.
inline bool IsToggleOn(bool hasStrategy, Toggle const& toggle)
{
    return hasStrategy != toggle.inverted;
}

// The change that makes the row read `enable`: "+strategy" or "-strategy" as ChangeStrategy takes it.
inline std::string ToggleChange(bool enable, Toggle const& toggle)
{
    bool const add = enable != toggle.inverted;
    return std::string(add ? "+" : "-") + toggle.strategy;
}

// An off-spec row belongs only to a class with the off-spec strategies (OffSpecPolicy.h's classes); every other row is the
// class's own. `classHasOffSpec` is the host's read of that pure list.
inline bool ToggleAppliesToClass(Toggle const& toggle, bool classHasOffSpec)
{
    return !toggle.offSpec || classHasOffSpec;
}

// The leading word of a choice tail: `party` is the caller's group, and any other word is a bot's name (the host's bot
// lookup normalizes it). There is no `raid` word: party covers a raid group.
enum class ScopeKind
{
    Bot,
    Party,
};

struct ParsedScope
{
    ScopeKind kind = ScopeKind::Bot;
    std::string name;
};

inline ParsedScope ParseScope(std::string_view word)
{
    if (word == "party")
        return ParsedScope{ ScopeKind::Party, std::string() };
    return ParsedScope{ ScopeKind::Bot, std::string(word) };
}

// The whitespace tokens of `<scope> <args...>`, each lowered: a bot name is normalized by the host's lookup, a totem or
// toggle word is compared lowercase, and a set list is rejoined by the caller (its parser trims around the commas).
inline std::vector<std::string> ChoiceWords(std::string_view tail)
{
    std::vector<std::string> words;
    std::size_t i = 0;
    while (i < tail.size())
    {
        while (i < tail.size() && std::isspace(static_cast<unsigned char>(tail[i])))
            ++i;
        std::size_t const begin = i;
        while (i < tail.size() && !std::isspace(static_cast<unsigned char>(tail[i])))
            ++i;
        if (i == begin)
            break;
        std::string word;
        word.reserve(i - begin);
        for (std::size_t k = begin; k < i; ++k)
            word.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(tail[k]))));
        words.push_back(word);
    }
    return words;
}

// `on`/`off` only; anything else is a usage refusal.
inline bool ParseOnOff(std::string_view word, bool& on)
{
    if (word == "on")
    {
        on = true;
        return true;
    }
    if (word == "off")
    {
        on = false;
        return true;
    }
    return false;
}
}
}

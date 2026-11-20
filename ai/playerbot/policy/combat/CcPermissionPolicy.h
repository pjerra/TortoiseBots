#pragma once

// Crowd-control permissions (issue #12): which kinds of crowd control the bots
// may use, per owner and per class, with fear denied until the owner allows it.
//
// Kinds (the issue's words): fear, sheep, banish, shackle, sap, sleep, trap,
// charm. Stuns, roots, snares and interrupts are NOT crowd control here: they
// have no kind and no deny can touch them.
//
// Pure (no core, no database, no threads): the host keeps the memory-only
// store and pushes each bot's effective mask into its PlayerbotAI, whose cast
// check refuses a denied spell and lets the rotation move on. The future
// `.bot` order reuses this header with its own owner key.

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ai
{
namespace CcPermissions
{
// One bit per kind, in the issue's order.
inline constexpr std::uint32_t kFear = 1u << 0;
inline constexpr std::uint32_t kSheep = 1u << 1;
inline constexpr std::uint32_t kBanish = 1u << 2;
inline constexpr std::uint32_t kShackle = 1u << 3;
inline constexpr std::uint32_t kSap = 1u << 4;
inline constexpr std::uint32_t kSleep = 1u << 5;
inline constexpr std::uint32_t kTrap = 1u << 6;
inline constexpr std::uint32_t kCharm = 1u << 7;
inline constexpr std::uint32_t kAll = 0xFFu;
// A restart returns to this: everything but fear.
inline constexpr std::uint32_t kDefaultDenied = kFear;
// The store's all-classes entry (a class word may also name one class).
inline constexpr std::uint8_t kAnyClass = 0;

inline char LowerChar(char c)
{
    return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
}

// Case-insensitive exact match without allocating (the cast check runs it per
// spell, so the input keeps its case and the table stays lowercase).
inline bool SpellMatches(char const* table, char const* name)
{
    for (;; ++table, ++name)
    {
        if (LowerChar(*name) != *table)
            return false;
        if (*table == '\0')
            return true;
    }
}

struct SpellKind
{
    char const* name;
    std::uint32_t kind;
};

// Every rank shares its line's name, so one entry covers all ranks.
inline SpellKind const* SpellKindTable(std::size_t& count)
{
    static SpellKind const table[] = {
        {"fear", kFear},
        {"howl of terror", kFear},
        {"death coil", kFear},
        {"psychic scream", kFear},
        {"intimidating shout", kFear},
        {"scare beast", kFear},
        {"turn undead", kFear},
        {"polymorph", kSheep},
        {"banish", kBanish},
        {"shackle undead", kShackle},
        {"sap", kSap},
        {"hibernate", kSleep},
        {"wyvern sting", kSleep},
        {"freezing trap", kTrap},
        {"seduction", kCharm},
        {"enslave demon", kCharm},
        {"mind control", kCharm},
    };
    count = sizeof(table) / sizeof(table[0]);
    return table;
}

// The kind a spell casts, 0 when no deny can touch it (damage, heals, stuns,
// roots, snares, interrupts, and the Razuvious mind-control tactic, whose
// action is not named "mind control").
inline std::uint32_t KindFromSpellName(char const* name)
{
    if (!name || !*name)
        return 0;
    std::size_t count = 0;
    SpellKind const* table = SpellKindTable(count);
    for (std::size_t i = 0; i < count; ++i)
        if (SpellMatches(table[i].name, name))
            return table[i].kind;
    // Polymorph's variants ("polymorph: pig", "polymorph: turtle").
    static char const prefix[] = "polymorph:";
    for (std::size_t i = 0; prefix[i]; ++i)
        if (LowerChar(name[i]) != prefix[i])
            return 0;
    return kSheep;
}

inline std::uint32_t KindFromSpellName(std::string const& name)
{
    return KindFromSpellName(name.c_str());
}

// The kind an order word names ("all" and "cc" name every kind), 0 when the
// word names none. The caller lowercases first.
inline std::uint32_t KindFromWord(std::string const& word)
{
    if (word == "fear")
        return kFear;
    if (word == "sheep")
        return kSheep;
    if (word == "banish")
        return kBanish;
    if (word == "shackle")
        return kShackle;
    if (word == "sap")
        return kSap;
    if (word == "sleep")
        return kSleep;
    if (word == "trap")
        return kTrap;
    if (word == "charm")
        return kCharm;
    if (word == "all" || word == "cc")
        return kAll;
    return 0;
}

inline char const* KindWord(std::uint32_t kind)
{
    if (kind == kFear)
        return "fear";
    if (kind == kSheep)
        return "sheep";
    if (kind == kBanish)
        return "banish";
    if (kind == kShackle)
        return "shackle";
    if (kind == kSap)
        return "sap";
    if (kind == kSleep)
        return "sleep";
    if (kind == kTrap)
        return "trap";
    if (kind == kCharm)
        return "charm";
    return nullptr;
}

// The denied mask as `fear;sheep` words, in kind order ("" when none).
inline std::vector<std::string> DeniedWords(std::uint32_t mask)
{
    static std::uint32_t const order[] = { kFear, kSheep, kBanish, kShackle, kSap, kSleep, kTrap, kCharm };
    std::vector<std::string> words;
    for (std::uint32_t kind : order)
        if ((mask & kind) && KindWord(kind))
            words.push_back(KindWord(kind));
    return words;
}

// A class by its core id (0 = all classes). The caller lowercases first; a
// trailing 's' names the plural ("mages"). Death knight (6) has no Turtle
// kit, but parsing it harms nothing: the brain never consults the store.
inline std::uint8_t ClassFromWord(std::string word)
{
    if (word == "warrior")
        return 1;
    if (word == "paladin")
        return 2;
    if (word == "hunter")
        return 3;
    if (word == "rogue")
        return 4;
    if (word == "priest")
        return 5;
    if (word == "deathknight" || word == "death knight")
        return 6;
    if (word == "shaman")
        return 7;
    if (word == "mage")
        return 8;
    if (word == "warlock")
        return 9;
    if (word == "druid")
        return 11;
    if (!word.empty() && word.back() == 's')
    {
        word.pop_back();
        return ClassFromWord(word);
    }
    return 0;
}

inline char const* ClassName(std::uint8_t klass)
{
    switch (klass)
    {
        case 1: return "warrior";
        case 2: return "paladin";
        case 3: return "hunter";
        case 4: return "rogue";
        case 5: return "priest";
        case 6: return "deathknight";
        case 7: return "shaman";
        case 8: return "mage";
        case 9: return "warlock";
        case 11: return "druid";
        default: break;
    }
    return "";
}

// Memory-only: (owner, class) to denied mask. The owner is whoever binds the
// bots (the commander's account on the host); class 0 is the all-classes
// entry an order without a class word writes. An explicit allow stores the
// mask 0, which still wins over the all-classes entry and the default: only
// a missing entry falls through.
class Store
{
public:
    void Set(std::uint32_t owner, std::uint8_t klass, std::uint32_t mask)
    {
        auto key = std::make_pair(owner, klass);
        auto inserted = entries_.insert({ key, mask });
        if (!inserted.second && inserted.first->second == mask)
            return;
        inserted.first->second = mask;
        ++version_;
    }

    bool Has(std::uint32_t owner, std::uint8_t klass) const
    {
        return entries_.find(std::make_pair(owner, klass)) != entries_.end();
    }

    std::uint32_t DeniedFor(std::uint32_t owner, std::uint8_t klass, std::uint32_t defaultMask) const
    {
        auto exact = entries_.find(std::make_pair(owner, klass));
        if (exact != entries_.end())
            return exact->second;
        if (klass != kAnyClass)
        {
            auto any = entries_.find(std::make_pair(owner, kAnyClass));
            if (any != entries_.end())
                return any->second;
        }
        return defaultMask;
    }

    std::size_t Size() const { return entries_.size(); }
    std::uint64_t Version() const { return version_; }

private:
    std::map<std::pair<std::uint32_t, std::uint8_t>, std::uint32_t> entries_;
    std::uint64_t version_ = 0;
};
}
}

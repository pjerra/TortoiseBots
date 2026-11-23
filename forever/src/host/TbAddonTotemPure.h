// The `totem` addon verb's core-free half (TBF #133 piece c-totem): TB's
// `.bot totem` grammar (TortoiseBots/commands/Totem.cpp:229-464) over the TBM
// addon channel. No core header and no legacy type; tested standalone by
// forever/tools/test_addon_totem.cpp. The live half that reads the live
// objects and the bot AIs is host/TbAddonTotem.cpp.
//
// The slot/mode/order tables stay single-sourced in the shared pure policy
// (ai/playerbot/strategy/shaman/ShamanTotemCommandPolicy.h): this header
// only parses the verb tail and formats the TB wire.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "ai/playerbot/strategy/shaman/ShamanTotemCommandPolicy.h"
#include "host/TbAddonPure.h"

namespace TortoiseBots
{
namespace totem
{
// One parsed `totem <scope> ...` tail: `<bot|party|raid>
// <slot> <totem|auto|none> | set <e>,<f>,<w>,<a> | mode <auto|prepull|manual>
// | drop | status` (TB's usage string, Totem.cpp:255-256). `stored` holds the
// canonical stored form ("", "auto" or the totem name); `mode` the canonical
// drop mode ("auto", "prepull", "manual"). `error` is the TB ERR code
// ("invalid", "unknown-totem") with the human reason in `message`.
struct Request
{
    enum class Kind
    {
        Slot,
        Set,
        Mode,
        Drop,
        Status
    };

    bool ok = false;
    std::string scope;        // lowered bot name, or "party"/"raid"
    bool partyScope = false;
    Kind kind = Kind::Status;
    int slot = -1;            // Kind::Slot only
    std::string stored;       // Kind::Slot only
    std::string storedSet[4]; // Kind::Set only
    std::string mode;         // Kind::Mode only
    std::string error;
    std::string message;
};

inline std::string Lower(std::string_view word)
{
    std::string out;
    out.reserve(word.size());
    for (char c : word)
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

inline std::string Trim(std::string const& text)
{
    std::size_t begin = 0;
    while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t'))
        ++begin;
    std::size_t end = text.size();
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t'))
        --end;
    return text.substr(begin, end - begin);
}

inline char const* Usage()
{
    return "Usage: totem <bot|party|raid> <earth|fire|water|air> <totem name|auto|none> | set <earth>,<fire>,<water>,<air> | mode <auto|prepull|manual> | drop | status";
}

inline Request InvalidRequest()
{
    Request out;
    out.error = "invalid";
    out.message = Usage();
    return out;
}

inline Request ParseTotemRequest(std::string_view args)
{
    Request out = InvalidRequest();
    std::string const input = Trim(std::string(args));
    std::size_t pos = input.find_first_of(" \t");
    std::string const scope = Lower(input.substr(0, pos));
    std::string const afterScope = pos == std::string::npos ? std::string() : Trim(input.substr(pos + 1));
    pos = afterScope.find_first_of(" \t");
    std::string const word = Lower(afterScope.substr(0, pos));
    std::string const rest = pos == std::string::npos ? std::string() : Trim(afterScope.substr(pos + 1));
    if (scope.empty())
        return out;
    out.scope = scope;
    out.partyScope = scope == "party" || scope == "raid";

    if (word == "status")
    {
        out.ok = true;
        out.kind = Request::Kind::Status;
        return out;
    }
    if (word == "drop")
    {
        out.ok = true;
        out.kind = Request::Kind::Drop;
        return out;
    }
    if (word == "mode")
    {
        std::string const mode = Lower(rest);
        if (mode != "auto" && mode != "prepull" && mode != "pre-pull" && mode != "manual")
            return out;
        out.ok = true;
        out.kind = Request::Kind::Mode;
        out.mode = ai::TotemDropModeName(ai::ParseTotemDropMode(mode));
        return out;
    }
    if (word == "set")
    {
        if (!ai::ParseTotemSetList(Lower(rest), out.storedSet))
            return out;
        out.ok = true;
        out.kind = Request::Kind::Set;
        return out;
    }
    int const slot = ai::TotemSlotIndex(word);
    if (slot < 0 || rest.empty())
        return out;
    bool valid = false;
    std::string const stored = ai::NormalizeTotemOrder(slot, Lower(rest), valid);
    if (!valid)
    {
        out.error = "unknown-totem";
        out.message = "Unknown totem '" + rest + "' for the " + ai::TotemSlotName(slot) + " slot.";
        return out;
    }
    out.ok = true;
    out.kind = Request::Kind::Slot;
    out.slot = slot;
    out.stored = stored;
    return out;
}

// TB's status words (Totem.cpp:124-138): an empty stored order reads "none",
// an empty drop mode reads "auto".
inline std::string DisplaySlot(std::string const& stored)
{
    return stored.empty() ? "none" : stored;
}

inline std::string DisplayMode(std::string const& stored)
{
    return stored.empty() ? "auto" : stored;
}

// TB's `known` field (Totem.cpp:144-160): every trained totem once, slot
// order earth..air, joined by '+' ("fire resistance totem" is on two slots'
// lists, so it must dedup).
inline std::string JoinKnown(std::vector<std::string> const& names)
{
    std::string out;
    for (std::string const& name : names)
    {
        if (!out.empty() && ("+" + out + "+").find("+" + name + "+") != std::string::npos)
            continue;
        if (!out.empty())
            out += "+";
        out += name;
    }
    return out;
}

// One TBM:TOTEM record (Totem.cpp:161-163), the line BotTotem.lua parses.
// TB sends the whole `known` list over chat (no length cap); the addon
// channel caps at 255 bytes, so a shaman that trained half of Alterac
// would truncate mid-list and the panel would offer a broken subset. A
// missing `known` reads as nil (older server) and the panel offers every
// totem of the slot instead — the server still rejects untrained picks —
// so the line drops the list when it does not fit, never a prefix of it.
inline std::string FormatTotemLine(std::string const& bot, std::string const& earth, std::string const& fire,
    std::string const& water, std::string const& air, std::string const& mode, std::string const& known)
{
    std::string const core = "TBM:TOTEM|" + bot + "|earth=" + earth + ",fire=" + fire + ",water=" + water + ",air=" + air +
        ",mode=" + mode;
    std::string const full = core + ",known=" + known;
    return full.size() <= addon::pure::kMaxLineBytes ? full : core;
}

// TB's ACK scope (Totem.cpp:291): "party" for a party/raid scope, else the bot.
inline std::string AckScope(bool partyScope, std::string const& bot)
{
    return partyScope ? "party" : "bot:" + bot;
}
} // namespace totem
} // namespace TortoiseBots

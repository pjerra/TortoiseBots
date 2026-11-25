// The add-on hint rule without the core (TBF #46): a character whose client
// never spoke the TBM prefix is told once where to get the add-on. No core
// header and no legacy type; tested standalone by
// forever/tools/test_addon_hint_pure.cpp. The live half that reads the live
// objects is host/TbHostAddonHint.cpp.
//
// "Has the add-on" is one TBM message from that character in this session
// (WorldSession::IsAddonRegistered answers true for a session that registered
// no prefix at all, so it cannot be used). "Once" is the characters-DB row;
// the caller decides what to do with each step.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace TortoiseBots
{
namespace addonhint
{
namespace pure
{
// One login of a real player waiting for the hint: the character guid (the
// characters table's `guid`, ObjectGuid::GetCounter) and the millisecond clock
// value of its login (getMSTime, wraps).
struct HintEntry
{
    uint64_t guid;
    uint32_t loginMs;
};

// What the world tick does with one pending entry this tick.
enum class HintStep
{
    Wait,       // stay pending
    DropSeen,   // the add-on answered: never hint this character
    DropStaff,  // an elevated account: no unasked line, and no row either
    Send        // the delay passed and nothing came: send the hint
};

// The delay bounds, in seconds: the conf value is clamped into them.
inline constexpr int32_t kMinDelaySeconds = 10;
inline constexpr int32_t kMaxDelaySeconds = 300;

// At most this many hints go out in one world tick.
inline constexpr uint32_t kMaxHintsPerTick = 20;

// The step for one pending entry. `seenThisSession` wins over everything
// (a character with the add-on is never told), then an elevated account has
// its entry dropped unanswered, then a character already flagged in the store
// waits forever, then the wrap-safe elapsed time decides. `staff` cannot be
// inferred here: the caller reads it from the session's security, above the
// normal player level.
inline HintStep Decide(bool alreadyFlagged, bool seenThisSession, uint32_t loginMs, uint32_t nowMs, uint32_t delayMs,
    bool staff = false)
{
    if (seenThisSession)
        return HintStep::DropSeen;
    if (staff)
        return HintStep::DropStaff;
    if (alreadyFlagged)
        return HintStep::Wait;
    if (static_cast<uint32_t>(nowMs - loginMs) >= delayMs)
        return HintStep::Send;
    return HintStep::Wait;
}

// The conf value clamped into kMinDelaySeconds .. kMaxDelaySeconds.
inline uint32_t ClampDelaySeconds(int32_t seconds)
{
    if (seconds < kMinDelaySeconds)
        return static_cast<uint32_t>(kMinDelaySeconds);
    if (seconds > kMaxDelaySeconds)
        return static_cast<uint32_t>(kMaxDelaySeconds);
    return static_cast<uint32_t>(seconds);
}

// The line the player sees. An empty url keeps the sentence and drops the
// "Get it at" clause.
inline std::string HintText(std::string_view url)
{
    std::string text = "TortoiseBots: this server's bots have a control panel, the TortoiseBotsForeverManager add-on.";
    if (!url.empty())
    {
        text += " Get it at ";
        text += url;
    }
    text += " (shown once).";
    return text;
}

} // namespace pure
} // namespace addonhint
} // namespace TortoiseBots

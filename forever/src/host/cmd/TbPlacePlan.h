// Issue #15 (`place` / `rally` / `fall back`): the command words, the placement plan and the hold bookkeeping, pure. No core header, no Player, no Map:
// the command (TbPlace.cpp) reads the world and this header decides what happens. Tested by forever/tools/test_place_plan.cpp.
//
// ONE PARSE. `.tb cmd` passes the command as typed (verb included) and the chat form builds the same words, so both go through ParseCommand: a
// `.tb cmd fall back` can never be read as `back`, and `back` on its own is the same fall back. ChatWordsAreCommand is the chat-only gate: a bare alias
// counts only when the whole message is the command, so ordinary conversation is never an order.
//
// ONE PLAN. PlanPlace/PlanFallback return one step per selected bot in slot order: the bot, the slot, the world point, or the typed refusal. The world
// facts the plan cannot know by itself (the floor under a point, whether this bot can walk there) come in through Probe; the host answers them with
// GetHeightCompat and PathGenerator, the tests with a fake.
//
// THE HOLD. State is the commander's memory as plain data: the hold rows, the rally point and the pending cursor request, keyed by the commander's
// counter. Its rules are here so they can be tested without a world: a placement replaces only the rows of the bots it addressed and a placement that
// put nobody leaves the old hold alone; a fall back replaces the whole hold; a later command ends a pending request; logout forgets all three.
#pragma once

#include "TbPlacePure.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace TortoiseBots
{
namespace place
{
// ---- the command words ----

enum class CommandKind { None, Place, PlaceFollow, RallyHere, Fallback, Resume };

struct Command
{
    CommandKind kind = CommandKind::None;
    Shape shape = Shape::Close;
    Gap gap = Gap::Normal;
    Anchor anchor = Anchor::Here;
    std::vector<WhoToken> who;
    std::string code;       // a place verb with invalid words: the typed refusal
    std::string detail;
};

inline bool IsPlaceVerbWord(std::string_view word)
{
    std::string const w = Lower(word);
    return w == "place" || w == "rally" || w == "fallback" || w == "back" || w == "resume" || w == "fall";
}

// The words as typed, verb first (`{"fall", "back"}` is a fall back; `{"back"}` is the chat alias of the same).
inline Command ParseCommand(std::vector<std::string> const& words)
{
    Command out;
    if (words.empty() || !IsPlaceVerbWord(words[0]))
        return out;
    std::string const verb = Lower(words[0]);
    if (verb == "place")
    {
        out.kind = CommandKind::Place;
        if (words.size() <= 1)
        {
            out.code = "missing-argument";
            out.detail = "place <close|circle|arc|spread|line|column|spear|arrow> [tight|normal|wide] [here|target|cursor] [who...] | place follow [who...]";
            return out;
        }
        size_t pos = 1;
        if (Lower(words[pos]) == "follow")
        {
            out.kind = CommandKind::PlaceFollow;
            for (++pos; pos < words.size(); ++pos)
            {
                WhoToken token;
                ParseWho(words[pos], token);
                out.who.push_back(token);
            }
            return out;
        }
        if (!ParseShape(words[pos], out.shape))
        {
            out.code = "bad-argument";
            out.detail = words[pos] + " is not a shape: close, circle, arc, spread, line, column, spear, arrow, or follow";
            return out;
        }
        ++pos;
        bool haveGap = false, haveAnchor = false;
        for (; pos < words.size(); ++pos)
        {
            Gap gap;
            Anchor anchor;
            if (ParseGap(words[pos], gap))
            {
                if (haveGap)
                {
                    out.code = "duplicate";
                    out.detail = "the gap is given twice";
                    return out;
                }
                out.gap = gap;
                haveGap = true;
                continue;
            }
            if (ParseAnchor(words[pos], anchor))
            {
                if (haveAnchor)
                {
                    out.code = "duplicate";
                    out.detail = "the anchor is given twice";
                    return out;
                }
                out.anchor = anchor;
                haveAnchor = true;
                continue;
            }
            break;      // the who list starts here
        }
        for (; pos < words.size(); ++pos)
        {
            WhoToken token;
            ParseWho(words[pos], token);
            out.who.push_back(token);
        }
        return out;
    }
    if (verb == "fall")
    {
        out.kind = CommandKind::Fallback;
        if (words.size() < 2)
        {
            out.code = "missing-argument";
            out.detail = "fall back";
            return out;
        }
        if (Lower(words[1]) != "back")
        {
            out.code = "bad-argument";
            out.detail = "the only fall back form is `fall back`";
            return out;
        }
        if (words.size() > 2)
        {
            out.code = "unexpected-argument";
            out.detail = words[2];
        }
        return out;
    }
    if (verb == "rally")
    {
        out.kind = CommandKind::RallyHere;
        if (words.size() <= 1)
        {
            out.code = "missing-argument";
            out.detail = "rally here";
            return out;
        }
        if (Lower(words[1]) != "here")
        {
            out.code = "bad-argument";
            out.detail = "the only rally point is `here` (where you stand)";
            return out;
        }
        if (words.size() > 2)
        {
            out.code = "unexpected-argument";
            out.detail = words[2];
        }
        return out;
    }
    if (verb == "resume")
    {
        out.kind = CommandKind::Resume;
        if (words.size() > 1)
        {
            out.code = "unexpected-argument";
            out.detail = words[1];
        }
        return out;
    }
    out.kind = CommandKind::Fallback;       // fallback | back
    if (words.size() > 1)
    {
        out.code = "unexpected-argument";
        out.detail = words[1];
    }
    return out;
}

// The chat gate: a bare alias counts only when the whole message is the command. `place ...` is unambiguous and always the command.
inline bool ChatWordsAreCommand(std::vector<std::string> const& words)
{
    if (words.empty() || !IsPlaceVerbWord(words[0]))
        return false;
    std::string const verb = Lower(words[0]);
    if (verb == "place")
        return true;
    if (verb == "fall")
        return words.size() == 2 && Lower(words[1]) == "back";
    if (verb == "rally")
        return words.size() == 2 && Lower(words[1]) == "here";
    return (verb == "fallback" || verb == "back" || verb == "resume") && words.size() == 1;
}

// ---- the placement plan ----

// What the world says about one bot before the plan runs. Ownership is decided by the host (never trust a bot it did not check): these are the facts
// the plan needs to refuse a slot per bot.
struct BotFacts
{
    std::string name;
    std::string className;
    uint32_t group = 1;
    Rank rank = Rank::Melee;
    bool alive = true;
    bool sameMap = true;    // same map and instance as the anchor
};

// The anchor as the host resolved it (TbPlace.cpp ResolveAnchor).
struct AnchorFacts
{
    Anchor kind = Anchor::Here;
    Vec2 player;
    Vec2 playerFront{ 0.0f, 1.0f };
    Vec2 target;
    float targetHalf = 0.0f;
    Vec2 cursor;
    float z = 0.0f;
    uint32_t map = 0;
    uint32_t instance = 0;
};

// The two world questions the plan asks. `ground` fills in the floor under a point (false when there is none, or the point is off the map);
// `reachable` answers whether that bot can walk there from where it stands.
struct Probe
{
    std::function<bool(Vec2 const&, float&)> ground;
    std::function<bool(size_t bot, Vec2 const&, float z)> reachable;
};

struct Step
{
    size_t bot = 0;         // index into the facts the plan was given
    bool placed = false;
    int slot = -1;          // the slot the bot was assigned (its index in the slot order)
    Vec2 pos;
    float z = 0.0f;
    std::string code;       // the typed refusal when not placed
    std::string detail;
};

struct PlanResult
{
    bool ok = false;        // false: the whole command is refused (code/detail)
    std::string code;
    std::string detail;
    int selected = 0;
    int placed = 0;
    int refused = 0;
    Layout layout;
    Frame frame;
    std::vector<Step> steps;
};

struct Selection
{
    std::vector<size_t> order;
    std::string code;
    std::string detail;
};

// The selected bots in slot order (tanks, melee, ranged, healers; name breaks a tie). A who word that names no bot is a typed refusal.
inline Selection SelectBots(std::vector<BotFacts> const& bots, std::vector<WhoToken> const& who)
{
    Selection out;
    for (WhoToken const& token : who)
    {
        if (token.kind != WhoKind::Name)
            continue;
        bool found = false;
        for (BotFacts const& bot : bots)
            if (Lower(bot.name) == Lower(token.word))
            {
                found = true;
                break;
            }
        if (!found)
        {
            out.code = "bad-target";
            out.detail = token.word + " is not one of the bots you command";
            return out;
        }
    }
    for (size_t i = 0; i < bots.size(); ++i)
    {
        BotInfo const info{ bots[i].name, bots[i].className, bots[i].rank, bots[i].group };
        if (Selected(who, info))
            out.order.push_back(i);
    }
    if (out.order.empty())
    {
        out.code = "no-bots";
        out.detail = "none of the bots you command matches that selection";
        return out;
    }
    std::stable_sort(out.order.begin(), out.order.end(), [&](size_t a, size_t b)
    {
        return RankBefore(bots[a].rank, bots[a].name, bots[b].rank, bots[b].name);
    });
    return out;
}

// One step per selected bot: alive, on the map, a slot left, inside the ceiling, a floor and a path. A refusal never moves the bot.
inline std::vector<Step> PlanSteps(std::vector<BotFacts> const& bots, std::vector<size_t> const& order, std::vector<Slot> const& slots,
    std::vector<bool> const& usable, Frame const& frame, Probe const& probe, int& placed, int& refused)
{
    std::vector<Step> steps;
    for (size_t i = 0; i < order.size(); ++i)
    {
        Step step;
        step.bot = order[i];
        step.slot = int(i);
        BotFacts const& bot = bots[order[i]];
        auto const refuse = [&](std::string code, std::string detail)
        {
            step.code = std::move(code);
            step.detail = std::move(detail);
            ++refused;
        };
        if (!bot.alive)
            refuse("dead", "a dead bot cannot walk to its slot");
        else if (!bot.sameMap)
            refuse("other-map", "the bot is not on the commander's map and instance");
        else if (i >= slots.size() || i >= usable.size() || !usable[i])
            refuse("no-slot", "the commander's own spot leaves no slot for this bot");
        else
        {
            Vec2 const point = Place(frame, slots[i]);
            float const dx = point.x - frame.centre.x, dy = point.y - frame.centre.y;
            float const reach = std::sqrt(dx * dx + dy * dy);
            float z = 0.0f;
            if (reach > kMaxRadiusYd)
                refuse("too-far", "the slot is past the 35 yd ceiling");
            else if (!probe.ground || !probe.ground(point, z))
                refuse("no-ground", "no floor under the slot");
            else if (!probe.reachable || !probe.reachable(step.bot, point, z))
                refuse("unreachable", "no walkable path to the slot");
            else
            {
                step.placed = true;
                step.pos = point;
                step.z = z;
                ++placed;
            }
        }
        steps.push_back(std::move(step));
    }
    return steps;
}

inline std::string RingTooFar(int bots)
{
    return "the ring for " + std::to_string(bots) + " bots (or round a target that size) would be wider than 35 yd: use a smaller gap or a row shape";
}

inline PlanResult PlanPlace(Shape shape, Gap gap, AnchorFacts const& anchor, std::vector<BotFacts> const& bots,
    std::vector<WhoToken> const& who, Probe const& probe)
{
    PlanResult out;
    Selection const selection = SelectBots(bots, who);
    if (!selection.code.empty())
    {
        out.code = selection.code;
        out.detail = selection.detail;
        return out;
    }
    out.selected = int(selection.order.size());
    out.layout = LayoutShape(shape, gap, out.selected, anchor.kind == Anchor::Target ? anchor.targetHalf : 0.0f);
    if (out.layout.tooFar)
    {
        out.code = "too-far";
        out.detail = RingTooFar(out.selected);
        return out;
    }
    out.frame = FrameFor(anchor.kind, out.layout, GapYd(gap), anchor.player, anchor.playerFront, anchor.target, anchor.targetHalf, anchor.cursor);
    std::vector<Slot> slots = out.layout.slots;
    std::vector<bool> const usable = ClearCommanderSpot(slots, shape, out.frame, anchor.player, out.selected);
    out.steps = PlanSteps(bots, selection.order, slots, usable, out.frame, probe, out.placed, out.refused);
    out.ok = true;
    return out;
}

// The fall-back formation's front: from a stored rally point it faces the commander (the group came from that side), so the tanks stand nearest the
// enemy; without one it is the commander's own facing. A commander on the point falls back to its facing.
inline Vec2 FallbackFront(Vec2 centre, Vec2 commander, Vec2 facing, bool storedRally)
{
    Vec2 const fallback = Normalized(facing, { 0.0f, 1.0f });
    if (!storedRally)
        return fallback;
    return Normalized({ commander.x - centre.x, commander.y - centre.y }, fallback);
}

inline PlanResult PlanFallback(Gap gap, Vec2 centre, Vec2 commander, Vec2 facing, bool storedRally,
    std::vector<BotFacts> const& bots, Probe const& probe)
{
    PlanResult out;
    Selection const selection = SelectBots(bots, {});
    if (!selection.code.empty())
    {
        out.code = selection.code;
        out.detail = selection.detail;
        return out;
    }
    out.selected = int(selection.order.size());
    FallbackCounts counts;
    for (size_t index : selection.order)
    {
        switch (bots[index].rank)
        {
            case Rank::Tank: ++counts.tanks; break;
            case Rank::Melee: ++counts.melee; break;
            case Rank::Ranged: ++counts.ranged; break;
            case Rank::Healer: ++counts.healers; break;
        }
    }
    std::vector<Slot> slots = FallbackSlots(counts, gap);
    out.frame.centre = centre;
    out.frame.front = FallbackFront(centre, commander, facing, storedRally);
    std::vector<bool> const usable = ClearCommanderSpot(slots, Shape::Close, out.frame, commander, out.selected);
    out.steps = PlanSteps(bots, selection.order, slots, usable, out.frame, probe, out.placed, out.refused);
    out.ok = true;
    return out;
}

// ---- the hold's bookkeeping ----

// The clock: a 32-bit game clock counts up and wraps every 49.7 days, so a plain `<` freezes a timeout for half a cycle.
inline bool TimeReached(uint32_t now, uint32_t target)
{
    return int32_t(now - target) >= 0;
}

// A dropped row releases its bot with `follow` only while no newer order owns it: a higher order number than the row's `come` is a newer order.
inline bool NewerOrderOwns(uint32_t lastSeq, uint32_t rowSeq)
{
    return lastSeq > rowSeq;
}

struct GuidKey
{
    uint64_t high = 0;
    uint64_t low = 0;
    bool operator==(GuidKey const& other) const { return high == other.high && low == other.low; }
    bool operator!=(GuidKey const& other) const { return !(*this == other); }
};

struct HoldRow
{
    GuidKey bot;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    uint32_t map = 0;
    uint32_t instance = 0;
    int slot = 0;
    uint32_t seq = 0;       // the `come` order that put the bot on the slot
    Rank rank = Rank::Melee;
    uint32_t walkMs = 0;    // when the walk back was last asked for
    uint32_t walkFails = 0; // failed reachability checks since the last walk back
};

// The walk back queued a `come` with a new order number: the row names it, so the stay it leaves can be cleared later
// (h3::ReleasePlaceStay matches the row's seq against the bot's last order).
inline void AfterWalkBack(HoldRow& row, uint32_t seq, uint32_t now)
{
    row.walkMs = now;
    row.walkFails = 0;
    row.seq = seq;
}

struct Hold
{
    bool fallback = false;
    std::vector<HoldRow> rows;
};

struct RallyState
{
    bool set = false;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    uint32_t map = 0;
    uint32_t instance = 0;
};

struct PendingRequest
{
    uint32_t commander = 0;
    uint32_t deadline = 0;
    Shape shape = Shape::Close;
    Gap gap = Gap::Normal;
    std::vector<WhoToken> who;
    std::vector<std::string> words;   // the command as typed, for the reply's token
    std::string token;
    bool badToken = false;
};

// The commander's memory: hold rows, rally point and pending cursor request, keyed by the commander's counter.
struct State
{
    std::map<uint32_t, Hold> holds;
    std::map<uint32_t, RallyState> rallies;
    std::map<uint32_t, PendingRequest> pendings;
};

// A new placement replaces only the rows of the bots it addressed (placed or refused); the rows of every other bot stay. A command that placed
// nobody leaves the hold exactly as it was. The dropped rows are returned for their release.
inline std::vector<HoldRow> StorePlace(State& state, uint32_t commander, std::vector<HoldRow> const& rows,
    std::vector<GuidKey> const& addressed, bool anyPlaced)
{
    std::vector<HoldRow> dropped;
    if (!anyPlaced)
        return dropped;
    auto it = state.holds.find(commander);
    if (it == state.holds.end())
    {
        if (!rows.empty())
        {
            Hold hold;
            hold.fallback = false;
            hold.rows = rows;
            state.holds[commander] = std::move(hold);
        }
        return dropped;
    }
    std::vector<HoldRow>& held = it->second.rows;
    for (auto row = held.begin(); row != held.end();)
    {
        bool const replaced = std::any_of(addressed.begin(), addressed.end(), [&](GuidKey const& key) { return key == row->bot; });
        if (replaced)
        {
            dropped.push_back(*row);
            row = held.erase(row);
        }
        else
            ++row;
    }
    held.insert(held.end(), rows.begin(), rows.end());
    it->second.fallback = false;
    if (held.empty())
        state.holds.erase(it);
    // A row that a new row of the same bot replaced is not released: the bot already has its new order.
    dropped.erase(std::remove_if(dropped.begin(), dropped.end(), [&](HoldRow const& row)
    {
        return std::any_of(rows.begin(), rows.end(), [&](HoldRow const& fresh) { return fresh.bot == row.bot; });
    }), dropped.end());
    return dropped;
}

// A fall back forms the whole group anew: the old hold (place or fall back) is replaced, never merged.
inline std::vector<HoldRow> StoreFallback(State& state, uint32_t commander, std::vector<HoldRow> const& rows)
{
    std::vector<HoldRow> dropped;
    auto it = state.holds.find(commander);
    if (it != state.holds.end())
    {
        dropped = std::move(it->second.rows);
        state.holds.erase(it);
    }
    if (!rows.empty())
    {
        Hold hold;
        hold.fallback = true;
        hold.rows = rows;
        state.holds[commander] = std::move(hold);
    }
    // Only the rows no new order replaced are released.
    dropped.erase(std::remove_if(dropped.begin(), dropped.end(), [&](HoldRow const& row)
    {
        return std::any_of(rows.begin(), rows.end(), [&](HoldRow const& fresh) { return fresh.bot == row.bot; });
    }), dropped.end());
    return dropped;
}

inline std::vector<HoldRow> DropRows(State& state, uint32_t commander, std::function<bool(HoldRow const&)> const& drop)
{
    std::vector<HoldRow> dropped;
    auto it = state.holds.find(commander);
    if (it == state.holds.end())
        return dropped;
    std::vector<HoldRow>& rows = it->second.rows;
    for (auto row = rows.begin(); row != rows.end();)
    {
        if (drop(*row))
        {
            dropped.push_back(*row);
            row = rows.erase(row);
        }
        else
            ++row;
    }
    if (rows.empty())
        state.holds.erase(it);
    return dropped;
}

// A later command from the commander ends a pending cursor request; the caller answers its token with `cancelled`.
inline bool TakePending(State& state, uint32_t commander, PendingRequest& out)
{
    auto it = state.pendings.find(commander);
    if (it == state.pendings.end())
        return false;
    out = it->second;
    state.pendings.erase(it);
    return true;
}

// Logout: the commander's rallies, holds and pending request all go.
inline void Forget(State& state, uint32_t commander)
{
    state.holds.erase(commander);
    state.rallies.erase(commander);
    state.pendings.erase(commander);
}

// The walk back: a slot that stays unreachable is given up after this many checks, and said once.
constexpr uint32_t kWalkBackGiveUps = 3;

inline bool WalkGiveUp(uint32_t fails)
{
    return fails >= kWalkBackGiveUps;
}

// Why a hold row ended. Every terminal reason releases the bot: still commanded by this commander, the release is the module's own `follow`; a bot that
// changed commander is only cleared of the stay this placement wrote (never given an order by its old commander).
enum class RowEnd { None, Dead, Teleport, OtherMap, Commander, Run, NewerOrder, Unreachable };
enum class Release { None, Follow, ClearStay };

inline Release ReleaseOn(RowEnd end, bool stillCommanded)
{
    switch (end)
    {
        case RowEnd::None:
            return Release::None;
        case RowEnd::Run:
        case RowEnd::NewerOrder:
            // Another driver owns the bot's movement now: never queue an order that would fight it.
            return Release::ClearStay;
        default:
            return stillCommanded ? Release::Follow : Release::ClearStay;
    }
}
}
}

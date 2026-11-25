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

enum class CommandKind { None, Place, PlaceFollow, RallyShow, RallyHere, RallyClear, RallyAt, Fallback, Resume };
enum class RallyWord { Show, Here, Clear, At };
enum class FallbackTo { Default, Rally, Here, At };

struct Command
{
    CommandKind kind = CommandKind::None;
    Shape shape = Shape::Close;
    Gap gap = Gap::Normal;          // the preset the gap word named (Normal for a number)
    GapSpec gapSpec;
    Anchor anchor = Anchor::Here;
    bool hasFace = false;
    int faceDeg = 0;
    Vec2 point;
    float pointZ = 0.0f;
    bool pointHasZ = false;
    RallyWord rallyWord = RallyWord::Show;
    FallbackTo fallbackTo = FallbackTo::Default;
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
//
// `place <shape> [<gap>] [<anchor>] [face:<deg>] [who...]`: the options come in any order between the shape and the first who word, each at most once.
// A bare number is the gap (no who word is a number). `rally` alone is the read-back; `fallback`/`fall back` take at most one target word.
inline Command ParseCommand(std::vector<std::string> const& words)
{
    Command out;
    if (words.empty() || !IsPlaceVerbWord(words[0]))
        return out;
    std::string const verb = Lower(words[0]);
    auto const parsePointWord = [&](std::string const& word) -> bool
    {
        if (!ParsePoint(std::string_view(word).substr(3), out.point, out.pointZ, out.pointHasZ))
        {
            out.code = "bad-argument";
            out.detail = "a point is at:<x>,<y> or at:<x>,<y>,<z> (two or three numbers)";
            return false;
        }
        return true;
    };
    auto const parseFallbackTail = [&out](std::vector<std::string> const& words, size_t first) -> void
    {
        if (words.size() <= first)
            return;
        if (words.size() > first + 1)
        {
            out.code = "unexpected-argument";
            out.detail = words[first + 1];
            return;
        }
        std::string const word = Lower(words[first]);
        if (word == "rally" || word == "last")
            out.fallbackTo = FallbackTo::Rally;
        else if (word == "here")
            out.fallbackTo = FallbackTo::Here;
        else if (word.compare(0, 3, "at:") == 0)
        {
            if (ParsePoint(std::string_view(words[first]).substr(3), out.point, out.pointZ, out.pointHasZ))
                out.fallbackTo = FallbackTo::At;
            else
            {
                out.code = "bad-argument";
                out.detail = "a point is at:<x>,<y> or at:<x>,<y>,<z> (two or three numbers)";
            }
        }
        else
        {
            out.code = "bad-argument";
            out.detail = "fallback takes rally, last, here or at:<x>,<y>[,<z>]";
        }
    };
    if (verb == "place")
    {
        out.kind = CommandKind::Place;
        if (words.size() <= 1)
        {
            out.code = "missing-argument";
            out.detail = "place <close|circle|arc|spread|line|column|spear|arrow> [tight|normal|wide|<yd>] [here|target|cursor|tank|at:<x>,<y>[,<z>]] [face:<deg>] [who...] | place follow [who...]";
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
        bool haveGap = false, haveAnchor = false, haveFace = false;
        for (; pos < words.size(); ++pos)
        {
            std::string const lower = Lower(words[pos]);
            GapSpec spec;
            if (ParseGapWord(words[pos], spec))
            {
                if (haveGap)
                {
                    out.code = "duplicate";
                    out.detail = "the gap is given twice";
                    return out;
                }
                out.gapSpec = spec;
                out.gap = spec.preset;
                haveGap = true;
                continue;
            }
            if (LooksNumeric(words[pos]))
            {
                out.code = "bad-argument";
                out.detail = "the gap is 1.5 to 12 yd";
                return out;
            }
            if (lower.compare(0, 5, "face:") == 0)
            {
                if (haveFace)
                {
                    out.code = "duplicate";
                    out.detail = "the facing is given twice";
                    return out;
                }
                if (!ParseFace(std::string_view(words[pos]).substr(5), out.faceDeg))
                {
                    out.code = "bad-argument";
                    out.detail = "face:<deg> is 0 to 359 degrees";
                    return out;
                }
                out.hasFace = true;
                haveFace = true;
                continue;
            }
            if (lower.compare(0, 3, "at:") == 0)
            {
                if (haveAnchor)
                {
                    out.code = "duplicate";
                    out.detail = "the anchor is given twice";
                    return out;
                }
                if (!parsePointWord(words[pos]))
                    return out;
                out.anchor = Anchor::Point;
                haveAnchor = true;
                continue;
            }
            Anchor anchor;
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
        if (out.hasFace && out.anchor == Anchor::Target)
        {
            out.code = "bad-argument";
            out.detail = "the target anchor faces the target; face: is for here, cursor, tank and at:";
            return out;
        }
        if (out.anchor == Anchor::Tank && out.shape != Shape::Circle && out.shape != Shape::Arc && out.shape != Shape::Spread)
        {
            out.code = "bad-argument";
            out.detail = "the tank anchor takes circle, arc or spread";
            return out;
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
        parseFallbackTail(words, 2);
        return out;
    }
    if (verb == "rally")
    {
        out.kind = CommandKind::RallyShow;
        out.rallyWord = RallyWord::Show;
        if (words.size() <= 1)
            return out;
        if (words.size() > 2)
        {
            out.code = "unexpected-argument";
            out.detail = words[2];
            return out;
        }
        std::string const word = Lower(words[1]);
        if (word == "here")
        {
            out.kind = CommandKind::RallyHere;
            out.rallyWord = RallyWord::Here;
            return out;
        }
        if (word == "clear")
        {
            out.kind = CommandKind::RallyClear;
            out.rallyWord = RallyWord::Clear;
            return out;
        }
        if (word.compare(0, 3, "at:") == 0)
        {
            if (!parsePointWord(words[1]))
                return out;
            out.kind = CommandKind::RallyAt;
            out.rallyWord = RallyWord::At;
            return out;
        }
        out.code = "bad-argument";
        out.detail = "rally takes here, clear or at:<x>,<y>[,<z>]; no word reads the point back";
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
    if (verb == "back")
    {
        if (words.size() > 1)
        {
            out.code = "unexpected-argument";
            out.detail = words[1];
        }
        return out;
    }
    parseFallbackTail(words, 1);
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
        return words.size() == 1 || (words.size() == 2 && (Lower(words[1]) == "here" || Lower(words[1]) == "clear"));
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
    float z = 0.0f;             // the floor search's base under a slot (the anchor's own z)
    float anchorZ = 0.0f;       // the lane rule's anchor: the ledge test is against this
    uint32_t map = 0;
    uint32_t instance = 0;
    Vec2 tankPos;
    Vec2 tankFacing{ 0.0f, 1.0f };
    float tankReach = 0.0f;
    Vec2 point;
    bool hasFace = false;
    Vec2 faceFront{ 0.0f, 1.0f };
};

// What a path check found: the move rule's own verdict, so the plan only names the refusal. Defined in TbPlacePure.h.

// The world questions the plan asks. `ground` fills in the floor under a point (false when there is none, or the point is off the map);
// `liquid` is true when a stand point is refused by the run's one liquid rule (water at or above the floor, lava, slime); `reachable` is the
// path verdict (NoPath, a path that enters liquid, or a detour).
struct Probe
{
    std::function<bool(Vec2 const&, float&)> ground;
    std::function<bool(Vec2 const&, float z)> liquid;
    std::function<PathVerdict(size_t bot, Vec2 const&, float z)> reachable;
};

// The detour rule lives in TbPlacePure.h (`IsDetour`, shared with the walk-back verdict).

struct Step
{
    size_t bot = 0;         // index into the facts the plan was given
    bool placed = false;
    int slot = -1;          // the slot the bot was assigned (its index in the slot order); -1 when sent to a fall-back centre
    Vec2 pos;
    float z = 0.0f;
    float nudged = 0.0f;    // the slot was moved this far towards the frame centre
    std::string to;         // a fall-back refusal that still disengaged: centre | commander
    std::string code;       // the typed refusal when not placed (or the slot's refusal on a to= row)
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
// `exclude` is the anchor tank's name: the tank is still a bot the words may name (never `bad-target`), but it is never a step and
// never takes a slot (it holds its own mob). A selection left empty by the exclusion is `no-bots`.
inline Selection SelectBots(std::vector<BotFacts> const& bots, std::vector<WhoToken> const& who, std::string_view exclude = {})
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
        if (!exclude.empty() && Lower(bots[i].name) == Lower(exclude))
            continue;
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

// The one rule list for a stand point, in order: a floor, the ledge step, a dry stand, a path, a dry path, a detour. The first failing rule names
// the code. `too-far` is the slot's own rule (it needs the frame centre) and is asked before this.
inline bool JudgeSlot(Vec2 point, float anchorZ, size_t bot, Probe const& probe, std::string& code, float& z)
{
    z = 0.0f;
    if (!probe.ground || !probe.ground(point, z))
    {
        code = "no-ground";
        return false;
    }
    if (std::fabs(z - anchorZ) > kMaxSlotStepYd)
    {
        code = "ledge";
        return false;
    }
    if (probe.liquid && probe.liquid(point, z))
    {
        code = "liquid";
        return false;
    }
    if (!probe.reachable)
    {
        code = "unreachable";
        return false;
    }
    switch (probe.reachable(bot, point, z))
    {
        case PathVerdict::Ok:
            break;
        case PathVerdict::NoPath:
            code = "unreachable";
            return false;
        case PathVerdict::LiquidPath:
            code = "liquid-path";
            return false;
        case PathVerdict::Detour:
            code = "detour";
            return false;
    }
    code.clear();
    return true;
}

inline char const* SlotDetail(std::string_view code)
{
    if (code == "dead") return "a dead bot cannot walk to its slot";
    if (code == "other-map") return "the bot is not on the commander's map and instance";
    if (code == "no-slot") return "the commander's or the tank's own spot leaves no slot for this bot";
    if (code == "too-far") return "the slot is past the 35 yd ceiling";
    if (code == "no-ground") return "no floor under the slot";
    if (code == "ledge") return "the slot is more than 5 yd above or below the anchor";
    if (code == "liquid") return "the slot is in water or lava";
    if (code == "liquid-path") return "the path enters water or lava";
    if (code == "unreachable") return "no walkable path to the slot";
    if (code == "detour") return "the path is too long for the distance";
    return "";
}

// The extra rules a plan adds to the common one. `guardCentre`/`guardHalf`: for a `target` ring a nudge may not walk a
// bot inside the target (nearer than its combat reach). `nudgeCleared`: a fall back may nudge a slot whose only failure
// is the commander's or the tank's own spot; a place leaves such a slot empty (round-1 rule).
struct StepRules
{
    Vec2 guardCentre;
    float guardHalf = 0.0f;
    bool nudgeCleared = false;
};

// One step per selected bot: alive, on the map, a slot left, inside the ceiling, then the stand-point rules; a refused slot is nudged up to three
// times towards the frame centre. A refusal never moves the bot (a fall back turns it into a to= row, PlanFallback).
inline std::vector<Step> PlanSteps(std::vector<BotFacts> const& bots, std::vector<size_t> const& order, std::vector<Slot> const& slots,
    std::vector<bool> const& usable, Frame const& frame, Probe const& probe, float anchorZ, float gapYd,
    std::vector<Vec2> const& cleared, StepRules const& rules, int& placed, int& refused)
{
    std::vector<Step> steps;
    std::vector<Vec2> accepted;
    for (size_t i = 0; i < order.size(); ++i)
    {
        Step step;
        step.bot = order[i];
        step.slot = int(i);
        BotFacts const& bot = bots[order[i]];
        auto const refuse = [&](std::string code)
        {
            step.code = code;
            step.detail = SlotDetail(code);
            ++refused;
        };
        bool const haveSlot = i < slots.size() && i < usable.size();
        bool const slotUsable = haveSlot && usable[i];
        if (!bot.alive)
            refuse("dead");
        else if (!bot.sameMap)
            refuse("other-map");
        else if (!haveSlot)
            refuse("no-slot");
        else
        {
            Vec2 const point = Place(frame, slots[i]);
            float const dx = point.x - frame.centre.x, dy = point.y - frame.centre.y;
            float const reach = std::sqrt(dx * dx + dy * dy);
            float z = 0.0f;
            std::string code;
            bool ok = false;
            if (slotUsable)
                ok = reach <= kMaxRadiusYd && JudgeSlot(point, anchorZ, step.bot, probe, code, z);
            if (!slotUsable)
                code = "no-slot";
            if (reach > kMaxRadiusYd)
                code = "too-far";
            if (ok)
            {
                step.placed = true;
                step.pos = point;
                step.z = z;
                accepted.push_back(point);
                ++placed;
            }
            else if (code != "too-far" && code != "no-ground" && (slotUsable || rules.nudgeCleared))
            {
                // The nudge: the slot's own straight line towards the frame centre, 1, 2, 3 yd; the first dry, reachable, free candidate wins.
                Vec2 const inward{ frame.centre.x - point.x, frame.centre.y - point.y };
                float const distance = std::sqrt(inward.x * inward.x + inward.y * inward.y);
                for (float const stepYd : kNudgeStepsYd)
                {
                    if (!(distance > 1e-3f))
                        break;
                    Vec2 const candidate{ point.x + inward.x / distance * stepYd, point.y + inward.y / distance * stepYd };
                    float const cx = candidate.x - frame.centre.x, cy = candidate.y - frame.centre.y;
                    if (std::sqrt(cx * cx + cy * cy) > kMaxRadiusYd)
                        continue;
                    if (rules.guardHalf > 0.0f)
                    {
                        float const gx = candidate.x - rules.guardCentre.x, gy = candidate.y - rules.guardCentre.y;
                        if (std::sqrt(gx * gx + gy * gy) < rules.guardHalf)
                            continue;
                    }
                    bool free = true;
                    for (Vec2 const& spot : cleared)
                    {
                        float const ex = candidate.x - spot.x, ey = candidate.y - spot.y;
                        if (ex * ex + ey * ey < kCommanderClearYd * kCommanderClearYd)
                        {
                            free = false;
                            break;
                        }
                    }
                    for (Vec2 const& other : accepted)
                    {
                        float const ex = candidate.x - other.x, ey = candidate.y - other.y;
                        if (float const spacing = 0.5f * gapYd; ex * ex + ey * ey < spacing * spacing)
                        {
                            free = false;
                            break;
                        }
                    }
                    if (!free)
                        continue;
                    float candidateZ = 0.0f;
                    std::string candidateCode;
                    if (!JudgeSlot(candidate, anchorZ, step.bot, probe, candidateCode, candidateZ))
                        continue;
                    step.placed = true;
                    step.pos = candidate;
                    step.z = candidateZ;
                    step.nudged = stepYd;
                    accepted.push_back(candidate);
                    ++placed;
                    break;
                }
                if (!step.placed)
                    refuse(code);
            }
            else
                refuse(code);
        }
        steps.push_back(std::move(step));
    }
    return steps;
}

inline std::string RingTooFar(int bots)
{
    return "the ring for " + std::to_string(bots) + " bots (or round a target that size) would be wider than 35 yd: use a smaller gap or a row shape";
}

inline PlanResult PlanPlace(Shape shape, GapSpec const& gap, AnchorFacts const& anchor, std::vector<BotFacts> const& bots,
    std::vector<WhoToken> const& who, Probe const& probe, std::string_view anchorName = {})
{
    PlanResult out;
    Selection const selection = SelectBots(bots, who, anchorName);
    if (!selection.code.empty())
    {
        out.code = selection.code;
        out.detail = selection.detail;
        return out;
    }
    out.selected = int(selection.order.size());
    if (anchor.kind == Anchor::Point)
    {
        float const dx = anchor.point.x - anchor.player.x, dy = anchor.point.y - anchor.player.y;
        if (std::sqrt(dx * dx + dy * dy) > kPointReachYd)
        {
            out.code = "too-far";
            out.detail = "the point is more than 100 yd from you";
            return out;
        }
    }
    float const targetHalf = anchor.kind == Anchor::Target ? anchor.targetHalf : (anchor.kind == Anchor::Tank ? anchor.tankReach : 0.0f);
    out.layout = LayoutShape(shape, gap.yd, out.selected, targetHalf);
    if (out.layout.tooFar)
    {
        out.code = "too-far";
        out.detail = RingTooFar(out.selected);
        return out;
    }
    FrameInput in;
    in.player = anchor.player;
    in.playerFront = anchor.playerFront;
    in.target = anchor.target;
    in.targetHalf = anchor.targetHalf;
    in.cursor = anchor.cursor;
    in.tank = anchor.tankPos;
    in.tankFacing = anchor.tankFacing;
    in.tankReach = anchor.tankReach;
    in.point = anchor.point;
    in.hasFace = anchor.hasFace;
    in.faceFront = anchor.faceFront;
    out.frame = FrameFor(anchor.kind, out.layout, gap.yd, in);
    std::vector<Slot> slots = out.layout.slots;
    std::vector<Vec2> spots{ anchor.player };
    if (anchor.kind == Anchor::Tank)
        spots.push_back(anchor.tankPos);
    std::vector<bool> const usable = ClearSpots(slots, shape, out.frame, spots, out.selected);
    StepRules rules;
    if (anchor.kind == Anchor::Target && anchor.targetHalf > 0.0f)
    {
        // A ring nudge must never walk a bot into the target's own body.
        rules.guardCentre = anchor.target;
        rules.guardHalf = anchor.targetHalf;
    }
    out.steps = PlanSteps(bots, selection.order, slots, usable, out.frame, probe, anchor.anchorZ, gap.yd, spots, rules, out.placed, out.refused);
    out.ok = true;
    return out;
}

inline PlanResult PlanPlace(Shape shape, Gap gap, AnchorFacts const& anchor, std::vector<BotFacts> const& bots,
    std::vector<WhoToken> const& who, Probe const& probe, std::string_view anchorName = {})
{
    GapSpec spec;
    spec.preset = gap;
    spec.yd = GapYd(gap);
    return PlanPlace(shape, spec, anchor, bots, who, probe, anchorName);
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

// Where a fall back forms, from the word the commander used. `rally`/`last` insist on the stored point (never the commander's position);
// `here` ignores it; `at:` is a point the host has already judged.
struct FallbackChoice
{
    bool ok = true;
    std::string code;
    std::string detail;
    Vec2 centre;
    float z = 0.0f;
    bool frontStored = false;
    std::string from = "commander";
};

inline FallbackChoice ChooseFallback(FallbackTo to, bool stored, Vec2 storedPoint, float storedZ,
    Vec2 commander, float commanderZ, Vec2 point, float pointZ)
{
    FallbackChoice out;
    out.centre = commander;
    out.z = commanderZ;
    switch (to)
    {
        case FallbackTo::Default:
            if (stored)
            {
                out.centre = storedPoint;
                out.z = storedZ;
                out.frontStored = true;
                out.from = "memory";
            }
            break;
        case FallbackTo::Rally:
            if (!stored)
            {
                out.ok = false;
                out.code = "no-rally";
                out.detail = "no rally point on this map (.tb cmd rally here)";
                break;
            }
            out.centre = storedPoint;
            out.z = storedZ;
            out.frontStored = true;
            out.from = "memory";
            break;
        case FallbackTo::Here:
            break;      // the commander's position and facing, whatever is stored
        case FallbackTo::At:
            out.centre = point;
            out.z = pointZ;
            out.frontStored = true;
            out.from = "point";
            break;
    }
    return out;
}

inline PlanResult PlanFallback(GapSpec const& gap, Vec2 centre, Vec2 commander, Vec2 facing, bool storedRally,
    float anchorZ, float commanderZ, std::vector<BotFacts> const& bots, Probe const& probe, std::string_view anchorName = {})
{
    PlanResult out;
    Selection const selection = SelectBots(bots, {}, anchorName);
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
    std::vector<Slot> slots = FallbackSlots(counts, gap.yd);
    out.frame.centre = centre;
    out.frame.front = FallbackFront(centre, commander, facing, storedRally);
    std::vector<Vec2> const spots{ commander };
    std::vector<bool> const usable = ClearSpots(slots, Shape::Close, out.frame, spots, out.selected);
    StepRules rules;
    rules.nudgeCleared = true;      // a fall back may nudge a slot off the commander's own spot
    out.steps = PlanSteps(bots, selection.order, slots, usable, out.frame, probe, anchorZ, gap.yd, spots, rules, out.placed, out.refused);

    // A fall back is an emergency: a bot whose slot still fails after the nudge is sent to the centre (when the centre itself is standable) or to
    // the commander, so it drops its target instead of fighting where it stood. Both destinations are judged first: an unstandable point is not
    // an emergency destination, and a bot that cannot be sent anywhere safe stays refused (only death, another map and the order queue also do).
    for (Step& step : out.steps)
    {
        if (step.placed || step.code == "dead" || step.code == "other-map")
            continue;
        std::string const refusal = step.code, detail = step.detail;
        float z = 0.0f;
        std::string code;
        if (JudgeSlot(centre, anchorZ, step.bot, probe, code, z))
        {
            step.pos = centre;
            step.z = z;
            step.to = "centre";
        }
        else
        {
            float commanderPointZ = 0.0f;
            std::string commanderCode;
            if (!JudgeSlot(commander, commanderZ, step.bot, probe, commanderCode, commanderPointZ))
            {
                step.code = refusal;
                step.detail = detail;
                continue;
            }
            step.pos = commander;
            step.z = commanderPointZ;
            step.to = "commander";
        }
        step.placed = true;
        step.slot = -1;
        step.code = refusal;
        step.detail = detail;
        ++out.placed;
        if (out.refused > 0)
            --out.refused;
    }
    out.ok = true;
    return out;
}

inline PlanResult PlanFallback(Gap gap, Vec2 centre, Vec2 commander, Vec2 facing, bool storedRally,
    std::vector<BotFacts> const& bots, Probe const& probe, std::string_view anchorName = {})
{
    GapSpec spec;
    spec.preset = gap;
    spec.yd = GapYd(gap);
    return PlanFallback(spec, centre, commander, facing, storedRally, 0.0f, 0.0f, bots, probe, anchorName);
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
    uint32_t seq = 0;       // the `come` order that last moved the bot (a walk-back re-queues it under a new number)
    uint32_t passiveSeq = 0; // the `come` order that owns the row's emergency passive: the fall back's own, never a walk-back's newer number
    Rank rank = Rank::Melee;
    uint32_t walkMs = 0;    // when the walk back was last asked for
    uint32_t disengageMs = 0; // a fall back row: when its passive hold started (the cap's clock; a walk back never restarts it)
    uint32_t walkFails = 0; // failed reachability checks since the last walk back
    bool disengage = false; // a fall back row: passive until the bot reaches the spot
    bool placed = false;    // a placement row: its `come` keeps the combat-time behaviour (no Reset, no combat `stay`)
};

// A `come` order's effects, decided by its kind and the bot's combat state. Every order that is not a placement behaves as it always has: the engine's
// per-fight state is reset before the walk. Only a placement (`.tb cmd place`) keeps its target in a fight and keeps `stay` out of the combat state, so
// reach/LOS micro-movement still works; a fall back additionally stops the swing/cast/pet attack and holds the bot passive on the way.
struct ComePlan
{
    bool stopFight = false;         // AttackStop + InterruptNonMeleeSpells + the pet's AttackStop, before the reset
    bool reset = true;              // the engine rebuilds its per-fight state
    bool passive = false;           // held out of the fight on the way (a fall back)
    bool keepCombatStayOut = false; // `stay` is dropped from the combat state (a placement)
};

inline ComePlan PlanCome(bool placed, bool disengage, bool inCombat)
{
    ComePlan out;
    out.stopFight = disengage;
    out.passive = disengage;
    // A run's walk, a rally and a plain `come` reset in combat exactly as before: the stay strategy's "return to stay position" must answer the pull.
    out.reset = !placed || !inCombat;
    out.keepCombatStayOut = placed && !disengage;
    return out;
}

// The fall-back passivity's two ends besides the row being dropped: the bot is at its spot, or it has stopped short. A bot that is in a fight and no longer
// walking has stopped short (a blocked or crowded spot): after a short grace it fights again. The cap is the backstop for a bot that never stops moving:
// the emergency hold must never outlive the walk.
constexpr uint32_t kDisengageStoppedShortMs = 1500;
constexpr uint32_t kDisengageMaxMs = 10000;

inline bool DisengageEnds(float distance, bool inCombat, bool walkEnded, uint32_t elapsedMs)
{
    if (distance <= kSlotToleranceYd)
        return true;
    if (inCombat && walkEnded && elapsedMs >= kDisengageStoppedShortMs)
        return true;
    return elapsedMs >= kDisengageMaxMs;
}

// The walk back re-queues an off-slot idle bot's `come` at most this often (place::Update).
constexpr uint32_t kWalkBackEveryMs = 5000u;

// A new row: the walk-back clock and the fall back's own clock both start with the row's `come`, and a fall back's `come` also owns the
// emergency passive it sets (`disengage` is read here, so it is set before this call).
inline void StartRowClock(HoldRow& row, uint32_t now)
{
    row.walkMs = now;
    row.disengageMs = now;
    row.passiveSeq = row.disengage ? row.seq : 0;
}

// F1b (live 11-24): the fall back's passive ends at its spot, when the bot stops short in a fight, or at the cap, all counted from the
// fall back's start (`disengageMs`). The walk back restarts `walkMs` every kWalkBackEveryMs while the bot is idle off its slot, so a
// cap on `walkMs` never fired for a bot that stood still out of combat: the passive and the row's re-queued `come` went on until `resume`.
inline bool RowDisengageEnds(HoldRow const& row, float distance, bool inCombat, bool walkEnded, uint32_t now)
{
    return row.disengage && DisengageEnds(distance, inCombat, walkEnded, now - row.disengageMs);
}

// The row's hold is the `stay` its own `come` wrote. A whisper shortcut (`follow`, `free`, ...) runs in the engine's chat path: it drops the stay
// and advances no order number, so the row's `come` is still the bot's last order while the stay is gone - the row then survived, and the walk
// back queued another `come` that pulled the bot back to the old slot (#316). The row ends on the stay being gone, unless its own `come` is still
// waiting in the queue (its stay has not been written yet) or there is no engine to read the strategy from (`stayRead` false).
inline bool RowStayGone(bool stayRead, bool stayActive, bool ownComeQueued)
{
    return stayRead && !stayActive && !ownComeQueued;
}

// F1 (cold review): the fall back's passive is the row's own only when the row added it. A bot the owner already held passive
// (`.bot co +passive`, a `hold`, a `flee`) keeps that passive when the fall back ends, and the row's legal walk-back `come`
// (`carried`: the bot already carries this row's record) takes the record on under its new order number.
inline bool PlaceClaimsPassive(bool passiveAlreadyOn, bool carried)
{
    return carried || !passiveAlreadyOn;
}

// F1: an order that sets `passive` itself: `hold`, `flee` (its chat shortcut sets it in both states), an owner's `co/nc +passive`,
// and a fall back's own `come` (the disengage one; a plain `come` sets no passive).
inline bool OrderSetsPassive(std::string_view verb, std::string_view arg, bool disengage)
{
    if (verb == "come")
        return disengage;
    if (verb == "hold" || verb == "flee")
        return true;
    if (verb == "co" || verb == "nc")
        return arg.find("+passive") != std::string_view::npos;
    return false;
}

// F1: whether a fall back's exit may drop `passive` now. `claimed` is the row's record (this `come` set the passive itself);
// `lastPassiveSeq` is the number of the bot's last order that sets `passive` itself (0: none, or one that is not newer than the row).
// The row drops only what it set, and never while a newer order that sets passive itself is the bot's last one: `flee` and `hold`
// keep their own, and so does an owner's `+passive`. The row's own `come` (the walk-back carries the row's number) and any newer
// order that sets no passive leave the release to the row, so the 21 Nov rule "passive is released on every exit path" still holds
// for the fall back's own passive.
inline bool PlacePassiveReleasable(bool claimed, uint32_t rowSeq, uint32_t lastPassiveSeq)
{
    if (!claimed)
        return false;
    return lastPassiveSeq == 0 || lastPassiveSeq <= rowSeq;
}

// The walk back queued a `come` with a new order number: the row names it, so the stay it leaves can be cleared later
// (h3::ReleasePlaceStay matches the row's seq against the bot's last order). The emergency passive's owner (`passiveSeq`) is NOT moved: that
// number belongs to the fall back's own `come`, and the order record still names it (`ReleasePlacePassive` matches by that number).
inline void AfterWalkBack(HoldRow& row, uint32_t seq, uint32_t now)
{
    row.walkMs = now;
    row.walkFails = 0;
    row.seq = seq;
}

// The order number that owns the row's emergency passive. A fall back's own `come` set it (StartRowClock); a walk-back moved only `seq`, and the
// engine's record kept the first. Releasing with the newest movement number finds no record, reports "nothing to release" and leaves the bot
// passive for good (#316): every passive release asks this.
inline uint32_t PassiveOwner(HoldRow const& row)
{
    return row.passiveSeq ? row.passiveSeq : row.seq;
}

// The end of a fall back's hold: the row stops disengaging only when its emergency passive is really gone (released, or held by an order that is
// not the row's). A release that could not run (the bot's engine is away) keeps both fields, so the next 1 Hz pass retries it.
inline bool DisengageSettled(HoldRow& row, bool passiveGone)
{
    if (!passiveGone)
        return false;
    row.disengage = false;
    row.passiveSeq = 0;
    return true;
}

// The bot's record of the emergency passive (BotRec in TbOrders.cpp), pure so the row and the order bookkeeping are tested as one state. `owner`
// is the row's token - the fall back `come` whose `passive` is on (0: none of a fall back's); `last` is the token of the last order that set
// `passive` itself (0: none, or older than the row). A token, not the order number: a walk back re-queues the movement under a new number and
// must not look like a newer owner.
struct PassiveBook
{
    uint32_t owner = 0;
    uint32_t last = 0;
};

// A fall back `come` that just ran added the passive. It owns it only when it added it (PlaceClaimsPassive): a bot the owner already held
// passive keeps that passive, and the row's own walk back carries the row's token instead of taking the record over under its newer number.
// Returns the owner to record.
inline uint32_t RanComePassiveOwner(PassiveBook const& book, bool alreadyPassive, uint32_t seq)
{
    bool const carried = book.owner != 0;
    if (!PlaceClaimsPassive(alreadyPassive, carried))
        return book.owner;
    return carried ? book.owner : seq;
}

// An order that just ran, for the record. A fall back `come` (a walk back included) records the token of the `come` that owns the passive, never
// its own newer number; any other order that sets `passive` itself (`hold`, `flee`, `co/nc +passive`) becomes the newer one and takes the row's
// record away. Failures are no record.
inline void NoteRanPassive(PassiveBook& book, std::string_view verb, std::string_view arg, bool disengage, uint32_t seq)
{
    if (!OrderSetsPassive(verb, arg, disengage))
        return;
    if (verb == "come")
    {
        book.last = book.owner;
        return;
    }
    book.last = seq;
    book.owner = 0;
}

// The release of a fall back's passive by the row's owner token. Returns whether the host drops `passive` now: true only when the record is the
// row's and no newer order that sets `passive` itself took it over. The record is consumed either way; a token that is not the record's leaves
// another row's (or another order's) passive alone.
inline bool TakeOwnerPassive(PassiveBook& book, uint32_t token)
{
    if (book.owner != token)
        return false;
    bool const drop = PlacePassiveReleasable(true, token, book.last);
    book.owner = 0;
    return drop;
}

// A walk back carries the row's owner token on the queued `come`. The come may add `passive` only while that token still owns the record: the
// 10 s cap (the order lives 15 s) or an owner's `co -passive` released it, and the row is over then. Without this the late come claimed the record
// with its own newer number and turned the bot passive for good (#321 review). A token of 0 is a fall back's own come, which always may.
inline bool ComeKeepsPassive(uint32_t token, PassiveBook const& book)
{
    return token == 0 || book.owner == token;
}

// The come's passive step as the engine sees it: `PassiveOn` answers whether the bot carries `passive` in any of the three states NOW, `AddPassive`
// adds it in all three. The read happens BEFORE the add: a bot that was already passive keeps that passive's record (`hold`, `flee`, an owner's
// `+passive`), and the come owns only the passive it added itself. `book` is the bot's record, null when it has none (the passive is still added,
// there is simply nothing to release later).
template <typename Engine>
inline void ApplyComePassive(Engine& engine, PassiveBook* book, uint32_t seq, uint32_t token)
{
    if (book && !ComeKeepsPassive(token, *book))
        return;
    bool const alreadyPassive = engine.PassiveOn();
    engine.AddPassive();
    if (book)
        book->owner = RanComePassiveOwner(*book, alreadyPassive, seq);
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
    GapSpec gap;
    bool hasFace = false;
    int faceDeg = 0;
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

// The stored rally point that applies on this map and instance, or null. A point on another map is no point at all: `fallback
// last` refuses rather than relabelling the old coordinates with the current map.
inline RallyState const* StoredRallyFor(State const& state, uint32_t commander, uint32_t map, uint32_t instance)
{
    auto const it = state.rallies.find(commander);
    if (it == state.rallies.end() || !it->second.set)
        return nullptr;
    if (it->second.map != map || it->second.instance != instance)
        return nullptr;
    return &it->second;
}

// What `rally here` / `rally at:` would store. The decision is made before any state is touched: a refused `rally at:` answers the
// typed error and leaves the stored point (absent or older) exactly as it was. The host commits `value` only when `ok`.
struct RallyDecision
{
    bool ok = true;
    std::string code;
    std::string detail;
    RallyState value;
};

// `judgeAt(point, z, code, detail)` is the host's typed-point rule (ground, liquid, reach, path); it is only called for `At`.
inline RallyDecision DecideRally(RallyWord word, Vec2 here, float hereZ, uint32_t map, uint32_t instance, Vec2 point,
    std::function<bool(Vec2&, float&, std::string&, std::string&)> const& judgeAt)
{
    RallyDecision out;
    Vec2 at = point;
    float atZ = 0.0f;
    if (word == RallyWord::At)
    {
        if (!judgeAt || !judgeAt(at, atZ, out.code, out.detail))
        {
            out.ok = false;
            if (out.code.empty())
            {
                out.code = "no-ground";
                out.detail = "no floor under the point";
            }
            return out;
        }
    }
    out.value.set = true;
    out.value.map = map;
    out.value.instance = instance;
    out.value.x = word == RallyWord::At ? at.x : here.x;
    out.value.y = word == RallyWord::At ? at.y : here.y;
    out.value.z = word == RallyWord::At ? atZ : hereZ;
    return out;
}

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
// changed commander is only cleared of the stay this placement wrote (never given an order by its old commander). `StayGone`: the bot's movement
// strategy is no longer the stay this row wrote (a newer order, or a whisper shortcut that numbered nothing) - its driver is not fought.
enum class RowEnd { None, Dead, Teleport, OtherMap, Commander, Run, StayGone, Unreachable };
enum class Release { None, Follow, ClearStay };

inline Release ReleaseOn(RowEnd end, bool stillCommanded)
{
    switch (end)
    {
        case RowEnd::None:
            return Release::None;
        case RowEnd::Run:
        case RowEnd::StayGone:
            // Another driver owns the bot's movement now: never queue an order that would fight it.
            return Release::ClearStay;
        default:
            return stillCommanded ? Release::Follow : Release::ClearStay;
    }
}

// ---- the card keys: who holds which slot, and where the rally point is (TbCmdBinding.cpp's status loop) ----

struct Vec3f
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// The commander's memory as a card reads it (TbPlace.cpp ViewFor). Absent keys mean empty: no hold row, no rally.
struct HoldView
{
    bool any = false;
    bool fallback = false;
    std::vector<std::pair<GuidKey, Vec3f>> slots;
    bool rallySet = false;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    uint32_t map = 0;
    uint32_t inst = 0;
};

struct CardKeys
{
    std::string hold;       // "place" | "fallback" | empty (no row)
    bool hasRally = false;
    Vec3f rally;
    uint32_t rmap = 0;
    uint32_t rinst = 0;
    std::vector<std::pair<GuidKey, Vec3f>> slots;

    bool SlotOf(GuidKey const& bot, Vec3f& out) const
    {
        for (auto const& row : slots)
            if (row.first == bot)
            {
                out = row.second;
                return true;
            }
        return false;
    }
};

// The keys the binding and its member records carry. A hold with rows names the verb once on the binding and its slot on exactly those members; a
// rally point is printed with its own map and instance (it can be another map).
inline CardKeys CardHoldKeys(HoldView const& view)
{
    CardKeys keys;
    if (view.any && !view.slots.empty())
    {
        keys.hold = view.fallback ? "fallback" : "place";
        keys.slots = view.slots;
    }
    if (view.rallySet)
    {
        keys.hasRally = true;
        keys.rally = { view.x, view.y, view.z };
        keys.rmap = view.map;
        keys.rinst = view.inst;
    }
    return keys;
}
}
}

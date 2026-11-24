// Issue #15 (`place` / `rally` / `fall back`): the geometry, the words and the minimap-ping layout, pure. No core header, no Player, no Map: the command
// (TbPlace.cpp) reads the world and this header decides where the slots are. Tested by forever/tools/test_place_pure.cpp.
//
// LOCAL FRAME. Every shape is built around its centre in a local frame where +y points to the FRONT (the side the group faces: the enemy for `here`,
// the player for `target`, away from the player for `cursor`) and +x is the world-right of that front, so a slot's world point is
//   world = centre + right * x + front * y,   right = (front.y, -front.x).
// Slot 0 is the front slot (the tank), and the slot order IS the rank order: tanks, melee, ranged, healers (SortByRank).
//
// ANCHORS (FrameFor). `here`: the whole shape stands along the player's facing, its nearest slot kHereClearYd from the player and never on the player's
// own spot. `target`: a ring is centred on the target and never smaller than the target's size plus one gap; the row shapes start one gap clear of the
// target's size on the player's side and grow away from it, so no slot is inside the target. `cursor`: the shape is centred on the minimap ping and
// faces away from the player. `tank`: centred on the anchor tank and facing behind it (slot 0 away from what the tank faces), the ring never narrower
// than the tank's reach plus one gap. `at:`: centred on a typed point and facing away from the player. `face:<deg>` replaces the front for `here`,
// `cursor`, `tank` and `at:`.
//
// CEILINGS. kMaxRadiusYd is the "never over ~35 yd" rule: a ring that cannot stay inside it (a huge target, or more bots than the circumference holds)
// is refused (`tooFar`), never silently shrunk onto the target. Row shapes only ever use a few gaps for a real party; the command drops a slot past the
// ceiling (`too-far`) instead of placing a bot outside it.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "../run/TbRunLiquid.h"   // the ONE liquid rule (pure: no core header)

namespace TortoiseBots
{
namespace place
{
// The issue's gaps: tight 2.5 yd, normal 4 yd, wide 8 yd.
constexpr float kTightYd = 2.5f;
constexpr float kNormalYd = 4.0f;
constexpr float kWideYd = 8.0f;
// "A ring ... never over ~35 yd".
constexpr float kMaxRadiusYd = 35.0f;
// The spread grid: "~2.5 gaps apart so nobody shares a splash".
constexpr float kSpreadGapFactor = 2.5f;
// `here`: the nearest slot stands this far in front of the player, so no bot is ever on the player's own spot.
constexpr float kHereClearYd = 2.0f;
// The hold: a placed bot this far (2D) from its slot has lost it and is walked back outside combat.
constexpr float kSlotToleranceYd = 3.0f;
// `cursor`: how long the command waits for the player's minimap ping.
constexpr int kCursorWaitSeconds = 30;
// No slot ever stands this close to the commander, whatever the anchor: the commander's own spot is never a bot's.
constexpr float kCommanderClearYd = 1.5f;
// A slot on another floor of the map (a ledge, a balcony) is refused: the floor search alone reaches 25 yd down and a long path can still get there.
constexpr float kMaxSlotStepYd = 5.0f;
// A refused slot is nudged this far towards the frame centre, in this order, before the bot is refused.
constexpr float kNudgeStepsYd[] = { 1.0f, 2.0f, 3.0f };
// A path more than this factor of the straight distance (plus the slack) is a detour the group cannot hold.
constexpr float kDetourFactor = 2.5f;
constexpr float kDetourSlackYd = 10.0f;
// `at:` / `rally at:`: how far from the commander a typed point may be, in every axis (a floor tens of yards above or
// below the commander is not the point the player meant).
constexpr float kPointReachYd = 100.0f;
// A path that ends farther than this from the slot did not reach it.
constexpr float kPathReachYd = 3.0f;

// A world point on the horizontal plane.
struct Vec2
{
    float x = 0.0f;
    float y = 0.0f;
};

// The move rule's own verdict, so a plan only names the refusal.
enum class PathVerdict { Ok, NoPath, LiquidPath, Detour };

inline char const* PathVerdictWord(PathVerdict verdict)
{
    switch (verdict)
    {
        case PathVerdict::Ok: return "ok";
        case PathVerdict::NoPath: return "unreachable";
        case PathVerdict::LiquidPath: return "liquid-path";
        case PathVerdict::Detour: return "detour";
    }
    return "";
}

// The detour rule: a path this much longer than the straight line is a walk the group cannot hold.
inline bool IsDetour(float pathLen, float straight)
{
    return pathLen > kDetourFactor * straight + kDetourSlackYd;
}

struct PathPoint
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// One PathFinder answer as the pure verdict reads it: the type, the points in order and the actual end.
struct PathWalk
{
    bool noPath = false;
    std::vector<PathPoint> points;
    PathPoint end;
};

// One path's verdict: no path, an end that does not reach the slot, liquid, or a detour. The liquid rule is the run's
// ONE move rule over the WHOLE path (a bot that stands in water may walk out of it: a wet prefix followed by dry ground
// is accepted; a wet sample after dry ground, or a wet end, is refused). Asking it per leg would refuse a wet start
// whose own leg ends wet even when the path walks out.
template <class LiquidRefused>
inline PathVerdict WalkVerdict(PathWalk const& walk, Vec2 point, float z, Vec2 start, LiquidRefused&& refused)
{
    if (walk.noPath || walk.points.size() < 2)
        return PathVerdict::NoPath;
    float const ex = walk.end.x - point.x, ey = walk.end.y - point.y, ez = walk.end.z - z;
    if (std::sqrt(ex * ex + ey * ey + ez * ez) > kPathReachYd)
        return PathVerdict::NoPath;
    if (run::liquid::PathPrefixRefused(walk.points.data(), walk.points.size(), refused))
        return PathVerdict::LiquidPath;
    float length = 0.0f;
    for (size_t i = 1; i < walk.points.size(); ++i)
    {
        float const lx = walk.points[i].x - walk.points[i - 1].x;
        float const ly = walk.points[i].y - walk.points[i - 1].y;
        float const lz = walk.points[i].z - walk.points[i - 1].z;
        length += std::sqrt(lx * lx + ly * ly + lz * lz);
    }
    float const sx = point.x - start.x, sy = point.y - start.y;
    if (IsDetour(length, std::sqrt(sx * sx + sy * sy)))
        return PathVerdict::Detour;
    return PathVerdict::Ok;
}

enum class Shape { Close, Circle, Arc, Spread, Line, Column, Spear, Arrow };
enum class Gap { Tight, Normal, Wide };
enum class Anchor { Here, Target, Cursor, Tank, Point };
// The slot order: tanks front, then melee, ranged, healers at the back.
enum class Rank { Tank = 0, Melee = 1, Ranged = 2, Healer = 3 };

inline char const* ShapeWord(Shape shape)
{
    switch (shape)
    {
        case Shape::Close: return "close";
        case Shape::Circle: return "circle";
        case Shape::Arc: return "arc";
        case Shape::Spread: return "spread";
        case Shape::Line: return "line";
        case Shape::Column: return "column";
        case Shape::Spear: return "spear";
        case Shape::Arrow: return "arrow";
    }
    return "";
}

inline char const* GapWord(Gap gap)
{
    switch (gap)
    {
        case Gap::Tight: return "tight";
        case Gap::Normal: return "normal";
        case Gap::Wide: return "wide";
    }
    return "";
}

inline char const* AnchorWord(Anchor anchor)
{
    switch (anchor)
    {
        case Anchor::Here: return "here";
        case Anchor::Target: return "target";
        case Anchor::Cursor: return "cursor";
        case Anchor::Tank: return "tank";
        case Anchor::Point: return "at";
    }
    return "";
}

inline char const* RankWord(Rank rank)
{
    switch (rank)
    {
        case Rank::Tank: return "tank";
        case Rank::Melee: return "melee";
        case Rank::Ranged: return "ranged";
        case Rank::Healer: return "healer";
    }
    return "";
}

inline float GapYd(Gap gap)
{
    switch (gap)
    {
        case Gap::Tight: return kTightYd;
        case Gap::Normal: return kNormalYd;
        case Gap::Wide: return kWideYd;
    }
    return kNormalYd;
}

inline bool IsRing(Shape shape) { return shape == Shape::Circle || shape == Shape::Arc; }

inline std::string Lower(std::string_view text)
{
    std::string out(text);
    for (char& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

inline bool ParseShape(std::string_view word, Shape& out)
{
    std::string const w = Lower(word);
    for (Shape shape : { Shape::Close, Shape::Circle, Shape::Arc, Shape::Spread, Shape::Line, Shape::Column, Shape::Spear, Shape::Arrow })
        if (w == ShapeWord(shape))
        {
            out = shape;
            return true;
        }
    return false;
}

inline bool ParseGap(std::string_view word, Gap& out)
{
    std::string const w = Lower(word);
    for (Gap gap : { Gap::Tight, Gap::Normal, Gap::Wide })
        if (w == GapWord(gap))
        {
            out = gap;
            return true;
        }
    return false;
}

inline bool ParseAnchor(std::string_view word, Anchor& out)
{
    std::string const w = Lower(word);
    for (Anchor anchor : { Anchor::Here, Anchor::Target, Anchor::Cursor, Anchor::Tank })
        if (w == AnchorWord(anchor))
        {
            out = anchor;
            return true;
        }
    return false;
}

// The gap as typed: a preset word or a number of yards (1.5..12.0, at most one fraction digit).
struct GapSpec
{
    Gap preset = Gap::Normal;
    float yd = kNormalYd;
    bool custom = false;
};

// A strict decimal: digits, one optional '.', and at most one digit after it; no sign, exponent, nan or inf.
inline bool ParseGapNumber(std::string_view word, float& yd)
{
    if (word.empty() || word.size() > 24)
        return false;
    size_t i = 0;
    while (i < word.size() && word[i] >= '0' && word[i] <= '9')
        ++i;
    if (i == 0)
        return false;
    if (i < word.size() && word[i] == '.')
    {
        ++i;
        size_t fraction = 0;
        while (i < word.size() && word[i] >= '0' && word[i] <= '9')
        {
            ++i;
            if (++fraction > 1)
                return false;
        }
        if (fraction == 0)      // `5.` is not a gap: one fraction digit is required after the point
            return false;
    }
    if (i != word.size())
        return false;
    std::string const text(word);
    char* end = nullptr;
    yd = std::strtof(text.c_str(), &end);
    return end && *end == '\0' && std::isfinite(yd);
}

// The parser asks this to tell "a gap word I do not know" (a who name) from "a number I must refuse": a word that
// starts like a number is a bad gap, never a bot name. A bare `nan`/`inf` is NOT numeric-looking: `Nan` and `Inf`
// are legal character names, and the gap parser already refuses those words as gaps.
inline bool LooksNumeric(std::string_view word)
{
    if (word.empty())
        return false;
    char const c = word[0];
    return (c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.';
}

inline bool ParseGapWord(std::string_view word, GapSpec& out)
{
    Gap gap = Gap::Normal;
    if (ParseGap(word, gap))
    {
        out.preset = gap;
        out.yd = GapYd(gap);
        out.custom = false;
        return true;
    }
    float yd = 0.0f;
    if (!ParseGapNumber(word, yd) || yd < 1.5f || yd > 12.0f)
        return false;
    out.preset = Gap::Normal;
    out.yd = yd;
    out.custom = true;
    return true;
}

inline std::string GapText(GapSpec const& gap)
{
    if (!gap.custom)
        return GapWord(gap.preset);
    char text[16];
    std::snprintf(text, sizeof text, "%.1f", double(gap.yd));
    return text;
}

// `face:<deg>`: digits only, 0..359. The game's own orientation in degrees (0 = +X, 90 = +Y).
inline bool ParseFace(std::string_view word, int& deg)
{
    if (word.empty() || word.size() > 3)
        return false;
    int value = 0;
    for (char c : word)
    {
        if (c < '0' || c > '9')
            return false;
        value = value * 10 + (c - '0');
    }
    if (value > 359)
        return false;
    deg = value;
    return true;
}

// One signed strict decimal of a point: an optional '-', digits, an optional '.' and digits. No exponent, nan or inf.
inline bool ParsePointNumber(std::string_view word, float& out)
{
    if (word.empty() || word.size() > 24)
        return false;
    size_t i = 0;
    if (word[i] == '-')
        ++i;
    size_t const digits = i;
    while (i < word.size() && word[i] >= '0' && word[i] <= '9')
        ++i;
    if (i == digits)
        return false;
    if (i < word.size() && word[i] == '.')
    {
        ++i;
        size_t const fraction = i;
        while (i < word.size() && word[i] >= '0' && word[i] <= '9')
            ++i;
        if (i == fraction)
            return false;
    }
    if (i != word.size())
        return false;
    std::string const text(word);
    char* end = nullptr;
    out = std::strtof(text.c_str(), &end);
    return end && *end == '\0' && std::isfinite(out);
}



struct Slot
{
    float x = 0.0f;
    float y = 0.0f;
};

struct Layout
{
    std::vector<Slot> slots;    // in assignment order: slot 0 is the front slot (the tank)
    float radius = 0.0f;        // the ring radius of Circle/Arc (0 for the other shapes)
    float extent = 0.0f;        // the farthest slot from the local origin
    bool tooFar = false;        // a ring that cannot stay inside kMaxRadiusYd
};

// The slots of one shape, centred on the local origin. `targetHalfYd` is the target's size (its combat reach) for the ring minimum; 0 for `here`/`cursor`.
inline Layout LayoutShape(Shape shape, float gapYd, int count, float targetHalfYd)
{
    Layout layout;
    if (count <= 0)
        return layout;
    float const g = gapYd;
    float const targetHalf = targetHalfYd > 0.0f ? targetHalfYd : 0.0f;
    float const ringMinimum = targetHalf > 0.0f ? targetHalf + g : g;

    switch (shape)
    {
        case Shape::Close:
        {
            // A tight block two wide; every row one gap behind the last, the front row first.
            int const cols = count >= 2 ? 2 : 1;
            int const rows = (count + cols - 1) / cols;
            for (int i = 0; i < count; ++i)
            {
                int const row = i / cols, col = i % cols;
                layout.slots.push_back({ (col - (cols - 1) * 0.5f) * g, ((rows - 1) * 0.5f - row) * g });
            }
            break;
        }
        case Shape::Line:
        {
            // One row across; the rank order runs centre-out, so the tanks hold the middle and the healers the ends.
            std::vector<float> xs;
            for (int i = 0; i < count; ++i)
                xs.push_back((i - (count - 1) * 0.5f) * g);
            std::stable_sort(xs.begin(), xs.end(), [](float a, float b)
            {
                float const aa = std::fabs(a), bb = std::fabs(b);
                if (aa != bb)
                    return aa < bb;
                return a > b;
            });
            for (float x : xs)
                layout.slots.push_back({ x, 0.0f });
            break;
        }
        case Shape::Column:
            for (int i = 0; i < count; ++i)
                layout.slots.push_back({ 0.0f, ((count - 1) * 0.5f - i) * g });
            break;
        case Shape::Spread:
        {
            // A grid 2.5 gaps apart, front row first, so no two bots share a splash.
            float const s = kSpreadGapFactor * g;
            int cols = int(std::ceil(std::sqrt(float(count))));
            if (cols < 1)
                cols = 1;
            int const rows = (count + cols - 1) / cols;
            for (int i = 0; i < count; ++i)
            {
                int const row = i / cols, col = i % cols;
                layout.slots.push_back({ (col - (cols - 1) * 0.5f) * s, ((rows - 1) * 0.5f - row) * s });
            }
            break;
        }
        case Shape::Spear:
        {
            // A wedge: the tip alone in front, one more bot in every row behind it.
            int rows = 1;
            while (rows * (rows + 1) / 2 < count)
                ++rows;
            int i = 0;
            for (int r = 0; r < rows && i < count; ++r)
                for (int j = 0; j <= r && i < count; ++j, ++i)
                    layout.slots.push_back({ (j - r * 0.5f) * g, ((rows - 1) * 0.5f - r) * g });
            break;
        }
        case Shape::Arrow:
        {
            // A V opening backwards: the tip first, then both arms a step further back and out.
            int const arms = (count - 1 + 1) / 2;
            float const c = std::cos(3.14159265358979f / 4.0f);
            float const depth = arms * g * c;
            layout.slots.push_back({ 0.0f, depth * 0.5f });
            for (int k = 1; k <= arms && int(layout.slots.size()) < count; ++k)
            {
                float const step = k * g * c;
                float const y = depth * 0.5f - step;
                layout.slots.push_back({ step, y });
                if (int(layout.slots.size()) < count)
                    layout.slots.push_back({ -step, y });
            }
            break;
        }
        case Shape::Circle:
        case Shape::Arc:
        {
            // The ring holds every bot one gap apart; round a target it also clears the target's size.
            float const circumference = shape == Shape::Circle ? float(count) * g / (2.0f * 3.14159265358979f)
                                                               : float(count) * g / 3.14159265358979f;
            float radius = std::max(circumference, ringMinimum);
            if (radius > kMaxRadiusYd)
            {
                layout.tooFar = true;
                radius = kMaxRadiusYd;
            }
            layout.radius = radius;
            std::vector<float> angles;
            if (shape == Shape::Circle)
                for (int i = 0; i < count; ++i)
                    angles.push_back(2.0f * 3.14159265358979f * float(i) / float(count));   // from the front, clockwise
            else if (count == 1)
                angles.push_back(0.0f);
            else
            {
                float const half = 3.14159265358979f * 0.5f;
                for (int i = 0; i < count; ++i)
                    angles.push_back(-half + 2.0f * half * float(i) / float(count - 1));
                std::stable_sort(angles.begin(), angles.end(), [](float a, float b)
                {
                    float const aa = std::fabs(a), bb = std::fabs(b);
                    if (aa != bb)
                        return aa < bb;
                    return a > b;
                });
            }
            for (float a : angles)
                layout.slots.push_back({ radius * std::sin(a), radius * std::cos(a) });
            break;
        }
    }

    for (Slot const& slot : layout.slots)
        layout.extent = std::max(layout.extent, std::sqrt(slot.x * slot.x + slot.y * slot.y));
    return layout;
}

inline Layout LayoutShape(Shape shape, Gap gap, int count, float targetHalfYd)
{
    return LayoutShape(shape, GapYd(gap), count, targetHalfYd);
}

// `at:<x>,<y>[,<z>]`: two or three strict decimals. One or four parts is refused.
inline bool ParsePoint(std::string_view word, Vec2& point, float& z, bool& hasZ)
{
    std::vector<std::string_view> parts;
    size_t start = 0;
    for (size_t i = 0; i <= word.size(); ++i)
        if (i == word.size() || word[i] == ',')
        {
            parts.push_back(word.substr(start, i - start));
            start = i + 1;
        }
    if (parts.size() != 2 && parts.size() != 3)
        return false;
    float x = 0.0f, y = 0.0f, zz = 0.0f;
    if (!ParsePointNumber(parts[0], x) || !ParsePointNumber(parts[1], y))
        return false;
    if (parts.size() == 3 && !ParsePointNumber(parts[2], zz))
        return false;
    point.x = x;
    point.y = y;
    hasZ = parts.size() == 3;
    z = hasZ ? zz : 0.0f;
    return true;
}

inline Vec2 Normalized(Vec2 v, Vec2 fallback)
{
    float const length = std::sqrt(v.x * v.x + v.y * v.y);
    if (!(length > 1e-4f))
        return fallback;
    return { v.x / length, v.y / length };
}

struct Frame
{
    Vec2 centre;    // the shape's centre in the world
    Vec2 front;     // unit vector: where the group faces
};

// The core's forward vector for an orientation, in world (x, y): x += cos(o) * d, y += sin(o) * d
// (Position::RelocateOffset, WorldObject::GetNearPoint2D). The ONE facing helper of the module: player facing, anchor
// tank facing, `face:<deg>` and TbThrown's knockback point all come from here, so an orientation means the same
// direction everywhere.
inline Vec2 FrontFromOrientation(float radians)
{
    return { std::cos(radians), std::sin(radians) };
}

// `face:<deg>`: the game's own orientation in degrees (0 = +X, 90 = +Y), the number `.gps` prints.
inline Vec2 FaceFront(int deg)
{
    return FrontFromOrientation(float(deg) * 3.14159265358979f / 180.0f);
}

// Everything the frames read from the world, one struct so the round-1 call stays a call.
struct FrameInput
{
    Vec2 player;
    Vec2 playerFront{ 0.0f, 1.0f };
    Vec2 target;
    float targetHalf = 0.0f;
    Vec2 cursor;
    Vec2 tank;
    Vec2 tankFacing{ 0.0f, 1.0f };
    float tankReach = 0.0f;
    Vec2 point;
    bool hasFace = false;
    Vec2 faceFront{ 0.0f, 1.0f };
};

// Where one shape stands for one anchor. `playerFront` is the player's facing (a unit vector or not: it is normalized).
inline Frame FrameFor(Anchor anchor, Layout const& layout, float gapYd, FrameInput const& in)
{
    Frame frame;
    Vec2 const facing = Normalized(in.playerFront, { 0.0f, 1.0f });
    Vec2 const override = in.hasFace ? Normalized(in.faceFront, facing) : facing;
    switch (anchor)
    {
        case Anchor::Here:
        {
            // The whole shape ahead of the player along the facing (or face:), nearest slot kHereClearYd away.
            frame.front = in.hasFace ? override : facing;
            float const offset = kHereClearYd + layout.extent;
            frame.centre = { in.player.x + frame.front.x * offset, in.player.y + frame.front.y * offset };
            break;
        }
        case Anchor::Target:
        {
            // A ring is centred on the target and starts on the player's side (the tank is the bot nearest the player, the group wraps round the
            // target). The row shapes stand between the player and the target with the tank row nearest the target, so the group never stands on the
            // target and heals and ranged keep their distance from it. The target anchor faces the target: face: is refused.
            if (layout.radius > 0.0f)
            {
                frame.front = Normalized({ in.player.x - in.target.x, in.player.y - in.target.y }, facing);
                frame.centre = in.target;
            }
            else
            {
                frame.front = Normalized({ in.target.x - in.player.x, in.target.y - in.player.y }, facing);
                float const offset = (in.targetHalf > 0.0f ? in.targetHalf : 0.0f) + gapYd + layout.extent;
                frame.centre = { in.target.x - frame.front.x * offset, in.target.y - frame.front.y * offset };
            }
            break;
        }
        case Anchor::Cursor:
            // Facing away from the player (or face:), centred on the ping.
            frame.front = in.hasFace ? override : Normalized({ in.cursor.x - in.player.x, in.cursor.y - in.player.y }, facing);
            frame.centre = in.cursor;
            break;
        case Anchor::Tank:
            // Slot 0 stands behind the tank, away from what it faces; face: overrides.
            frame.front = in.hasFace ? override : Normalized({ -in.tankFacing.x, -in.tankFacing.y }, facing);
            frame.centre = in.tank;
            break;
        case Anchor::Point:
        {
            // Centred on the point, facing away from the player; within 1 yd the player's own facing stands in.
            float const dx = in.point.x - in.player.x, dy = in.point.y - in.player.y;
            frame.front = in.hasFace ? override : (std::sqrt(dx * dx + dy * dy) < 1.0f ? facing : Normalized({ dx, dy }, facing));
            frame.centre = in.point;
            break;
        }
    }
    return frame;
}

inline Frame FrameFor(Anchor anchor, Layout const& layout, float gapYd, Vec2 player, Vec2 playerFront, Vec2 target, float targetHalfYd, Vec2 cursor)
{
    FrameInput in;
    in.player = player;
    in.playerFront = playerFront;
    in.target = target;
    in.targetHalf = targetHalfYd;
    in.cursor = cursor;
    return FrameFor(anchor, layout, gapYd, in);
}

// One local slot in the world. right = (front.y, -front.x).
inline Vec2 Place(Frame const& frame, Slot const& slot)
{
    return { frame.centre.x + slot.x * frame.front.y + slot.y * frame.front.x,
             frame.centre.y - slot.x * frame.front.x + slot.y * frame.front.y };
}

// A cleared spot (the commander's own, or the anchor tank's) is never a slot's: a circle turns by half a slot once (so it keeps every bot), every other
// shape leaves the slots that stand on a spot empty. Geometry only: the slots stay in the local frame; the result says which slots a bot may still take.
inline std::vector<bool> ClearSpots(std::vector<Slot>& slots, Shape shape, Frame const& frame, std::vector<Vec2> const& spots, int count)
{
    std::vector<bool> usable(slots.size(), true);
    if (slots.empty() || spots.empty())
        return usable;
    std::vector<Vec2> local;
    for (Vec2 const& spot : spots)
    {
        float const dx = spot.x - frame.centre.x, dy = spot.y - frame.centre.y;
        local.push_back({ dx * frame.front.y - dy * frame.front.x, dx * frame.front.x + dy * frame.front.y });
    }
    auto const collides = [&](Slot const& slot)
    {
        for (Vec2 const& spot : local)
        {
            float const ex = slot.x - spot.x, ey = slot.y - spot.y;
            if (ex * ex + ey * ey < kCommanderClearYd * kCommanderClearYd)
                return true;
        }
        return false;
    };
    if (shape == Shape::Circle && count > 1)
    {
        bool hit = false;
        for (Slot const& slot : slots)
            hit = hit || collides(slot);
        if (hit)
        {
            float const angle = 3.14159265358979f / float(count);
            float const c = std::cos(angle), s = std::sin(angle);
            for (Slot& slot : slots)
                slot = { slot.x * c - slot.y * s, slot.x * s + slot.y * c };
            std::stable_sort(slots.begin(), slots.end(), [](Slot const& a, Slot const& b)
            {
                float const aa = std::atan2(a.x, a.y);
                float const ab = std::atan2(b.x, b.y);
                return (aa < 0.0f ? aa + 2.0f * 3.14159265358979f : aa) < (ab < 0.0f ? ab + 2.0f * 3.14159265358979f : ab);
            });
        }
    }
    for (size_t i = 0; i < slots.size(); ++i)
        usable[i] = !collides(slots[i]);
    return usable;
}

// The round-1 call: one cleared spot, the commander's own.
inline std::vector<bool> ClearCommanderSpot(std::vector<Slot>& slots, Shape shape, Frame const& frame, Vec2 commander, int count)
{
    return ClearSpots(slots, shape, frame, { commander }, count);
}

struct BotRank
{
    Rank rank = Rank::Melee;
    std::string name;
};

// The slot order: rank first, then the name, so one command always builds the same slots for the same bots.
inline bool RankBefore(Rank aRank, std::string_view aName, Rank bRank, std::string_view bName)
{
    if (aRank != bRank)
        return int(aRank) < int(bRank);
    size_t const length = std::min(aName.size(), bName.size());
    for (size_t i = 0; i < length; ++i)
    {
        unsigned char const a = static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(aName[i])));
        unsigned char const b = static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(bName[i])));
        if (a != b)
            return a < b;
    }
    return aName.size() < bName.size();
}

inline void SortByRank(std::vector<BotRank>& bots)
{
    std::stable_sort(bots.begin(), bots.end(), [](BotRank const& a, BotRank const& b)
    {
        return RankBefore(a.rank, a.name, b.rank, b.name);
    });
}

// ---- the who filter: names, all, tanks, healers, melee, ranged, dps, a class, group1..8; none = all the player's bots ----

enum class WhoKind { All, Tanks, Healers, Melee, Ranged, Dps, Class, Group, Name };

struct WhoToken
{
    WhoKind kind = WhoKind::All;
    std::string word;   // the class word (lower-case) or the name as typed
    uint32_t group = 0;
};

inline bool IsClassName(std::string const& lower)
{
    for (char const* name : { "warrior", "paladin", "hunter", "rogue", "priest", "shaman", "mage", "warlock", "druid" })
        if (lower == name)
            return true;
    return false;
}

// Always true for a non-empty word: an unknown word is a bot name. (The command refuses a name that is not one of the player's bots.)
inline bool ParseWho(std::string_view word, WhoToken& out)
{
    std::string const lower = Lower(word);
    out.word = std::string(word);
    out.group = 0;
    if (lower.empty() || lower == "all")
        out.kind = WhoKind::All;
    else if (lower == "tanks")
        out.kind = WhoKind::Tanks;
    else if (lower == "healers")
        out.kind = WhoKind::Healers;
    else if (lower == "melee")
        out.kind = WhoKind::Melee;
    else if (lower == "ranged")
        out.kind = WhoKind::Ranged;
    else if (lower == "dps")
        out.kind = WhoKind::Dps;
    else if (IsClassName(lower))
    {
        out.kind = WhoKind::Class;
        out.word = lower;
    }
    else if (lower.size() == 6 && lower.compare(0, 5, "group") == 0 && lower[5] >= '1' && lower[5] <= '8')
    {
        out.kind = WhoKind::Group;
        out.group = uint32_t(lower[5] - '0');
    }
    else
        out.kind = WhoKind::Name;
    return true;
}

struct BotInfo
{
    std::string name;
    std::string className;  // lower-case
    Rank rank = Rank::Melee;
    uint32_t group = 1;     // the raid subgroup (a party is 1)
};

inline bool Selected(WhoToken const& token, BotInfo const& bot)
{
    switch (token.kind)
    {
        case WhoKind::All: return true;
        case WhoKind::Tanks: return bot.rank == Rank::Tank;
        case WhoKind::Healers: return bot.rank == Rank::Healer;
        case WhoKind::Melee: return bot.rank == Rank::Melee;
        case WhoKind::Ranged: return bot.rank == Rank::Ranged;
        case WhoKind::Dps: return bot.rank == Rank::Melee || bot.rank == Rank::Ranged;
        case WhoKind::Class: return bot.className == token.word;
        case WhoKind::Group: return bot.group == token.group;
        case WhoKind::Name: return Lower(bot.name) == Lower(token.word);
    }
    return false;
}

inline bool Selected(std::vector<WhoToken> const& tokens, BotInfo const& bot)
{
    if (tokens.empty())
        return true;
    for (WhoToken const& token : tokens)
        if (Selected(token, bot))
            return true;
    return false;
}

// ---- fall back: tanks in front, melee just behind, ranged and healers on an arc behind them ----

struct FallbackCounts
{
    int tanks = 0;
    int melee = 0;
    int ranged = 0;
    int healers = 0;
};

inline std::vector<Slot> FallbackSlots(FallbackCounts const& counts, float gapYd)
{
    float const g = gapYd;
    std::vector<Slot> slots;
    // The tank row half a gap ahead, the melee row half a gap behind it: nobody stands on the rally point itself.
    for (int i = 0; i < counts.tanks; ++i)
        slots.push_back({ (i - (counts.tanks - 1) * 0.5f) * g, 0.5f * g });
    for (int i = 0; i < counts.melee; ++i)
        slots.push_back({ (i - (counts.melee - 1) * 0.5f) * g, -0.5f * g });
    // The arc behind them: the ranged hold its middle, the healers its ends (the widest reach).
    int const arc = counts.ranged + counts.healers;
    if (arc > 0)
    {
        float const radius = 2.0f * g;
        Vec2 const centre{ 0.0f, -3.5f * g };
        std::vector<Slot> points;
        if (arc == 1)
            points.push_back({ centre.x, centre.y - radius });
        else
        {
            float const half = 70.0f * 3.14159265358979f / 180.0f;
            std::vector<float> angles;
            for (int i = 0; i < arc; ++i)
                angles.push_back(-half + 2.0f * half * float(i) / float(arc - 1));
            std::stable_sort(angles.begin(), angles.end(), [](float a, float b)
            {
                float const aa = std::fabs(a), bb = std::fabs(b);
                if (aa != bb)
                    return aa < bb;
                return a > b;
            });
            for (float a : angles)
                points.push_back({ centre.x + radius * std::sin(a), centre.y - radius * std::cos(a) });
        }
        size_t next = 0;
        for (int i = 0; i < counts.ranged; ++i)
            slots.push_back(points[next++]);
        for (int i = 0; i < counts.healers; ++i)
            slots.push_back(points[next++]);
    }
    return slots;
}

inline std::vector<Slot> FallbackSlots(FallbackCounts const& counts, Gap gap)
{
    return FallbackSlots(counts, GapYd(gap));
}

// ---- the commander's minimap ping ----

struct CursorPoint
{
    float x = 0.0f;
    float y = 0.0f;
};

// MSG_MINIMAP_PING as the pinned core reads it (PartyPackets.cpp MinimapPingClient::Read): the client's body is two little-endian floats (x, y); the
// core's packet adds a flags byte in front of them whose bit 7 says a party index byte follows the floats. Read-only: the packet is never changed.
// False for a body of any other length and for a value that is not a finite number.
inline bool ParseMinimapPing(uint8_t const* data, size_t size, CursorPoint& out)
{
    if (!data)
        return false;
    size_t offset = 0;
    bool partyIndex = false;
    if (size == 9 || size == 10)
    {
        partyIndex = (data[0] & 0x80) != 0;
        if (partyIndex && size < 10)
            return false;
        offset = 1;
    }
    else if (size != 8)
        return false;
    if (offset + 8 > size)
        return false;
    float x = 0.0f, y = 0.0f;
    std::memcpy(&x, data + offset, sizeof x);
    std::memcpy(&y, data + offset + 4, sizeof y);
    if (!std::isfinite(x) || !std::isfinite(y))
        return false;
    out.x = x;
    out.y = y;
    return true;
}
}
}

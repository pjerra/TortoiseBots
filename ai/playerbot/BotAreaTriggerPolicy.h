#pragma once

// Bot area triggers (Sagiroth/TortoiseBots#984): a headless bot has no client,
// so it never sends CMSG_AREATRIGGER and a scripted area trigger never fires for
// it. Zul'Farrak's Witch Doctor Zum'rah only turns hostile through the script
// `at_zumrah`, so an all-bot party waited in front of a friendly boss forever.
// The module now runs the trigger's script for a grouped bot in a dungeon,
// the way WorldSession::HandleAreaTriggerOpcode does for a player (only the
// sScriptMgr.OnAreaTrigger part; teleports, taverns and the rest stay with the
// client). Not every trigger script is safe to start from a bot (Codex review
// on #1002: at_ring_of_law starts the BRD arena, the UBRS stadium trigger its
// event, at_shadowforge_bridge spawns guards), so only an audited allowlist
// (AiPlayerbot.BotAreaTriggers, off by default; 962 audited) fires, and only for a party that
// is clearing on purpose: a commander-bound or run-leader party, or one with a
// human member close by. This header holds the pure parts: the inside test
// (the same math as the core's IsPointInAreaTriggerZone), the filters, and the
// edge detection that fires each trigger once per entry.
// Unit-tested in tools/test_bot_area_trigger_policy.cpp.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ai
{

// A copy of the shape fields of the core's AreaTriggerEntry.
struct BotAreaTriggerShape
{
    uint32_t id = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float radius = 0.0f;
    float boxX = 0.0f, boxY = 0.0f, boxZ = 0.0f;
    float boxOrientation = 0.0f;
};

// A sphere when radius > 0, else a box rotated about z (the core's rule).
inline bool IsInsideAreaTriggerShape(BotAreaTriggerShape const& at, float px, float py, float pz, float delta)
{
    if (at.radius > 0.0f)
    {
        float const dx = px - at.x, dy = py - at.y, dz = pz - at.z;
        float const r = at.radius + delta;
        return dx * dx + dy * dy + dz * dz <= r * r;
    }

    double const rotation = 6.283185307179586 - at.boxOrientation; // 2 pi: orientation is counter-clockwise
    double const sinVal = std::sin(rotation);
    double const cosVal = std::cos(rotation);
    float const distX = px - at.x;
    float const distY = py - at.y;
    float const rotX = float(distX * cosVal - distY * sinVal);
    float const rotY = float(distY * cosVal + distX * sinVal);
    float const dz = pz - at.z;
    return std::fabs(rotX) <= at.boxX / 2 + delta &&
           std::fabs(rotY) <= at.boxY / 2 + delta &&
           std::fabs(dz) <= at.boxZ / 2 + delta;
}

// Which triggers a bot may fire: only those on the audited allowlist, with a
// script, on a dungeon map (not a raid: the caller passes
// MapEntry::IsNonRaidDungeon; raids wait until bots are tested there), and none
// the core's handler also treats as a teleport (instance entrances and exits),
// a tavern or a battleground entrance. Those need the client's own checks and
// replies, and a bot following its leader must never be ported.
inline bool IsBotFireableAreaTrigger(bool listed, bool hasScript, bool dungeonMap, bool isTeleport, bool isTavern, bool isBgEntrance)
{
    return listed && hasScript && dungeonMap && !isTeleport && !isTavern && !isBgEntrance;
}

template <class List>
inline bool IsListedAreaTrigger(List const& allowlist, uint32_t id)
{
    return std::find(allowlist.begin(), allowlist.end(), id) != allowlist.end();
}

// Whose bot may fire: a party clearing on purpose (bound to a remote commander,
// which every run-leader run is), or one with a human member within
// kBotAreaTriggerHumanRange. A follower that runs ahead of its player then
// cannot start an event alone; the player's own client fires it on arrival.
constexpr float kBotAreaTriggerHumanRange = 40.0f;
inline bool IsBotAreaTriggerPartyAllowed(bool commandedParty, bool humanNearby)
{
    return commandedParty || humanNearby;
}

// Outside this margin a trigger the bot was inside counts as left, so a bot
// standing on the edge does not fire it again on every check.
constexpr float kBotAreaTriggerLeaveMargin = 2.0f;
// How often a bot checks its map's triggers (ms). A bot runs about 7 yd/s, so
// it moves under 4 yd between checks: it cannot step over a 10 yd sphere.
constexpr uint32_t kBotAreaTriggerCheckMs = 500;

// Edge detection, per bot: the triggers it is inside and the instance they
// belong to. Entering a new map or instance forgets them all.
struct BotAreaTriggerState
{
    uint32_t mapId = 0;
    uint32_t instanceId = 0;
    std::vector<uint32_t> inside;

    // insideNow: triggers the bot is inside now (delta 0).
    // nearNow: triggers within kBotAreaTriggerLeaveMargin (a superset).
    // Returns the triggers just entered; those fire.
    std::vector<uint32_t> Update(uint32_t map, uint32_t instance,
                                 std::vector<uint32_t> const& insideNow,
                                 std::vector<uint32_t> const& nearNow)
    {
        if (map != mapId || instance != instanceId)
        {
            inside.clear();
            mapId = map;
            instanceId = instance;
        }

        std::vector<uint32_t> entered;
        for (uint32_t id : insideNow)
            if (std::find(inside.begin(), inside.end(), id) == inside.end())
                entered.push_back(id);

        // Keep what the bot has not left yet, add what it just entered.
        std::vector<uint32_t> kept;
        for (uint32_t id : inside)
            if (std::find(nearNow.begin(), nearNow.end(), id) != nearNow.end() ||
                std::find(insideNow.begin(), insideNow.end(), id) != insideNow.end())
                kept.push_back(id);
        for (uint32_t id : entered)
            kept.push_back(id);
        inside.swap(kept);
        return entered;
    }

    void Clear() { inside.clear(); }

    // One check: partyAllowed is only asked for (the callable runs) while the
    // bot stands inside a trigger. A party that may not fire forgets the
    // triggers, so they fire once it may (a human arrives, a run starts).
    template <class PartyAllowed>
    std::vector<uint32_t> Step(uint32_t map, uint32_t instance,
                               std::vector<uint32_t> const& insideNow,
                               std::vector<uint32_t> const& nearNow,
                               PartyAllowed partyAllowed)
    {
        if (!insideNow.empty() && !partyAllowed())
        {
            Clear();
            return {};
        }
        return Update(map, instance, insideNow, nearNow);
    }
};

} // namespace ai

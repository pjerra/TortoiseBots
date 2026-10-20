#pragma once

// Mobs for the dashboard's Dungeons tab (issue #1101): one DUNGEON_MOBS datagram set per 2 s snapshot cycle lists, for every
// instance copy (map + instance) that holds at least one bot, the run's members (guid, raw guid, target) and the mobs near them
// (the COMMANDERS mob object), so the dashboard map can draw mobs, threat lines and target lines for every run, not only for a
// commander binding.
//
// Where the mobs come from (the glue in commands/BotCommands.cpp, CollectDungeonMobs): the members' own victims and selections,
// the units attacking them, the bots' cached "attackers" lists and the cached "possible targets" lists of a few sight readers.
// Each guid is resolved at most once per run per cycle; no grid or world scan happens.
//
// This header holds the policy: the caps, the tier order, which bots read their sight lists, the run rotation and the packing
// into datagrams. Standard library only (Player is a forward declaration), so tools/test_dungeon_mob_telemetry.cpp compiles it
// without a game header.
//
// Wire format (additive: an older dashboard drops the unknown type; the protocol version is unchanged):
//   {"v":12,"session":S,"seq":N,"ts":T,"type":"DUNGEON_MOBS","part":0,"parts":2,"runs":[
//     {"map":389,"instance":7,"raid":false,
//      "members":[{"guid":1234,"id":"1234","target":"17370988236253874177"}],   target left out when none
//      "mobs_map":389,"mobs_total":57,"mobs":[ <the COMMANDERS mob object> ]}]}
// id, target and victim are raw guids as decimal strings (64 bits do not fit a JSON number). A run is never split across parts;
// each part stands alone. "mobs_map", "mobs_total" and "mobs" are present only when the mob list was read ("mobs":[] = read,
// none found). One datagram with "runs":[] follows the cycle in which the last run ended.

#include "CommanderTelemetry.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

class Player;

namespace TortoiseBots {
namespace DungeonMobTelemetry {

using CommanderTelemetry::MobInfo;

constexpr size_t kPartyMobCap = 24;        // a party run (<= kPartySize members): the commander feed's own cap
constexpr size_t kRaidMobCap = 48;         // a raid run
constexpr size_t kMinMobsPerRun = 8;       // the floor when many runs share the cycle budget
constexpr size_t kMaxMobsPerCycle = 384;   // all runs of one cycle together
constexpr size_t kMaxRunsPerCycle = 32;    // more runs than this take turns (RotationWindow)
constexpr size_t kMaxMobsPerPayload = 96;  // one datagram
constexpr size_t kMaxRunsPerPayload = 8;   // one datagram
constexpr size_t kMaxPayloadBytes = 32000; // one datagram (UDP allows 65507)
constexpr size_t kSightReaders = 10;       // bots of a raid run whose "possible targets" list is read
constexpr size_t kCandidateFactor = 4;     // stop gathering sight-list candidates at this many times the run's cap
constexpr size_t kPartySize = 5;
constexpr size_t kMaxMembersPerRun = 40;   // members listed per run (a raid holds 40)

// Where the gatherer found a unit (the first source wins: the glue resolves each guid once per run).
enum Source : uint8_t
{
    kFromSelection = 0, // a member's victim or selected target
    kFromAttackers = 1, // attacking a member (its attacker set or the bot's cached "attackers")
    kFromSight = 2,     // a bot's cached "possible targets"
};

// Selection order, lowest first.
enum Tier : uint8_t
{
    kTierEngaged = 0,  // a member's victim or target, or a unit that targets a member
    kTierAttacker = 1, // attacking a member; a boss from any source
    kTierHostile = 2,  // seen, hostile
    kTierNeutral = 3,  // seen, neutral
    kTierOther = 4,    // seen, a critter, civilian, guard or unattackable unit
};

struct Candidate
{
    MobInfo mob;
    uint8_t source = kFromSight;
    uint8_t tier = kTierOther; // set by SelectMobs
    float distSq = 0.0f;       // to the nearest member, set by SelectMobs
};

// A member of a run as the emitter reads it (a bot or a real player in the same instance copy).
struct MemberInfo
{
    uint32_t guid = 0;   // low guid: joins the roster row
    uint64_t id = 0;     // raw guid
    uint64_t target = 0; // raw guid of its selection, else its victim; 0 = none
    float x = 0.0f;
    float y = 0.0f;
    bool bot = false;    // has an AI (only bots hold cached lists)
    bool tank = false;
};

// One run as it goes on the wire.
struct RunInfo
{
    uint32_t map = 0;
    uint32_t instance = 0;
    bool raid = false;
    std::vector<MemberInfo> members;
    bool mobsReported = false; // the mob list was read (it may be empty)
    size_t mobsTotal = 0;      // distinct candidates before the cap
    std::vector<MobInfo> mobs;
};

// One run handed to the provider (the commands layer): live objects, world thread only. The emitter fills map .. cap, the
// provider fills mobsReported and candidates.
struct RunRef
{
    uint32_t map = 0;
    uint32_t instance = 0;
    bool raid = false;
    std::vector<Player*> members;      // bots and real players of this copy
    std::vector<Player*> sightReaders; // the bots whose "possible targets" list is read (PickSightReaders)
    size_t cap = kPartyMobCap;         // PerRunCap; the provider stops gathering sight lists at CandidateBudget(cap)
    bool mobsReported = false;
    std::vector<Candidate> candidates;
};

// The mob cap of one run: a party 24, a raid 48; when runCount runs at their normal cap would pass kMaxMobsPerCycle, every
// run gets max(kMinMobsPerRun, kMaxMobsPerCycle / runCount) at most. runCount is the number of runs sent this cycle (<=
// kMaxRunsPerCycle), so the cycle total stays <= kMaxMobsPerCycle.
inline size_t PerRunCap(size_t runCount, size_t members)
{
    size_t const normal = members > kPartySize ? kRaidMobCap : kPartyMobCap;
    if (runCount == 0)
        return normal;
    size_t const share = std::max(kMinMobsPerRun, kMaxMobsPerCycle / runCount);
    return std::min(normal, share);
}

inline size_t CandidateBudget(size_t cap)
{
    return cap * kCandidateFactor;
}

// Which of `total` runs are sent this cycle: all of them up to `max`, else `max` of them starting at offset (mod total),
// wrapping. The caller advances offset by `max` each cycle so every run takes its turn.
inline std::vector<size_t> RotationWindow(size_t total, size_t offset, size_t max = kMaxRunsPerCycle)
{
    std::vector<size_t> out;
    if (total == 0 || max == 0)
        return out;
    if (total <= max)
    {
        out.reserve(total);
        for (size_t i = 0; i < total; ++i)
            out.push_back(i);
        return out;
    }
    out.reserve(max);
    size_t const start = offset % total;
    for (size_t i = 0; i < max; ++i)
        out.push_back((start + i) % total);
    return out;
}

// The members (indices) whose cached "possible targets" list is read. A party: every bot. A raid run: the tanks first, then the
// bot nearest the run's centre, then the rest in turn (cycle picks where the turn starts), at most `max`. Players never.
inline std::vector<size_t> PickSightReaders(std::vector<MemberInfo> const& members, uint64_t cycle, size_t max = kSightReaders)
{
    std::vector<size_t> out;
    std::vector<size_t> bots;
    for (size_t i = 0; i < members.size(); ++i)
        if (members[i].bot)
            bots.push_back(i);
    if (members.size() <= kPartySize)
    {
        if (bots.size() > max)
            bots.resize(max);
        return bots;
    }
    std::vector<bool> taken(members.size(), false);
    auto take = [&](size_t i) { if (out.size() < max && !taken[i]) { taken[i] = true; out.push_back(i); } };
    for (size_t i : bots)
        if (members[i].tank)
            take(i);
    if (bots.empty())
        return out;
    float cx = 0.0f, cy = 0.0f;
    for (MemberInfo const& m : members)
    {
        cx += m.x;
        cy += m.y;
    }
    cx /= static_cast<float>(members.size());
    cy /= static_cast<float>(members.size());
    size_t nearest = members.size();
    float best = 0.0f;
    for (size_t i : bots)
    {
        if (taken[i])
            continue;
        float const dx = members[i].x - cx, dy = members[i].y - cy;
        float const d = dx * dx + dy * dy;
        if (nearest == members.size() || d < best)
        {
            nearest = i;
            best = d;
        }
    }
    if (nearest != members.size())
        take(nearest);
    size_t const start = static_cast<size_t>(cycle % bots.size());
    for (size_t k = 0; k < bots.size() && out.size() < max; ++k)
        take(bots[(start + k) % bots.size()]);
    return out;
}

// The tier of a candidate (see Tier).
inline uint8_t TierOf(Candidate const& c, std::unordered_set<uint64_t> const& memberIds)
{
    uint8_t tier;
    if (c.source == kFromSelection || (c.mob.victim && memberIds.count(c.mob.victim)))
        tier = kTierEngaged;
    else if (c.source == kFromAttackers)
        tier = kTierAttacker;
    else if (c.mob.kind != 0)
        tier = kTierOther;
    else
        tier = c.mob.reaction == 'h' ? kTierHostile : kTierNeutral;
    if (c.mob.rank == 'b' && tier > kTierAttacker)
        tier = kTierAttacker;
    return tier;
}

// Picks the run's mobs: duplicates dropped (the first one kept), then the lowest tier first and the nearest to a member within
// a tier (a stable order), at most `cap`. Returns the number of distinct candidates before the cut (mobs_total).
inline size_t SelectMobs(std::vector<Candidate>& c, std::vector<MemberInfo> const& members, size_t cap, std::vector<MobInfo>& out)
{
    out.clear();
    std::unordered_set<uint64_t> memberIds;
    for (MemberInfo const& m : members)
        if (m.id)
            memberIds.insert(m.id);
    std::unordered_set<uint64_t> seen;
    size_t kept = 0;
    for (size_t i = 0; i < c.size(); ++i)
    {
        if (!seen.insert(c[i].mob.id).second)
            continue;
        Candidate& k = c[kept++];
        if (&k != &c[i])
            k = std::move(c[i]);
        k.tier = TierOf(k, memberIds);
        float best = 0.0f;
        bool any = false;
        for (MemberInfo const& m : members)
        {
            float const dx = k.mob.x - m.x, dy = k.mob.y - m.y;
            float const d = dx * dx + dy * dy;
            if (!any || d < best)
            {
                best = d;
                any = true;
            }
        }
        k.distSq = best;
    }
    c.resize(kept);
    std::stable_sort(c.begin(), c.end(), [](Candidate const& a, Candidate const& b)
    {
        if (a.tier != b.tier)
            return a.tier < b.tier;
        return a.distSq < b.distSq;
    });
    size_t const count = std::min(cap, c.size());
    out.reserve(count);
    for (size_t i = 0; i < count; ++i)
        out.push_back(c[i].mob);
    return kept;
}

// One run's JSON object. Members are capped at kMaxMembersPerRun and mobs at kRaidMobCap here too, so no caller can make a run
// larger than one datagram holds.
inline std::string BuildRunJson(RunInfo const& r)
{
    std::ostringstream ss;
    ss.setf(std::ios::fixed);
    ss.precision(1);
    ss << "{\"map\":" << r.map << ",\"instance\":" << r.instance << ",\"raid\":" << (r.raid ? "true" : "false")
       << ",\"members\":[";
    size_t members = 0;
    for (MemberInfo const& m : r.members)
    {
        if (members >= kMaxMembersPerRun)
            break;
        if (members++)
            ss << ",";
        ss << "{\"guid\":" << m.guid;
        if (m.id)
            ss << ",\"id\":\"" << m.id << "\"";
        if (m.target)
            ss << ",\"target\":\"" << m.target << "\"";
        ss << "}";
    }
    ss << "]";
    if (r.mobsReported)
    {
        ss << ",\"mobs_map\":" << r.map << ",\"mobs_total\":" << r.mobsTotal << ",\"mobs\":[";
        size_t mobs = 0;
        for (MobInfo const& m : r.mobs)
        {
            if (mobs >= kRaidMobCap)
                break;
            if (mobs++)
                ss << ",";
            CommanderTelemetry::AppendMobJson(ss, m);
        }
        ss << "]";
    }
    ss << "}";
    return ss.str();
}

inline size_t ListedMobs(RunInfo const& r)
{
    return r.mobsReported ? std::min(r.mobs.size(), kRaidMobCap) : 0;
}

inline std::string PartHeader(int protocolVersion, uint64_t session, uint64_t seq, int64_t ts, size_t part, size_t parts)
{
    std::ostringstream ss;
    ss << "{\"v\":" << protocolVersion << ",\"session\":" << session << ",\"seq\":" << seq << ",\"ts\":" << ts
       << ",\"type\":\"DUNGEON_MOBS\",\"part\":" << part << ",\"parts\":" << parts << ",\"runs\":[";
    return ss.str();
}

// The datagrams of one cycle. Whole runs only; a part closes when the next run would pass kMaxMobsPerPayload mobs,
// kMaxRunsPerPayload runs or kMaxPayloadBytes bytes. No runs: one datagram with "runs":[] (the end marker).
inline std::vector<std::string> PackRuns(int protocolVersion, uint64_t session, uint64_t seq, int64_t ts,
                                         std::vector<RunInfo> const& runs)
{
    std::vector<std::string> bodies;
    bodies.reserve(runs.size());
    for (RunInfo const& r : runs)
        bodies.push_back(BuildRunJson(r));

    // Group first (the header names the part count), with the longest header this cycle can have.
    size_t const headerBytes = PartHeader(protocolVersion, session, seq, ts, runs.size(), runs.size()).size() + 2; // + "]}"
    std::vector<std::pair<size_t, size_t>> groups; // [first, end) into runs
    size_t first = 0, bytes = headerBytes, mobs = 0;
    for (size_t i = 0; i < runs.size(); ++i)
    {
        size_t const add = bodies[i].size() + (i > first ? 1 : 0);
        size_t const addMobs = ListedMobs(runs[i]);
        bool const full = i > first &&
            (i - first >= kMaxRunsPerPayload || mobs + addMobs > kMaxMobsPerPayload || bytes + add > kMaxPayloadBytes);
        if (full)
        {
            groups.emplace_back(first, i);
            first = i;
            bytes = headerBytes + bodies[i].size();
            mobs = addMobs;
            continue;
        }
        bytes += add;
        mobs += addMobs;
    }
    if (!runs.empty())
        groups.emplace_back(first, runs.size());

    std::vector<std::string> out;
    if (groups.empty())
    {
        out.push_back(PartHeader(protocolVersion, session, seq, ts, 0, 1) + "]}");
        return out;
    }
    out.reserve(groups.size());
    for (size_t g = 0; g < groups.size(); ++g)
    {
        std::string s = PartHeader(protocolVersion, session, seq, ts, g, groups.size());
        for (size_t i = groups[g].first; i < groups[g].second; ++i)
        {
            if (i > groups[g].first)
                s += ",";
            s += bodies[i];
        }
        s += "]}";
        out.push_back(std::move(s));
    }
    return out;
}

} // namespace DungeonMobTelemetry
} // namespace TortoiseBots

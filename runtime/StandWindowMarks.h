#pragma once

// The countdown of a boss's stand window: the core casts Deep Breath triggered, so it never reaches a cast slot and the
// module times it from when it first saw the window open (BossStandPolicy.h). A mark belongs to one unit in one instance
// — a creature's raw guid repeats across copies of a map — so the key is (map id, instance id, raw guid). Otherwise the
// second copy of Onyxia's Lair would count down the first copy's breath, and a mark the module never saw close would
// time the next encounter.
//
// A window can disappear without a closing observation: the unit leaves the report, the binding is released, the run
// ends. Every snapshot cycle prunes marks not observed for kStaleMs, a binding teardown forgets the keys that binding
// reported, and a mark that somehow outlived both is never reused (a stale one starts a new window on its next sighting).
//
// Standard library only, so tools/test_stand_window_marks.cpp compiles it without a game header.

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <unordered_map>
#include <unordered_set>

namespace TortoiseBots {
namespace StandWindow {

// How long a mark may go unobserved before it is pruned. Longer than any stand window (the hold is seconds) and short
// enough that a boss reset and re-engaged cannot reuse the old start.
constexpr uint32_t kStaleMs = 30000;

struct Key
{
    uint32_t map = 0;
    uint32_t instance = 0;
    uint64_t guid = 0;

    bool operator==(Key const& other) const
    {
        return map == other.map && instance == other.instance && guid == other.guid;
    }
    bool operator!=(Key const& other) const { return !(*this == other); }
};

struct KeyHash
{
    std::size_t operator()(Key const& key) const
    {
        std::size_t h = static_cast<std::size_t>(key.map) * 0x9E3779B1u;
        h ^= static_cast<std::size_t>(key.instance) * 0x85EBCA77u;
        h ^= static_cast<std::size_t>(key.guid) * 0xC2B2AE3Du;
        h ^= static_cast<std::size_t>(key.guid >> 32) * 0x27D4EB2Fu;
        return h;
    }
};

struct Mark
{
    uint32_t startMs = 0; // the window's first sighting; the countdown runs from it
    uint32_t seenMs = 0;  // the last sighting; the prune runs from it
};

// The marks of every unit whose window is being timed, world thread only like the rest of the commander snapshot.
class Marks
{
public:
    // The cast time left of a window of `totalMs` opened for this key, starting it on its first sighting. A mark not seen
    // for kStaleMs is treated as a new window, so a missed prune cannot carry a countdown into the next encounter.
    uint32_t CastLeft(Key const& key, uint32_t nowMs, uint32_t totalMs)
    {
        auto it = marks_.find(key);
        if (it == marks_.end() || IsStale(it->second, nowMs))
            it = marks_.insert_or_assign(key, Mark{nowMs, nowMs}).first;
        else
            it->second.seenMs = nowMs;
        uint32_t const elapsed = nowMs - it->second.startMs;
        return elapsed < totalMs ? totalMs - elapsed : 0;
    }

    void Forget(Key const& key) { marks_.erase(key); }

    // Drops every mark not observed for kStaleMs.
    void Prune(uint32_t nowMs)
    {
        for (auto it = marks_.begin(); it != marks_.end();)
            it = IsStale(it->second, nowMs) ? marks_.erase(it) : std::next(it);
    }

    bool Contains(Key const& key) const { return marks_.find(key) != marks_.end(); }

    std::size_t Size() const { return marks_.size(); }
    void Clear() { marks_.clear(); }

private:
    static bool IsStale(Mark const& mark, uint32_t nowMs) { return nowMs - mark.seenMs > kStaleMs; }

    std::unordered_map<Key, Mark, KeyHash> marks_;
};

// A mark's reporter: the binding (a commander's raw guid) whose snapshot observed the window, or kNoOwner for a report
// that belongs to no binding (the Dungeons tab reads runs, not bindings).
constexpr uint64_t kNoOwner = 0;

// The marks with the binding that reported each one. A binding's teardown forgets exactly its keys at once: the mark of
// a window it saw moments before the release is fresh, so the stale prune would keep it, and another binding still
// reporting the same unit would keep it alive until that binding ended too. A unit reported by two bindings keeps one
// countdown; a teardown drops the mark only when no other binding still claims it.
class OwnedMarks
{
public:
    // As Marks::CastLeft, recorded under the binding that reported it.
    uint32_t CastLeft(uint64_t owner, Key const& key, uint32_t nowMs, uint32_t totalMs)
    {
        uint32_t const left = marks_.CastLeft(key, nowMs, totalMs);
        owners_[owner].insert(key);
        return left;
    }

    // The window closed: the mark and every binding's claim on it go.
    void Forget(Key const& key)
    {
        marks_.Forget(key);
        for (auto it = owners_.begin(); it != owners_.end();)
        {
            it->second.erase(key);
            it = it->second.empty() ? owners_.erase(it) : std::next(it);
        }
    }

    // A binding's teardown: drops its marks, keeping one that another binding still reports.
    void ForgetOwner(uint64_t owner)
    {
        auto const owned = owners_.find(owner);
        if (owned == owners_.end())
            return;
        for (Key const& key : owned->second)
        {
            bool shared = false;
            for (auto const& other : owners_)
                if (other.first != owner && other.second.count(key))
                {
                    shared = true;
                    break;
                }
            if (!shared)
                marks_.Forget(key);
        }
        owners_.erase(owned);
    }

    // Drops every mark not observed for kStaleMs, and the claims on it.
    void Prune(uint32_t nowMs)
    {
        marks_.Prune(nowMs);
        for (auto it = owners_.begin(); it != owners_.end();)
        {
            for (auto claim = it->second.begin(); claim != it->second.end();)
                claim = marks_.Contains(*claim) ? std::next(claim) : it->second.erase(claim);
            it = it->second.empty() ? owners_.erase(it) : std::next(it);
        }
    }

    std::size_t Size() const { return marks_.Size(); }
    // How many marks the binding reported (a mark two bindings report counts for each of them).
    std::size_t OwnerSize(uint64_t owner) const
    {
        auto const owned = owners_.find(owner);
        return owned == owners_.end() ? 0 : owned->second.size();
    }

    void Clear()
    {
        marks_.Clear();
        owners_.clear();
    }

private:
    Marks marks_;
    std::unordered_map<uint64_t, std::unordered_set<Key, KeyHash>> owners_;
};

} // namespace StandWindow
} // namespace TortoiseBots

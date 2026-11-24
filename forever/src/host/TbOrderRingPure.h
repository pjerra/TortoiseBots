// The game-free half of the order transition ring (TBF-B1 third review finding 2): one small bounded ring per bot of the order
// words the bot engine went through (queued, ok, fail:<code>), read one entry per engine snapshot through the ring's own cursor.
// The order record's single `seq/verb/result` slot is what `.tb state` and the release helpers read; an order that a newer one
// overwrote still has to reach the API as its own transition, so `TbOrders.cpp` pushes every transition here and the provider
// (host/TbLuaBots.cpp) takes the next unread one per tick through `h3::PollOrderEvent`.
//
// Bounded: at most kCapacity entries; an overflow drops the oldest entry, and when that one was unread the reader is handed it
// once, failed as "lost", so a ticket of the engine never waits for a result that vanished. The ring lives in the bot's order
// record and goes with it at logout (nothing leaks across a relog).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace TortoiseBots
{
namespace h3
{
class OrderTransitionRing
{
public:
    static constexpr size_t kCapacity = 16;

    struct Entry
    {
        uint64_t id = 0;     // ring order (1, 2, 3 ...): the reader's cursor
        uint32_t seq = 0;    // the order's number (h3::OrderStatus::seq)
        std::string verb;
        std::string result;  // queued | pending... | ok | fail:<code> (a dropped unread one is reported as fail:lost)
    };

    // One transition of one order. Bounded: past kCapacity the oldest entry goes; an unread oldest is remembered as `fail:lost`
    // (the first such drop: a later drop before it is read is lost in the same answer).
    void Push(uint32_t seq, std::string verb, std::string result)
    {
        if (_entries.size() >= kCapacity)
        {
            Entry dropped = std::move(_entries.front());
            _entries.erase(_entries.begin());
            if (dropped.id > _read && !_lost)
            {
                _lost = true;
                dropped.result = "fail:lost";
                _lostEntry = std::move(dropped);
            }
        }
        Entry entry;
        entry.id = _nextId++;
        entry.seq = seq;
        entry.verb = std::move(verb);
        entry.result = std::move(result);
        _entries.push_back(std::move(entry));
    }

    // The oldest transition the reader has not taken, in ring order; false when there is none. A lost one is answered once, in
    // its place in the order (it was the oldest unread entry when it was dropped).
    bool Take(Entry& out)
    {
        if (_lost && _lostEntry.id > _read)
        {
            out = _lostEntry;
            _read = out.id;
            _lost = false;
            return true;
        }
        _lost = false;
        for (Entry const& entry : _entries)
            if (entry.id > _read)
            {
                out = entry;
                _read = out.id;
                return true;
            }
        return false;
    }

    // What the record's own last word reports: the newest transition pushed (0 = none yet).
    Entry const* Newest() const { return _entries.empty() ? nullptr : &_entries.back(); }

    bool Pending() const
    {
        if (_lost && _lostEntry.id > _read)
            return true;
        for (Entry const& entry : _entries)
            if (entry.id > _read)
                return true;
        return false;
    }

    size_t Size() const { return _entries.size(); }

    // A logout or a fresh record: nothing of the old session is left for the next reader.
    void Clear()
    {
        _entries.clear();
        _lost = false;
        _lostEntry = Entry();
        _read = 0;   // ids are per ring; a cleared ring starts its ids at 1 again
        _nextId = 1;
    }

private:
    std::vector<Entry> _entries;
    uint64_t _nextId = 1;
    uint64_t _read = 0;
    bool _lost = false;
    Entry _lostEntry;
};
}  // namespace h3
}  // namespace TortoiseBots

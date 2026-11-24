// Standalone test of the order transition ring (forever/src/host/TbOrderRingPure.h), TBF-B1 third review finding 2:
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_order_ring_check.cpp -o /tmp/test_order_ring_check && /tmp/test_order_ring_check
// The ring is the per-bot log behind the engine's lastOrder: every transition of every order, read one entry per snapshot by the
// ring's own cursor, bounded at 16 with an overflow reported as `fail:lost` for the oldest unread entry.
#include "forever/src/host/TbOrderRingPure.h"

#include <cstdio>
#include <string>

namespace h3 = TortoiseBots::h3;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void TestOrderOfTransitions()
{
    // An order overwritten by a newer one keeps its own outcome: queued(1), queued(2), ok(1) are three transitions, not one word.
    h3::OrderTransitionRing ring;
    CHECK(!ring.Pending());
    CHECK(ring.Size() == 0);

    ring.Push(1, "gather", "queued");
    ring.Push(2, "craft", "queued");
    ring.Push(1, "gather", "ok");

    h3::OrderTransitionRing::Entry entry;
    CHECK(ring.Pending());
    CHECK(ring.Take(entry) && entry.seq == 1 && entry.verb == "gather" && entry.result == "queued");
    CHECK(ring.Take(entry) && entry.seq == 2 && entry.verb == "craft" && entry.result == "queued");
    CHECK(ring.Take(entry) && entry.seq == 1 && entry.verb == "gather" && entry.result == "ok");
    CHECK(!ring.Take(entry));   // all read: nothing new
    CHECK(!ring.Pending());

    // A newer transition of the same order after the reader caught up is delivered again.
    ring.Push(2, "craft", "fail:expired");
    CHECK(ring.Take(entry) && entry.seq == 2 && entry.result == "fail:expired");
    CHECK(!ring.Take(entry));

    // Newest is the record's own word (h3::LastOrder), whatever the reader position.
    CHECK(ring.Newest() && ring.Newest()->seq == 2 && ring.Newest()->result == "fail:expired");
}

static void TestOverflow()
{
    // kCapacity entries, all unread: the next push drops the oldest unread and the reader is handed it once, failed as lost.
    h3::OrderTransitionRing ring;
    for (uint32_t i = 1; i <= h3::OrderTransitionRing::kCapacity; ++i)
        ring.Push(i, "attack", "queued");
    CHECK(ring.Size() == h3::OrderTransitionRing::kCapacity);

    h3::OrderTransitionRing::Entry entry;
    ring.Push(100, "follow", "queued");   // drops order 1 (unread)
    CHECK(ring.Take(entry) && entry.seq == 1 && entry.result == "fail:lost");
    CHECK(ring.Take(entry) && entry.seq == 2 && entry.result == "queued");
    for (uint32_t i = 3; i <= h3::OrderTransitionRing::kCapacity; ++i)
        CHECK(ring.Take(entry) && entry.seq == i);
    CHECK(ring.Take(entry) && entry.seq == 100);
    CHECK(!ring.Take(entry));

    // A drop of an already-read entry is silent: nothing is reported lost.
    h3::OrderTransitionRing read;
    for (uint32_t i = 1; i <= h3::OrderTransitionRing::kCapacity; ++i)
    {
        read.Push(i, "attack", "ok");
        CHECK(read.Take(entry) && entry.seq == i);
    }
    read.Push(200, "attack", "queued");   // drops order 1, which was read
    CHECK(read.Take(entry) && entry.seq == 200 && entry.result == "queued");
    CHECK(!read.Take(entry));

    // Two unread drops before a read: the first lost answer is the oldest one; the second stays lost and is not invented.
    h3::OrderTransitionRing twice;
    for (uint32_t i = 1; i <= h3::OrderTransitionRing::kCapacity; ++i)
        twice.Push(i, "attack", "queued");
    twice.Push(101, "attack", "queued");   // drops 1
    twice.Push(102, "attack", "queued");   // drops 2 (lost too)
    CHECK(twice.Take(entry) && entry.seq == 1 && entry.result == "fail:lost");
    CHECK(twice.Take(entry) && entry.seq == 3 && entry.result == "queued");   // 2 was dropped unread and is already accounted lost

    // Clear: nothing of the old session is left (the per-bot record is erased at logout).
    twice.Clear();
    CHECK(twice.Size() == 0 && !twice.Pending());
    CHECK(!twice.Take(entry));
    twice.Push(7, "follow", "ok");
    CHECK(twice.Take(entry) && entry.seq == 7);
}

int main()
{
    TestOrderOfTransitions();
    TestOverflow();
    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}

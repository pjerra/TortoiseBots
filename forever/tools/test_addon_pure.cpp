// Standalone test of the addon channel rule (forever/src/host/TbAddonPure.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -I forever/src forever/tools/test_addon_pure.cpp -o /tmp/test_addon && /tmp/test_addon
#include "host/TbAddonPure.h"

#include <cstdio>
#include <string>
#include <string_view>

namespace pure = TortoiseBots::addon::pure;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main()
{
    // 1. Parse: trim, first word lowercased is the verb, rest trimmed is the args.
    {
        pure::Parsed p = pure::Parse("  RoSter  swift  ");
        CHECK(p.verb == "roster");
        CHECK(p.args == "swift");
    }
    {
        pure::Parsed p = pure::Parse("ACTION Follow party");
        CHECK(p.verb == "action");
        CHECK(p.args == "Follow party");
    }
    {
        pure::Parsed p = pure::Parse("  ping  ");
        CHECK(p.verb == "ping");
        CHECK(p.args.empty());
    }
    {
        pure::Parsed p = pure::Parse("   ");
        CHECK(p.verb.empty());
        CHECK(p.args.empty());
    }
    {
        pure::Parsed p = pure::Parse("");
        CHECK(p.verb.empty());
        CHECK(p.args.empty());
    }
    {
        pure::Parsed p = pure::Parse("a\tb\tc");
        CHECK(p.verb == "a");
        CHECK(p.args == "b\tc");
    }

    // 2. Lower: registry keys stay lowercase.
    {
        CHECK(pure::Lower("RoSter") == "roster");
        CHECK(pure::Lower("") == "");
    }

    // 3. Safe: '|' CR LF become blanks, empty becomes "-".
    {
        CHECK(pure::Safe("a|b\rc\nd") == "a b c d");
        CHECK(pure::Safe("") == "-");
        CHECK(pure::Safe("follow") == "follow");
    }

    // 4. Ack/Err: exact TB wire shapes (CommandShared.cpp:81-99).
    {
        CHECK(pure::AckLine("follow", "party", 3, "-") == "TBM:ACTION_ACK|follow|party|3|-");
        CHECK(pure::AckLine("attack|raid", "party", 1, "") == "TBM:ACTION_ACK|attack raid|party|1|-");
    }
    {
        CHECK(pure::ErrLine("nonsense", "unsupported", "not available on this server yet") ==
            "TBM:ACTION_ERR|nonsense|unsupported|not available on this server yet");
        CHECK(pure::ErrLine("", "unsupported", "no|A") == "TBM:ACTION_ERR|-|unsupported|no A");
    }

    // 5. Send splits one message per non-empty line; truncation is 255 bytes.
    {
        std::vector<std::string> lines = pure::SplitLines("a\n\nb\n");
        CHECK(lines.size() == 2);
        CHECK(lines[0] == "a");
        CHECK(lines[1] == "b");
        CHECK(pure::SplitLines("").empty());
        CHECK(pure::SplitLines("one").size() == 1);
    }
    {
        std::string big(300, 'x');
        CHECK(pure::Truncate(big).size() == 255);
        CHECK(pure::Truncate("tiny") == "tiny");
        CHECK(pure::kMaxLineBytes == 255);
    }

    // 6. Queue: arrival order across requesters.
    {
        pure::PendingQueue q;
        pure::RequesterKey a{ 1, 0 }, b{ 2, 0 };
        CHECK(q.Enqueue(a, "first"));
        CHECK(q.Enqueue(b, "other"));
        CHECK(q.Enqueue(a, "second"));
        CHECK(q.PendingCount() == 3);
        std::string order;
        std::size_t n = q.Drain(
            [](pure::RequesterKey) { return 1; },
            [&](int, pure::PendingCommand const& c) { order += c.text + ";"; },
            [](int, pure::DroppedReport const&) { CHECK(false); });
        CHECK(n == 3);
        CHECK(order == "first;other;second;");
        CHECK(q.PendingCount() == 0);
    }

    // 7. Queue: cap 128 per requester, excess dropped with one report.
    {
        pure::PendingQueue q;
        pure::RequesterKey a{ 7, 0 };
        for (std::size_t i = 0; i < pure::kMaxPendingPerRequester; ++i)
            CHECK(q.Enqueue(a, "x"));
        CHECK(!q.Enqueue(a, "drop1"));
        CHECK(!q.Enqueue(a, "drop2"));
        std::size_t reports = 0;
        std::size_t dropped = 0;
        std::size_t ran = q.Drain(
            [](pure::RequesterKey) { return 1; },
            [](int, pure::PendingCommand const&) { },
            [&](int, pure::DroppedReport const& r) { ++reports; dropped = r.count; });
        CHECK(ran == 128);
        CHECK(reports == 1);
        CHECK(dropped == 2);
    }

    // 8. Queue: the cap is per requester; a logged-out requester drops silently.
    {
        pure::PendingQueue q;
        pure::RequesterKey a{ 1, 0 }, gone{ 9, 0 };
        for (std::size_t i = 0; i < pure::kMaxPendingPerRequester; ++i)
            CHECK(q.Enqueue(a, "x"));
        CHECK(q.Enqueue(gone, "y"));
        std::size_t ran = q.Drain(
            [](pure::RequesterKey k) { return k.lo == 1 ? 1 : 0; },
            [](int, pure::PendingCommand const&) { },
            [](int, pure::DroppedReport const&) { CHECK(false); });
        CHECK(ran == 128);
    }

    // 9. Flood message matches TB's queue-full text.
    {
        CHECK(pure::FloodMessage(3) == "Too many bot commands at once: 3 ignored.");
    }

    if (g_fail == 0)
        std::printf("OK addon pure: %d checks\n", g_checks);
    return g_fail == 0 ? 0 : 1;
}

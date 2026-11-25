// Standalone tests for runtime/DecisionCaptureRows.h (ticket #307): the read
// path's bounded window of decision rows, its paging and cursor rules, the row
// limit clamp, the free-text cut and the one-row-one-line guarantee.
//
// The header includes the standard library only, and this file compiles it
// without a single game header.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_decision_capture_rows.cpp -o /tmp/test_decision_capture_rows
//   /tmp/test_decision_capture_rows

#include "../runtime/DecisionCaptureRows.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace TortoiseBots::DecisionCapture;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)
#define CHECK_EQ(a, b) do { \
    ++checks; \
    std::string const va = (a); \
    std::string const vb = (b); \
    if (va != vb) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n  got:  %s\n  want: %s\n", __FILE__, __LINE__, #a, va.c_str(), vb.c_str()); \
        std::exit(1); \
    } \
} while (0)

namespace
{
Row MakeRow(std::uint64_t seq)
{
    Row row;
    row.seq = seq;
    row.unixMs = 1764000000000ull + seq;
    row.dtMs = 1000;
    row.state = 0;
    row.phase = 2;
    row.step = 3;
    row.order = "pullback";
    row.note = "pack at the door";
    return row;
}

std::deque<Row> Log(std::uint64_t first, std::uint64_t count)
{
    RowLog log;
    for (std::uint64_t i = 0; i < count; ++i)
        log.Push(MakeRow(first + i));
    return log.Rows();
}
}

static void TestWindowDropsOldest()
{
    RowLog log;
    std::size_t const total = kRowWindow + 10;
    for (std::size_t i = 0; i < total; ++i)
        log.Push(MakeRow(i + 1));
    CHECK_EQ(std::to_string(log.Size()), std::to_string(kRowWindow));
    CHECK_EQ(std::to_string(log.Oldest()), std::to_string(total - kRowWindow + 1));
    CHECK_EQ(std::to_string(log.Newest()), std::to_string(total));
    // The kept rows are the newest ones, still oldest first.
    CHECK_EQ(std::to_string(log.Rows().front().seq), std::to_string(total - kRowWindow + 1));
    CHECK_EQ(std::to_string(log.Rows().back().seq), std::to_string(total));
}

static void TestEmpty()
{
    RowLog log;
    CHECK(log.Empty());
    CHECK_EQ(std::to_string(log.Newest()), "0");
    CHECK_EQ(std::to_string(log.Oldest()), "0");
    Page const page = Select(log.Rows(), 0, 10);
    CHECK_EQ(std::to_string(page.count), "0");
    CHECK_EQ(std::to_string(page.next), "0");
    CHECK(!page.more);
}

static void TestOldestFirstPages()
{
    std::deque<Row> const rows = Log(1, 120);
    Page const page = Select(rows, 0, 50);
    CHECK_EQ(std::to_string(page.first), "0");
    CHECK_EQ(std::to_string(page.count), "50");
    CHECK_EQ(std::to_string(rows[page.first].seq), "1");
    CHECK_EQ(std::to_string(page.next), "50");
    CHECK(page.more);

    // The next call continues exactly where the header's next= said.
    Page const second = Select(rows, page.next, 50);
    CHECK_EQ(std::to_string(second.first), "50");
    CHECK_EQ(std::to_string(second.count), "50");
    CHECK_EQ(std::to_string(rows[second.first].seq), "51");
    CHECK_EQ(std::to_string(second.next), "100");
    CHECK(second.more);

    Page const last = Select(rows, second.next, 50);
    CHECK_EQ(std::to_string(last.count), "20");
    CHECK_EQ(std::to_string(last.next), "120");
    CHECK(!last.more);   // 120 was the newest row: nothing after it
}

static void TestOneAtATime()
{
    std::deque<Row> const rows = Log(1, 3);
    std::uint64_t cursor = 0;
    int pages = 0;
    for (;;)
    {
        Page const page = Select(rows, cursor, 1);
        if (page.count == 0)
            break;
        CHECK_EQ(std::to_string(rows[page.first].seq), std::to_string(cursor + 1));
        cursor = page.next;
        ++pages;
        if (pages > 10)
            break;   // a paging rule that never advances would loop forever
    }
    CHECK_EQ(std::to_string(pages), "3");   // one row per page, then the empty page ends it
    CHECK_EQ(std::to_string(cursor), "3");
}

static void TestMoreOnAnExactFit()
{
    std::deque<Row> const rows = Log(1, 50);
    Page const page = Select(rows, 0, 50);
    CHECK_EQ(std::to_string(page.count), "50");
    CHECK_EQ(std::to_string(page.next), "50");
    CHECK(!page.more);
}

static void TestSinceCursor()
{
    std::deque<Row> const rows = Log(1, 100);
    // Rows AFTER the cursor, not the cursor's own row.
    Page const page = Select(rows, 40, 10);
    CHECK_EQ(std::to_string(page.count), "10");
    CHECK_EQ(std::to_string(rows[page.first].seq), "41");
    CHECK_EQ(std::to_string(page.next), "50");
    CHECK(page.more);

    // A cursor the window has dropped: the read starts at the oldest kept row
    // (the reader can see the gap from next= and count=, and never gets a row
    // it did not ask for).
    Page const fromAcked = Select(rows, 0, 200);
    CHECK_EQ(std::to_string(fromAcked.first), "0");
    CHECK_EQ(std::to_string(fromAcked.count), "100");
    CHECK(!fromAcked.more);

    // A cursor older than every kept row reads the whole window.
    std::deque<Row> const dropped = Log(500, 20);
    Page const again = Select(dropped, 10, 500);
    CHECK_EQ(std::to_string(again.count), "20");
    CHECK_EQ(std::to_string(again.next), "519");
}

static void TestCursorPastTheEnd()
{
    std::deque<Row> const rows = Log(1, 10);
    Page const page = Select(rows, 10, 50);
    CHECK_EQ(std::to_string(page.count), "0");
    CHECK_EQ(std::to_string(page.next), "10");   // unchanged: the caller's cursor is still its place
    CHECK(!page.more);

    Page const beyond = Select(rows, 999, 50);
    CHECK_EQ(std::to_string(beyond.count), "0");
    CHECK_EQ(std::to_string(beyond.next), "999");
    CHECK(!beyond.more);

    Page const emptyLog = Select(Log(0, 0), 7, 50);
    CHECK_EQ(std::to_string(emptyLog.count), "0");
    CHECK_EQ(std::to_string(emptyLog.next), "7");
}

static void TestLimitClamp()
{
    CHECK_EQ(std::to_string(ClampRows(-5)), "1");
    CHECK_EQ(std::to_string(ClampRows(0)), "1");
    CHECK_EQ(std::to_string(ClampRows(1)), "1");
    CHECK_EQ(std::to_string(ClampRows(50)), "50");
    CHECK_EQ(std::to_string(ClampRows(200)), "200");
    CHECK_EQ(std::to_string(ClampRows(201)), "200");
    CHECK_EQ(std::to_string(ClampRows(1'000'000)), "200");
    CHECK_EQ(std::to_string(kDefaultRows), "50");
    CHECK_EQ(std::to_string(kMaxRows), "200");
}

static void TestTextFieldOrder()
{
    Row const row = MakeRow(1);
    auto const fields = TextFields(row);
    CHECK_EQ(std::to_string(fields.size()), "3");
    CHECK_EQ(fields[0].key, "order");
    CHECK_EQ(fields[1].key, "note");
    CHECK_EQ(fields[2].key, "say");
    CHECK(fields[0].value == &row.order);
    CHECK(fields[1].value == &row.note);
    CHECK(fields[2].value == &row.say);
}

static void TestCut()
{
    CHECK_EQ(Cut("short", 240), "short");
    CHECK_EQ(Cut("", 240), "");
    std::string const exact(kRowTextBytes, 'x');
    CHECK_EQ(Cut(exact, kRowTextBytes), exact);
    // One byte over is cut to the cap, not dropped and not kept whole.
    CHECK_EQ(Cut(exact + "y", kRowTextBytes), exact);
    // A cut never leaves half of a UTF-8 sequence behind.
    std::string utf8;
    for (int i = 0; i < 200; ++i)
        utf8 += "\xC3\xA9";   // e-acute, two bytes
    CHECK_EQ(std::to_string(Cut(utf8, 9).size()), "8");
    CHECK_EQ(std::to_string(Cut(utf8, 240).size()), "240");
    // The cut is on a byte boundary of the cap, never past it.
    for (std::size_t cap = 1; cap < 16; ++cap)
        CHECK(Cut(utf8, cap).size() <= cap);
}

static void TestRecordMarker()
{
    RowLog log;
    log.SetNewestRecord(kRecordTick);   // an empty log: a no-op, never a crash
    CHECK(log.Empty());
    log.Push(MakeRow(1));
    CHECK_EQ(std::to_string(log.Rows().back().record), std::to_string(kRecordDropped));   // pessimistic until the sink answers
    log.SetNewestRecord(kRecordStub);
    CHECK_EQ(std::to_string(log.Rows().back().record), std::to_string(kRecordStub));
    log.Push(MakeRow(2));
    CHECK_EQ(std::to_string(log.Rows().back().record), std::to_string(kRecordDropped));
    CHECK_EQ(std::to_string(log.Rows().front().record), std::to_string(kRecordStub));    // the older row keeps its own fate
    CHECK_EQ(RecordName(kRecordTick), "tick");
    CHECK_EQ(RecordName(kRecordStub), "stub");
    CHECK_EQ(RecordName(kRecordDropped), "dropped");
}

static void TestStoredTextIsBounded()
{
    RowLog log;
    Row row = MakeRow(1);
    row.note.assign(kRowStoreBytes + 100, 'n');
    row.order.assign(kRowStoreBytes + 100, 'o');
    row.say.assign(kRowStoreBytes + 100, 's');
    log.Push(std::move(row));
    CHECK_EQ(std::to_string(log.Rows().back().note.size()), std::to_string(kRowStoreBytes));
    CHECK_EQ(std::to_string(log.Rows().back().order.size()), std::to_string(kRowStoreBytes));
    CHECK_EQ(std::to_string(log.Rows().back().say.size()), std::to_string(kRowStoreBytes));
}

static void TestOneRowIsOneLine()
{
    // The grammar caps a reply line at 3,000 bytes (host/TbH3.h MaxLineBytes),
    // and one decision row is never split over continuation lines. A value is
    // percent-encoded, so one byte can cost three; the worst case must fit.
    CHECK(kWorstRowLineBytes <= kRowLineBytes);
    CHECK(kWorstRowLineBytes <= 3000);

    Row row = MakeRow(1);
    row.seq = 18446744073709551615ull;
    row.unixMs = 18446744073709551615ull;
    row.dtMs = 4294967295u;
    row.step = 4294967295u;
    row.order.assign(kRowTextBytes, ' ');
    row.note.assign(kRowTextBytes, '%');
    row.say.assign(kRowTextBytes, '=');
    // Simulate the encoder: every byte outside [A-Za-z0-9._-] costs 3.
    std::size_t line = std::string_view("decision").size();
    auto addNumber = [&line](char const* key, std::uint64_t value) {
        line += 1 + std::string_view(key).size() + 1 + std::to_string(value).size();
    };
    addNumber("seq", row.seq);
    addNumber("t", row.unixMs);
    addNumber("dt", row.dtMs);
    line += 1 + std::string_view("state=").size() + 8;
    line += 1 + std::string_view("phase=").size() + 12;
    addNumber("step", row.step);
    line += 1 + std::string_view("record=").size() + 8;
    for (TextField const& field : TextFields(row))
    {
        line += 1 + std::string_view(field.key).size() + 1;
        for (char c : *field.value)
        {
            bool const plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
            line += plain ? 1 : 3;
        }
    }
    line += 1 + std::string_view("trunc=yes").size();
    line += 1 + 33;   // a reply token: '#', then at most 32 characters
    CHECK(line <= kRowLineBytes);
    CHECK(line <= kWorstRowLineBytes);
}

int main()
{
    TestWindowDropsOldest();
    TestEmpty();
    TestOldestFirstPages();
    TestOneAtATime();
    TestMoreOnAnExactFit();
    TestSinceCursor();
    TestCursorPastTheEnd();
    TestLimitClamp();
    TestTextFieldOrder();
    TestCut();
    TestRecordMarker();
    TestStoredTextIsBounded();
    TestOneRowIsOneLine();
    std::printf("PASS: test_decision_capture_rows, %d checks\n", checks);
    return 0;
}

#pragma once

// Decision capture, read side (ticket #307): the bounded window of decision
// rows that `.tb run decisions` prints, and the paging rules over it.
//
// One row is the DECISION of one run-leader tick, not a capture record: the
// tick's identity (`seq`, the capture's tick number, which is the cursor; `t`;
// `dt`) and the values the capture writes under a record's `out` object (the
// run's state, phase, step, note and say after the tick, and the order the tick
// gave). A `stub` record (a tick whose full record was too big for its size
// cap) still has its row here: the row is the decision, the stub is the file's
// cap. The facts and the plan are not in a row; the capture's files have them
// (docs/server/decision-capture.md).
//
// The window holds the newest kRowWindow rows of a LIVE capture, oldest first;
// a full log drops its oldest row. That is the whole read path: no new capture,
// no file reads, no database, and nothing a second store would have to keep in
// step. `AiPlayerbot.DecisionCapture.Enabled` stays the only toggle.
//
// A row also says what became of its file record (`record=`): the row is the
// decision, the record is the evidence, and under storage pressure (a refused
// segment header, a full writer queue, a failed write) the decision is all that
// is left. `dropped` is that case; an API consumer can tell durable rows from
// rows with no evidence instead of reading one flat stream as if all of it
// reached the disk.
//
// Paging: `Select(rows, since, limit)` returns the rows with `seq > since`
// (oldest first, at most `limit`), the cursor to pass next time (`next`) and
// whether a newer row exists past the last returned one (`more`). A `since` the
// window has already dropped reads from the oldest kept row: the caller sees
// the gap from `next=`/`count=`, and never a row it did not ask for.
//
// One row is one reply line (host/TbH3.h, MaxLineBytes = 3,000): a value is
// percent-encoded (one byte costs at most three), so the free-text fields are
// cut to kRowTextBytes and kWorstRowLineBytes below proves the worst case fits.
// The cut is made by the host's `Rec::T`, which also writes `trunc=yes`.
//
// Standard library only: tools/test_decision_capture_rows.cpp builds it
// without a game header.

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <utility>

namespace TortoiseBots
{
namespace DecisionCapture
{

// The read path's own numbers. None of them is a config key: the capture's
// keys (AiPlayerbot.DecisionCapture.*) stay the only settings.
inline constexpr std::size_t kRowWindow = 512;        // rows a live capture keeps for reading (about 8.5 minutes of ticks)
inline constexpr std::size_t kRowStoreBytes = 512;    // a free-text field kept in the window is cut here (memory bound)
inline constexpr std::size_t kRowTextBytes = 240;     // a free-text value printed is cut here (the one-line guarantee)
inline constexpr int kDefaultRows = 50;
inline constexpr int kMinRows = 1;
inline constexpr int kMaxRows = 200;

// The grammar's cap on one reply line (host/TbH3.h, MaxLineBytes).
inline constexpr std::size_t kRowLineBytes = 3000;

// What became of the row's capture record (the file evidence for its decision).
// Pessimistic until the sink takes it: a row is pushed before the write is
// known, and a push that never reaches the file stays `dropped`.
inline constexpr std::uint8_t kRecordDropped = 0; // no record in the file: a refused header, a refused line, a failed write
inline constexpr std::uint8_t kRecordTick = 1;    // a full tick record
inline constexpr std::uint8_t kRecordStub = 2;    // a stub: the record was over the capture's RecordKiB cap

inline char const* RecordName(std::uint8_t record)
{
    switch (record)
    {
        case kRecordTick: return "tick";
        case kRecordStub: return "stub";
        default: return "dropped";
    }
}

// One run-leader tick's decision. `state` and `phase` are the values of
// DungeonRun::State and DungeonRun::Phase (kept as numbers so this header needs
// no policy header; the command prints their words through StateName/PhaseName).
struct Row
{
    std::uint64_t seq = 0;
    std::uint64_t unixMs = 0;
    std::uint32_t dtMs = 0;
    std::uint8_t state = 0;
    std::uint8_t phase = 0;
    std::uint32_t step = 0;
    std::uint8_t record = kRecordDropped; // set by the recorder once the sink answers
    std::string order; // what the tick ordered ("" = it gave no order)
    std::string note;  // the run's note after the tick
    std::string say;   // the say text after the tick
};

// A text cut at a byte cap, never inside a UTF-8 sequence.
inline std::string Cut(std::string const& text, std::size_t cap)
{
    if (text.size() <= cap)
        return text;
    std::size_t keep = cap;
    while (keep > 0 && (static_cast<unsigned char>(text[keep]) & 0xC0) == 0x80)
        --keep;
    return text.substr(0, keep);
}

// The newest kRowWindow rows, oldest first. Filled on the world thread by the
// recorder, read on the world thread by the command: plain data, no lock.
class RowLog
{
public:
    void Push(Row row)
    {
        row.order = Cut(row.order, kRowStoreBytes);
        row.note = Cut(row.note, kRowStoreBytes);
        row.say = Cut(row.say, kRowStoreBytes);
        rows_.push_back(std::move(row));
        while (rows_.size() > kRowWindow)
            rows_.pop_front();
    }

    std::deque<Row> const& Rows() const { return rows_; }
    bool Empty() const { return rows_.empty(); }
    std::size_t Size() const { return rows_.size(); }
    std::uint64_t Newest() const { return rows_.empty() ? 0 : rows_.back().seq; }
    std::uint64_t Oldest() const { return rows_.empty() ? 0 : rows_.front().seq; }
    void Clear() { rows_.clear(); }
    // The record's fate, once the tick's write is known. Only the row just
    // pushed is ever marked (one tick marks its own row; the world thread runs
    // no other tick between) and a row past the window is gone, so a mark on an
    // empty log is a no-op.
    void SetNewestRecord(std::uint8_t record)
    {
        if (!rows_.empty())
            rows_.back().record = record;
    }

private:
    std::deque<Row> rows_;
};

// One page of rows: its first row's index in the log, how many, the cursor to
// pass next time, and whether a newer row is waiting past the last returned.
struct Page
{
    std::size_t first = 0;
    std::size_t count = 0;
    std::uint64_t next = 0;
    bool more = false;
};

inline Page Select(std::deque<Row> const& rows, std::uint64_t since, std::size_t limit)
{
    Page page;
    page.next = since;
    std::size_t first = 0;
    while (first < rows.size() && rows[first].seq <= since)
        ++first;
    page.first = first;
    if (first >= rows.size())
        return page;   // nothing after the cursor: next= stays the caller's place
    std::size_t const remaining = rows.size() - first;
    page.count = limit < remaining ? limit : remaining;
    page.next = rows[first + page.count - 1].seq;
    page.more = page.count < remaining;
    return page;
}

// `limit=` as the config reader hands it in (signed: a negative value clamps
// to the low bound, never wraps to a huge size).
inline int ClampRows(std::int64_t rows)
{
    if (rows < kMinRows)
        return kMinRows;
    if (rows > kMaxRows)
        return kMaxRows;
    return static_cast<int>(rows);
}

// The free-text fields in the printed order: the one key order every reader may
// rely on.
struct TextField
{
    char const* key;
    std::string const* value;
};

inline std::array<TextField, 3> TextFields(Row const& row)
{
    return { { { "order", &row.order }, { "note", &row.note }, { "say", &row.say } } };
}

// The worst a row line can get: the kind, every key, the longest number in
// every numeric field, the longest state/phase word, three values of
// kRowTextBytes bytes at the encoder's worst expansion (three bytes for one),
// `trunc=yes`, and a reply token. A row is never split over continuation lines,
// so this must stay under the grammar's kRowLineBytes.
inline constexpr std::size_t kWorstRowLineBytes =
    std::string_view("decision").size()                                        // the record kind
    + (1 + std::string_view("seq=").size() + 20)                               // uint64
    + (1 + std::string_view("t=").size() + 20)                                 // uint64
    + (1 + std::string_view("dt=").size() + 10)                                // uint32
    + (1 + std::string_view("state=").size() + 8)                              // "running"
    + (1 + std::string_view("phase=").size() + 12)                             // "gather"
    + (1 + std::string_view("step=").size() + 10)                              // uint32
    + (1 + std::string_view("record=").size() + 8)                             // "dropped"
    + (1 + std::string_view("order=").size() + kRowTextBytes * 3)
    + (1 + std::string_view("note=").size() + kRowTextBytes * 3)
    + (1 + std::string_view("say=").size() + kRowTextBytes * 3)
    + (1 + std::string_view("trunc=yes").size())
    + (1 + 1 + 32);                                                            // a space, then '#', then the longest token (host/TbH3.h)
static_assert(kWorstRowLineBytes <= kRowLineBytes, "one decision row must fit one reply line");

} // namespace DecisionCapture
} // namespace TortoiseBots

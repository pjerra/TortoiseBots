#pragma once

// Run trace, file writer: a bounded queue and one writer thread that does every
// disk write of a run after the file is opened: the trace lines, the summary
// file, the end record and closing the file. The world thread only queues work
// and polls a flag; it never waits for the disk while a run is going or while
// it ends. Only Open (at start: create the directory, open the file) and the
// shutdown wait (bounded by a deadline) touch the disk or wait for it.
// Standard library only; tools/test_run_trace_writer.cpp builds it standalone.
//
// Bounds: the queue holds at most maxQueueRecords lines and maxQueueBytes bytes;
// a line that does not fit is dropped and counted (dropped), never waited for.
// The file holds at most maxBytes: past it the file is closed to ordinary lines
// (truncated = true, every further line counts as dropped), but the end record
// (and a line pushed with `final`) still gets a reserved tail so a capped trace
// stays well-formed.
//
// Lifetime: everything the writer thread touches lives in a shared state that
// the thread holds itself, so Detach() can let a stalled thread go at shutdown
// without freeing anything under it and without std::terminate.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "RunTraceRecords.h"

namespace TortoiseBots {
namespace RunTrace {

// Bytes kept free in the file for the end line.
constexpr uint64_t kWriterTailReserve = 8192;

struct WriterStats
{
    uint64_t records = 0;   // lines written
    uint64_t bytes = 0;     // bytes written
    uint64_t dropped = 0;   // lines refused (queue full, size cap, write failure)
    bool truncated = false; // the size cap closed the file
    bool failed = false;    // the file could not be opened or written
};

// How a run ends, handed to the writer thread with the end marker. It holds
// copies only (no pointer into the service), so the thread can finish it after
// the service has moved on, or after a shutdown stopped waiting for it.
struct EndJob
{
    // The run's own counters (frames, actions, ...). The writer fills records /
    // bytes / dropped / truncated when it reaches the marker: every line queued
    // before the end record is then written or dropped, so they count exactly
    // the lines before it.
    Counters counters;
    // Build the end line, then the summary, from the same final counters, so
    // the two agree. An end line that could not be written is added to
    // `dropped` before the summary is built.
    std::function<std::string(Counters const&)> buildEnd;
    std::function<std::string(Counters const&)> buildSummary;
    std::string summaryPath;
};

class RunTraceWriter
{
public:
    RunTraceWriter() = default;
    // Joins a thread that is still attached (a run that never ended).
    ~RunTraceWriter() { Close(); }
    RunTraceWriter(RunTraceWriter const&) = delete;
    RunTraceWriter& operator=(RunTraceWriter const&) = delete;

    // Creates the parent directory, opens `path` for writing (truncating) and
    // starts the thread. False with `err` filled when the file cannot be made.
    bool Open(std::string const& path, uint64_t maxBytes, size_t maxQueueRecords, size_t maxQueueBytes,
        std::string& err);
    // The owning (world) thread. True when the line was queued.
    bool Push(std::string line, bool final = false);
    // Queues an atomic write of a whole file (the "running" summary). Not
    // subject to the queue bounds.
    bool PushFile(std::string path, std::string content);
    // Queues the end marker. The caller returns at once; the writer thread
    // writes what is ahead of it, then the end line and the summary, closes the
    // file and sets Finished(). Nothing is accepted after it.
    bool PushEnd(EndJob job);
    // Polled by the world thread: the end marker has been handled.
    bool Finished() const;
    // The counters the end line and the summary carry (valid once Finished()).
    Counters FinalCounters() const;
    // After Finished(): joins the thread, which has already returned, so this
    // does not wait for the disk. Returns the final writer stats.
    WriterStats Reap();
    // Shutdown only: waits at most `deadline` for Finished().
    bool WaitFinished(std::chrono::milliseconds deadline);
    // Shutdown only: lets a thread that missed the deadline go. It keeps the
    // shared state alive by itself and finishes, or ends with the process.
    void Detach();
    // Stops after writing what is queued (no end record), joins, closes. Waits
    // for the disk: tests and the destructor only. Idempotent.
    WriterStats Close();
    WriterStats Snapshot() const;
    bool IsOpen() const { return open_; }

    // Test seam: called on the writer thread before each queued item is written.
    void SetBeforeWriteHookForTest(std::function<void()> hook);

    struct State;

private:
    static void Run(std::shared_ptr<State> state);

    std::shared_ptr<State> state_;
    std::thread thread_;
    bool open_ = false;
};

// Writes `content` to `path` through `path`.tmp and a rename, so a reader (the
// scoring tool, the dashboard) never sees half a file. Creates the directory.
bool WriteFileAtomic(std::string const& path, std::string const& content, std::string& err);

} // namespace RunTrace
} // namespace TortoiseBots

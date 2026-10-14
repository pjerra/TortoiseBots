#pragma once

// Run trace, file writer: a bounded queue and one writer thread that appends
// JSONL lines to the trace file, so the world thread never waits on the disk.
// Standard library only; tools/test_run_trace_writer.cpp builds it standalone.
//
// Bounds: the queue holds at most maxQueueRecords lines and maxQueueBytes bytes;
// a line that does not fit is dropped and counted (dropped), never waited for.
// The file holds at most maxBytes: past it the file is closed to ordinary lines
// (truncated = true, every further line counts as dropped), but a line pushed
// with `final` (the end record) still gets a reserved tail so a capped trace
// stays well-formed.

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

namespace TortoiseBots {
namespace RunTrace {

// Bytes kept free in the file for the `final` line.
constexpr uint64_t kWriterTailReserve = 8192;

struct WriterStats
{
    uint64_t records = 0;   // lines written
    uint64_t bytes = 0;     // bytes written
    uint64_t dropped = 0;   // lines refused (queue full, size cap, write failure)
    bool truncated = false; // the size cap closed the file
    bool failed = false;    // the file could not be opened or written
};

class RunTraceWriter
{
public:
    RunTraceWriter() = default;
    ~RunTraceWriter() { Close(); }
    RunTraceWriter(RunTraceWriter const&) = delete;
    RunTraceWriter& operator=(RunTraceWriter const&) = delete;

    // Creates the parent directory, opens `path` for writing (truncating) and
    // starts the thread. False with `err` filled when the file cannot be made.
    bool Open(std::string const& path, uint64_t maxBytes, size_t maxQueueRecords, size_t maxQueueBytes,
        std::string& err);

    // Any thread. True when the line was queued.
    bool Push(std::string line, bool final = false);

    // Drains the queue, joins the thread, closes the file. Idempotent.
    WriterStats Close();

    WriterStats Snapshot() const;
    bool IsOpen() const { return open_.load(); }

private:
    void Run();

    std::ofstream out_;
    std::thread thread_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<std::pair<std::string, bool>> queue_;
    size_t queuedBytes_ = 0;
    size_t maxQueueRecords_ = 0;
    size_t maxQueueBytes_ = 0;
    uint64_t maxBytes_ = 0;
    bool stopping_ = false;
    WriterStats stats_;
    std::atomic<bool> open_{false};
};

// Writes `content` to `path` through `path`.tmp and a rename, so a reader (the
// scoring tool, the dashboard) never sees half a file. Creates the directory.
bool WriteFileAtomic(std::string const& path, std::string const& content, std::string& err);

} // namespace RunTrace
} // namespace TortoiseBots

#include "RunTraceWriter.h"

#include <filesystem>
#include <system_error>
#include <utility>

namespace TortoiseBots {
namespace RunTrace {

struct RunTraceWriter::State
{
    struct Item
    {
        enum class Kind { Line, Final, File, End };
        Kind kind = Kind::Line;
        std::string text;               // the line, or the file content
        std::string path;               // Kind::File
        std::shared_ptr<EndJob> end;    // Kind::End
    };

    mutable std::mutex mu;
    std::condition_variable cv;
    std::condition_variable doneCv;
    std::deque<Item> queue;
    size_t queuedBytes = 0;
    size_t maxQueueRecords = 0;
    size_t maxQueueBytes = 0;
    uint64_t maxBytes = 0;
    bool stopping = false;  // Close(): write what is queued, then stop
    bool ended = false;     // the end marker is queued: nothing more is taken
    WriterStats stats;
    Counters finalCounters;
    std::atomic<bool> finished{false};
    std::function<void()> beforeWrite;
    std::ofstream out;      // the writer thread's only, once it runs
};

bool RunTraceWriter::Open(std::string const& path, uint64_t maxBytes, size_t maxQueueRecords,
    size_t maxQueueBytes, std::string& err)
{
    if (open_ || thread_.joinable())
    {
        err = "already open";
        return false;
    }
    auto state = std::make_shared<State>();
    std::error_code ec;
    std::filesystem::path const p(path);
    if (p.has_parent_path())
    {
        std::filesystem::create_directories(p.parent_path(), ec);
        if (ec)
        {
            err = "cannot create " + p.parent_path().string() + ": " + ec.message();
            return false;
        }
    }
    state->out.open(path, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!state->out.is_open())
    {
        err = "cannot open " + path;
        return false;
    }
    state->maxQueueRecords = maxQueueRecords;
    state->maxQueueBytes = maxQueueBytes;
    state->maxBytes = maxBytes;
    state_ = state;
    open_ = true;
    thread_ = std::thread(&RunTraceWriter::Run, state);
    return true;
}

bool RunTraceWriter::Push(std::string line, bool final)
{
    if (!state_)
        return false;
    State& s = *state_;
    std::lock_guard<std::mutex> lock(s.mu);
    if (!open_ || s.stopping || s.ended)
        return false;
    if (!final && (s.queue.size() >= s.maxQueueRecords || s.queuedBytes + line.size() > s.maxQueueBytes))
    {
        ++s.stats.dropped;
        return false;
    }
    s.queuedBytes += line.size();
    State::Item item;
    item.kind = final ? State::Item::Kind::Final : State::Item::Kind::Line;
    item.text = std::move(line);
    s.queue.push_back(std::move(item));
    s.cv.notify_one();
    return true;
}

bool RunTraceWriter::PushFile(std::string path, std::string content)
{
    if (!state_)
        return false;
    State& s = *state_;
    std::lock_guard<std::mutex> lock(s.mu);
    if (!open_ || s.stopping || s.ended)
        return false;
    State::Item item;
    item.kind = State::Item::Kind::File;
    item.path = std::move(path);
    item.text = std::move(content);
    s.queue.push_back(std::move(item));
    s.cv.notify_one();
    return true;
}

bool RunTraceWriter::PushEnd(EndJob job)
{
    if (!state_)
        return false;
    State& s = *state_;
    std::lock_guard<std::mutex> lock(s.mu);
    if (!open_ || s.stopping || s.ended)
        return false;
    State::Item item;
    item.kind = State::Item::Kind::End;
    item.end = std::make_shared<EndJob>(std::move(job));
    s.queue.push_back(std::move(item));
    s.ended = true;
    s.cv.notify_one();
    return true;
}

bool RunTraceWriter::Finished() const
{
    return state_ && state_->finished.load(std::memory_order_acquire);
}

Counters RunTraceWriter::FinalCounters() const
{
    if (!state_)
        return Counters();
    std::lock_guard<std::mutex> lock(state_->mu);
    return state_->finalCounters;
}

WriterStats RunTraceWriter::Reap()
{
    if (thread_.joinable())
        thread_.join();
    open_ = false;
    return Snapshot();
}

bool RunTraceWriter::WaitFinished(std::chrono::milliseconds deadline)
{
    if (!state_)
        return true;
    State& s = *state_;
    std::unique_lock<std::mutex> lock(s.mu);
    return s.doneCv.wait_for(lock, deadline, [&s] { return s.finished.load(std::memory_order_acquire); });
}

void RunTraceWriter::Detach()
{
    if (thread_.joinable())
        thread_.detach();
    open_ = false;
}

WriterStats RunTraceWriter::Close()
{
    if (!state_)
        return WriterStats();
    if (thread_.joinable())
    {
        {
            std::lock_guard<std::mutex> lock(state_->mu);
            state_->stopping = true;
        }
        state_->cv.notify_all();
        thread_.join();
    }
    open_ = false;
    return Snapshot();
}

WriterStats RunTraceWriter::Snapshot() const
{
    if (!state_)
        return WriterStats();
    std::lock_guard<std::mutex> lock(state_->mu);
    return state_->stats;
}

void RunTraceWriter::SetBeforeWriteHookForTest(std::function<void()> hook)
{
    if (!state_)
        return;
    std::lock_guard<std::mutex> lock(state_->mu);
    state_->beforeWrite = std::move(hook);
}

void RunTraceWriter::Run(std::shared_ptr<State> state)
{
    State& s = *state;
    std::deque<State::Item> batch;
    auto lastFlush = std::chrono::steady_clock::now();
    for (;;)
    {
        bool stop = false;
        std::function<void()> hook;
        // Only this thread writes the file and changes bytes / records / failed /
        // truncated, so it decides and writes without the lock (Push, on the
        // world thread, takes the same mutex and must never wait for the disk);
        // the lock is taken only to swap the queue and publish the counters.
        uint64_t bytes = 0;
        bool failed = false;
        bool truncated = false;
        {
            std::unique_lock<std::mutex> lock(s.mu);
            s.cv.wait_for(lock, std::chrono::milliseconds(500), [&s] { return !s.queue.empty() || s.stopping; });
            batch.swap(s.queue);
            s.queuedBytes = 0;
            stop = s.stopping;
            hook = s.beforeWrite;
            bytes = s.stats.bytes;
            failed = s.stats.failed;
            truncated = s.stats.truncated;
        }
        uint64_t records = 0;
        uint64_t dropped = 0;
        auto publish = [&]()
        {
            std::lock_guard<std::mutex> lock(s.mu);
            s.stats.bytes = bytes;
            s.stats.records += records;
            s.stats.dropped += dropped;
            s.stats.failed = failed;
            s.stats.truncated = truncated;
            records = 0;
            dropped = 0;
        };
        // One line into the file; false when it was dropped.
        auto writeLine = [&](std::string const& line, bool final) -> bool
        {
            uint64_t const need = line.size() + 1;
            uint64_t const limit = final ? s.maxBytes
                : (s.maxBytes > kWriterTailReserve ? s.maxBytes - kWriterTailReserve : 0);
            if (failed || bytes + need > limit)
            {
                if (!failed && !final)
                    truncated = true;
                ++dropped;
                return false;
            }
            s.out.write(line.data(), static_cast<std::streamsize>(line.size()));
            s.out.put('\n');
            if (!s.out)
            {
                failed = true;
                ++dropped;
                return false;
            }
            bytes += need;
            ++records;
            return true;
        };

        for (State::Item& item : batch)
        {
            if (hook)
                hook();
            switch (item.kind)
            {
                case State::Item::Kind::Line:
                case State::Item::Kind::Final:
                    writeLine(item.text, item.kind == State::Item::Kind::Final);
                    break;
                case State::Item::Kind::File:
                {
                    std::string err;
                    WriteFileAtomic(item.path, item.text, err);
                    break;
                }
                case State::Item::Kind::End:
                {
                    // Everything queued before the marker is written or dropped:
                    // its counters are final for the lines before the end record.
                    publish();
                    EndJob& job = *item.end;
                    Counters c = job.counters;
                    {
                        std::lock_guard<std::mutex> lock(s.mu);
                        c.records = s.stats.records;
                        c.bytes = s.stats.bytes;
                        c.dropped = s.stats.dropped;
                        c.truncated = s.stats.truncated;
                    }
                    if (!job.buildEnd || !writeLine(job.buildEnd(c), true))
                        ++c.dropped;
                    publish();
                    s.out.flush();
                    s.out.close();
                    if (job.buildSummary && !job.summaryPath.empty())
                    {
                        std::string err;
                        WriteFileAtomic(job.summaryPath, job.buildSummary(c), err);
                    }
                    {
                        std::lock_guard<std::mutex> lock(s.mu);
                        s.finalCounters = c;
                        s.finished.store(true, std::memory_order_release);
                    }
                    s.doneCv.notify_all();
                    return;
                }
            }
        }
        batch.clear();
        publish();
        auto const now = std::chrono::steady_clock::now();
        if (stop || now - lastFlush > std::chrono::seconds(1))
        {
            s.out.flush();
            lastFlush = now;
        }
        if (stop)
        {
            // Anything pushed between the swap and the stop flag.
            std::lock_guard<std::mutex> lock(s.mu);
            if (s.queue.empty())
            {
                s.out.close();
                return;
            }
        }
    }
}

bool WriteFileAtomic(std::string const& path, std::string const& content, std::string& err)
{
    std::error_code ec;
    std::filesystem::path const p(path);
    if (p.has_parent_path())
    {
        std::filesystem::create_directories(p.parent_path(), ec);
        if (ec)
        {
            err = "cannot create " + p.parent_path().string() + ": " + ec.message();
            return false;
        }
    }
    std::string const tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!f.is_open())
        {
            err = "cannot open " + tmp;
            return false;
        }
        f << content << '\n';
        f.flush();
        if (!f)
        {
            err = "cannot write " + tmp;
            return false;
        }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec)
    {
        err = "cannot rename " + tmp + ": " + ec.message();
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

} // namespace RunTrace
} // namespace TortoiseBots

#include "RunTraceWriter.h"

#include <filesystem>
#include <iterator>
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
    std::string failure;    // why ending the file failed; empty when it did not
    bool failFinalFlush = false;  // test seam
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

std::string RunTraceWriter::Failure() const
{
    if (!state_)
        return std::string();
    std::lock_guard<std::mutex> lock(state_->mu);
    return state_->failure;
}

void RunTraceWriter::SetFailFinalFlushForTest(bool fail)
{
    if (!state_)
        return;
    std::lock_guard<std::mutex> lock(state_->mu);
    state_->failFinalFlush = fail;
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
                    // A trace whose last bytes did not reach the disk is not
                    // complete: say so in the summary (status "failed").
                    std::string failure;
                    bool failFlush = false;
                    {
                        std::lock_guard<std::mutex> lock(s.mu);
                        failFlush = s.failFinalFlush;
                    }
                    if (failFlush)
                        s.out.setstate(std::ios::badbit);
                    s.out.flush();
                    if (!s.out)
                        failure = "cannot flush the trace file";
                    s.out.close();
                    if (failure.empty() && s.out.fail())
                        failure = "cannot close the trace file";
                    if (job.buildSummary && !job.summaryPath.empty())
                    {
                        std::string err;
                        if (!WriteFileAtomic(job.summaryPath, job.buildSummary(c, failure), err))
                            failure = (failure.empty() ? std::string() : failure + "; ") +
                                "cannot write the summary: " + err;
                    }
                    {
                        std::lock_guard<std::mutex> lock(s.mu);
                        s.finalCounters = c;
                        s.failure = failure;
                        if (!failure.empty())
                            s.stats.failed = true;
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

bool MarkSummaryIncomplete(std::string const& json, uint64_t nowUnix, std::string& out)
{
    // The summary is one flat JSON object written by Builder::BuildSummary or
    // rewritten by tools/trace_score.py (compact); its first "status" key is
    // the run's.
    auto skipWs = [&json](size_t i)
    {
        while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\n' || json[i] == '\r'))
            ++i;
        return i;
    };
    // [start, end) of the string value of `key`, or false.
    auto findString = [&](char const* key, size_t& start, size_t& end)
    {
        std::string const quoted = std::string("\"") + key + "\"";
        size_t at = json.find(quoted);
        if (at == std::string::npos)
            return false;
        size_t i = skipWs(at + quoted.size());
        if (i >= json.size() || json[i] != ':')
            return false;
        i = skipWs(i + 1);
        if (i >= json.size() || json[i] != '"')
            return false;
        size_t j = i + 1;
        while (j < json.size() && json[j] != '"')
            j += json[j] == '\\' ? 2 : 1;
        if (j >= json.size())
            return false;
        start = i;
        end = j + 1;
        return true;
    };
    size_t sStart = 0, sEnd = 0;
    if (!findString("status", sStart, sEnd) || json.compare(sStart, sEnd - sStart, "\"running\"") != 0)
        return false;
    size_t const close = json.find_last_of('}');
    if (close == std::string::npos || close < sEnd)
        return false;

    std::string const reason = std::string("\"") + kReconcileReason + "\"";
    size_t rStart = 0, rEnd = 0;
    bool const hasReason = findString("end_reason", rStart, rEnd);
    std::string body = json.substr(0, close);
    // Replace from the back so earlier offsets stay valid.
    if (hasReason && rStart > sStart)
    {
        body.replace(rStart, rEnd - rStart, reason);
        body.replace(sStart, sEnd - sStart, "\"incomplete\"");
    }
    else if (hasReason)
    {
        body.replace(sStart, sEnd - sStart, "\"incomplete\"");
        body.replace(rStart, rEnd - rStart, reason);
    }
    else
        body.replace(sStart, sEnd - sStart, "\"incomplete\",\"end_reason\":" + reason);
    while (!body.empty() && (body.back() == ' ' || body.back() == '\n' || body.back() == '\r' || body.back() == '\t'))
        body.pop_back();
    out = body + ",\"reconciled_at\":" + std::to_string(nowUnix) + json.substr(close);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
        out.pop_back();
    return true;
}

ReconcileResult ReconcileStaleSummaries(std::string const& dir, std::string const& activeRunId, uint64_t nowUnix)
{
    ReconcileResult result;
    static std::string const suffix = ".summary.json";
    std::error_code ec;
    std::filesystem::directory_iterator it(dir, ec);
    if (ec)
        return result;  // no trace directory yet
    for (; it != std::filesystem::directory_iterator(); it.increment(ec))
    {
        if (ec)
            break;
        std::filesystem::path const path = it->path();
        std::string const name = path.filename().string();
        if (name.size() <= suffix.size() || name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0)
            continue;
        std::string const runId = name.substr(0, name.size() - suffix.size());
        if (runId == activeRunId || !it->is_regular_file(ec))
            continue;
        std::ifstream in(path, std::ios::in | std::ios::binary);
        std::string const content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::string updated;
        if (!MarkSummaryIncomplete(content, nowUnix, updated))
            continue;
        std::string err;
        if (WriteFileAtomic(path.string(), updated, err))
            result.marked.push_back(runId);
        else
            result.errors.push_back(runId + ": " + err);
    }
    return result;
}

} // namespace RunTrace
} // namespace TortoiseBots

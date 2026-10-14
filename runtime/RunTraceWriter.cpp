#include "RunTraceWriter.h"

#include <chrono>
#include <filesystem>
#include <system_error>
#include <utility>

namespace TortoiseBots {
namespace RunTrace {

bool RunTraceWriter::Open(std::string const& path, uint64_t maxBytes, size_t maxQueueRecords,
    size_t maxQueueBytes, std::string& err)
{
    if (open_.load())
    {
        err = "already open";
        return false;
    }
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
    out_.open(path, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!out_.is_open())
    {
        err = "cannot open " + path;
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mu_);
        queue_.clear();
        queuedBytes_ = 0;
        maxQueueRecords_ = maxQueueRecords;
        maxQueueBytes_ = maxQueueBytes;
        maxBytes_ = maxBytes;
        stopping_ = false;
        stats_ = WriterStats();
    }
    open_.store(true);
    thread_ = std::thread(&RunTraceWriter::Run, this);
    return true;
}

bool RunTraceWriter::Push(std::string line, bool final)
{
    std::lock_guard<std::mutex> lock(mu_);
    if (!open_.load() || stopping_)
        return false;
    if (!final && (queue_.size() >= maxQueueRecords_ || queuedBytes_ + line.size() > maxQueueBytes_))
    {
        ++stats_.dropped;
        return false;
    }
    queuedBytes_ += line.size();
    queue_.emplace_back(std::move(line), final);
    cv_.notify_one();
    return true;
}

WriterStats RunTraceWriter::Close()
{
    if (!open_.load() && !thread_.joinable())
    {
        std::lock_guard<std::mutex> lock(mu_);
        return stats_;
    }
    {
        std::lock_guard<std::mutex> lock(mu_);
        stopping_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable())
        thread_.join();
    if (out_.is_open())
    {
        out_.flush();
        out_.close();
    }
    open_.store(false);
    std::lock_guard<std::mutex> lock(mu_);
    return stats_;
}

WriterStats RunTraceWriter::Snapshot() const
{
    std::lock_guard<std::mutex> lock(mu_);
    return stats_;
}

void RunTraceWriter::Run()
{
    std::deque<std::pair<std::string, bool>> batch;
    auto lastFlush = std::chrono::steady_clock::now();
    for (;;)
    {
        bool stop = false;
        {
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait_for(lock, std::chrono::milliseconds(500), [this] { return !queue_.empty() || stopping_; });
            batch.swap(queue_);
            queuedBytes_ = 0;
            stop = stopping_;
        }
        // Only this thread writes the file and changes bytes / records / failed /
        // truncated, so it decides and writes without the lock (Push, on the
        // world thread, takes the same mutex and must never wait for the disk);
        // the lock is taken only to publish the counters.
        uint64_t bytes = 0;
        uint64_t records = 0;
        uint64_t dropped = 0;
        bool failed = false;
        bool truncated = false;
        {
            std::lock_guard<std::mutex> lock(mu_);
            bytes = stats_.bytes;
            failed = stats_.failed;
            truncated = stats_.truncated;
        }
        for (auto& item : batch)
        {
            std::string const& line = item.first;
            uint64_t const need = line.size() + 1;
            uint64_t const limit = item.second ? maxBytes_ : (maxBytes_ > kWriterTailReserve ? maxBytes_ - kWriterTailReserve : 0);
            if (failed || bytes + need > limit)
            {
                if (!failed && !item.second)
                    truncated = true;
                ++dropped;
                continue;
            }
            out_.write(line.data(), static_cast<std::streamsize>(line.size()));
            out_.put('\n');
            if (!out_)
            {
                failed = true;
                ++dropped;
                continue;
            }
            bytes += need;
            ++records;
        }
        batch.clear();
        {
            std::lock_guard<std::mutex> lock(mu_);
            stats_.bytes = bytes;
            stats_.records += records;
            stats_.dropped += dropped;
            stats_.failed = failed;
            stats_.truncated = truncated;
        }
        auto const now = std::chrono::steady_clock::now();
        if (stop || now - lastFlush > std::chrono::seconds(1))
        {
            out_.flush();
            lastFlush = now;
        }
        if (stop)
        {
            // Anything pushed between the swap and the stop flag.
            std::lock_guard<std::mutex> lock(mu_);
            if (queue_.empty())
                return;
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

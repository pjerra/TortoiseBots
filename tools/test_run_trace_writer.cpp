// Standalone tests for the run trace's file writer (runtime/RunTraceWriter.h):
// ordered writes, the bounded queue and its drop counter, the file size cap
// with the reserved tail for the end record, and the atomic summary write.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_run_trace_writer.cpp runtime/RunTraceWriter.cpp -pthread -o /tmp/test_run_trace_writer
//   /tmp/test_run_trace_writer [scratch dir]

#include "../runtime/RunTraceWriter.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace TortoiseBots::RunTrace;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static std::vector<std::string> ReadLines(std::string const& path)
{
    std::ifstream in(path);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line))
        lines.push_back(line);
    return lines;
}

static std::string Slurp(std::string const& path)
{
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

int main(int argc, char** argv)
{
    namespace fs = std::filesystem;
    fs::path const dir = (argc > 1 ? fs::path(argv[1]) : fs::temp_directory_path()) / "rt_writer_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    std::string err;

    // Lines arrive in order; the directory is created.
    {
        std::string const path = (dir / "sub" / "a.jsonl").string();
        RunTraceWriter w;
        CHECK(w.Open(path, 1 << 20, 100, 1 << 20, err));
        CHECK(w.IsOpen());
        for (int i = 0; i < 50; ++i)
            CHECK(w.Push("{\"i\":" + std::to_string(i) + "}"));
        WriterStats s = w.Close();
        CHECK(!w.IsOpen());
        CHECK(s.records == 50 && s.dropped == 0 && !s.truncated && !s.failed);
        auto lines = ReadLines(path);
        CHECK(lines.size() == 50);
        CHECK(lines[0] == "{\"i\":0}" && lines[49] == "{\"i\":49}");
        CHECK(!w.Push("late")); // closed
        w.Close();              // idempotent
    }

    // A queue that cannot take more drops and counts, without blocking.
    {
        std::string const path = (dir / "b.jsonl").string();
        RunTraceWriter w;
        CHECK(w.Open(path, 1 << 20, 5, 1 << 20, err));
        // Hold the writer thread back by filling faster than it drains is not
        // deterministic; push far more than the queue holds in one burst.
        uint64_t refused = 0;
        for (int i = 0; i < 20000; ++i)
            if (!w.Push("{\"n\":" + std::to_string(i) + "}"))
                ++refused;
        WriterStats s = w.Close();
        CHECK(s.records + s.dropped == 20000);
        CHECK(s.dropped == refused);
        CHECK(ReadLines(path).size() == s.records);
    }

    // The byte bound of the queue counts too.
    {
        std::string const path = (dir / "c.jsonl").string();
        RunTraceWriter w;
        CHECK(w.Open(path, 1 << 20, 1000000, 100, err));
        uint64_t refused = 0;
        for (int i = 0; i < 1000; ++i)
            if (!w.Push(std::string(60, 'x')))
                ++refused;
        WriterStats s = w.Close();
        CHECK(s.records + s.dropped == 1000);
        CHECK(s.dropped == refused);
    }

    // The size cap stops ordinary lines, but the final line still fits.
    {
        std::string const path = (dir / "d.jsonl").string();
        RunTraceWriter w;
        uint64_t const cap = kWriterTailReserve + 1000;
        CHECK(w.Open(path, cap, 100000, 100 << 20, err));
        std::string const line(99, 'y');
        for (int i = 0; i < 100; ++i)
        {
            w.Push(line);
            if (i % 10 == 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        CHECK(w.Push("{\"k\":\"end\"}", true));
        WriterStats s = w.Close();
        CHECK(s.truncated);
        CHECK(s.dropped > 0);
        CHECK(s.bytes <= cap);
        auto lines = ReadLines(path);
        CHECK(!lines.empty() && lines.back() == "{\"k\":\"end\"}");
        CHECK(fs::file_size(path) <= cap);
    }

    // The end record and the summary agree: the writer thread builds both from
    // the counters it has when it reaches the end marker, after every line
    // queued before it (still queued when the run ended) is written or dropped.
    auto endAndSummaryAgree = [&](char const* name, uint64_t cap, int lines, size_t lineLen)
    {
        std::string const path = (dir / name).string();
        std::string const summaryPath = path + ".summary.json";
        RunTraceWriter w;
        CHECK(w.Open(path, cap, 100000, 64 << 20, err));
        Builder b("agree");
        Header h;
        CHECK(w.PushFile(summaryPath, b.BuildSummary(h, "running", "", 0, 0, Counters(), name)));
        for (int i = 0; i < lines; ++i)
            w.Push(std::string(lineLen, 'z'));
        EndJob job;
        job.counters.frames = 7;
        job.counters.actions = 3;
        job.buildEnd = [b](Counters const& cc)
            {
                EndInfo end;
                end.reason = "stop";
                end.counters = cc;
                return b.BuildEnd(100, end);
            };
        job.buildSummary = [b, h, name](Counters const& cc, std::string const& failure)
            {
                return b.BuildSummary(h, failure.empty() ? "complete" : "failed", "stop", 1, 100, cc, name, failure);
            };
        job.summaryPath = summaryPath;
        CHECK(w.PushEnd(job));
        CHECK(!w.Push("late"));          // nothing after the end marker
        CHECK(w.WaitFinished(std::chrono::seconds(30)));
        CHECK(w.Finished());
        w.Reap();
        Counters const c = w.FinalCounters();
        CHECK(c.frames == 7 && c.actions == 3);
        auto const file = ReadLines(path);
        CHECK(!file.empty());
        std::string const endLine = file.back();
        std::string const summary = Slurp(summaryPath);
        std::string const counters = Builder::CountersJson(c);
        CHECK(endLine.find("\"k\":\"end\"") != std::string::npos);
        CHECK(endLine.find("\"counters\":" + counters) != std::string::npos);
        CHECK(summary.find("\"counters\":" + counters) != std::string::npos);
        CHECK(summary.find("\"status\":\"complete\"") != std::string::npos);
        CHECK(w.Failure().empty());
        CHECK(summary.find("\"error\"") == std::string::npos);
        // The counters count every line before the end line.
        CHECK(c.records == file.size() - 1);
        CHECK(c.bytes == fs::file_size(path) - endLine.size() - 1);
        CHECK(c.records + c.dropped == static_cast<uint64_t>(lines));
        return c;
    };
    {
        Counters c = endAndSummaryAgree("g.jsonl", 64 << 20, 5000, 20);
        CHECK(c.records == 5000 && c.dropped == 0 && !c.truncated);
    }
    {
        // Capped: the truncation and the dropped lines are in both too.
        Counters c = endAndSummaryAgree("h.jsonl", kWriterTailReserve + 2000, 200, 99);
        CHECK(c.truncated && c.dropped > 0);
    }

    // A stalled disk: ending a run returns at once, and so does shutdown; the
    // thread is let go without freeing its state and finishes once the disk is
    // back.
    {
        std::string const path = (dir / "slow.jsonl").string();
        std::string const summaryPath = path + ".summary.json";
        auto gate = std::make_shared<std::atomic<bool>>(false);
        Builder b("slow");
        Header h;
        bool timedOut = false;
        {
            RunTraceWriter w;
            CHECK(w.Open(path, 64 << 20, 1000, 1 << 20, err));
            w.SetBeforeWriteHookForTest([gate]
                {
                    while (!gate->load())
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                });
            for (int i = 0; i < 100; ++i)
                CHECK(w.Push("{\"i\":" + std::to_string(i) + "}"));
            EndJob job;
            job.buildEnd = [b](Counters const& cc) { EndInfo e; e.reason = "shutdown"; e.counters = cc; return b.BuildEnd(1, e); };
            job.buildSummary = [b, h](Counters const& cc, std::string const&) { return b.BuildSummary(h, "complete", "shutdown", 1, 1, cc, "slow.jsonl"); };
            job.summaryPath = summaryPath;
            auto const t0 = std::chrono::steady_clock::now();
            CHECK(w.PushEnd(job));
            auto const pushMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0).count();
            CHECK(pushMs < 100);             // stop returns at once
            CHECK(!w.Finished());
            // Shutdown does not wait: the thread is let go at once.
            auto const t1 = std::chrono::steady_clock::now();
            w.Detach();
            auto const detachMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t1).count();
            CHECK(detachMs < 100);
            timedOut = !w.Finished();
            CHECK(!w.IsOpen());
        }   // the writer object is gone; the detached thread still holds its state
        CHECK(timedOut);
        gate->store(true);
        for (int i = 0; i < 600 && !fs::exists(summaryPath); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        CHECK(fs::exists(summaryPath));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        auto const file = ReadLines(path);
        CHECK(file.size() == 101 && file.back().find("\"k\":\"end\"") != std::string::npos);
    }

    // A failed end: the final flush fails, or the summary cannot be written.
    // The writer still finishes, but says why; the summary says "failed" when
    // it can still be written.
    auto failedEnd = [&](char const* name, bool failFlush, bool blockSummary)
    {
        std::string const path = (dir / name).string();
        std::string const summaryPath = path + ".summary.json";
        if (blockSummary)
            fs::create_directories(summaryPath + "/occupied");  // a directory where the summary goes
        RunTraceWriter w;
        CHECK(w.Open(path, 1 << 20, 100, 1 << 20, err));
        w.SetFailFinalFlushForTest(failFlush);
        CHECK(w.Push("{\"i\":1}"));
        Builder b("failed");
        Header h;
        EndJob job;
        job.buildEnd = [b](Counters const& cc) { EndInfo e; e.reason = "stop"; e.counters = cc; return b.BuildEnd(1, e); };
        job.buildSummary = [b, h, name](Counters const& cc, std::string const& failure)
            {
                return b.BuildSummary(h, failure.empty() ? "complete" : "failed", "stop", 1, 1, cc, name, failure);
            };
        job.summaryPath = summaryPath;
        CHECK(w.PushEnd(job));
        CHECK(w.WaitFinished(std::chrono::seconds(30)));
        w.Reap();
        CHECK(w.Snapshot().failed);
        return std::make_pair(w.Failure(), summaryPath);
    };
    {
        auto r = failedEnd("fail-flush.jsonl", true, false);
        CHECK(r.first == "cannot flush the trace file");
        std::string const summary = Slurp(r.second);
        CHECK(summary.find("\"status\":\"failed\"") != std::string::npos);
        CHECK(summary.find("\"error\":\"cannot flush the trace file\"") != std::string::npos);
    }
    {
        auto r = failedEnd("fail-summary.jsonl", false, true);
        CHECK(r.first.rfind("cannot write the summary: ", 0) == 0);
        CHECK(fs::is_directory(r.second));  // nothing half-written in its place
    }
    {
        auto r = failedEnd("fail-both.jsonl", true, true);
        CHECK(r.first.rfind("cannot flush the trace file; cannot write the summary: ", 0) == 0);
    }

    // Startup reconciliation, on a fake trace directory.
    {
        std::string const out1 = "{\"v\":1,\"run\":\"a\",\"status\":\"running\",\"end_reason\":\"\",\"counters\":{\"frames\":4},\"oracles\":[]}";
        std::string marked;
        CHECK(MarkSummaryIncomplete(out1, 1700000000, marked));
        CHECK(marked == "{\"v\":1,\"run\":\"a\",\"status\":\"incomplete\",\"end_reason\":\"server stopped before the trace was finalised\",\"counters\":{\"frames\":4},\"oracles\":[],\"reconciled_at\":1700000000}");
        // No end_reason key, spaces and a trailing newline.
        CHECK(MarkSummaryIncomplete("{ \"status\" : \"running\", \"counters\": {\"frames\":2} }\n", 5, marked));
        CHECK(marked == "{ \"status\" : \"incomplete\",\"end_reason\":\"server stopped before the trace was finalised\", \"counters\": {\"frames\":2},\"reconciled_at\":5}");
        // end_reason before status.
        CHECK(MarkSummaryIncomplete("{\"end_reason\":\"x\",\"status\":\"running\"}", 7, marked));
        CHECK(marked == "{\"end_reason\":\"server stopped before the trace was finalised\",\"status\":\"incomplete\",\"reconciled_at\":7}");
        // Only "running" summaries are touched.
        CHECK(!MarkSummaryIncomplete("{\"status\":\"complete\"}", 1, marked));
        CHECK(!MarkSummaryIncomplete("{\"status\":\"incomplete\",\"reconciled_at\":1}", 1, marked));
        CHECK(!MarkSummaryIncomplete("not json", 1, marked));

        fs::path const traces = dir / "traces";
        fs::create_directories(traces / "nested");
        auto put = [](fs::path const& p, std::string const& text) { std::ofstream f(p); f << text << '\n'; };
        Builder b1("stale");
        Header h;
        Counters c;
        c.frames = 9;
        c.records = 12;
        put(traces / "stale.summary.json", b1.BuildSummary(h, "running", "", 0, 0, c, "stale.jsonl"));
        put(traces / "stale.jsonl", "{\"k\":\"header\"}");
        put(traces / "done.summary.json", Builder("done").BuildSummary(h, "complete", "stop", 1, 1, c, "done.jsonl"));
        put(traces / "live.summary.json", Builder("live").BuildSummary(h, "running", "", 0, 0, c, "live.jsonl"));
        put(traces / "nested" / "deep.summary.json", Builder("deep").BuildSummary(h, "running", "", 0, 0, c, "deep.jsonl"));
        put(traces / "notes.json", "{\"status\":\"running\"}");
        std::string const doneBefore = Slurp((traces / "done.summary.json").string());

        ReconcileResult r = ReconcileStaleSummaries(traces.string(), "live", 1700000123);
        CHECK(r.marked.size() == 1 && r.marked[0] == "stale" && r.errors.empty());
        std::string const stale = Slurp((traces / "stale.summary.json").string());
        CHECK(stale.find("\"status\":\"incomplete\"") != std::string::npos);
        CHECK(stale.find("\"end_reason\":\"server stopped before the trace was finalised\"") != std::string::npos);
        CHECK(stale.find("\"reconciled_at\":1700000123") != std::string::npos);
        CHECK(stale.find("\"counters\":" + Builder::CountersJson(c)) != std::string::npos);  // counters kept
        CHECK(Slurp((traces / "stale.jsonl").string()) == "{\"k\":\"header\"}\n");      // trace untouched
        CHECK(Slurp((traces / "done.summary.json").string()) == doneBefore);
        CHECK(Slurp((traces / "live.summary.json").string()).find("\"status\":\"running\"") != std::string::npos);
        CHECK(Slurp((traces / "nested" / "deep.summary.json").string()).find("\"status\":\"running\"") != std::string::npos);
        CHECK(Slurp((traces / "notes.json").string()) == "{\"status\":\"running\"}\n");
        CHECK(!fs::exists(traces / "stale.summary.json.tmp"));

        // Idempotent; a missing directory is no error.
        CHECK(ReconcileStaleSummaries(traces.string(), "", 1).marked.size() == 1);   // now "live" too
        CHECK(ReconcileStaleSummaries(traces.string(), "", 1).marked.empty());
        CHECK(ReconcileStaleSummaries((dir / "no-such-dir").string(), "", 1).marked.empty());
    }

    // An unwritable location fails cleanly.
    {
        RunTraceWriter w;
        std::string const blocker = (dir / "blocker").string();
        { std::ofstream f(blocker); f << "x"; }
        CHECK(!w.Open(blocker + "/x/e.jsonl", 1000, 10, 1000, err));
        CHECK(!err.empty());
        CHECK(!w.IsOpen());
    }

    // The summary write is atomic: the target is replaced whole, no .tmp left.
    {
        std::string const path = (dir / "deep" / "r.summary.json").string();
        CHECK(WriteFileAtomic(path, "{\"a\":1}", err));
        CHECK(Slurp(path) == "{\"a\":1}\n");
        CHECK(WriteFileAtomic(path, "{\"a\":2}", err));
        CHECK(Slurp(path) == "{\"a\":2}\n");
        CHECK(!fs::exists(path + ".tmp"));
    }

    fs::remove_all(dir, ec);
    std::printf("test_run_trace_writer: %d checks passed\n", checks);
    return 0;
}

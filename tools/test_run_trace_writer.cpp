// Standalone tests for the run trace's file writer (runtime/RunTraceWriter.h):
// ordered writes, the bounded queue and its drop counter, the file size cap
// with the reserved tail for the end record, and the atomic summary write.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_run_trace_writer.cpp runtime/RunTraceWriter.cpp -pthread -o /tmp/test_run_trace_writer
//   /tmp/test_run_trace_writer [scratch dir]

#include "../runtime/RunTraceWriter.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
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

    // Drain waits for what is queued; the thread keeps running.
    {
        std::string const path = (dir / "f.jsonl").string();
        RunTraceWriter w;
        CHECK(w.Open(path, 64 << 20, 100000, 64 << 20, err));
        for (int i = 0; i < 5000; ++i)
            CHECK(w.Push("{\"i\":" + std::to_string(i) + "}"));
        WriterStats s = w.Drain();
        CHECK(s.records == 5000 && s.dropped == 0);
        CHECK(w.Push("{\"i\":5000}"));
        s = w.Close();
        CHECK(s.records == 5001);
        CHECK(ReadLines(path).size() == 5001);
    }

    // The end record and the summary agree: the end line is built after the
    // queue drained (every line pushed just before it counted), and the summary
    // is written from the same counters.
    auto endAndSummaryAgree = [&](char const* name, uint64_t cap, int lines, size_t lineLen)
    {
        std::string const path = (dir / name).string();
        RunTraceWriter w;
        CHECK(w.Open(path, cap, 100000, 64 << 20, err));
        Builder b("agree");
        for (int i = 0; i < lines; ++i)
            w.Push(std::string(lineLen, 'z'));  // still queued when the run ends
        Counters c;
        c.frames = 7;
        c.actions = 3;
        CHECK(w.CloseWithEnd([&](Counters const& cc)
            {
                EndInfo end;
                end.reason = "stop";
                end.counters = cc;
                return b.BuildEnd(100, end);
            }, c));
        Header h;
        std::string const summary = b.BuildSummary(h, "complete", "stop", 1, 100, c, name);
        auto const file = ReadLines(path);
        CHECK(!file.empty());
        std::string const endLine = file.back();
        std::string const counters = Builder::CountersJson(c);
        CHECK(endLine.find("\"counters\":" + counters) != std::string::npos);
        CHECK(summary.find("\"counters\":" + counters) != std::string::npos);
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

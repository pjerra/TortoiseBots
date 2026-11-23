// The addon channel rule without the core: the TBM text grammar, the wire
// shapes and the world-thread FIFO (TBF #130 piece f-chan). No core header
// and no legacy type; tested standalone by forever/tools/test_addon_pure.cpp.
// The live half that reads the live objects is host/TbAddonChannel.cpp.
#pragma once

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace TortoiseBots
{
namespace addon
{
namespace pure
{
// The prefix the client sends on and the server answers on.
inline constexpr std::string_view kPrefix = "TBM";
// A burst larger than any UI produces in one world tick; it bounds memory if a
// client floods addon messages (those skip chat flood control). Same value as
// TB's BotCommandQueue::kMaxPendingPerRequester.
inline constexpr std::size_t kMaxPendingPerRequester = 128;
// The client sends at most 255 bytes per addon text (the core drops longer
// ones); the server answers in lines of at most the same size.
inline constexpr std::size_t kMaxLineBytes = 255;

inline std::string Lower(std::string_view word)
{
    std::string out;
    out.reserve(word.size());
    for (char c : word)
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

inline std::string_view Trim(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);
    return text;
}

// The text after the prefix: first word lowercased is the verb, the trimmed
// rest is the handler's args. Mirrors the .bot grammar without the ".bot ".
struct Parsed
{
    std::string verb;
    std::string_view args;
};

inline Parsed Parse(std::string_view text)
{
    Parsed out;
    text = Trim(text);
    std::size_t i = 0;
    while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i])))
        ++i;
    out.verb = Lower(text.substr(0, i));
    out.args = Trim(text.substr(i));
    return out;
}

// TB's ProtocolSafe (commands/CommandShared.cpp:65-74): the wire splits on
// '|', so fields carrying one, or a line break, send a blank instead. Empty
// fields send "-" (the addon's nil).
inline std::string Safe(std::string_view field)
{
    if (field.empty())
        return "-";
    std::string out(field);
    for (char& c : out)
        if (c == '|' || c == '\r' || c == '\n')
            c = ' ';
    return out;
}

// TB's SendActionAck shape (commands/CommandShared.cpp:87-97). `detail` empty
// sends "-" (no executor yet).
inline std::string AckLine(std::string_view intent, std::string_view scope, std::uint32_t count, std::string_view detail)
{
    std::string safeDetail = detail.empty() ? "-" : Safe(detail);
    return "TBM:ACTION_ACK|" + Safe(intent) + "|" + Safe(scope) + "|" + std::to_string(count) + "|" + safeDetail;
}

// TB's SendActionError shape (commands/CommandShared.cpp:75-86).
inline std::string ErrLine(std::string_view intent, std::string_view code, std::string_view message)
{
    return "TBM:ACTION_ERR|" + Safe(intent) + "|" + Safe(code) + "|" + Safe(message);
}

// One addon message per non-empty line (TB's AddonReplyHandler splits the
// reply on '\n' the same way).
inline std::vector<std::string> SplitLines(std::string_view text)
{
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < text.size())
    {
        std::size_t end = text.find('\n', start);
        std::string line(text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
        if (!line.empty())
            lines.push_back(line);
        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }
    return lines;
}

inline std::string Truncate(std::string_view line)
{
    if (line.size() <= kMaxLineBytes)
        return std::string(line);
    return std::string(line.substr(0, kMaxLineBytes));
}

// TB's queue-full text (BotCommandQueue's reportDropped): one line per
// requester per drain, however many commands the cap dropped.
inline std::string FloodMessage(std::size_t count)
{
    return "Too many bot commands at once: " + std::to_string(count) + " ignored.";
}

// The requester across ticks: the two halves of its ObjectGuid (the drain
// rebuilds the guid and looks the player up again, never keeping a Player*
// past the tick). Core-free, so the order/cap/drop rule tests standalone.
struct RequesterKey
{
    std::uint64_t lo = 0;
    std::uint64_t hi = 0;
    bool operator==(RequesterKey const& other) const { return lo == other.lo && hi == other.hi; }
};

struct RequesterKeyHash
{
    std::size_t operator()(RequesterKey const& key) const
    {
        return std::hash<std::uint64_t>{}(key.lo) * 31 + std::hash<std::uint64_t>{}(key.hi);
    }
};

struct PendingCommand
{
    RequesterKey requester;
    std::string text;
};

struct DroppedReport
{
    RequesterKey requester;
    std::size_t count = 0;
};

// The world-thread FIFO: the hook enqueues from any thread, the drain runs
// the batch on the world thread. TB's BotCommandQueue without the deferral
// (the addon has no bot packets to stay behind).
class PendingQueue
{
public:
    // Any thread. False when the requester already has the maximum pending;
    // the command is then dropped and counted for one report at the next
    // drain.
    bool Enqueue(RequesterKey requester, std::string text)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::size_t& pending = m_pendingPerRequester[requester];
        if (pending >= kMaxPendingPerRequester)
        {
            DroppedReport& dropped = m_dropped[requester];
            dropped.requester = requester;
            ++dropped.count;
            return false;
        }
        ++pending;
        m_pending.push_back(PendingCommand{ requester, std::move(text) });
        return true;
    }

    // World thread. `find` maps a requester key to its live player (a
    // null-like value once the player logged out: the command is then
    // dropped); `run` executes one command; `report` tells a live player how
    // many of its commands the cap dropped. The lock is released before
    // anything runs, so a command that queues another one does not deadlock;
    // that command runs on the next drain. Returns commands executed.
    template <class Find, class Run, class Report>
    std::size_t Drain(Find find, Run run, Report report)
    {
        std::vector<PendingCommand> batch;
        std::unordered_map<RequesterKey, DroppedReport, RequesterKeyHash> dropped;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            batch.swap(m_pending);
            dropped.swap(m_dropped);
            m_pendingPerRequester.clear();
        }
        std::size_t executed = 0;
        for (PendingCommand const& command : batch)
        {
            auto requester = find(command.requester);
            if (!requester)
                continue;
            run(requester, command);
            ++executed;
        }
        for (auto const& entry : dropped)
        {
            auto requester = find(entry.first);
            if (requester)
                report(requester, entry.second);
        }
        return executed;
    }

    void Clear()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending.clear();
        m_dropped.clear();
        m_pendingPerRequester.clear();
    }

    std::size_t PendingCount() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_pending.size();
    }

private:
    mutable std::mutex m_mutex;
    std::vector<PendingCommand> m_pending;
    std::unordered_map<RequesterKey, std::size_t, RequesterKeyHash> m_pendingPerRequester;
    std::unordered_map<RequesterKey, DroppedReport, RequesterKeyHash> m_dropped;
};
} // namespace pure
} // namespace addon
} // namespace TortoiseBots

#pragma once

// The whispers real players send the leader of an advertising bot LFG group
// are taken on the packet hook (any thread; the bot's AI never sees them,
// and its chat gate may be closed) and handed to BotLfgService on the world
// thread. Standard library only.

#include "BotChatGate.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace TortoiseBots
{

struct LfgWhisper
{
    uint64_t leader = 0;
    uint64_t sender = 0;
    std::string text;
};

class BotLfgWhisperInbox
{
public:
    static constexpr size_t kMaxQueued = 64;
    static constexpr size_t kMaxQueuedPerSender = 4;
    static constexpr size_t kMaxTextBytes = 255;

    static BotLfgWhisperInbox& Instance()   // function-local static
    {
        static BotLfgWhisperInbox inbox;
        return inbox;
    }

    void PublishLeaders(std::vector<uint64_t> const& leaders)   // world thread
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_leaders.clear();
        m_leaders.insert(leaders.begin(), leaders.end());
        m_queue.erase(std::remove_if(m_queue.begin(), m_queue.end(),
            [&](LfgWhisper const& queued) { return m_leaders.find(queued.leader) == m_leaders.end(); }),
            m_queue.end());
        m_any.store(!m_leaders.empty(), std::memory_order_release);
    }

    // World thread, first step of a withdrawal: from here on no whisper for this leader is accepted (Offer
    // returns false, so it reaches the bot's AI), and whatever is still queued for him is dropped.
    void WithdrawLeader(uint64_t leader)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_leaders.erase(leader);
        m_queue.erase(std::remove_if(m_queue.begin(), m_queue.end(),
            [&](LfgWhisper const& queued) { return queued.leader == leader; }),
            m_queue.end());
        m_any.store(!m_leaders.empty(), std::memory_order_release);
    }

    bool IsLeader(uint64_t guid) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_leaders.find(guid) != m_leaders.end();
    }

    // Any thread. True when the whisper belongs to an advertising leader: it is queued (one entry per leader
    // and sender, a newer whisper replacing the queued text; dropped over the cap) and must not reach the bot's AI.
    // False when the sender already fills his own share: an ordinary whisper for the bot's AI.
    bool Offer(uint64_t botGuid, uint8_t const* data, size_t size, uint8_t whisperType, uint32_t addonLanguage)
    {
        if (!data || size < 1 || data[0] != whisperType)
            return false;
        if (!m_any.load(std::memory_order_acquire))
            return false;
        WhisperChatLine line;
        if (!ParseWhisperChatLine(data, size, line))
            return false;
        if (line.language == addonLanguage)
            return false;
        if (line.sender == botGuid)
            return false;
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_leaders.find(botGuid) == m_leaders.end())
            return false;
        size_t length = line.textLength < kMaxTextBytes ? line.textLength : kMaxTextBytes;
        for (LfgWhisper& queued : m_queue)
            if (queued.leader == botGuid && queued.sender == line.sender)
            {
                queued.text.assign(line.text, length);
                return true;
            }
        size_t senderQueued = 0;
        for (LfgWhisper const& queued : m_queue)
            if (queued.sender == line.sender)
                ++senderQueued;
        if (senderQueued >= kMaxQueuedPerSender)
            return false;
        if (m_queue.size() >= kMaxQueued)
        {
            ++m_dropped;
            return true;
        }
        m_queue.push_back(LfgWhisper{botGuid, line.sender, std::string(line.text, length)});
        return true;
    }

    std::vector<LfgWhisper> Drain()   // world thread
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<LfgWhisper> taken;
        taken.swap(m_queue);
        return taken;
    }

    size_t TakeDropped()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        size_t taken = m_dropped;
        m_dropped = 0;
        return taken;
    }

private:
    mutable std::mutex m_mutex;
    std::atomic<bool> m_any{false};
    std::unordered_set<uint64_t> m_leaders;
    std::vector<LfgWhisper> m_queue;
    size_t m_dropped = 0;
};

} // namespace TortoiseBots

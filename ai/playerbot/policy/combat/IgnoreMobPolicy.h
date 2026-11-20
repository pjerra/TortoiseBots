// Pure rules of `.tb ignore` (the "leave it alone" list): a commander's mobs
// its bots never pick and never splash. Core-free so tools/test_ignore13.cpp
// proves it without a server; the host (forever/src/host/TbIgnore.cpp) keeps
// one list per commander and the engine asks it through IsIgnoredMobCompat.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ai
{
// At most this many mobs per commander; an entry dies with its mob or after
// this many seconds, whichever comes first (the host erases dead units it can
// still see; anything else ages out here).
inline constexpr uint32_t kIgnoreMobMax = 8;
inline constexpr uint32_t kIgnoreMobExpirySec = 30u * 60u;
// No enemy-area spell goes out while an ignored mob is within this distance
// of the bot, or within the blast plus this distance of the blast's centre.
inline constexpr float kIgnoreMobAoERadiusYd = 30.0f;

template <class Guid>
struct IgnoreMobEntry
{
    Guid guid{};
    uint32_t addedSec = 0;
    std::string name;
};

// Fixed-cap list with expiry. Every read takes the caller's now: an expired
// entry counts as absent everywhere, and PruneExpired physically drops them.
template <class Guid>
class IgnoreMobList
{
public:
    enum class AddResult
    {
        Added,
        Duplicate,
        Full,
    };

    AddResult Add(Guid const& guid, std::string name, uint32_t nowSec)
    {
        PruneExpired(nowSec);
        for (IgnoreMobEntry<Guid> const& entry : m_entries)
            if (entry.guid == guid)
                return AddResult::Duplicate;
        if (m_entries.size() >= kIgnoreMobMax)
            return AddResult::Full;
        m_entries.push_back({ guid, nowSec, std::move(name) });
        return AddResult::Added;
    }

    // True when an entry was dropped, expired or not.
    bool Remove(Guid const& guid)
    {
        for (auto it = m_entries.begin(); it != m_entries.end(); ++it)
            if (it->guid == guid)
            {
                m_entries.erase(it);
                return true;
            }
        return false;
    }

    // Every entry goes, live or expired; returns how many went.
    uint32_t Clear()
    {
        uint32_t const count = uint32_t(m_entries.size());
        m_entries.clear();
        return count;
    }

    bool Contains(Guid const& guid, uint32_t nowSec) const
    {
        for (IgnoreMobEntry<Guid> const& entry : m_entries)
            if (entry.guid == guid && !Expired(entry, nowSec))
                return true;
        return false;
    }

    uint32_t PruneExpired(uint32_t nowSec)
    {
        uint32_t removed = 0;
        for (auto it = m_entries.begin(); it != m_entries.end();)
        {
            if (Expired(*it, nowSec))
            {
                it = m_entries.erase(it);
                ++removed;
            }
            else
                ++it;
        }
        return removed;
    }

    // Seconds until the entry lapses, 0 when absent or already lapsed.
    uint32_t ExpiresIn(Guid const& guid, uint32_t nowSec) const
    {
        for (IgnoreMobEntry<Guid> const& entry : m_entries)
        {
            if (!(entry.guid == guid) || Expired(entry, nowSec))
                continue;
            return kIgnoreMobExpirySec - (nowSec - entry.addedSec);
        }
        return 0;
    }

    std::vector<IgnoreMobEntry<Guid>> Live(uint32_t nowSec) const
    {
        std::vector<IgnoreMobEntry<Guid>> out;
        for (IgnoreMobEntry<Guid> const& entry : m_entries)
            if (!Expired(entry, nowSec))
                out.push_back(entry);
        return out;
    }

private:
    static bool Expired(IgnoreMobEntry<Guid> const& entry, uint32_t nowSec)
    {
        return nowSec - entry.addedSec >= kIgnoreMobExpirySec;
    }

    std::vector<IgnoreMobEntry<Guid>> m_entries;
};

// One ignored mob against one enemy-area spell: the spell stays unfired while
// the mob stands within the ignore radius of the bot, or within the blast
// plus that radius of the blast's centre. The caller passes the two distances;
// this is only the comparison, so a test can pin the boundary.
inline bool AoEBlockedByIgnored(float blastRadiusYd, float ignoredToBotYd, float ignoredToBlastYd)
{
    return ignoredToBotYd <= kIgnoreMobAoERadiusYd ||
        ignoredToBlastYd <= blastRadiusYd + kIgnoreMobAoERadiusYd;
}
} // namespace ai

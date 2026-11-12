#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace TortoiseBots { namespace RaidBossLog {

class Throttle
{
public:
    bool Due(uint32_t diff)
    {
        elapsed += diff;
        if (!started || elapsed >= 2000)
        {
            started = true;
            elapsed = 0;
            return true;
        }
        return false;
    }
private:
    bool started = false;
    uint64_t elapsed = 0;
};

inline std::string Csv(std::string const& text)
{
    std::string out = "\"";
    for (char c : text)
    {
        if (c == '"') out += '"';
        out += c;
    }
    return out + '"';
}

inline std::string Row(std::string const& timestamp, std::string const& actor, std::string const& event,
    std::string const& positionCsv, uint32_t race, uint32_t playerClass, float level,
    std::string const& subject, std::string const& detail)
{
    std::ostringstream out;
    out << timestamp << "+00," << Csv(actor) << ',' << event << ',' << positionCsv
        << race << ',' << playerClass << ',' << std::fixed << std::setprecision(2) << level
        << ',' << Csv(subject) << ',' << Csv(detail);
    return out.str();
}

// Names in the key/value payload cannot inject its separators or newlines.
inline std::string Name(std::string const& text)
{
    static char const hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : text)
    {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-') out += c;
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}

struct ThreatEntry { uint64_t guid; std::string name; float threat; };
inline std::string Threat(uint32_t entry, uint32_t map, uint32_t instance, uint64_t guid,
    std::string const& victim, std::vector<ThreatEntry> entries)
{
    entries.erase(std::remove_if(entries.begin(), entries.end(), [](ThreatEntry const& e) {
        return !std::isfinite(e.threat);
    }), entries.end());
    auto count = std::min<size_t>(5, entries.size());
    std::partial_sort(entries.begin(), entries.begin() + count, entries.end(),
        [](ThreatEntry const& a, ThreatEntry const& b) {
            return a.threat != b.threat ? a.threat > b.threat : a.guid < b.guid;
        });
    std::ostringstream out;
    out << "entry=" << entry << "|map=" << map << "|instance=" << instance << "|boss_guid=" << guid
        << "|victim=" << Name(victim) << "|top=" << count << std::fixed << std::setprecision(2);
    for (size_t i = 0; i < count; ++i)
        out << "|t" << i + 1 << "=" << Name(entries[i].name) << ":" << entries[i].threat
            << ":" << entries[i].guid;
    return out.str();
}

// Packed guids, then fixed-width fields; a short buffer fails without reading past its end.
struct PacketReader
{
    uint8_t const* data = nullptr;
    size_t size = 0;
    size_t pos = 0;

    bool Packed(uint64_t& guid)
    {
        if (pos == size) return false;
        uint8_t mask = data[pos++];
        guid = 0;
        for (unsigned i = 0; i < 8; ++i)
            if (mask & (1u << i))
            {
                if (pos == size) return false;
                guid |= uint64_t(data[pos++]) << (8 * i);
            }
        return true;
    }

    uint32_t U32()
    {
        uint32_t v = 0;
        for (unsigned i = 0; i < 4; ++i) v |= uint32_t(data[pos++]) << (8 * i);
        return v;
    }

    uint8_t U8() { return data[pos++]; }
};

struct Damage { uint64_t victim = 0, caster = 0; uint32_t spell = 0, amount = 0; };
inline bool ParseDamage(uint8_t const* data, size_t size, Damage& result)
{
    PacketReader reader{data, size};
    if (!reader.Packed(result.victim) || !reader.Packed(result.caster) || size - reader.pos < 28) return false;
    result.spell = reader.U32(); result.amount = reader.U32();
    return result.victim && result.caster && result.spell;
}

// SMSG_SPELLHEALLOG: packed target, packed caster, then the spell, the heal before any overheal cut, and the crit flag.
struct Heal { uint64_t target = 0, caster = 0; uint32_t spell = 0, amount = 0; bool crit = false; };
inline bool ParseHeal(uint8_t const* data, size_t size, Heal& result)
{
    PacketReader reader{data, size};
    if (!reader.Packed(result.target) || !reader.Packed(result.caster) || size - reader.pos < 9) return false;
    result.spell = reader.U32(); result.amount = reader.U32(); result.crit = reader.U8() != 0;
    return result.target && result.caster && result.spell;
}

inline bool VictimCopy(uint64_t recipient, Damage const& damage) { return recipient == damage.victim; }
inline bool TargetCopy(uint64_t recipient, Heal const& heal) { return recipient == heal.target; }
inline std::string Hit(uint32_t entry, uint32_t map, uint32_t instance, Damage const& damage,
    std::string const& caster, std::string const& victim, uint32_t health)
{
    std::ostringstream out;
    out << "entry=" << entry << "|map=" << map << "|instance=" << instance
        << "|caster=" << Name(caster) << "|caster_guid=" << damage.caster
        << "|spell=" << damage.spell << "|victim=" << Name(victim) << "|victim_guid=" << damage.victim
        << "|reported_damage=" << damage.amount << "|hp_sample=" << health
        << "|hp_est=" << (damage.amount < health ? health - damage.amount : 0)
        << "|hp_sample_at=packet_send";
    return out.str();
}

inline std::string HealRow(uint32_t map, uint32_t instance, Heal const& heal, std::string const& healer,
    std::string const& target, uint32_t health, uint32_t maxHealth)
{
    std::ostringstream out;
    out << "map=" << map << "|instance=" << instance
        << "|healer=" << Name(healer) << "|healer_guid=" << heal.caster
        << "|spell=" << heal.spell << "|target=" << Name(target) << "|target_guid=" << heal.target
        << "|amount=" << heal.amount << "|crit=" << (heal.crit ? 1 : 0)
        << "|hp_sample=" << health << "|hp_max=" << maxHealth
        << "|hp_sample_at=packet_send";
    return out.str();
}
} }

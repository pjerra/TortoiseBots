#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace TortoiseBots {

// Issue #1003: names a real player would pick instead of random syllables.
// Three styles, rolled by AiPlayerbot.RandomBotNameStyleWeights:
//  - rp:       race and gender flavoured, built by an order-2 letter chain
//              trained on the rp:<race>:<gender> samples (never a sample itself)
//  - compound: start word + end word ("Darkblade"), start words per class
//  - fun:      puns and nods ("Kidneyshotz"), a full pool name or prefix+suffix
// Pure logic: the caller passes a random source and still runs the server's
// own name checks (length, profanity, reserved names, taken) on the result.
//
// Pool text is "key" lines in [brackets] followed by whitespace-separated
// words, e.g.  [fun:rogue] Kidneyshotz Stabbyjoe
// Keys: rp:<race>:<male|female>, start, start:<class>, end, fun, fun:<class>,
// fun:<race>, prefix:<class>, prefix:<race>, suffix.

enum BotNameStyle : uint32_t { NAME_STYLE_RP = 0, NAME_STYLE_COMPOUND = 1, NAME_STYLE_FUN = 2, NAME_STYLE_COUNT = 3 };

uint32_t const kBotNameMaxLength = 12; // MAX_PLAYER_NAME
uint32_t const kBotNameTakenVariantPct = 6;

inline char const* BotNameRaceKey(uint8_t race)
{
    static char const* const keys[] = { "", "human", "orc", "dwarf", "nightelf", "undead",
        "tauren", "gnome", "troll", "goblin", "highelf" };
    // The two Forever Skyborne are of high-elven descent (core comment on their player_racestats rows), so they share that
    // pool instead of falling back to the generic names.
    if (race == 95 || race == 96)
        return "highelf";
    return race < sizeof(keys) / sizeof(keys[0]) ? keys[race] : "";
}

inline char const* BotNameClassKey(uint8_t cls)
{
    static char const* const keys[] = { "", "warrior", "paladin", "hunter", "rogue", "priest", "",
        "shaman", "mage", "warlock", "", "druid" };
    return cls < sizeof(keys) / sizeof(keys[0]) ? keys[cls] : "";
}

// "55,20,25" -> rp/compound/fun weights. False (out untouched) unless it is
// exactly three non-negative integers with a positive sum.
inline bool ParseBotNameStyleWeights(std::string const& text, uint32_t out[NAME_STYLE_COUNT])
{
    uint32_t parsed[NAME_STYLE_COUNT] = {};
    uint32_t count = 0;
    std::string digits;
    for (size_t i = 0; i <= text.size(); ++i)
    {
        char c = i < text.size() ? text[i] : ',';
        if (c >= '0' && c <= '9')
            digits += c;
        else if (c == ',')
        {
            if (digits.empty() || digits.size() > 6 || count == NAME_STYLE_COUNT)
                return false;
            parsed[count++] = uint32_t(std::stoul(digits));
            digits.clear();
        }
        else if (c != ' ')
            return false;
    }
    if (count != NAME_STYLE_COUNT || parsed[0] + parsed[1] + parsed[2] == 0)
        return false;
    for (uint32_t i = 0; i < NAME_STYLE_COUNT; ++i)
        out[i] = parsed[i];
    return true;
}

// "kIDney" -> "Kidney"; empty unless 2..12 ASCII letters.
inline std::string NormalizeBotName(std::string const& raw)
{
    if (raw.size() < 2 || raw.size() > kBotNameMaxLength)
        return std::string();
    std::string name;
    for (char c : raw)
    {
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');
        if (c < 'a' || c > 'z')
            return std::string();
        name += c;
    }
    name[0] = char(name[0] - 'a' + 'A');
    return name;
}

// Generated names can land on rude words the client's profanity list misses
// ("Vajina" from troll samples); refuse any name containing one of these.
inline bool IsUnfortunateBotName(std::string const& name)
{
    static char const* const banned[] = { "vagin", "vajin", "penis", "penus", "anus", "anal", "nazi", "rape",
        "fuck", "fuk", "shit", "cunt", "kunt", "cock", "dick", "dik", "porn", "slut", "whore",
        "nigg", "nigr", "fag", "cum", "sex", "boob", "tits", "nipp", "pube", "semen", "puss", "jizz",
        "wank", "twat", "bitch", "hitler", "rapist", "kkk", "retard", "spaz" };
    std::string lower;
    for (char c : name)
        lower += (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
    for (char const* word : banned)
        if (lower.find(word) != std::string::npos)
            return true;
    return false;
}

class BotNamePools
{
public:
    // Adds the words of `text` to the pools; returns the number of words added.
    // Words that cannot be a name (not letters, too long) are skipped.
    size_t Parse(std::string const& text)
    {
        size_t added = 0;
        std::string key;
        size_t i = 0;
        while (i < text.size())
        {
            char c = text[i];
            if (c == '#')
            {
                while (i < text.size() && text[i] != '\n')
                    ++i;
                continue;
            }
            if (c == '[')
            {
                size_t close = text.find(']', i);
                if (close == std::string::npos)
                    break;
                key = text.substr(i + 1, close - i - 1);
                for (char& k : key)
                    if (k >= 'A' && k <= 'Z')
                        k = char(k - 'A' + 'a');
                i = close + 1;
                continue;
            }
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',')
            {
                ++i;
                continue;
            }
            size_t start = i;
            while (i < text.size() && text[i] != ' ' && text[i] != '\t' && text[i] != '\r' &&
                   text[i] != '\n' && text[i] != ',' && text[i] != '[' && text[i] != '#')
                ++i;
            std::string word = text.substr(start, i - start);
            if (key.empty() || (NormalizeBotName(word).empty() && !IsPartKey(key, word)))
                continue;
            m_words[key].push_back(LowerWord(word));
            ++added;
        }
        BuildChains();
        return added;
    }

    std::vector<std::string> const& Words(std::string const& key) const
    {
        static std::vector<std::string> const empty;
        auto it = m_words.find(key);
        return it == m_words.end() ? empty : it->second;
    }

    // One candidate name (normalized, may be empty if the pools cannot build one).
    // `rng(lo, hi)` returns a uniform integer in [lo, hi].
    template <typename Rng>
    std::string Generate(uint8_t race, uint8_t cls, uint8_t gender, uint32_t const weights[NAME_STYLE_COUNT], Rng&& rng) const
    {
        uint32_t total = weights[0] + weights[1] + weights[2];
        BotNameStyle style = NAME_STYLE_RP;
        if (total)
        {
            uint32_t roll = rng(0u, total - 1);
            style = roll < weights[0] ? NAME_STYLE_RP : roll < weights[0] + weights[1] ? NAME_STYLE_COMPOUND : NAME_STYLE_FUN;
        }
        std::string race_key = BotNameRaceKey(race);
        std::string cls_key = BotNameClassKey(cls);
        std::string name;
        if (style == NAME_STYLE_FUN)
            name = Fun(race_key, cls_key, rng);
        else if (style == NAME_STYLE_COMPOUND)
            name = Compound(cls_key, rng);
        if (name.empty())
            name = Rp(race_key, gender, rng);
        if (IsUnfortunateBotName(name))
            return std::string();
        return NormalizeBotName(name);
    }

private:
    static std::string LowerWord(std::string word)
    {
        for (char& c : word)
            if (c >= 'A' && c <= 'Z')
                c = char(c - 'A' + 'a');
        return word;
    }

    // Prefix/suffix/start/end pieces may be one letter ("z").
    static bool IsPartKey(std::string const& key, std::string const& word)
    {
        if (key.compare(0, 6, "prefix") && key.compare(0, 6, "suffix") && key.compare(0, 5, "start") && key.compare(0, 3, "end"))
            return false;
        if (word.empty() || word.size() > kBotNameMaxLength)
            return false;
        for (char c : word)
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
                return false;
        return true;
    }

    template <typename Rng>
    std::string const* PickFrom(std::vector<std::vector<std::string> const*> const& lists, Rng& rng) const
    {
        size_t total = 0;
        for (auto const* list : lists)
            total += list->size();
        if (!total)
            return nullptr;
        size_t pick = rng(0u, uint32_t(total - 1));
        for (auto const* list : lists)
        {
            if (pick < list->size())
                return &(*list)[pick];
            pick -= list->size();
        }
        return nullptr;
    }

    template <typename Rng>
    std::string Fun(std::string const& race_key, std::string const& cls_key, Rng& rng) const
    {
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            if (rng(0u, 1u) == 0)
            {
                std::string const* full = PickFrom({ &Words("fun"), &Words("fun:" + cls_key), &Words("fun:" + race_key) }, rng);
                if (full)
                    return *full;
            }
            std::string const* prefix = PickFrom({ &Words("prefix:" + cls_key), &Words("prefix:" + race_key) }, rng);
            std::string const* suffix = PickFrom({ &Words("suffix") }, rng);
            if (prefix && suffix && prefix->size() + suffix->size() <= kBotNameMaxLength)
                return *prefix + *suffix;
        }
        return std::string();
    }

    template <typename Rng>
    std::string Compound(std::string const& cls_key, Rng& rng) const
    {
        for (int attempt = 0; attempt < 6; ++attempt)
        {
            std::string const* start = PickFrom({ &Words("start"), &Words("start:" + cls_key) }, rng);
            std::string const* end = PickFrom({ &Words("end") }, rng);
            if (start && end && *start != *end && start->size() + end->size() <= kBotNameMaxLength)
                return *start + *end;
        }
        return std::string();
    }

    // Order-2 letter chain: "^^" starts a name, '$' ends it.
    struct Chain
    {
        std::map<std::string, std::string> next;
        std::set<std::string> samples;
    };

    // Built once per Parse so Generate stays read-only (pool bots and hires may
    // name characters from different threads).
    void BuildChains()
    {
        m_chains.clear();
        for (auto const& entry : m_words)
        {
            if (entry.first.compare(0, 3, "rp:"))
                continue;
            Chain& chain = m_chains[entry.first];
            for (std::string const& sample : entry.second)
            {
                chain.samples.insert(sample);
                std::string padded = "^^" + sample + "$";
                for (size_t i = 2; i < padded.size(); ++i)
                    chain.next[padded.substr(i - 2, 2)] += padded[i];
            }
        }
    }

    Chain const* ChainFor(std::string const& key) const
    {
        auto it = m_chains.find(key);
        return it == m_chains.end() || it->second.samples.empty() ? nullptr : &it->second;
    }

    template <typename Rng>
    std::string Rp(std::string const& race_key, uint8_t gender, Rng& rng) const
    {
        Chain const* chain = ChainFor("rp:" + race_key + (gender ? ":female" : ":male"));
        if (!chain)
            chain = ChainFor("rp:" + race_key + ":male");
        if (!chain)
            chain = ChainFor("rp:human:male");
        if (!chain)
            return std::string();
        for (int attempt = 0; attempt < 40; ++attempt)
        {
            std::string name;
            std::string state = "^^";
            while (name.size() <= kBotNameMaxLength)
            {
                auto it = chain->next.find(state);
                if (it == chain->next.end())
                    break;
                char c = it->second[rng(0u, uint32_t(it->second.size() - 1))];
                if (c == '$')
                    break;
                name += c;
                state = state.substr(1) + c;
            }
            if (name.size() < 4 || name.size() > 9)
                continue;
            if (rng(0u, 99u) < kBotNameTakenVariantPct)
                name = TakenVariant(name, rng);
            if (!chain->samples.count(name) && !IsUnfortunateBotName(name))
                return name;
        }
        return std::string();
    }

    // "Arthas" taken -> "Arthaas" / "Arthass": double one vowel or the last letter.
    template <typename Rng>
    static std::string TakenVariant(std::string const& name, Rng& rng)
    {
        if (name.size() >= kBotNameMaxLength)
            return name;
        std::vector<size_t> vowels;
        for (size_t i = 1; i < name.size(); ++i)
            if (std::string("aeiouy").find(name[i]) != std::string::npos && name[i] != name[i - 1] &&
                (i + 1 >= name.size() || name[i + 1] != name[i]))
                vowels.push_back(i);
        size_t at = name.size() - 1;
        if (!vowels.empty() && rng(0u, 1u) == 0)
            at = vowels[rng(0u, uint32_t(vowels.size() - 1))];
        if (at >= 1 && name[at - 1] == name[at])
            return name;
        return name.substr(0, at + 1) + name[at] + name.substr(at + 1);
    }

    std::map<std::string, std::vector<std::string>> m_words;
    std::map<std::string, Chain> m_chains;
};

} // namespace TortoiseBots

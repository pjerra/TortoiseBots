// The characters store as the standalone add-on hint test needs it: the schema probe answers from FakeWorld, the
// flagged-rows SELECT reads the fake rows, and every statement is recorded. An INSERT IGNORE adds its guids to the
// fake rows (so a second LoadAtStartup would see them) and a DELETE removes the named guid.
#pragma once

#include "../FakeWorld.h"
#include "DatabaseEnv.h"

#include <cctype>
#include <set>
#include <string>
#include <vector>

inline bool CharacterSchemaHasCompat(std::string const&, std::set<std::string> const&)
{
    return fakeworld::schemaAvailable;
}

inline QueryResult CharacterQueryGuardedCompat(std::string const& sql)
{
    fakeworld::executed.push_back(sql);
    if (sql.find("`tortoise_bots_player_flag`") == std::string::npos)
        return QueryResult();
    std::vector<uint64> rows(fakeworld::storedFlags.begin(), fakeworld::storedFlags.end());
    return QueryResult(std::move(rows));
}

namespace fakeworld
{
inline uint64 FirstNumber(std::string const& text, std::size_t from)
{
    std::size_t at = from;
    while (at < text.size() && !std::isdigit(static_cast<unsigned char>(text[at])))
        ++at;
    uint64 value = 0;
    while (at < text.size() && std::isdigit(static_cast<unsigned char>(text[at])))
        value = value * 10 + static_cast<uint64>(text[at++] - '0');
    return value;
}
} // namespace fakeworld

inline void CharacterExecuteGuardedCompat(std::string const& sql)
{
    fakeworld::executed.push_back(sql);
    if (sql.rfind("INSERT IGNORE", 0) == 0)
    {
        std::size_t at = sql.find("VALUES");
        while (at != std::string::npos && (at = sql.find('(', at)) != std::string::npos)
        {
            uint64 const guid = fakeworld::FirstNumber(sql, at + 1);
            if (guid)
                fakeworld::storedFlags.insert(guid);
            ++at;
        }
    }
    else if (sql.rfind("DELETE", 0) == 0)
    {
        std::size_t const at = sql.find("`guid` =");
        if (at != std::string::npos)
            fakeworld::storedFlags.erase(fakeworld::FirstNumber(sql, at + 8));
    }
}

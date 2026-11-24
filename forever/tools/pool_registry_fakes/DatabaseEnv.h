// The characters-database surface the pool registry and creation use, with ForeverCore's row semantics
// (Database/QueryResult.h and AdhocStatement.cpp): DatabaseWorkerPool::Query hands the result set over already standing on its
// FIRST row and returns NULL when the set is empty. The fake's two tables carry the registry's own SQL shapes (INSERT ... VALUES,
// UPDATE ... SET ... WHERE, DELETE FROM ... WHERE, the single-table SELECTs) so the tables' real code runs unchanged: a write the
// fake refuses is a write that did not land, and the read-back of the registry sees exactly that.
//
// The fake also holds the core's own `characters` table (`coreCharacters`), which the pool reads to confirm a creation and to
// prune an account; `failCoreReads` makes it answer nothing, which must never be read as "no character".
//
// Failure injection: failAccountInserts / failCharacterInserts / failUpdateNumber / failDeletes / failCoreReads. The statement and
// core-event log (`fakePoolEvents`) records every write and every account/character creation in order: the write-ahead protocol
// is what it shows.
#pragma once

#include "Define.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

inline std::vector<std::string> fakePoolEvents;

// The characters database's synchronous transaction surface (TbCompat_CharStore.h uses it for the one multi-statement
// save). The pool's tests never depend on its atomicity: the statements are applied in order like the direct ones.
class FakeTransaction
{
public:
    void Append(char const* /*sql*/) {}
};
using CharacterDatabaseTransaction = std::shared_ptr<FakeTransaction>;

class Field
{
public:
    void SetUInt64(uint64 value)
    {
        _number = value;
        _text.clear();
        _null = false;
    }

    void SetText(std::string value)
    {
        _text = std::move(value);
        _number = 0;
        _null = false;
    }

    // A LEFT JOIN's missing side: the core's Field::IsNull, which a caller must not read as 0 or as an empty name.
    void SetNull()
    {
        _number = 0;
        _text.clear();
        _null = true;
    }

    uint64 GetUInt64() const { return _number; }
    uint32 GetUInt32() const { return uint32(_number); }
    uint8 GetUInt8() const { return uint8(_number); }
    std::string GetString() const { return _text; }
    bool IsNull() const { return _null; }

private:
    uint64 _number = 0;
    std::string _text;
    bool _null = false;
};

class ResultSet
{
public:
    explicit ResultSet(std::vector<std::vector<Field>> rows) : _rows(std::move(rows)) {}

    uint64 GetRowCount() const { return _rows.size(); }

    bool NextRow()
    {
        if (_at >= _rows.size())
            return false;
        _current = _rows[_at++];
        return true;
    }

    Field* Fetch() { return _current.data(); }

private:
    std::vector<std::vector<Field>> _rows;
    std::vector<Field> _current;
    std::size_t _at = 0;
};

using QueryResult = std::shared_ptr<ResultSet>;

class FakePoolDatabase
{
public:
    struct AccountRow
    {
        std::string username;
        bool hasId = false;
        uint32 id = 0;
        std::string stamp;
        std::string state = "creating";
        int64 created = 0;
    };

    struct CharacterRow
    {
        std::string name;
        bool hasGuid = false;
        uint64 guid = 0;
        uint32 accountId = 0;
        uint8 race = 0;
        uint8 klass = 0;
        uint8 gender = 0;
        std::string state = "creating";
        int64 created = 0;
        int64 lastLogin = 0;
        int64 lastLogout = 0;
    };

    // The core's own `characters` table: what the pool reads to confirm a creation, and never the character cache.
    struct CoreCharacterRow
    {
        std::string name;
        uint64 guid = 0;
        uint32 accountId = 0;
        bool online = false;
    };

    std::vector<AccountRow> accounts;
    std::vector<CharacterRow> characters;
    std::vector<CoreCharacterRow> coreCharacters;

    // The startup reset's world: the guild tables, auctions, the module's row tables and the reset's own state table.
    // `extraTables` is the information_schema answer for those (a table absent from the map is missing).
    struct GuildRow
    {
        uint32 guildid = 0;
        std::string name;
        uint64 leaderGuid = 0;
    };
    struct GuildMemberRow
    {
        uint32 guildid = 0;
        uint64 guid = 0;
    };
    struct StateRow
    {
        std::string poolName;
        std::string appliedGeneration;
        std::string completedAt;
    };
    std::vector<GuildRow> guilds;
    std::vector<GuildMemberRow> guildMembers;
    std::vector<StateRow> stateRows;
    std::set<uint64> auctionOwners;
    std::vector<uint64> moduleDbStoreGuids;
    std::vector<uint64> moduleCustomStrategyOwners;
    std::vector<uint64> moduleCommanderGuids;
    std::vector<uint64> moduleOrderBotGuids;
    std::vector<uint64> moduleDisenchantBotGuids;
    std::map<std::string, std::set<std::string>> extraTables;
    bool failGuildReads = false;
    bool failAuctionReads = false;

    std::set<std::string> accountColumns = { "username", "account_id", "stamp", "state", "created" };
    std::set<std::string> characterColumns = { "name", "guid", "account_id", "race", "class", "gender", "state", "created", "last_login", "last_logout" };
    bool haveAccountTable = true;
    bool haveCharacterTable = true;
    bool failAccountInserts = false;
    bool failCharacterInserts = false;
    int failUpdateNumber = 0;        // 1-based: this UPDATE and every one after it do not land (0 = none)
    bool failDeletes = false;
    bool failCharacterUpdate = false;
    bool failPoolReads = false;      // the pool tables' reads (rows and counts) do not answer
    bool failCoreReads = false;      // the characters database does not answer the pool's authoritative reads
    int updates = 0;
    int creates = 0;

    QueryResult Query(char const* text)
    {
        std::string const sql(text ? text : "");
        if (sql.find("information_schema.COLUMNS") != std::string::npos)
            return SchemaAnswer(sql);
        if (failPoolReads && sql.find("`tb_pool_") != std::string::npos)
            return nullptr;
        if (sql == "SELECT COUNT(*) FROM `tb_pool_account`" || sql == "SELECT COUNT(*) FROM `tb_pool_character`")
        {
            std::vector<std::vector<Field>> out;
            out.push_back({ Num(sql.find("account") != std::string::npos ? accounts.size() : characters.size()) });
            return MakeResult(std::move(out));
        }
        if (sql.rfind("SELECT COUNT(*) FROM `characters` WHERE ", 0) == 0)
        {
            if (failCoreReads)
                return nullptr;                       // "the characters database did not answer"
            std::string const where = sql.substr(std::string("SELECT COUNT(*) FROM `characters` WHERE ").size());
            uint64 count = 0;
            if (where.rfind("`name` = ", 0) == 0)
            {
                std::string name, rest;
                if (!ParseLiteral(where.substr(std::string("`name` = ").size()), name, rest) || !rest.empty())
                    return nullptr;                   // a malformed literal is a SQL error: no answer
                for (CoreCharacterRow const& row : coreCharacters)
                    if (row.name == name)
                        ++count;
            }
            else if (where.rfind("`account` IN (", 0) == 0)
            {
                std::size_t const close = where.find(')');
                if (close == std::string::npos)
                    return nullptr;
                std::set<uint32> ids;
                for (std::string const& part : Split(where.substr(std::string("`account` IN (").size(), close - std::string("`account` IN (").size())))
                    ids.insert(uint32(std::strtoul(part.c_str(), nullptr, 10)));
                for (CoreCharacterRow const& row : coreCharacters)
                    if (ids.count(row.accountId))
                        ++count;
            }
            else if (where.rfind("`account` = ", 0) == 0)
            {
                uint32 const accountId = uint32(std::strtoul(where.substr(std::string("`account` = ").size()).c_str(), nullptr, 10));
                for (CoreCharacterRow const& row : coreCharacters)
                    if (row.accountId == accountId)
                        ++count;
            }
            else if (where.rfind("`guid` IN (", 0) == 0)
            {
                std::size_t const close = where.find(')');
                if (close == std::string::npos)
                    return nullptr;
                std::set<uint64> guids;
                for (std::string const& part : Split(where.substr(std::string("`guid` IN (").size(), close - std::string("`guid` IN (").size())))
                    guids.insert(uint64(std::strtoull(part.c_str(), nullptr, 10)));
                for (CoreCharacterRow const& row : coreCharacters)
                    if (guids.count(row.guid))
                        ++count;
            }
            else
                return nullptr;
            return MakeResult({ { Num(count) } });
        }
        if (sql.rfind("SELECT `guid`, `account` FROM `characters` WHERE ", 0) == 0)
        {
            if (failCoreReads)
                return nullptr;
            std::string const where = sql.substr(std::string("SELECT `guid`, `account` FROM `characters` WHERE ").size());
            std::string name, rest;
            if (where.rfind("`name` = ", 0) != 0 || !ParseLiteral(where.substr(std::string("`name` = ").size()), name, rest))
                return nullptr;
            if (rest != " LIMIT 1")
                return nullptr;
            for (CoreCharacterRow const& row : coreCharacters)
                if (row.name == name)
                    return MakeResult({ { Num(row.guid), Num(row.accountId) } });
            return nullptr;
        }
        if (sql.rfind("SELECT `guid`, `account`, `name`, `online` FROM `characters` WHERE ", 0) == 0)
        {
            if (failCoreReads)
                return nullptr;
            std::string const where = sql.substr(std::string("SELECT `guid`, `account`, `name`, `online` FROM `characters` WHERE ").size());
            uint32 const accountId = uint32(std::strtoul(where.substr(std::string("`account` = ").size()).c_str(), nullptr, 10));
            std::vector<std::vector<Field>> out;
            for (CoreCharacterRow const& row : coreCharacters)
                if (row.accountId == accountId)
                    out.push_back({ Num(row.guid), Num(row.accountId), Text(row.name), Num(row.online ? 1 : 0) });
            return MakeResult(std::move(out));
        }
        if (sql.rfind("SELECT `username`, `account_id`, `stamp`, `state`, `created` FROM `tb_pool_account`", 0) == 0 && haveAccountTable)
        {
            std::vector<std::vector<Field>> out;
            for (AccountRow const& row : accounts)
                out.push_back({ Text(row.username), Num(row.hasId ? row.id : 0), Text(row.stamp), Text(row.state), Num(uint64(row.created)) });
            return MakeResult(std::move(out));
        }
        // The shapes of the tables' first round (before the state and stamp): the pre-fix translation units are run against the
        // same fake to show which checks catch them.
        if (sql.rfind("SELECT `account_id`, `username`, `created` FROM `tb_pool_account`", 0) == 0 && haveAccountTable)
        {
            std::vector<std::vector<Field>> out;
            for (AccountRow const& row : accounts)
                out.push_back({ Num(row.hasId ? row.id : 0), Text(row.username), Num(uint64(row.created)) });
            return MakeResult(std::move(out));
        }
        if (sql.rfind("SELECT `guid`, `account_id`, `race`, `class`, `gender`, `created`, `last_login`, `last_logout` FROM `tb_pool_character`", 0) == 0 && haveCharacterTable)
        {
            std::vector<std::vector<Field>> out;
            for (CharacterRow const& row : characters)
                out.push_back({ Num(row.hasGuid ? row.guid : 0), Num(row.accountId), Num(row.race), Num(row.klass), Num(row.gender),
                    Num(uint64(row.created)), Num(uint64(row.lastLogin)), Num(uint64(row.lastLogout)) });
            return MakeResult(std::move(out));
        }
        if (sql.rfind("SELECT `name`, `guid`, `account_id`, `race`, `class`, `gender`, `state`, `created`, `last_login`, `last_logout` FROM `tb_pool_character`", 0) == 0 && haveCharacterTable)
        {
            std::vector<std::vector<Field>> out;
            for (CharacterRow const& row : characters)
                out.push_back({ Text(row.name), Num(row.hasGuid ? row.guid : 0), Num(row.accountId), Num(row.race), Num(row.klass), Num(row.gender),
                    Text(row.state), Num(uint64(row.created)), Num(uint64(row.lastLogin)), Num(uint64(row.lastLogout)) });
            return MakeResult(std::move(out));
        }

        // --- the startup reset (TbPoolReset.cpp): its own SQL shapes, parsed strictly ---
        if (sql.rfind("SELECT `guid`, `account`, `name` FROM `characters` WHERE `account` IN (", 0) == 0)
        {
            if (failCoreReads)
                return nullptr;
            std::string const where = sql.substr(std::string("SELECT `guid`, `account`, `name` FROM `characters` WHERE `account` IN (").size());
            std::set<uint32> ids;
            for (std::string const& part : Split(where.substr(0, where.find(')'))))
                ids.insert(uint32(std::strtoul(part.c_str(), nullptr, 10)));
            std::vector<std::vector<Field>> out;
            for (CoreCharacterRow const& row : coreCharacters)
                if (ids.count(row.accountId))
                    out.push_back({ Num(row.guid), Num(row.accountId), Text(row.name) });
            return MakeResult(std::move(out));
        }
        if (sql.rfind("SELECT COUNT(*) FROM `guild` g JOIN `guild_member` gm ON gm.`guildid` = g.`guildid` LEFT JOIN `characters` c ON c.`guid` = gm.`guid` LEFT JOIN `tb_pool_character` p ON p.`guid` = gm.`guid` AND p.`state` = 'ready' WHERE ", 0) == 0)
            return GuildOutsiderAnswer(sql, true);
        if (sql.rfind("SELECT gm.`guid`, c.`account`, c.`name`, g.`name`, g.`leaderguid` FROM `guild` g JOIN `guild_member` gm ON gm.`guildid` = g.`guildid` LEFT JOIN `characters` c ON c.`guid` = gm.`guid` LEFT JOIN `tb_pool_character` p ON p.`guid` = gm.`guid` AND p.`state` = 'ready' WHERE ", 0) == 0)
            return GuildOutsiderAnswer(sql, false);
        if (sql.rfind("SELECT COUNT(*) FROM `auctionhouse` WHERE `owner` IN (", 0) == 0)
        {
            if (failAuctionReads)
                return nullptr;
            std::string const where = sql.substr(std::string("SELECT COUNT(*) FROM `auctionhouse` WHERE `owner` IN (").size());
            std::set<uint64> owners;
            for (std::string const& part : Split(where.substr(0, where.find(')'))))
                owners.insert(uint64(std::strtoull(part.c_str(), nullptr, 10)));
            uint64 count = 0;
            for (uint64 const owner : auctionOwners)
                if (owners.count(owner))
                    ++count;
            return MakeResult({ { Num(count) } });
        }
        if (sql.rfind("SELECT COUNT(*) FROM `auctionhouse` WHERE `owner` = ", 0) == 0)
        {
            if (failAuctionReads)
                return nullptr;
            uint64 const owner = std::strtoull(sql.substr(std::string("SELECT COUNT(*) FROM `auctionhouse` WHERE `owner` = ").size()).c_str(), nullptr, 10);
            return MakeResult({ { Num(auctionOwners.count(owner) ? 1 : 0) } });
        }
        if (sql.rfind("SELECT COUNT(*) FROM `tb_pool_state` WHERE `pool_name` = ", 0) == 0 && HasExtraTable("tb_pool_state"))
        {
            std::string const pool = WhereValue(sql, "pool_name");
            uint64 count = 0;
            for (StateRow const& row : stateRows)
                if (row.poolName == pool)
                    ++count;
            return MakeResult({ { Num(count) } });
        }
        if (sql.rfind("SELECT `applied_generation` FROM `tb_pool_state` WHERE `pool_name` = ", 0) == 0 && HasExtraTable("tb_pool_state"))
        {
            std::string const pool = WhereValue(sql, "pool_name");
            for (StateRow const& row : stateRows)
                if (row.poolName == pool)
                    return MakeResult({ { Text(row.appliedGeneration) } });
            return nullptr;
        }
        return nullptr;
    }

    // ForeverCore's DatabaseWorkerPool::EscapeString (mysql_real_escape_string): a quote, a double quote, a backslash, a newline,
    // a carriage return, a NUL and ^Z each get a backslash, so a value is a literal and never SQL. The reads and writes below parse
    // a literal strictly (ParseLiteral), so a caller that does not escape its text gets no answer, like a syntax error.
    void EscapeString(std::string& text)
    {
        std::string out;
        for (char const c : text)
        {
            switch (c)
            {
                case '\'': out += "\\'"; break;
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\0': out += "\\0"; break;
                case '\x1a': out += "\\Z"; break;
                default: out += c;
            }
        }
        text = out;
    }

    CharacterDatabaseTransaction BeginTransaction()
    {
        return std::make_shared<FakeTransaction>();
    }

    void DirectCommitTransaction(CharacterDatabaseTransaction const& /*trans*/)
    {
    }

    bool DirectExecute(char const* text)
    {
        std::string const sql(text ? text : "");
        if (sql.rfind("CREATE TABLE IF NOT EXISTS `tb_pool_account`", 0) == 0)
        {
            ++creates;
            fakePoolEvents.push_back("CREATE tb_pool_account");
            return true;
        }
        if (sql.rfind("CREATE TABLE IF NOT EXISTS `tb_pool_character`", 0) == 0)
        {
            ++creates;
            fakePoolEvents.push_back("CREATE tb_pool_character");
            return true;
        }
        if (sql.rfind("INSERT INTO `tb_pool_account`", 0) == 0)
        {
            fakePoolEvents.push_back("INSERT tb_pool_account " + FirstQuoted(sql));
            if (failAccountInserts)
                return false;
            return InsertAccount(sql);
        }
        if (sql.rfind("UPDATE `tb_pool_account`", 0) == 0)
        {
            fakePoolEvents.push_back("UPDATE tb_pool_account " + WhereValue(sql, "username"));
            if (failUpdateNumber && ++updates >= failUpdateNumber)
                return false;
            return UpdateAccount(sql);
        }
        if (sql.rfind("DELETE FROM `tb_pool_account`", 0) == 0)
        {
            fakePoolEvents.push_back("DELETE tb_pool_account " + WhereValue(sql, "username"));
            if (failDeletes)
                return false;
            std::string const username = WhereValue(sql, "username");
            accounts.erase(std::remove_if(accounts.begin(), accounts.end(), [&](AccountRow const& row) { return row.username == username; }), accounts.end());
            return true;
        }
        if (sql.rfind("INSERT INTO `tb_pool_character`", 0) == 0)
        {
            fakePoolEvents.push_back("INSERT tb_pool_character " + FirstQuoted(sql));
            if (failCharacterInserts)
                return false;
            return InsertCharacter(sql);
        }
        if (sql.rfind("UPDATE `tb_pool_character`", 0) == 0)
        {
            fakePoolEvents.push_back("UPDATE tb_pool_character " + WhereValue(sql, "name"));
            if (failCharacterUpdate || (failUpdateNumber && ++updates >= failUpdateNumber))
                return false;
            return UpdateCharacter(sql);
        }
        if (sql.rfind("DELETE FROM `tb_pool_character`", 0) == 0)
        {
            fakePoolEvents.push_back("DELETE tb_pool_character " + WhereValue(sql, "name"));
            if (failDeletes)
                return false;
            std::string const name = WhereValue(sql, "name");
            characters.erase(std::remove_if(characters.begin(), characters.end(), [&](CharacterRow const& row) { return row.name == name; }), characters.end());
            return true;
        }

        // --- the startup reset's writes ---
        if (sql.rfind("CREATE TABLE IF NOT EXISTS `tb_pool_state`", 0) == 0)
        {
            ++creates;
            fakePoolEvents.push_back("CREATE tb_pool_state");
            if (!HasExtraTable("tb_pool_state"))
                extraTables["tb_pool_state"] = { "pool_name", "applied_generation", "completed_at" };
            return true;
        }
        if (sql.rfind("INSERT INTO `tb_pool_state`", 0) == 0)
        {
            fakePoolEvents.push_back("INSERT tb_pool_state");
            if (!HasExtraTable("tb_pool_state"))
                return false;
            std::size_t const valuesAt = sql.find("VALUES (");
            if (valuesAt == std::string::npos)
                return false;
            std::string const values = sql.substr(valuesAt + std::string("VALUES (").size());
            std::vector<std::string> const parts = Split(values.substr(0, values.find(')')));
            if (parts.size() < 2)
                return false;
            std::string const pool = ParseValue(parts[0]).text;
            std::string const generation = ParseValue(parts[1]).text;
            auto const found = std::find_if(stateRows.begin(), stateRows.end(), [&](StateRow const& row) { return row.poolName == pool; });
            std::string const completed = "now";
            if (found == stateRows.end())
                stateRows.push_back(StateRow{ pool, generation, completed });
            else if (sql.find("`applied_generation` = VALUES") != std::string::npos)
                found->appliedGeneration = generation;
            else if (found->completedAt.empty())
                found->completedAt = completed;
            return true;
        }
        if (sql.rfind("DELETE FROM `tb_pool_lineage`", 0) == 0)
        {
            fakePoolEvents.push_back("DELETE tb_pool_lineage");
            if (!HasExtraTable("tb_pool_lineage"))
                return false;
            extraTables["tb_pool_lineage"] = extraTables["tb_pool_lineage"];
            return true;
        }
        if (sql.rfind("DELETE FROM `ai_playerbot_db_store`", 0) == 0)
        {
            uint64 const guid = uint64(std::strtoull(WhereValue(sql, "guid").c_str(), nullptr, 10));
            fakePoolEvents.push_back("DELETE ai_playerbot_db_store " + std::to_string(guid));
            moduleDbStoreGuids.erase(std::remove(moduleDbStoreGuids.begin(), moduleDbStoreGuids.end(), guid), moduleDbStoreGuids.end());
            return true;
        }
        if (sql.rfind("DELETE FROM `ai_playerbot_custom_strategy`", 0) == 0)
        {
            uint64 const owner = uint64(std::strtoull(WhereValue(sql, "owner").c_str(), nullptr, 10));
            fakePoolEvents.push_back("DELETE ai_playerbot_custom_strategy " + std::to_string(owner));
            moduleCustomStrategyOwners.erase(std::remove(moduleCustomStrategyOwners.begin(), moduleCustomStrategyOwners.end(), owner), moduleCustomStrategyOwners.end());
            return true;
        }
        if (sql.rfind("DELETE FROM `tb_commander_binding`", 0) == 0)
        {
            uint64 const guid = uint64(std::strtoull(WhereValue(sql, "commander_guid").c_str(), nullptr, 10));
            fakePoolEvents.push_back("DELETE tb_commander_binding " + std::to_string(guid));
            moduleCommanderGuids.erase(std::remove(moduleCommanderGuids.begin(), moduleCommanderGuids.end(), guid), moduleCommanderGuids.end());
            return true;
        }
        if (sql.rfind("DELETE FROM `tortoise_bots_guild_order`", 0) == 0)
        {
            uint64 const guid = uint64(std::strtoull(WhereValue(sql, "bot_guid").c_str(), nullptr, 10));
            fakePoolEvents.push_back("DELETE tortoise_bots_guild_order " + std::to_string(guid));
            moduleOrderBotGuids.erase(std::remove(moduleOrderBotGuids.begin(), moduleOrderBotGuids.end(), guid), moduleOrderBotGuids.end());
            return true;
        }
        if (sql.rfind("DELETE FROM `tortoise_bots_guild_disenchant`", 0) == 0)
        {
            uint64 const guid = uint64(std::strtoull(WhereValue(sql, "bot_guid").c_str(), nullptr, 10));
            fakePoolEvents.push_back("DELETE tortoise_bots_guild_disenchant " + std::to_string(guid));
            moduleDisenchantBotGuids.erase(std::remove(moduleDisenchantBotGuids.begin(), moduleDisenchantBotGuids.end(), guid), moduleDisenchantBotGuids.end());
            return true;
        }
        return false;
    }

private:
    struct Value
    {
        bool null = false;
        std::string text;
    };

    static std::string Trim(std::string text)
    {
        std::size_t const begin = text.find_first_not_of(" \t\n,");
        std::size_t const end = text.find_last_not_of(" \t\n");
        if (begin == std::string::npos || end == std::string::npos || end < begin)
            return "";
        return text.substr(begin, end - begin + 1);
    }

    static Value ParseValue(std::string const& raw)
    {
        Value value;
        std::string const text = Trim(raw);
        if (text == "NULL")
        {
            value.null = true;
            return value;
        }
        if (!text.empty() && text[0] == '\'')
        {
            for (std::size_t at = 1; at < text.size(); ++at)
            {
                if (text[at] == '\\' && at + 1 < text.size())
                {
                    value.text += text[++at];
                    continue;
                }
                if (text[at] == '\'')
                    break;
                value.text += text[at];
            }
            return value;
        }
        value.text = text;
        return value;
    }

    static std::vector<std::string> Split(std::string const& text)
    {
        std::vector<std::string> parts;
        std::size_t start = 0;
        for (std::size_t at = 0; at <= text.size(); ++at)
        {
            if (at == text.size() || text[at] == ',')
            {
                parts.push_back(Trim(text.substr(start, at - start)));
                start = at + 1;
            }
        }
        return parts;
    }

    // One SQL string literal at the start of `text` ('...' with backslash escapes): its value, and what follows the closing quote.
    // False when it is not a literal or never closes.
    static bool ParseLiteral(std::string const& text, std::string& value, std::string& rest)
    {
        value.clear();
        if (text.empty() || text[0] != '\'')
            return false;
        for (std::size_t at = 1; at < text.size(); ++at)
        {
            char const c = text[at];
            if (c == '\\' && at + 1 < text.size())
            {
                char const next = text[++at];
                value += next == 'n' ? '\n' : next == 'r' ? '\r' : next == '0' ? '\0' : next == 'Z' ? '\x1a' : next;
                continue;
            }
            if (c == '\'')
            {
                rest = text.substr(at + 1);
                return true;
            }
            value += c;
        }
        return false;
    }

    // The first quoted value of an INSERT: the registry's own row key is its first column.
    static std::string FirstQuoted(std::string const& sql)
    {
        std::size_t const at = sql.find('\'');
        if (at == std::string::npos)
            return "";
        std::size_t const close = sql.find('\'', at + 1);
        return sql.substr(at + 1, close == std::string::npos ? std::string::npos : close - at - 1);
    }

    // `... WHERE `column` = 'value'` (the registry's own writes); empty when the statement has no such clause.
    static std::string WhereValue(std::string const& sql, std::string const& column)
    {
        std::string const marker = "`" + column + "` = ";
        std::size_t const at = sql.rfind(marker);
        if (at == std::string::npos)
            return "";
        return ParseValue(sql.substr(at + marker.size())).text;
    }

    // The reset's one guild predicate, in both its COUNT and its detail form: a member is outside the pool when the
    // characters table has no row for it, its account is not in the statement's account list, or no ready pool row
    // names it. This mirrors the LEFT JOINs in TbPoolReset.cpp.
    QueryResult GuildOutsiderAnswer(std::string const& sql, bool countOnly)
    {
        if (failGuildReads)
            return nullptr;
        std::string const where = sql.substr(sql.find(" WHERE ") + std::string(" WHERE ").size());
        std::set<uint64> leaders;
        if (where.rfind("g.`leaderguid` IN (", 0) == 0)
        {
            std::string const list = where.substr(std::string("g.`leaderguid` IN (").size());
            for (std::string const& part : Split(list.substr(0, list.find(')'))))
                leaders.insert(uint64(std::strtoull(part.c_str(), nullptr, 10)));
        }
        else if (where.rfind("g.`leaderguid` = ", 0) == 0)
        {
            leaders.insert(uint64(std::strtoull(where.c_str() + std::string("g.`leaderguid` = ").size(), nullptr, 10)));
        }
        else
            return nullptr;

        std::set<uint32> accounts;
        std::size_t const accountsAt = where.find("c.`account` NOT IN (");
        if (accountsAt != std::string::npos)
        {
            std::string const list = where.substr(accountsAt + std::string("c.`account` NOT IN (").size());
            for (std::string const& part : Split(list.substr(0, list.find(')'))))
                accounts.insert(uint32(std::strtoul(part.c_str(), nullptr, 10)));
        }

        std::vector<std::vector<Field>> out;
        uint64 count = 0;
        for (GuildMemberRow const& member : guildMembers)
        {
            GuildRow const* guild = nullptr;
            for (GuildRow const& row : guilds)
                if (row.guildid == member.guildid)
                    guild = &row;
            if (!guild || !leaders.count(guild->leaderGuid))
                continue;
            CoreCharacterRow const* character = nullptr;
            for (CoreCharacterRow const& row : coreCharacters)
                if (row.guid == member.guid)
                    character = &row;
            bool readyPoolRow = false;
            for (CharacterRow const& row : characters)
                if (row.hasGuid && row.guid == member.guid && row.state == "ready")
                    readyPoolRow = true;
            bool const accountOutside = character && !accounts.empty() && !accounts.count(character->accountId);
            if (character && readyPoolRow && !accountOutside)
                continue;
            ++count;
            if (!countOnly)
            {
                if (!character)
                    out.push_back({ Num(member.guid), Null(), Null(), Text(guild->name), Num(guild->leaderGuid) });
                else
                    out.push_back({ Num(member.guid), Num(character->accountId), Text(character->name), Text(guild->name), Num(guild->leaderGuid) });
            }
        }
        if (countOnly)
            return MakeResult({ { Num(count) } });
        return MakeResult(std::move(out));
    }

    bool InsertAccount(std::string const& sql)
    {
        std::size_t const colsAt = sql.find('(', 10);
        std::size_t const colsEnd = sql.find(')', colsAt);
        std::size_t const valuesAt = sql.find("VALUES (", colsEnd);
        std::size_t const valuesEnd = sql.rfind(')');
        if (colsAt == std::string::npos || valuesAt == std::string::npos || valuesEnd == std::string::npos)
            return false;
        std::vector<std::string> const columns = Split(sql.substr(colsAt + 1, colsEnd - colsAt - 1));
        std::vector<std::string> const values = Split(sql.substr(valuesAt + 8, valuesEnd - (valuesAt + 8)));
        if (columns.size() != values.size())
            return false;
        AccountRow row;
        for (std::size_t i = 0; i < columns.size(); ++i)
        {
            std::string const column = Trim(columns[i]).substr(1, Trim(columns[i]).size() - 2);
            Value const value = ParseValue(values[i]);
            if (column == "username") row.username = value.text;
            else if (column == "account_id") { row.hasId = !value.null; row.id = uint32(std::strtoull(value.text.c_str(), nullptr, 10)); }
            else if (column == "stamp") row.stamp = value.text;
            else if (column == "state") row.state = value.text;
            else if (column == "created") row.created = int64(std::strtoll(value.text.c_str(), nullptr, 10));
        }
        auto const found = std::find_if(accounts.begin(), accounts.end(), [&](AccountRow const& other) { return other.username == row.username; });
        if (found != accounts.end())
            return false;
        accounts.push_back(row);
        return true;
    }

    bool InsertCharacter(std::string const& sql)
    {
        std::size_t const colsAt = sql.find('(', 10);
        std::size_t const colsEnd = sql.find(')', colsAt);
        std::size_t const valuesAt = sql.find("VALUES (", colsEnd);
        std::size_t const valuesEnd = sql.rfind(')');
        if (colsAt == std::string::npos || valuesAt == std::string::npos || valuesEnd == std::string::npos)
            return false;
        std::vector<std::string> const columns = Split(sql.substr(colsAt + 1, colsEnd - colsAt - 1));
        std::vector<std::string> const values = Split(sql.substr(valuesAt + 8, valuesEnd - (valuesAt + 8)));
        if (columns.size() != values.size())
            return false;
        CharacterRow row;
        for (std::size_t i = 0; i < columns.size(); ++i)
        {
            std::string const column = Trim(columns[i]).substr(1, Trim(columns[i]).size() - 2);
            Value const value = ParseValue(values[i]);
            if (column == "name") row.name = value.text;
            else if (column == "guid") { row.hasGuid = !value.null; row.guid = std::strtoull(value.text.c_str(), nullptr, 10); }
            else if (column == "account_id") row.accountId = uint32(std::strtoull(value.text.c_str(), nullptr, 10));
            else if (column == "race") row.race = uint8(std::strtoul(value.text.c_str(), nullptr, 10));
            else if (column == "class") row.klass = uint8(std::strtoul(value.text.c_str(), nullptr, 10));
            else if (column == "gender") row.gender = uint8(std::strtoul(value.text.c_str(), nullptr, 10));
            else if (column == "state") row.state = value.text;
            else if (column == "created") row.created = int64(std::strtoll(value.text.c_str(), nullptr, 10));
            else if (column == "last_login") row.lastLogin = int64(std::strtoll(value.text.c_str(), nullptr, 10));
            else if (column == "last_logout") row.lastLogout = int64(std::strtoll(value.text.c_str(), nullptr, 10));
        }
        auto const found = std::find_if(characters.begin(), characters.end(), [&](CharacterRow const& other) { return other.name == row.name; });
        if (found != characters.end())
            return false;
        characters.push_back(row);
        return true;
    }

    bool UpdateAccount(std::string const& sql)
    {
        std::size_t const setAt = sql.find(" SET ");
        std::size_t const whereAt = sql.find(" WHERE ");
        if (setAt == std::string::npos || whereAt == std::string::npos)
            return false;
        std::string const username = WhereValue(sql, "username");
        auto const found = std::find_if(accounts.begin(), accounts.end(), [&](AccountRow const& row) { return row.username == username; });
        if (found == accounts.end())
            return false;
        for (std::string const& assignment : Split(sql.substr(setAt + 5, whereAt - (setAt + 5))))
        {
            std::size_t const eq = assignment.find('=');
            if (eq == std::string::npos)
                continue;
            std::string const column = Trim(assignment.substr(0, eq));
            Value const value = ParseValue(assignment.substr(eq + 1));
            if (column == "`account_id`") { found->hasId = !value.null; found->id = uint32(std::strtoull(value.text.c_str(), nullptr, 10)); }
            else if (column == "`stamp`") found->stamp = value.text;
            else if (column == "`state`") found->state = value.text;
            else if (column == "`created`") found->created = int64(std::strtoll(value.text.c_str(), nullptr, 10));
        }
        return true;
    }

    bool UpdateCharacter(std::string const& sql)
    {
        std::size_t const setAt = sql.find(" SET ");
        std::size_t const whereAt = sql.find(" WHERE ");
        if (setAt == std::string::npos || whereAt == std::string::npos)
            return false;
        std::string const name = WhereValue(sql, "name");
        auto const found = std::find_if(characters.begin(), characters.end(), [&](CharacterRow const& row) { return row.name == name; });
        if (found == characters.end())
            return false;
        for (std::string const& assignment : Split(sql.substr(setAt + 5, whereAt - (setAt + 5))))
        {
            std::size_t const eq = assignment.find('=');
            if (eq == std::string::npos)
                continue;
            std::string const column = Trim(assignment.substr(0, eq));
            Value const value = ParseValue(assignment.substr(eq + 1));
            if (column == "`guid`") { found->hasGuid = !value.null; found->guid = std::strtoull(value.text.c_str(), nullptr, 10); }
            else if (column == "`state`") found->state = value.text;
            else if (column == "`race`") found->race = uint8(std::strtoul(value.text.c_str(), nullptr, 10));
            else if (column == "`class`") found->klass = uint8(std::strtoul(value.text.c_str(), nullptr, 10));
            else if (column == "`gender`") found->gender = uint8(std::strtoul(value.text.c_str(), nullptr, 10));
        }
        return true;
    }

    QueryResult SchemaAnswer(std::string const& sql)
    {
        std::string table;
        std::size_t const at = sql.find("TABLE_NAME = '");
        if (at != std::string::npos)
        {
            std::size_t const start = at + 14;
            table = sql.substr(start, sql.find('\'', start) - start);
        }
        std::set<std::string> columns;
        bool present = false;
        if (table == "tb_pool_account")
        {
            columns = accountColumns;
            present = haveAccountTable;
        }
        else if (table == "tb_pool_character")
        {
            columns = characterColumns;
            present = haveCharacterTable;
        }
        else
        {
            auto const found = extraTables.find(table);
            if (found != extraTables.end())
            {
                columns = found->second;
                present = true;
            }
        }
        if (!present)
            return MakeResult({ { Num(0) } });
        std::size_t const inAt = sql.find("COLUMN_NAME IN (");
        if (inAt == std::string::npos)
            return MakeResult({ { Num(1) } });
        std::size_t const close = sql.find(')', inAt);
        uint64 matched = 0;
        for (std::string const& raw : Split(sql.substr(inAt + 16, close - (inAt + 16))))
        {
            std::string const name = Trim(raw);
            if (name.size() >= 2 && name.front() == '\'' && name.back() == '\'')
            {
                if (columns.count(name.substr(1, name.size() - 2)))
                    ++matched;
            }
        }
        return MakeResult({ { Num(matched) } });
    }

    static std::shared_ptr<ResultSet> MakeResult(std::vector<std::vector<Field>> rows)
    {
        auto set = std::make_shared<ResultSet>(std::move(rows));
        if (!set->GetRowCount() || !set->NextRow())   // BasicStatementTask::Query's own three lines
            return nullptr;
        return set;
    }

    static Field Num(uint64 value)
    {
        Field field;
        field.SetUInt64(value);
        return field;
    }

    static Field Text(std::string value)
    {
        Field field;
        field.SetText(std::move(value));
        return field;
    }

    static Field Null()
    {
        Field field;
        field.SetNull();
        return field;
    }

    bool HasExtraTable(std::string const& table) const
    {
        return extraTables.count(table) != 0;
    }
};

inline FakePoolDatabase CharacterDatabase;
inline FakePoolDatabase LoginDatabase;

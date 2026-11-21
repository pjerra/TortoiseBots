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
#include <memory>
#include <set>
#include <string>
#include <vector>

inline std::vector<std::string> fakePoolEvents;

class Field
{
public:
    void SetUInt64(uint64 value)
    {
        _number = value;
        _text.clear();
    }

    void SetText(std::string value)
    {
        _text = std::move(value);
        _number = 0;
    }

    uint64 GetUInt64() const { return _number; }
    uint32 GetUInt32() const { return uint32(_number); }
    uint8 GetUInt8() const { return uint8(_number); }
    std::string GetString() const { return _text; }

private:
    uint64 _number = 0;
    std::string _text;
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
        std::set<std::string> const& columns = table == "tb_pool_account" ? accountColumns : characterColumns;
        bool const present = table == "tb_pool_account" ? haveAccountTable : table == "tb_pool_character" ? haveCharacterTable : false;
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
};

inline FakePoolDatabase CharacterDatabase;
inline FakePoolDatabase LoginDatabase;

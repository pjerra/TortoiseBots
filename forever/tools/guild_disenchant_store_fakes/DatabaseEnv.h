// The characters-database surface the disenchant store uses, with
// ForeverCore's row semantics: a QueryResult the store receives already
// STANDS ON ITS FIRST ROW (BasicStatementTask::Query calls NextRow; an empty
// answer is a null result), so a reader that advances before its first Fetch
// loses a row. One-way statements are void on the core; the store confirms
// them with the read-back this fake serves faithfully.
#pragma once

#include "Define.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

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
    std::string GetString() const { return _text; }

private:
    uint64 _number = 0;
    std::string _text;
};

class ResultSet
{
public:
    explicit ResultSet(std::vector<std::vector<Field>> rows) : _rows(std::move(rows)) {}

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

class FakeTransaction
{
public:
    void Append(char const*) { }
};

using CharacterDatabaseTransaction = std::shared_ptr<FakeTransaction>;

class FakeDatabase
{
public:
    struct Row
    {
        uint64 bot = 0;
        uint64 sender = 0;
        uint64 itemGuid = 0;
        uint64 itemEntry = 0;
        std::string itemName;
        uint64 pending = 0;
        std::string results;
    };

    std::vector<Row> rows;
    int columns = 8;           // what information_schema reports for the eight required columns
    bool failWrites = false;   // a REPLACE/DELETE that does not land
    int selects = 0;
    int writes = 0;

    QueryResult Query(char const* text)
    {
        ++selects;
        std::string const sql(text ? text : "");
        if (sql.find("information_schema.COLUMNS") != std::string::npos)
        {
            // The guard asks for the names it needs; the fake table holds
            // `columns` of them, so it reports how many of the asked names fit.
            std::size_t const in = sql.find("COLUMN_NAME IN (");
            uint64 requested = 1;
            if (in != std::string::npos)
            {
                requested = 0;
                for (std::size_t at = in; at < sql.size(); ++at)
                    if (sql[at] == '\'')
                        ++requested;
                requested /= 2;
            }
            return MakeResult({ { Num(std::min<uint64>(uint64(columns), requested)) } });
        }
        if (sql.rfind("SELECT `bot_guid`, `sender_guid`, `item_guid`, `item_entry`, `item_name`, `pending`, `results` "
                      "FROM `tortoise_bots_guild_disenchant`", 0) == 0)
        {
            uint64 filter = 0;
            bool const filtered = sql.find("WHERE `bot_guid` = ") != std::string::npos;
            if (filtered)
            {
                std::size_t at = sql.find("WHERE `bot_guid` = ") + 18;
                filter = Number(sql, at);
            }
            std::vector<std::vector<Field>> out;
            for (Row const& row : rows)
            {
                if (filtered && row.bot != filter)
                    continue;
                out.push_back({ Num(row.bot), Num(row.sender), Num(row.itemGuid), Num(row.itemEntry),
                    Text(row.itemName), Num(row.pending), Text(row.results) });
            }
            return MakeResult(std::move(out));
        }
        return nullptr;
    }

    void DirectExecute(char const* text)
    {
        ++writes;
        if (failWrites)
            return;
        std::string const sql(text ? text : "");
        if (sql.rfind("REPLACE INTO `tortoise_bots_guild_disenchant`", 0) == 0)
        {
            std::size_t at = sql.find("VALUES (");
            if (at == std::string::npos)
                return;
            at += 8;
            Row row;
            row.bot = Number(sql, at);
            row.sender = Number(sql, at);
            row.itemGuid = Number(sql, at);
            row.itemEntry = Number(sql, at);
            row.itemName = Quoted(sql, at);
            row.pending = Number(sql, at);
            row.results = Quoted(sql, at);
            auto const found = std::find_if(rows.begin(), rows.end(), [&](Row const& other)
            {
                return other.bot == row.bot && other.itemGuid == row.itemGuid;
            });
            if (found == rows.end())
                rows.push_back(row);
            else
                *found = row;
            return;
        }
        if (sql.rfind("DELETE FROM `tortoise_bots_guild_disenchant`", 0) == 0)
        {
            std::size_t const at = sql.find("WHERE `bot_guid` = ");
            if (at == std::string::npos)
                return;
            std::size_t pos = at + 19;
            uint64 const bot = Number(sql, pos);
            std::size_t const itemAt = sql.find("AND `item_guid` = ");
            if (itemAt == std::string::npos)
                return;
            pos = itemAt + 18;
            uint64 const itemGuid = Number(sql, pos);
            rows.erase(std::remove_if(rows.begin(), rows.end(), [&](Row const& row)
            {
                return row.bot == bot && row.itemGuid == itemGuid;
            }), rows.end());
        }
    }

    CharacterDatabaseTransaction BeginTransaction()
    {
        return std::make_shared<FakeTransaction>();
    }

    void DirectCommitTransaction(CharacterDatabaseTransaction const&) { }

    void EscapeString(std::string& value)
    {
        std::string escaped;
        escaped.reserve(value.size() + 8);
        for (char c : value)
        {
            if (c == '\'')
                escaped += "\\'";
            else
                escaped += c;
        }
        value = std::move(escaped);
    }

private:
    static std::shared_ptr<ResultSet> MakeResult(std::vector<std::vector<Field>> rows)
    {
        auto set = std::make_shared<ResultSet>(std::move(rows));
        if (set->NextRow())   // BasicStatementTask::Query's own line: the result stands on row one
            return set;
        return nullptr;
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

    static uint64 Number(std::string const& sql, std::size_t& at)
    {
        while (at < sql.size() && (sql[at] < '0' || sql[at] > '9'))
        {
            if (sql[at] == '\'')
            {
                for (++at; at < sql.size() && sql[at] != '\''; ++at)
                    if (sql[at] == '\\')
                        ++at;
            }
            ++at;
        }
        uint64 value = 0;
        while (at < sql.size() && sql[at] >= '0' && sql[at] <= '9')
            value = value * 10 + uint64(sql[at++] - '0');
        return value;
    }

    static std::string Quoted(std::string const& sql, std::size_t& at)
    {
        while (at < sql.size() && sql[at] != '\'')
            ++at;
        std::string value;
        for (++at; at < sql.size() && sql[at] != '\''; ++at)
        {
            if (sql[at] == '\\' && at + 1 < sql.size())
            {
                value += sql[++at];
                continue;
            }
            value += sql[at];
        }
        if (at < sql.size())
            ++at;
        return value;
    }
};

inline FakeDatabase CharacterDatabase;

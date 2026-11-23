// The characters-database surface the relist store uses, with ForeverCore's
// row semantics: a QueryResult the store receives already STANDS ON ITS FIRST
// ROW (BasicStatementTask::Query calls NextRow; an empty answer is a null
// result), so a reader that advances before its first Fetch loses a row. One-way
// statements are void on the core; the store confirms them with the read-back
// this fake serves faithfully.
#pragma once

#include "Define.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

class Field
{
public:
    void SetUInt64(uint64 value) { _number = value; _text.clear(); }
    void SetText(std::string value) { _text = std::move(value); _number = 0; }

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
        uint64 seq = 0;
        uint64 guild = 0;
        uint64 item = 0;
        uint64 count = 0;
        uint64 unitPrice = 0;
        uint64 bid = 0;
        uint64 buyout = 0;
        uint64 deposit = 0;
        uint64 postedAt = 0;
        uint64 expiresAt = 0;
        uint64 state = 0;
        uint64 claimMail = 0;
        uint64 heldGold = 0;
        uint64 heldCount = 0;
    };

    std::vector<Row> rows;
    int columns = 15;         // what information_schema reports for the fifteen required columns
    bool failWrites = false;  // a REPLACE/UPDATE/DELETE that does not land
    int selects = 0;
    int writes = 0;           // REPLACE/UPDATE/DELETE only
    int creates = 0;          // CREATE TABLE IF NOT EXISTS

    QueryResult Query(char const* text)
    {
        ++selects;
        std::string const sql(text ? text : "");
        if (sql.find("information_schema.COLUMNS") != std::string::npos)
        {
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
        if (sql.rfind("SELECT `bot_guid`, `seq`, `guild_id`, `item_id`, `count`, `unit_price`, `bid`, "
                      "`buyout`, `deposit`, `posted_at`, `expires_at`, `state`, `claim_mail`, "
                      "`held_gold`, `held_count` FROM `tortoise_bots_guild_relist`", 0) == 0)
        {
            bool const byBot = sql.find("WHERE `bot_guid` = ") != std::string::npos;
            bool const bySeq = sql.find("AND `seq` = ") != std::string::npos;
            uint64 const bot = byBot ? WhereNumber(sql, "`bot_guid` = ") : 0;
            uint64 const seq = bySeq ? WhereNumber(sql, "`seq` = ") : 0;
            std::vector<std::vector<Field>> out;
            for (Row const& row : rows)
            {
                if (byBot && row.bot != bot)
                    continue;
                if (bySeq && row.seq != seq)
                    continue;
                out.push_back({ Num(row.bot), Num(row.seq), Num(row.guild), Num(row.item), Num(row.count),
                    Num(row.unitPrice), Num(row.bid), Num(row.buyout), Num(row.deposit), Num(row.postedAt),
                    Num(row.expiresAt), Num(row.state), Num(row.claimMail), Num(row.heldGold),
                    Num(row.heldCount) });
            }
            return MakeResult(std::move(out));
        }
        return nullptr;
    }

    void DirectExecute(char const* text)
    {
        std::string const sql(text ? text : "");
        if (sql.rfind("CREATE TABLE IF NOT EXISTS `tortoise_bots_guild_relist`", 0) == 0)
        {
            ++creates;
            return;
        }
        ++writes;
        if (failWrites)
            return;
        if (sql.rfind("REPLACE INTO `tortoise_bots_guild_relist`", 0) == 0)
        {
            std::size_t at = sql.find("VALUES (");
            if (at == std::string::npos)
                return;
            at += 8;
            Row row;
            row.bot = Number(sql, at);
            row.seq = Number(sql, at);
            row.guild = Number(sql, at);
            row.item = Number(sql, at);
            row.count = Number(sql, at);
            row.unitPrice = Number(sql, at);
            row.bid = Number(sql, at);
            row.buyout = Number(sql, at);
            row.deposit = Number(sql, at);
            row.postedAt = Number(sql, at);
            row.expiresAt = Number(sql, at);
            row.state = Number(sql, at);
            row.claimMail = Number(sql, at);
            row.heldGold = Number(sql, at);
            row.heldCount = Number(sql, at);
            auto const found = std::find_if(rows.begin(), rows.end(), [&](Row const& other)
            {
                return other.bot == row.bot && other.seq == row.seq;
            });
            if (found == rows.end())
                rows.push_back(row);
            else
                *found = row;
            return;
        }
        if (sql.rfind("UPDATE `tortoise_bots_guild_relist`", 0) == 0)
        {
            uint64 const bot = WhereNumber(sql, "`bot_guid` = ");
            uint64 const seq = WhereNumber(sql, "`seq` = ");
            for (Row& row : rows)
            {
                if (row.bot != bot || row.seq != seq)
                    continue;
                row.state = SetNumber(sql, "`state` = ");
                row.claimMail = SetNumber(sql, "`claim_mail` = ");
                row.heldGold = SetNumber(sql, "`held_gold` = ");
                row.heldCount = SetNumber(sql, "`held_count` = ");
                return;
            }
            return;
        }
        if (sql.rfind("DELETE FROM `tortoise_bots_guild_relist`", 0) == 0)
        {
            uint64 const bot = WhereNumber(sql, "`bot_guid` = ");
            uint64 const seq = WhereNumber(sql, "`seq` = ");
            rows.erase(std::remove_if(rows.begin(), rows.end(), [&](Row const& row)
            {
                return row.bot == bot && row.seq == seq;
            }), rows.end());
        }
    }

    CharacterDatabaseTransaction BeginTransaction()
    {
        return std::make_shared<FakeTransaction>();
    }

    void DirectCommitTransaction(CharacterDatabaseTransaction const&) { }

    void EscapeString(std::string&) { }

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

    // The number after `key`, or 0 when the key is absent.
    static uint64 WhereNumber(std::string const& sql, std::string const& key)
    {
        std::size_t at = sql.find(key);
        if (at == std::string::npos)
            return 0;
        at += key.size();
        return Number(sql, at);
    }

    // The number after `key` in a SET list.
    static uint64 SetNumber(std::string const& sql, std::string const& key)
    {
        std::size_t at = sql.find(key);
        if (at == std::string::npos)
            return 0;
        at += key.size();
        return Number(sql, at);
    }

    static uint64 Number(std::string const& sql, std::size_t& at)
    {
        while (at < sql.size() && (sql[at] < '0' || sql[at] > '9'))
            ++at;
        uint64 value = 0;
        while (at < sql.size() && sql[at] >= '0' && sql[at] <= '9')
            value = value * 10 + uint64(sql[at++] - '0');
        return value;
    }
};

inline FakeDatabase CharacterDatabase;

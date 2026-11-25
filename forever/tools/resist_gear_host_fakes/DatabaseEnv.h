// The characters-database surface the hold store uses, with ForeverCore's row contract: a result set the caller
// receives already stands on its first row (BasicStatementTask::Query calls NextRow; an empty answer is a null
// result), and a one-way statement answers nothing. A transaction lands as a whole or not at all, so a refused
// commit is a write that really did not happen.
//
// Statements are the shapes TbResistGear.cpp builds (one table, its own literal): the information_schema probe, the
// hold's two SELECTs (with the hold's state column), the CREATE, the DELETE by guid (all slots or a slot list), the
// UPDATE that marks a hold releasing, and the VALUES-only INSERT with the state.
#pragma once

#include "Define.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

class Field
{
public:
    void SetUInt64(uint64 value) { _number = value; }
    uint64 GetUInt64() const { return _number; }
    uint32 GetUInt32() const { return uint32(_number); }
    uint8 GetUInt8() const { return uint8(_number); }

private:
    uint64 _number = 0;
};

class ResultSet
{
public:
    explicit ResultSet(std::vector<std::vector<Field>> rows) : _rows(std::move(rows)) {}

    // The core's contract: the set arrives standing on its first row; NextRow walks on and answers false at the end.
    bool NextRow()
    {
        if (_at + 1 >= _rows.size())
        {
            _at = _rows.size();
            return false;
        }
        ++_at;
        return true;
    }

    Field* Fetch() { return _at < _rows.size() ? _rows[_at].data() : nullptr; }

private:
    std::vector<std::vector<Field>> _rows;
    std::size_t _at = 0;
};

using QueryResult = std::shared_ptr<ResultSet>;

struct HoldRow
{
    uint64 guid = 0;
    uint8 slot = 0;
    uint8 school = 0;
    uint64 own = 0;
    uint64 resist = 0;
    bool releasing = false;
};

class FakeDatabase;

class FakeTransaction
{
public:
    explicit FakeTransaction(FakeDatabase* db) : _db(db) {}
    void Append(char const* sql) { _statements.emplace_back(sql ? sql : ""); }
    void Commit();

private:
    FakeDatabase* _db;
    std::vector<std::string> _statements;
};

using CharacterDatabaseTransaction = std::shared_ptr<FakeTransaction>;

class FakeDatabase
{
public:
    std::vector<HoldRow> rows;
    int availableColumns = 6;   // how many of the hold's columns information_schema reports (0 = no table, 5 = the old shape)
    bool failWrites = false;    // a statement or a transaction that is dropped instead of landing
    int selects = 0;            // every query
    int probes = 0;             // the information_schema existence/column probes
    int holdReads = 0;          // the hold table's own SELECTs
    int writes = 0;

    QueryResult Query(char const* text)
    {
        ++selects;
        std::string const sql(text ? text : "");
        if (sql.find("information_schema.COLUMNS") != std::string::npos)
        {
            ++probes;
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
            return MakeResult({ { Num(std::min<uint64>(uint64(availableColumns), requested)) } });
        }
        if (sql.rfind("SELECT `guid`, `slot`, `school`, `own_guid`, `resist_guid`, `releasing` FROM `tortoise_bots_resist_hold`", 0) == 0)
        {
            ++holdReads;
            std::vector<HoldRow> sorted = rows;
            std::sort(sorted.begin(), sorted.end(), [](HoldRow const& a, HoldRow const& b)
            {
                return a.guid != b.guid ? a.guid < b.guid : a.slot < b.slot;
            });
            std::vector<std::vector<Field>> out;
            for (HoldRow const& row : sorted)
                out.push_back({ Num(row.guid), Num(row.slot), Num(row.school), Num(row.own), Num(row.resist), Num(row.releasing ? 1 : 0) });
            return MakeResult(std::move(out));
        }
        if (sql.rfind("SELECT `slot`, `school`, `own_guid`, `resist_guid`, `releasing` FROM `tortoise_bots_resist_hold`", 0) == 0)
        {
            ++holdReads;
            uint64 const guid = FilterGuid(sql);
            std::vector<HoldRow> sorted;
            for (HoldRow const& row : rows)
                if (row.guid == guid)
                    sorted.push_back(row);
            std::sort(sorted.begin(), sorted.end(), [](HoldRow const& a, HoldRow const& b) { return a.slot < b.slot; });
            std::vector<std::vector<Field>> out;
            for (HoldRow const& row : sorted)
                out.push_back({ Num(row.slot), Num(row.school), Num(row.own), Num(row.resist), Num(row.releasing ? 1 : 0) });
            return MakeResult(std::move(out));
        }
        return nullptr;
    }

    void DirectExecute(char const* text)
    {
        ++writes;
        if (failWrites)
            return;
        Apply(std::string(text ? text : ""));
    }

    CharacterDatabaseTransaction BeginTransaction() { return std::make_shared<FakeTransaction>(this); }

    void DirectCommitTransaction(CharacterDatabaseTransaction const& transaction)
    {
        if (transaction)
            transaction->Commit();
    }

    // Applies one statement of the shapes the hold store builds.
    void Apply(std::string const& sql)
    {
        if (sql.rfind("CREATE TABLE", 0) == 0)
            return;
        if (sql.rfind("ALTER TABLE", 0) == 0)
            return;
        if (sql.rfind("DELETE FROM `tortoise_bots_resist_hold`", 0) == 0)
        {
            uint64 const guid = FilterGuid(sql);
            std::vector<uint8> only;
            std::size_t const in = sql.find("AND `slot` IN (");
            if (in != std::string::npos)
            {
                std::size_t at = in + 15;
                while (at < sql.size() && sql[at] != ')')
                {
                    if (sql[at] >= '0' && sql[at] <= '9')
                        only.push_back(uint8(Number(sql, at)));
                    else
                        ++at;
                }
            }
            rows.erase(std::remove_if(rows.begin(), rows.end(), [&](HoldRow const& row)
            {
                if (row.guid != guid)
                    return false;
                return only.empty() || std::find(only.begin(), only.end(), row.slot) != only.end();
            }), rows.end());
            return;
        }
        if (sql.rfind("UPDATE `tortoise_bots_resist_hold` SET `releasing` = 1", 0) == 0)
        {
            uint64 const guid = FilterGuid(sql);
            for (HoldRow& row : rows)
                if (row.guid == guid)
                    row.releasing = true;
            return;
        }
        if (sql.rfind("INSERT INTO `tortoise_bots_resist_hold`", 0) == 0)
        {
            std::size_t at = sql.find("VALUES ");
            if (at == std::string::npos)
                return;
            at += 7;
            while (at < sql.size())
            {
                while (at < sql.size() && sql[at] != '(')
                    ++at;
                if (at >= sql.size())
                    return;
                ++at;
                HoldRow row;
                row.guid = Number(sql, at);
                row.slot = uint8(Number(sql, at));
                row.school = uint8(Number(sql, at));
                row.own = Number(sql, at);
                row.resist = Number(sql, at);
                row.releasing = Number(sql, at) != 0;
                rows.erase(std::remove_if(rows.begin(), rows.end(), [&](HoldRow const& other)
                {
                    return other.guid == row.guid && other.slot == row.slot;
                }), rows.end());
                rows.push_back(row);
            }
        }
    }

    void CommitAll(std::vector<std::string> const& statements)
    {
        ++writes;
        if (failWrites)
            return;
        for (std::string const& sql : statements)
            Apply(sql);
    }

private:
    friend class FakeTransaction;

    static QueryResult MakeResult(std::vector<std::vector<Field>> rows)
    {
        if (rows.empty())
            return nullptr;
        return std::make_shared<ResultSet>(std::move(rows));
    }

    static Field Num(uint64 value)
    {
        Field field;
        field.SetUInt64(value);
        return field;
    }

    static uint64 FilterGuid(std::string const& sql)
    {
        std::size_t at = sql.find("WHERE `guid` = ");
        if (at == std::string::npos)
            return 0;
        at += 15;
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

inline void FakeTransaction::Commit()
{
    _db->CommitAll(_statements);
}

inline FakeDatabase CharacterDatabase;

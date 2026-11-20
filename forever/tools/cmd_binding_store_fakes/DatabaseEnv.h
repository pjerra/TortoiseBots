// The characters-database surface the binding store uses, with ForeverCore's row semantics (Database/QueryResult.h and AdhocStatement.cpp):
// DatabaseWorkerPool::Query runs BasicStatementTask::Query, which builds the result set, asks it for its first row and returns NULL when the set
// is empty - so a QueryResult the store receives already STANDS ON ITS FIRST ROW, and Fetch() before any NextRow is what reads it. The test's
// fake table reproduces that contract exactly; the counter that makes skip-the-first-row visible is the single-row case.
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
    explicit ResultSet(std::vector<std::vector<Field>> rows) : _rows(std::move(rows)), _current(1) {}

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

// One table's rows and the statements the store sent. Statements this fake does not understand count as a test bug: the store then reads a failed
// query, never an invented answer.
class FakeDatabase
{
public:
    struct Row
    {
        uint64 commander = 0;
        uint64 lead = 0;
        std::string members;
        uint32 flags = 0;
        int64 boundAt = 0;
    };

    std::vector<Row> rows;
    int columns = 5;            // what information_schema.COLUMNS reports for tb_commander_binding
    bool failInserts = false;   // an INSERT that does not land (a refused write)
    bool failDeletes = false;   // a DELETE that does not land
    int selects = 0;
    int inserts = 0;
    int deletes = 0;
    int creates = 0;

    QueryResult Query(char const* text)
    {
        ++selects;
        std::string const sql(text ? text : "");
        if (sql.find("information_schema.COLUMNS") != std::string::npos)
            return MakeResult({ { Num(uint64(columns)) } });
        if (sql.rfind("SELECT `commander_guid`, `lead_guid`, `members`, `flags`, `bound_at` FROM `tb_commander_binding`", 0) == 0)
        {
            std::vector<std::vector<Field>> out;
            for (Row const& row : rows)
                out.push_back({ Num(row.commander), Num(row.lead), Text(row.members), Num(row.flags), Num(uint64(row.boundAt)) });
            return MakeResult(std::move(out));
        }
        return nullptr;
    }

    bool DirectExecute(char const* text)
    {
        std::string const sql(text ? text : "");
        if (sql.rfind("CREATE TABLE", 0) == 0)
        {
            ++creates;
            return true;
        }
        if (sql.rfind("INSERT INTO `tb_commander_binding`", 0) == 0)
        {
            ++inserts;
            if (failInserts)
                return false;
            std::size_t at = sql.find("VALUES (");
            if (at == std::string::npos)
                return false;
            at += 8;
            Row row;
            row.commander = Number(sql, at);
            row.lead = Number(sql, at);
            row.members = Quoted(sql, at);
            row.flags = uint32(Number(sql, at));
            row.boundAt = int64(Number(sql, at));
            auto const found = std::find_if(rows.begin(), rows.end(), [&](Row const& other) { return other.commander == row.commander; });
            if (found == rows.end())
                rows.push_back(row);
            else
                *found = row;
            return true;
        }
        if (sql.rfind("DELETE FROM `tb_commander_binding`", 0) == 0)
        {
            ++deletes;
            if (failDeletes)
                return false;
            std::size_t const at = sql.find('=');
            if (at == std::string::npos)
                return false;
            std::size_t pos = at + 1;
            uint64 const commander = Number(sql, pos);
            rows.erase(std::remove_if(rows.begin(), rows.end(), [&](Row const& row) { return row.commander == commander; }), rows.end());
            return true;
        }
        return false;
    }

private:
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

    static uint64 Number(std::string const& sql, std::size_t& at)
    {
        while (at < sql.size() && (sql[at] < '0' || sql[at] > '9'))
            ++at;
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
            value += sql[at];
        return value;
    }
};

inline FakeDatabase CharacterDatabase;

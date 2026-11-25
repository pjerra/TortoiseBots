// The result set the add-on hint reads (ForeverCore's QueryResult is a shared_ptr<ResultSet>): it answers false
// when the query returned no rows, and ResultSet::Fetch/NextRow walk the rows the way the live half does.
#pragma once

#include "Define.h"

#include <cstddef>
#include <memory>
#include <vector>

class Field
{
public:
    Field() = default;
    explicit Field(uint64 value) : value_(value) {}

    uint64 GetUInt64() const { return value_; }

private:
    uint64 value_ = 0;
};

class ResultSet
{
public:
    ResultSet() = default;
    explicit ResultSet(std::vector<uint64> values) : values_(std::move(values))
    {
        if (!values_.empty())
            field_ = Field(values_.front());
    }

    Field const* Fetch() const { return &field_; }

    bool NextRow()
    {
        if (row_ + 1 >= values_.size())
            return false;
        field_ = Field(values_[++row_]);
        return true;
    }

private:
    std::vector<uint64> values_;
    std::size_t row_ = 0;
    Field field_;
};

class QueryResult
{
public:
    QueryResult() = default;
    explicit QueryResult(std::vector<uint64> values)
    {
        if (!values.empty())
            set_ = std::make_shared<ResultSet>(std::move(values));
    }

    explicit operator bool() const { return static_cast<bool>(set_); }

    ResultSet* operator->() const { return set_.get(); }

private:
    std::shared_ptr<ResultSet> set_;
};

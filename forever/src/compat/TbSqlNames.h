// The names a world-database query of the module names (std only: tools/test_forever_sql_guard.cpp compiles it on its own). Used by the schema guard
// `WorldQueryCompat` in TbCompat_S5.h.
#pragma once

#include <cstddef>
#include <set>
#include <string>

// The table and the backticked names a single-table `SELECT ... FROM `table` ...` query names, in its select list AND after the table (WHERE, ORDER BY):
// every one of them must exist or the core aborts. Quoted string literals ('...', backslash and doubled-quote escapes) are skipped, so a plan name
// containing a backtick cannot fake a column. False for any other form (do less).
inline bool WorldQueryNamesCompat(std::string const& text, std::string& table, std::set<std::string>& columns)
{
    std::size_t const from = text.find(" FROM `");
    if (from == std::string::npos || text.compare(0, 7, "SELECT ") != 0)
        return false;
    std::size_t const tableEnd = text.find('`', from + 7);
    if (tableEnd == std::string::npos)
        return false;
    table = text.substr(from + 7, tableEnd - (from + 7));
    columns.clear();
    for (std::size_t at = 7; at < text.size();)
    {
        if (at >= from && at < tableEnd + 1)
        {
            at = tableEnd + 1;
            continue;
        }
        char const c = text[at];
        if (c == '\'')
        {
            for (++at; at < text.size(); ++at)
            {
                if (text[at] == '\\')
                    ++at;
                else if (text[at] == '\'')
                {
                    if (at + 1 < text.size() && text[at + 1] == '\'')
                        ++at;
                    else
                        break;
                }
            }
            ++at;
        }
        else if (c == '`')
        {
            std::size_t const close = text.find('`', at + 1);
            if (close == std::string::npos)
                return false;
            columns.insert(text.substr(at + 1, close - at - 1));
            at = close + 1;
        }
        else
            ++at;
    }
    return !table.empty();
}

// Piece c-store (TBF #133): standalone test of the VALUES-only row/SQL builders
// (forever/src/slice/SliceDbStorePure.h). No core, no DB, no legacy headers.
//   g++ -std=c++17 -Wall -Wextra forever/tools/test_aistore_pure.cpp -o /tmp/test_aistore_pure && /tmp/test_aistore_pure
#include "../src/slice/SliceDbStorePure.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

namespace
{

using ai::slice::aistore::BuildSaveStatements;
using ai::slice::aistore::EncodeRow;
using ai::slice::aistore::ParseRow;
using ai::slice::aistore::SelectValuesSql;
using ai::slice::aistore::ValueKey;

// The production escaper doubles backslashes and quotes (what MySQL's EscapeString does to the
// characters that matter here); the builders must pass every interpolated literal through it exactly once.
void FakeEscape(std::string& text)
{
    std::string out;
    for (char c : text)
    {
        if (c == '\'' || c == '"' || c == '\\')
            out += '\\';
        out += c;
    }
    text = out;
}

void NoEscape(std::string&) {}

}

int main()
{
    // Row shape is TB's: "name>data".
    assert(EncodeRow("manual saved string::totem order earth", "strength of earth totem") ==
        "manual saved string::totem order earth>strength of earth totem");

    // Round trip, including an empty value (an order of "none" stores the empty string, like TB).
    std::string name, saved;
    assert(ParseRow("manual saved string::choice aura>devotion aura", name, saved));
    assert(name == "manual saved string::choice aura" && saved == "devotion aura");
    assert(ParseRow("manual saved string::totem order fire>", name, saved));
    assert(name == "manual saved string::totem order fire" && saved.empty());

    // TB's Load drops a row whose data holds '>' (it splits on '>' and skips >2 parts).
    assert(!ParseRow("manual saved string::k>a>b", name, saved));

    // A row without '>' is a name with an empty value, never a skip.
    assert(ParseRow("manual saved string::k", name, saved));
    assert(name == "manual saved string::k" && saved.empty());

    // Only the 'value' key is ever read or written.
    assert(std::string(ValueKey()) == "value");

    // SELECT names only ai_playerbot_db_store, numeric guid unquoted, preset escaped and quoted.
    std::string select = SelectValuesSql(1234, "a'b\\c", FakeEscape);
    assert(select == "SELECT `key`,`value` FROM `ai_playerbot_db_store` WHERE `guid` = 1234 AND `preset` = 'a\\'b\\\\c'");

    // SAVE clears exactly this bot+preset's value rows: strategy rows (co/nc/dead/react) are left alone for #135.
    std::vector<std::string> rows = { "manual saved string::totem order earth>strength of earth totem",
        "manual saved string::blessing order warrior>might" };
    auto statements = BuildSaveStatements(1234, "", rows, NoEscape);
    assert(statements.deleteSql ==
        "DELETE FROM `ai_playerbot_db_store` WHERE `guid` = 1234 AND `preset` = '' AND `key` = 'value'");
    assert(statements.insertSql.size() == 2);
    assert(statements.insertSql[0] ==
        "INSERT INTO `ai_playerbot_db_store` (`guid`, `preset`, `key`, `value`) VALUES (1234, '', 'value', "
        "'manual saved string::totem order earth>strength of earth totem')");
    assert(statements.insertSql[1].size() > 0 &&
        statements.insertSql[1].find("'manual saved string::blessing order warrior>might')") != std::string::npos);

    // INSERT order follows the context's Save() order.
    assert(statements.insertSql[0].find("totem order earth") != std::string::npos);

    // Every interpolated string is escaped exactly once: preset and row, never the fixed literals.
    auto escaped = BuildSaveStatements(7, "p'x", { "manual saved string::k>v'y" }, FakeEscape);
    assert(escaped.deleteSql.find("`preset` = 'p\\'x'") != std::string::npos);
    assert(escaped.insertSql.size() == 1);
    assert(escaped.insertSql[0].find("(7, 'p\\'x', 'value', 'manual saved string::k>v\\'y')") != std::string::npos);
    assert(escaped.insertSql[0].find("\\\\") == std::string::npos); // escaped once, not twice

    // A save with no saved values still clears stale rows (TB's Reset always runs first).
    auto empty = BuildSaveStatements(7, "", {}, NoEscape);
    assert(empty.insertSql.empty() && !empty.deleteSql.empty());

    std::cout << "aistore pure: all assertions passed" << std::endl;
    return 0;
}

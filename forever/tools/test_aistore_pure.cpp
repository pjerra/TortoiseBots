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

using ai::slice::aistore::BuildDeleteAllValuesSql;
using ai::slice::aistore::SkipPoolStrategies;
using ai::slice::aistore::BuildInvalidateStrategySnapshotsSql;
using ai::slice::aistore::BuildLoadPlan;
using ai::slice::aistore::BuildResetSql;
using ai::slice::aistore::BuildSaveAllStatements;
using ai::slice::aistore::BuildSaveStatements;
using ai::slice::aistore::BuildSelectDistinctPresetsSql;
using ai::slice::aistore::BuildSelectValueOnlySql;
using ai::slice::aistore::DropEmptyCustomAttach;
using ai::slice::aistore::EncodeRow;
using ai::slice::aistore::FormatStrategies;
using ai::slice::aistore::LoadPlan;
using ai::slice::aistore::ParseRow;
using ai::slice::aistore::SelectValuesSql;
using ai::slice::aistore::StrategyRow;
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

    // Strategy list formatting (TB PlayerbotDbStore::FormatStrategies).
    assert(FormatStrategies({}) == "");
    assert(FormatStrategies({ "arms" }) == "+arms");
    assert(FormatStrategies({ "arms", "dps assist", "close" }) == "+arms,+dps assist,+close");

    // Full save (values + co/nc/dead/react) deletes all keys for (guid, preset) and inserts values + strategies.
    std::vector<StrategyRow> strats = {
        { "co", "+arms,+dps assist" },
        { "nc", "+arms,+follow" },
        { "dead", "+dead" },
        { "react", "+arms" },
    };
    auto full = BuildSaveAllStatements(42, "p'1", { "manual saved string::k>v'1" }, strats, FakeEscape);
    assert(full.deleteSql == "DELETE FROM `ai_playerbot_db_store` WHERE `guid` = 42 AND `preset` = 'p\\'1'");
    assert(full.insertSql.size() == 5);
    assert(full.insertSql[0] ==
        "INSERT INTO `ai_playerbot_db_store` (`guid`, `preset`, `key`, `value`) VALUES (42, 'p\\'1', 'value', 'manual saved string::k>v\\'1')");
    assert(full.insertSql[1] ==
        "INSERT INTO `ai_playerbot_db_store` (`guid`, `preset`, `key`, `value`) VALUES (42, 'p\\'1', 'co', '+arms,+dps assist')");
    assert(full.insertSql[4] ==
        "INSERT INTO `ai_playerbot_db_store` (`guid`, `preset`, `key`, `value`) VALUES (42, 'p\\'1', 'react', '+arms')");

    // Strategy snapshot load plan: saved rows replace the kit-built engines verbatim, no baseline is added.
    // A removal the player saved (nc without +loot) must come back without loot: replay adds nothing.
    {
        std::vector<std::pair<std::string, std::string>> rows = {
            { "co", "+discipline,+dps assist" },
            { "nc", "+discipline,+dps assist,+follow" },
            { "dead", "+dead" },
            { "react", "+discipline" },
            { "value", "manual saved string::k>v" },
        };
        LoadPlan plan = BuildLoadPlan(rows);
        assert(plan.clearCombat && plan.clearNonCombat);
        assert(plan.ops.size() == 4);
        assert(plan.ops[0].key == "co" && plan.ops[0].value == "+discipline,+dps assist");
        assert(plan.ops[1].key == "nc" && plan.ops[1].value == "+discipline,+dps assist,+follow");
        assert(plan.ops[2].key == "dead" && plan.ops[2].value == "+dead");
        assert(plan.ops[3].key == "react" && plan.ops[3].value == "+discipline");
    }
    // No saved strategy rows: empty plan, the kit stands untouched.
    {
        std::vector<std::pair<std::string, std::string>> rows = { { "value", "manual saved string::k>v" } };
        LoadPlan plan = BuildLoadPlan(rows);
        assert(!plan.clearCombat && !plan.clearNonCombat && plan.ops.empty());
    }
    // Only one engine stored: only that engine is cleared and replayed.
    {
        std::vector<std::pair<std::string, std::string>> rows = { { "nc", "+discipline,+follow" } };
        LoadPlan plan = BuildLoadPlan(rows);
        assert(!plan.clearCombat && plan.clearNonCombat);
        assert(plan.ops.size() == 1 && plan.ops[0].key == "nc");
    }
    // Unknown keys are never replayed.
    {
        std::vector<std::pair<std::string, std::string>> rows = { { "bogus", "+x" } };
        LoadPlan plan = BuildLoadPlan(rows);
        assert(!plan.clearCombat && !plan.clearNonCombat && plan.ops.empty());
    }
    // An empty-qualifier custom attach is dropped from the replay: it would
    // store the engine entry under `custom::`, which nothing finds again.
    assert(DropEmptyCustomAttach("+mimic consumables,+custom::,+loot") == "+mimic consumables,+loot");
    assert(DropEmptyCustomAttach("+custom") == "");
    assert(DropEmptyCustomAttach("+custom::") == "");
    assert(DropEmptyCustomAttach("-custom::") == "");
    assert(DropEmptyCustomAttach("+custom::say") == "+custom::say");
    assert(DropEmptyCustomAttach("+custom::say,+loot") == "+custom::say,+loot");
    assert(DropEmptyCustomAttach("+loot") == "+loot");
    {
        std::vector<std::pair<std::string, std::string>> rows = { { "nc", "+custom::" } };
        LoadPlan plan = BuildLoadPlan(rows);
        assert(plan.clearNonCombat && plan.ops.empty());
    }

    // Pool bots without a real player relation keep no strategy snapshot (TB
    // random-bot rule); owned/commanded bots and non-pool bots use the store.
    assert(SkipPoolStrategies(true, false));
    assert(!SkipPoolStrategies(true, true));
    assert(!SkipPoolStrategies(false, false));
    assert(!SkipPoolStrategies(false, true));
    assert(BuildResetSql(42, "a'b", FakeEscape) ==
        "DELETE FROM `ai_playerbot_db_store` WHERE `guid` = 42 AND `preset` = 'a\\'b'");
    assert(BuildInvalidateStrategySnapshotsSql(42) ==
        "DELETE FROM `ai_playerbot_db_store` WHERE `guid` = 42 AND `key` IN ('co','nc','dead','react')");
    assert(BuildSelectValueOnlySql(42, "p'1", FakeEscape) ==
        "SELECT `value` FROM `ai_playerbot_db_store` WHERE `guid` = 42 AND `preset` = 'p\\'1' AND `key` = 'value'");
    assert(BuildDeleteAllValuesSql(42) ==
        "DELETE FROM `ai_playerbot_db_store` WHERE `guid` = 42 AND `key` = 'value'");
    assert(BuildSelectDistinctPresetsSql(42) ==
        "SELECT DISTINCT `preset` FROM `ai_playerbot_db_store` WHERE `guid` = 42");

    std::cout << "aistore pure: all assertions passed" << std::endl;
    return 0;
}

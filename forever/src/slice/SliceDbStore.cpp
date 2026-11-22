// Piece c-store (TBF #133): the VALUES half of PlayerbotDbStore on ForeverCore.
//
// TB persists every saved AI value plus the co/nc/dead/react strategy lists in characters.ai_playerbot_db_store
// (ai/playerbot/PlayerbotDbStore.cpp). The strategy lists are #135's (their load/replay/reset semantics, presets and
// `save/load/reset ai` verbs all belong there); this file makes Load/Save work for VALUES only, so the engine calls
// the pieces already make (`sPlayerbotDbStore.Save/Load`, e.g. EquipAction, ResetStrategies on brain creation) persist
// the qualified "manual saved string" choices and the other saved values, and nothing else.
//
// Guard: ForeverCore aborts on a statement naming a missing table or column, so every statement goes through
// compat/TbCompat_CharStore.h after an information_schema probe, and the save's DELETE+INSERTs run in one synchronous
// transaction (a crash between them must not leave half a choice set). When the table is missing the module attempts
// its own CREATE TABLE IF NOT EXISTS once (same belt-and-braces as the commander binding store); a database whose user
// may not create tables keeps working with no persistence and one warning.
#include "compat/botpch_forever.h"

#include "playerbot/playerbot.h"
#include "playerbot/MovementModePolicy.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotDbStore.h"
#include "playerbot/strategy/AiObjectContext.h"

#include "SliceDbStore.h"
#include "SliceDbStorePure.h"
#include "SliceLog.h"

#include "compat/TbCompat_CharStore.h"

#include "DatabaseEnv.h"

#include <list>
#include <set>
#include <string>
#include <vector>

namespace
{

std::set<std::string> const kColumns = { "guid", "preset", "key", "value" };

// The DDL of forever/sql/characters/2026_11_22_00_tb_aistore.sql, kept as text (forever/tools/test_aistore_wiring.py
// pins the two to the same columns). TB's schema (TortoiseBots/data/sql/character/20260824090001_char.sql:52-60),
// utf8mb4 like the module's other tables: values hold player names (blessing orders), which are UTF-8.
char const* const kCreate =
    "CREATE TABLE IF NOT EXISTS `ai_playerbot_db_store` ("
    "`id` bigint unsigned NOT NULL AUTO_INCREMENT,"
    "`guid` bigint unsigned NOT NULL,"
    "`preset` varchar(32) NOT NULL,"
    "`key` varchar(32) NOT NULL,"
    "`value` varchar(4000) DEFAULT NULL,"
    "PRIMARY KEY (`id`),"
    "KEY `guid` (`guid`)"
    ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";

void Escape(std::string& text) { CharacterDatabase.EscapeString(text); }

// The schema probe, attempting the module's own CREATE once (see above). A missing table or column disables
// persistence with one warning; the caller then does less, never the core's abort.
bool SchemaReady()
{
    static bool tableTried = false;
    static bool warned = false;
    if (CharacterSchemaHasCompat("ai_playerbot_db_store", kColumns))
        return true;
    if (!tableTried)
    {
        tableTried = true;
        CharacterExecuteGuardedCompat(kCreate);
        if (CharacterSchemaHasCompat("ai_playerbot_db_store", kColumns))
            return true;
    }
    if (!warned)
    {
        warned = true;
        ai::slice::LogWarn("TortoiseBots: `ai_playerbot_db_store` is missing or lacks a column: bot choices are not "
            "persisted. Apply forever/sql/characters/2026_11_22_00_tb_aistore.sql.");
    }
    return false;
}

}

namespace ai
{
namespace slice
{
namespace aistore
{

void LoadValues(PlayerbotAI* ai, uint32_t guid, std::string const& preset)
{
    if (!ai || !ai->GetAiObjectContext() || !SchemaReady())
        return;
    QueryResult result = CharacterQueryGuardedCompat(SelectValuesSql(guid, preset, Escape));
    if (!result)
        return;
    // The core's Query hands the result over standing on its first row, so that row is read before the loop advances.
    std::list<std::string> values;
    do
    {
        Field* fields = result->Fetch();
        if (fields[0].GetString() != ValueKey())
            continue;
        std::string row = fields[1].IsNull() ? std::string() : fields[1].GetString();
        std::string name, saved;
        // TB's Load looks the row's name up and skips a null value; the slice logs a missing-key line for an
        // unregistered name instead, so empty names are skipped here (an empty row stores nothing).
        if (!ParseRow(row, name, saved) || name.empty())
            continue;
        values.push_back(row);
    } while (result->NextRow());
    ai->GetAiObjectContext()->Load(values);
}

void SaveValues(PlayerbotAI* ai, uint32_t guid, std::string const& preset)
{
    if (!ai || !ai->GetAiObjectContext() || !SchemaReady())
        return;
    std::list<std::string> data = ai->GetAiObjectContext()->Save();
    SaveStatements statements = BuildSaveStatements(guid, preset, std::vector<std::string>(data.begin(), data.end()), Escape);
    std::vector<std::string> all;
    all.reserve(statements.insertSql.size() + 1);
    all.push_back(statements.deleteSql);
    all.insert(all.end(), statements.insertSql.begin(), statements.insertSql.end());
    CharacterTransactGuardedCompat(all);
}

void LoadAll(PlayerbotAI* ai, uint32_t guid, std::string const& preset)
{
    if (!ai || !ai->GetAiObjectContext())
        return;

    ai->EndPullWindow("reload");

    if (!SchemaReady())
    {
        ai->ReleasePullHold();
        return;
    }

    QueryResult result = CharacterQueryGuardedCompat(SelectValuesSql(guid, preset, Escape));
    if (result)
    {
        struct Row
        {
            std::string key;
            std::string value;
        };

        std::vector<Row> rows;
        ai::StoredStrategyRows stored;
        do
        {
            Field* fields = result->Fetch();
            std::string key = fields[0].IsNull() ? std::string() : fields[0].GetString();
            std::string val = fields[1].IsNull() ? std::string() : fields[1].GetString();
            stored.Note(key.c_str());
            rows.push_back({ std::move(key), std::move(val) });
        } while (result->NextRow());

        if (stored.combat)
        {
            ai->ClearStrategies(BotState::BOT_STATE_COMBAT);
            ai->ChangeStrategy("+chat", BotState::BOT_STATE_COMBAT);
        }
        if (stored.nonCombat)
        {
            ai->ClearStrategies(BotState::BOT_STATE_NON_COMBAT);
            ai->ChangeStrategy("+chat,+loot,+quest", BotState::BOT_STATE_NON_COMBAT);
        }

        std::list<std::string> values;
        for (Row const& row : rows)
        {
            if (row.key == ValueKey())
            {
                std::string name, saved;
                if (ParseRow(row.value, name, saved) && !name.empty())
                    values.push_back(row.value);
            }
            else if (row.key == "co")
                ai->ChangeStrategy(row.value, BotState::BOT_STATE_COMBAT);
            else if (row.key == "nc")
                ai->ChangeStrategy(row.value, BotState::BOT_STATE_NON_COMBAT);
            else if (row.key == "dead")
                ai->ChangeStrategy(row.value, BotState::BOT_STATE_DEAD);
            else if (row.key == "react")
                ai->ChangeStrategy(row.value, BotState::BOT_STATE_REACTION);
        }

        ai->GetAiObjectContext()->Load(values);
    }

    ai->ReleasePullHold();
}

void SaveAll(PlayerbotAI* ai, uint32_t guid, std::string const& preset)
{
    if (!ai || !ai->GetAiObjectContext() || !SchemaReady())
        return;

    std::list<std::string> data = ai->GetAiObjectContext()->Save();
    std::string const waitTimeRow = "wait for attack time>";
    std::vector<std::string> valueRows;
    valueRows.reserve(data.size());
    for (std::string& entry : data)
    {
        if (ai->HasPullWindow() && entry.compare(0, waitTimeRow.size(), waitTimeRow) == 0)
            entry = waitTimeRow + std::to_string(ai->PullWindowSavedSeconds());
        valueRows.push_back(std::move(entry));
    }

    auto collect = [ai](BotState state)
    {
        std::list<std::string_view> strategies = ai->GetStrategies(state);
        if (ai->PullWindowAdded(state))
            strategies.remove(std::string_view("wait for attack"));
        std::vector<std::string> out;
        out.reserve(strategies.size());
        for (std::string_view s : strategies)
            out.emplace_back(s);
        return FormatStrategies(out);
    };

    std::vector<StrategyRow> stratRows = {
        { "co", collect(BotState::BOT_STATE_COMBAT) },
        { "nc", collect(BotState::BOT_STATE_NON_COMBAT) },
        { "dead", collect(BotState::BOT_STATE_DEAD) },
        { "react", collect(BotState::BOT_STATE_REACTION) },
    };

    SaveStatements statements = BuildSaveAllStatements(guid, preset, valueRows, stratRows, Escape);
    std::vector<std::string> all;
    all.reserve(statements.insertSql.size() + 1);
    all.push_back(statements.deleteSql);
    all.insert(all.end(), statements.insertSql.begin(), statements.insertSql.end());
    CharacterTransactGuardedCompat(all);
}

void ResetAll(uint32_t guid, std::string const& preset)
{
    if (!SchemaReady())
        return;
    CharacterExecuteGuardedCompat(BuildResetSql(guid, preset, Escape));
}

void InvalidateStrategySnapshots(uint32_t guid)
{
    if (!SchemaReady())
        return;
    CharacterExecuteGuardedCompat(BuildInvalidateStrategySnapshotsSql(guid));
}

QueryResult QueryValuesOnly(uint32_t guid, std::string const& preset)
{
    if (!SchemaReady())
        return nullptr;
    return CharacterQueryGuardedCompat(BuildSelectValueOnlySql(guid, preset, Escape));
}

void DeleteAllValues(uint32_t guid)
{
    if (!SchemaReady())
        return;
    CharacterExecuteGuardedCompat(BuildDeleteAllValuesSql(guid));
}

QueryResult QueryDistinctPresets(uint32_t guid)
{
    if (!SchemaReady())
        return nullptr;
    return CharacterQueryGuardedCompat(BuildSelectDistinctPresetsSql(guid));
}

}
}
}

void PlayerbotDbStore::Load(PlayerbotAI* ai, std::string preset)
{
    if (!ai || !ai->GetBot())
        return;
    ai::slice::aistore::LoadAll(ai, ai->GetBot()->GetGUID().GetCounter(), preset);
}

void PlayerbotDbStore::Save(PlayerbotAI* ai, std::string preset)
{
    if (!ai || !ai->GetBot())
        return;
    ai::slice::aistore::SaveAll(ai, ai->GetBot()->GetGUID().GetCounter(), preset);
}

void PlayerbotDbStore::Reset(PlayerbotAI* ai, std::string preset)
{
    if (!ai || !ai->GetBot())
        return;
    ai::slice::aistore::ResetAll(ai->GetBot()->GetGUID().GetCounter(), preset);
}

void PlayerbotDbStore::InvalidateStrategySnapshots(PlayerbotAI* ai)
{
    if (!ai || !ai->GetBot())
        return;
    ai::slice::aistore::InvalidateStrategySnapshots(ai->GetBot()->GetGUID().GetCounter());
}

QueryResult DbStoreQueryValuesCompat(uint64 guid)
{
    return ai::slice::aistore::QueryValuesOnly(static_cast<uint32_t>(guid), std::string());
}

void DbStoreDeleteValuesCompat(uint64 guid)
{
    ai::slice::aistore::DeleteAllValues(static_cast<uint32_t>(guid));
}

QueryResult DbStoreQueryPresetsCompat(uint64 guid)
{
    return ai::slice::aistore::QueryDistinctPresets(static_cast<uint32_t>(guid));
}

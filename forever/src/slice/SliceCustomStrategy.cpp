// Piece P135-C (TBF #135): guarded `ai_playerbot_custom_strategy` loader, editor, and `CustomStrategyEditAction`.
#include "compat/botpch_forever.h"

#include "playerbot/playerbot.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/CustomStrategy.h"
#include "playerbot/strategy/actions/CustomStrategyEditAction.h"

#include "SliceCustomStrategy.h"
#include "SliceCustomStrategyPure.h"
#include "SliceLog.h"

#include "compat/TbCompat_CharStore.h"

#include "DatabaseEnv.h"

#include <cstdlib>
#include <list>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace
{

std::set<std::string> const kCustomColumns = { "name", "idx", "owner", "action_line" };

char const* const kCreateCustomTable =
    "CREATE TABLE IF NOT EXISTS `ai_playerbot_custom_strategy` ("
    "`id` bigint unsigned NOT NULL AUTO_INCREMENT,"
    "`name` varchar(255) NOT NULL,"
    "`idx` bigint DEFAULT NULL,"
    "`owner` bigint DEFAULT NULL,"
    "`action_line` varchar(1024) NOT NULL,"
    "PRIMARY KEY (`id`),"
    "KEY `name` (`name`)"
    ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";

void EscapeChar(std::string& text) { CharacterDatabase.EscapeString(text); }

bool CustomSchemaReady()
{
    static bool tableTried = false;
    static bool warned = false;
    if (CharacterSchemaHasCompat("ai_playerbot_custom_strategy", kCustomColumns))
        return true;
    if (!tableTried)
    {
        tableTried = true;
        CharacterExecuteGuardedCompat(kCreateCustomTable);
        if (CharacterSchemaHasCompat("ai_playerbot_custom_strategy", kCustomColumns))
            return true;
    }
    if (!warned)
    {
        warned = true;
        ai::slice::LogWarn("TortoiseBots: `ai_playerbot_custom_strategy` is missing or lacks a column: custom strategies "
            "are disabled. Apply forever/sql/characters/2026_11_22_01_tb_custom_strategy.sql.");
    }
    return false;
}

} // namespace

void LoadCustomStrategyLinesCompat(std::string const& qualifier, uint32 owner, std::list<std::string>& lines)
{
    if (!CustomSchemaReady())
        return;
    QueryResult results = CharacterQueryGuardedCompat(
        ai::slice::aipreset::SelectCustomLinesSql(qualifier, owner, EscapeChar));
    if (!results)
        return;
    do
    {
        Field* fields = results->Fetch();
        std::string line = fields[0].IsNull() ? std::string() : fields[0].GetString();
        if (!line.empty())
            lines.push_back(std::move(line));
    } while (results->NextRow());
}

namespace ai
{
namespace slice
{
namespace aipreset
{

std::vector<std::string> ListCustomStrategies(uint32_t owner)
{
    std::vector<std::string> names;
    if (!CustomSchemaReady())
        return names;
    QueryResult results = CharacterQueryGuardedCompat(SelectCustomNamesSql(owner));
    if (!results)
        return names;
    do
    {
        Field* fields = results->Fetch();
        std::string name = fields[0].IsNull() ? std::string() : fields[0].GetString();
        if (!name.empty())
            names.push_back(std::move(name));
    } while (results->NextRow());
    return names;
}

std::vector<std::pair<uint32_t, std::string>> ReadCustomStrategyLines(uint32_t owner, std::string const& name)
{
    std::vector<std::pair<uint32_t, std::string>> rows;
    if (name.empty() || name.size() > 255 || !CustomSchemaReady())
        return rows;
    QueryResult results = CharacterQueryGuardedCompat(SelectCustomIndexedLinesSql(name, owner, EscapeChar));
    if (!results)
        return rows;
    do
    {
        Field* fields = results->Fetch();
        uint32_t idx = fields[0].IsNull() ? 0 : fields[0].GetUInt32();
        std::string line = fields[1].IsNull() ? std::string() : fields[1].GetString();
        rows.emplace_back(idx, std::move(line));
    } while (results->NextRow());
    return rows;
}

bool EditCustomStrategyLine(PlayerbotAI* ai, uint32_t owner, std::string const& name, uint32_t idx, std::string const& command)
{
    if (name.empty() || name.size() > 255 || command.size() > 1024 || !CustomSchemaReady())
        return false;

    QueryResult existing = CharacterQueryGuardedCompat(SelectCustomLineExistsSql(name, owner, idx, EscapeChar));
    if (existing)
    {
        if (command.empty())
            CharacterExecuteGuardedCompat(DeleteCustomLineSql(name, owner, idx, EscapeChar));
        else
            CharacterExecuteGuardedCompat(UpdateCustomLineSql(name, owner, idx, command, EscapeChar));
    }
    else if (!command.empty())
    {
        CharacterExecuteGuardedCompat(InsertCustomLineSql(name, owner, idx, command, EscapeChar));
    }

    if (ai && ai->GetAiObjectContext())
    {
        Strategy* strategy = ai->GetAiObjectContext()->GetStrategy("custom::" + name);
        if (strategy)
        {
            if (CustomStrategy* cs = dynamic_cast<CustomStrategy*>(strategy))
            {
                cs->Reset();
                ai->ReInitCurrentEngine();
            }
        }
    }
    return true;
}

} // namespace aipreset
} // namespace slice
} // namespace ai

using namespace ai;

bool CustomStrategyEditAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    std::string text = event.GetParam();
    std::size_t pos = text.find(' ');
    if (pos == std::string::npos)
        return PrintHelp(requester);
    std::string name = text.substr(0, pos);
    text = text.substr(pos + 1);

    pos = text.find(' ');
    if (pos == std::string::npos)
        pos = text.size();
    std::string idx = text.substr(0, pos);
    text = pos >= text.size() ? "" : text.substr(pos + 1);

    return idx == "?" ? Print(name, requester) : Edit(name, static_cast<uint32>(std::atoi(idx.c_str())), text, requester);
}

bool CustomStrategyEditAction::PrintHelp(Player* requester)
{
    ai->TellPlayer(requester, "=== Custom strategies ===");
    uint32 const owner = static_cast<uint32>(ai->GetBot()->GetGUIDLow());
    for (std::string const& name : ai::slice::aipreset::ListCustomStrategies(owner))
        ai->TellPlayer(requester, name);
    ai->TellPlayer(requester, "Usage: cs <name> <idx> <command>");
    return false;
}

bool CustomStrategyEditAction::Print(std::string name, Player* requester)
{
    if (name.size() > 255)
        return false;
    std::ostringstream out;
    out << "=== " << name << " ===";
    ai->TellPlayer(requester, out.str());
    uint32 const owner = static_cast<uint32>(ai->GetBot()->GetGUIDLow());
    for (auto const& row : ai::slice::aipreset::ReadCustomStrategyLines(owner, name))
        PrintActionLine(row.first, row.second, requester);
    return true;
}

bool CustomStrategyEditAction::Edit(std::string name, uint32 idx, std::string command, Player* requester)
{
    uint32 const owner = static_cast<uint32>(ai->GetBot()->GetGUIDLow());
    if (!ai::slice::aipreset::EditCustomStrategyLine(ai, owner, name, idx, command))
        return false;
    PrintActionLine(idx, command, requester);
    return true;
}

bool CustomStrategyEditAction::PrintActionLine(uint32 idx, std::string command, Player* requester)
{
    std::ostringstream out;
    out << "#" << idx << " " << command;
    ai->TellPlayer(requester, out.str());
    return true;
}

// Piece c-store (TBF #133): VALUES-only persistence of ai_playerbot_db_store on ForeverCore.
//
// Free functions so the brain (forever/src/host/TbEngineBrain.cpp) can save by the guid counter it stored at
// creation: at teardown the Player may be mid-logout, so no Player is touched here, only the AI and its context.
// No legacy header is included here; PlayerbotAI is only forward-declared.
#pragma once

#include "DatabaseEnvFwd.h"

#include <cstdint>
#include <string>

class PlayerbotAI;

namespace ai
{
namespace slice
{
namespace aistore
{

// Reads this bot+preset's `value` rows into the context. Does less when the table or a column is missing.
void LoadValues(PlayerbotAI* ai, uint32_t guid, std::string const& preset);
// Writes the context's saved values over this bot+preset's `value` rows in one synchronous transaction (strategy
// rows are left alone, used at brain teardown). Does less when the table or a column is missing.
void SaveValues(PlayerbotAI* ai, uint32_t guid, std::string const& preset);

// Full strategy + value store operations (P135-B).
void LoadAll(PlayerbotAI* ai, uint32_t guid, std::string const& preset);
void SaveAll(PlayerbotAI* ai, uint32_t guid, std::string const& preset);
void ResetAll(uint32_t guid, std::string const& preset);
void InvalidateStrategySnapshots(uint32_t guid);

// Guarded helpers for ResetAiAction / ListAiAction.
QueryResult QueryValuesOnly(uint32_t guid, std::string const& preset);
void DeleteAllValues(uint32_t guid);
QueryResult QueryDistinctPresets(uint32_t guid);

}
}
}

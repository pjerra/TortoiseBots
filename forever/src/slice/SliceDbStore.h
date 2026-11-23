// Piece c-store (TBF #133): VALUES-only persistence of ai_playerbot_db_store on ForeverCore.
//
// Free functions so the brain (forever/src/host/TbEngineBrain.cpp) can save by the guid counter it stored at
// creation: at teardown the Player may be mid-logout, so no Player is touched here, only the AI and its context.
// No legacy header is included here; PlayerbotAI is only forward-declared.
#pragma once

#include <cstdint>
#include <string>

class PlayerbotAI;

namespace ai
{
namespace slice
{
namespace aistore
{

// Reads this bot+preset's `value` rows into the context (strategy rows are #135's and are ignored). Does less
// (no rows applied) when the table or a column is missing.
void LoadValues(PlayerbotAI* ai, uint32_t guid, std::string const& preset);
// Writes the context's saved values over this bot+preset's `value` rows in one synchronous transaction (strategy
// rows are left alone, #135). Does less (nothing written) when the table or a column is missing.
void SaveValues(PlayerbotAI* ai, uint32_t guid, std::string const& preset);

}
}
}

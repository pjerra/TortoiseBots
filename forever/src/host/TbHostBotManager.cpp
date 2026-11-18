// Host service R6B-1: the legacy bot manager's ownership questions, answered from the ForeverCore host's own bot table
// (TortoiseBots::BotMgr, forever/src/TbBotMgr.h). Built only with TORTOISEBOTS_SLICE1 (CMakeLists.txt).
//
// The legacy engine asks `TortoiseBots::BotManager` (runtime/BotManager.h) who is a module-owned bot, `AltBotService` whether a
// bot plays on its own, and `RandomBotFacade` whether it belongs to the random-bot pool. On ForeverCore every bot the engine runs
// is a host bot: a BotMgr entry that exists from `BotMgr::Login` on, with a master (or none), never a pool bot, never an
// autonomous alt. The functions below keep the legacy names and signatures and answer exactly that; every answer is listed in
// forever/docs/SLICE1-R6B-NOTES.md (section 3, choice c). Nothing here keeps state of its own.
#include "compat/botpch_forever.h"

#include "TbBotMgr.h"

#include "ObjectAccessor.h"
#include "Player.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/RandomBotFacade.h"
#include "PlayerbotAIStorage.h"
#include "runtime/AltBotService.h"
#include "runtime/BotManager.h"

namespace TortoiseBots
{
// The legacy singleton is never filled: its records stay empty because the host's table is the registry (see above). It exists so
// the legacy call sites (`BotManager::Instance().IsBot(...)`) have an object to call.
BotManager& BotManager::Instance()
{
    static BotManager instance;
    return instance;
}

// A host bot: an entry of BotMgr, in the table from Login on, before its Player is in the world (findings items 1 and 10: this is
// what makes `PlayerbotAI::IsOwnedBot` true before `new PlayerbotAI`).
bool BotManager::IsBot(ObjectGuid guid) const
{
    return BotMgr::Instance().IsBot(guid);
}

// Host bots are never pool bots (the random-bot pool is not ported).
bool BotManager::IsRandomBot(ObjectGuid /*guid*/) const
{
    return false;
}

// There is no BotRecord on ForeverCore: the host's entry has no legacy record. Null = "no record"; the callers read it as an
// unowned or pre-ownership bot (PlayerbotAI.cpp IsBotOwnerOrGm falls through to the account check).
BotRecord* BotManager::FindBot(ObjectGuid /*guid*/)
{
    return nullptr;
}

bool BotManager::GetOwnedCharacter(ObjectGuid /*characterGuid*/, OwnedCharacter& /*result*/)
{
    return false;     // no durable ownership table on this host
}

std::vector<Player*> BotManager::GetBotsForMaster(ObjectGuid masterGuid) const
{
    return BotMgr::Instance().BotsOfMaster(masterGuid);
}

// The master binding of a host bot is BotMgr's master guid plus the engine's own master pointer (what the Turtle manager's
// BindBotMaster does to the BotRecord and to the live PlayerbotAI).
bool BotManager::BindBotMaster(ObjectGuid botGuid, ObjectGuid masterGuid)
{
    if (!BotMgr::Instance().SetMaster(botGuid, masterGuid))
        return false;
    Player* bot = BotMgr::Instance().FindBot(botGuid);
    Player* master = ObjectAccessor::FindPlayer(masterGuid);
    if (bot && master)
        if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot))
            ai->SetMaster(master);
    return true;
}

bool BotManager::ClearBotMaster(ObjectGuid botGuid)
{
    if (!BotMgr::Instance().SetMaster(botGuid, ObjectGuid::Empty))
        return false;
    if (Player* bot = BotMgr::Instance().FindBot(botGuid))
        if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot))
            ai->SetMaster(nullptr);
    return true;
}

// The legacy call (PlayerbotAI.cpp, idle logout while the master rests) asks for a save and a removal: the host logs the bot out
// (its session saves the character the way a client logout does).
bool BotManager::RemoveBot(ObjectGuid guid, bool /*save*/)
{
    return BotMgr::Instance().Logout(guid, "engine asked for a logout");
}

AltBotService& AltBotService::Instance()
{
    static AltBotService instance;
    return instance;
}

// No host bot is an autonomous alt (the alt-bot service is not ported): the owner-bound master rules stay on.
bool AltBotService::IsAutonomous(uint32_t /*guidLow*/) const
{
    return false;
}
}

// RandomBotFacade (ai/playerbot/RandomBotFacade.h): the pool questions. A host bot is not a random bot and not a free alt.
bool RandomBotFacade::IsRandomBot(Player* /*bot*/)
{
    return false;
}

bool RandomBotFacade::IsRandomBot(uint32 /*guid*/)
{
    return false;
}

bool RandomBotFacade::IsFreeBot(Player* /*bot*/)
{
    return false;
}

bool RandomBotFacade::IsFreeBot(uint32 /*guid*/)
{
    return false;
}

// RandomBotFacade::GetValue reads a per-bot value row of the random-bot tables (for example "always online"): there are none.
uint32 RandomBotFacade::GetValue(uint32 /*guid*/, std::string /*type*/)
{
    return 0;
}

uint32 RandomBotFacade::GetValue(Player* /*bot*/, std::string /*type*/)
{
    return 0;
}

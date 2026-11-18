// Slice 1: the AiFactory of the slice build (FOREVER-SLICE1-SPEC section 4), compiled INSTEAD of ai/playerbot/AiFactory.cpp.
//
// ai/playerbot/AiFactory.cpp is 2,200 lines of per-class kits, config strings, hire / commander / world-PvP rules and
// the context augmenters of other modules. The slice has one warrior that follows its master, takes the master's target
// and fights with the under-30 arms kit, so this file defines only the members the slice's sources call (found with
// `grep AiFactory::` over forever/SLICE1.txt: PlayerbotAI.cpp, ChatHelper.cpp):
//
//   createAiObjectContext, createCombatEngine, createNonCombatEngine, createDeadEngine, createReactionEngine,
//   AddDefaultCombatStrategies, AddDefaultNonCombatStrategies, AddDefaultDeadStrategies, AddDefaultReactionStrategies,
//   GetPlayerSpecTab, GetPlayerSpecTabs, GetPlayerRoles(const Player*)
//
// Everything else the header declares stays undefined: a reference to it fails at link time, which is the point (R6).
//
// Defaults, from the spec: combat "arms", "dps assist", "close", "buff"; non-combat "arms", "dps assist", "follow";
// dead and reaction engines get no strategy; the talent tab is 0 (arms) whatever the bot's talents. A strategy name the
// context does not know is skipped by the engine, and logged once by the guard (SliceMissingKeyGuard.h).
#include "compat/botpch_forever.h"
#include "playerbot/playerbot.h"
#include "playerbot/AiFactory.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/ReactionEngine.h"
#include "playerbot/strategy/warrior/WarriorAiObjectContext.h"
#include "SliceMissingKeyGuard.h"

AiObjectContext* AiFactory::createAiObjectContext(Player* player, PlayerbotAI* ai)
{
    // The slice registers the warrior's strategies only (SliceAiObjectContext.cpp + WarriorAiObjectContext), whatever
    // the class: another class would run on the warrior's kit, so say so once.
    static bool warned = false;
    if (player && player->GetClass() != CLASS_WARRIOR && !warned)
    {
        warned = true;
        TC_LOG_WARN("tortoisebots", "slice: {} is not a warrior; the slice build gives every bot the warrior context",
            player->GetName());
    }
    return new slice::GuardedContext<WarriorAiObjectContext>(ai);
}

int AiFactory::GetPlayerSpecTab(const Player* /*player*/)
{
    return 0; // arms
}

std::map<uint32, int32> AiFactory::GetPlayerSpecTabs(const Player* /*player*/)
{
    std::map<uint32, int32> tabs;
    for (uint32 i = 0; i < uint32(3); i++)
        tabs[i] = 0;
    return tabs;
}

BotRoles AiFactory::GetPlayerRoles(const Player* player)
{
    // Spec tab 0 of a warrior is a damage role (AiFactory.cpp GetPlayerRoles(cls, tab): tab 2 tanks, the others dps).
    if (player && player->GetClass() == CLASS_WARRIOR)
        return BOT_ROLE_DPS;
    return BOT_ROLE_NONE;
}

void AiFactory::AddDefaultCombatStrategies(Player* /*player*/, PlayerbotAI* const /*facade*/, Engine* combatEngine)
{
    combatEngine->addStrategies("arms", "dps assist", "close", "buff", NULL);
}

void AiFactory::AddDefaultNonCombatStrategies(Player* /*player*/, PlayerbotAI* const /*facade*/, Engine* nonCombatEngine)
{
    nonCombatEngine->addStrategies("arms", "dps assist", "follow", NULL);
}

void AiFactory::AddDefaultDeadStrategies(Player* /*player*/, PlayerbotAI* const /*facade*/, Engine* /*deadEngine*/)
{
    // Death and resurrection are out of the slice (spec section 1).
}

void AiFactory::AddDefaultReactionStrategies(Player* /*player*/, PlayerbotAI* const /*facade*/, ReactionEngine* /*reactionEngine*/)
{
    // Reactions (AoE avoidance, potions, chat) are out of the slice.
}

Engine* AiFactory::createCombatEngine(Player* player, PlayerbotAI* const facade, AiObjectContext* AiObjectContext)
{
    Engine* engine = new Engine(facade, AiObjectContext, BotState::BOT_STATE_COMBAT);
    facade->SetEngine(BotState::BOT_STATE_COMBAT, engine);
    AddDefaultCombatStrategies(player, facade, engine);
    return engine;
}

Engine* AiFactory::createNonCombatEngine(Player* player, PlayerbotAI* const facade, AiObjectContext* AiObjectContext)
{
    Engine* nonCombatEngine = new Engine(facade, AiObjectContext, BotState::BOT_STATE_NON_COMBAT);
    facade->SetEngine(BotState::BOT_STATE_NON_COMBAT, nonCombatEngine);
    AddDefaultNonCombatStrategies(player, facade, nonCombatEngine);
    return nonCombatEngine;
}

Engine* AiFactory::createDeadEngine(Player* player, PlayerbotAI* const facade, AiObjectContext* AiObjectContext)
{
    Engine* deadEngine = new Engine(facade, AiObjectContext, BotState::BOT_STATE_DEAD);
    facade->SetEngine(BotState::BOT_STATE_DEAD, deadEngine);
    AddDefaultDeadStrategies(player, facade, deadEngine);
    return deadEngine;
}

ReactionEngine* AiFactory::createReactionEngine(Player* player, PlayerbotAI* const facade, AiObjectContext* AiObjectContext)
{
    ReactionEngine* reactionEngine = new ReactionEngine(facade, AiObjectContext, BotState::BOT_STATE_REACTION);
    facade->SetEngine(BotState::BOT_STATE_REACTION, reactionEngine);
    AddDefaultReactionStrategies(player, facade, reactionEngine);
    return reactionEngine;
}

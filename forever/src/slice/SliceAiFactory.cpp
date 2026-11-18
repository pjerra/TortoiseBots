// Slice 1 (slice 2 additions: the priest, R3): the AiFactory of the slice build (FOREVER-SLICE1-SPEC section 4), compiled INSTEAD of ai/playerbot/AiFactory.cpp.
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
// dead and reaction engines get no strategy; the talent tab is 0 (arms) whatever the bot's talents.
// Slice 2 (FOREVER-SLICE2-SPEC section 3): a priest gets its own context (PriestAiObjectContext, compiled as it is) and, on tab 0
// (discipline: the slice never reads talents), combat "discipline", "offdps", "dps assist", "ranged", "buff", "cast time"; non-combat
// "discipline", "dps assist", "follow", "buff", "food", "loot", "default"; reaction "discipline". The full factory's "flee", "cure",
// "cc", "aoe" and "boost" are left out on purpose (the spec's list). The warrior's non-combat set gains "loot" and "default" (the
// money looting and its "loot response" event; live test observation k has both bots open their corpse), its combat set is unchanged. A strategy name the
// context does not know is skipped by the engine, and logged once by the guard (SliceMissingKeyGuard.h).
// Slice 3 (FOREVER-SLICE3-SPEC section 3, lead decisions D1/D2): the warrior has a ROLE, read from the forced role (`.tb role` sets the player-wide
// stored role and the bot's own; PlayerbotAI::ResetStrategies re-runs this file afterwards). tank (AiFactory.cpp:727, :1445, without "pull back"):
// combat "protection", "tank assist", "pull", "close", "tank face"; non-combat "protection", "tank assist", "follow", "food", "default". Any other
// warrior (dps): the arms kit as before plus "threat" (where KeepsPartyThreatBackoff says so) and non-combat "food". The priest gets "threat" the same
// way. Left out on purpose (spec): "pull back", "behind", "aoe", "cc", "boost", "should pull" (autonomous pulls), "chat".
#include "compat/botpch_forever.h"
#include "playerbot/playerbot.h"
#include "playerbot/AiFactory.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/ReactionEngine.h"
#include "playerbot/strategy/warrior/WarriorAiObjectContext.h"
#include "playerbot/strategy/priest/PriestAiObjectContext.h"
#include "../../../runtime/PlayerbotAIStorage.h"
#include "../../../runtime/CommandedGroupQuery.h"
#include "../../../runtime/RemoteCommanderPolicy.h"
#include "SliceLog.h"
#include "SliceMissingKeyGuard.h"

AiObjectContext* AiFactory::createAiObjectContext(Player* player, PlayerbotAI* ai)
{
    // The slice registers the warrior's and the priest's class strategies (SliceAiObjectContext.cpp + the class context compiled as it
    // is); another class would run on the warrior's kit, so say so once.
    if (player && player->GetClass() == CLASS_PRIEST)
        return new slice::GuardedContext<PriestAiObjectContext>(ai);

    static bool warned = false;
    if (player && player->GetClass() != CLASS_WARRIOR && !warned)
    {
        warned = true;
        slice::LogWarn("slice: " + player->GetName() + " is neither a warrior nor a priest; the slice build gives every other bot the warrior context");
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

namespace
{
// The role the bot was ordered to play (`.tb role`): the bot's own forced role, else the player-wide stored one (PlayerbotAI::IsTank reads the
// stored one first, PlayerbotAI.cpp:4339). 0 = none. PlayerbotAI::SetForcedRole stores 1 = tank, 2 = healer, anything else = damage.
uint8 OrderedRole(Player const* player, PlayerbotAI* const facade)
{
    if (!player)
        return 0;
    uint8 const stored = PlayerbotAIStorage::Instance().GetPlayerForcedRole(player->GetObjectGuid());
    if (stored != static_cast<uint8>(BOT_ROLE_NONE))
        return stored;
    uint8 const own = facade ? facade->GetForcedRole() : 0;       // 1 = tank, 2 = healer, anything else = damage (PlayerbotAI.cpp:4263)
    return own == 0 ? 0 : (own == 1 ? static_cast<uint8>(BOT_ROLE_TANK) : (own == 2 ? static_cast<uint8>(BOT_ROLE_HEALER) : static_cast<uint8>(BOT_ROLE_DPS)));
}

bool WarriorIsTank(Player const* player, PlayerbotAI* const facade)
{
    return player && player->GetClass() == CLASS_WARRIOR && OrderedRole(player, facade) == static_cast<uint8>(BOT_ROLE_TANK);
}
}

// The full factory's text (AiFactory.cpp:633-642), compiled here because AiFactory.cpp is not in the slice: non-tank bots of a party with a real
// player master or of a commanded party back off the tank's threat. The full factory also calls BotCommands::NoteCommanderThreat (the commander's
// "this strategy came from the binding" mark, undone when the commander unbinds); the slice has no unbind, so the mark is not kept (NEEDS FROM
// host: none; SLICE3-P-NOTES.md section 6).
bool AiFactory::KeepsPartyThreatBackoff(Player* player, PlayerbotAI* const facade)
{
    if (!player || !facade || player->InBattleGround())
        return false;
    bool const isTank = PlayerbotAI::IsTank(player);
    if (facade->HasRealPlayerMaster())
        return !isTank;
    bool const commanded = TortoiseBots::BotCommands::IsInCommandedGroup(player);
    ai::slice::LogInfo(std::string("slice: threat back-off for ") + player->GetName() + ": commanded=" + (commanded ? "yes" : "no") + " tank=" + (isTank ? "yes" : "no"));
    return TortoiseBots::RemoteCommander::KeepsCommandedThreatBackoff(commanded, isTank);
}

BotRoles AiFactory::GetPlayerRoles(const Player* player)
{
    // The role the bot was ordered to play comes first (spec section 3: "the forced role first"), as PlayerbotAI::IsTank reads it.
    if (player)
    {
        uint8 ordered = PlayerbotAIStorage::Instance().GetPlayerForcedRole(player->GetObjectGuid());
        // The bot's own (AI-level) forced role too: the full GetPlayerRoles(player) ignores it (AiFactory.cpp:586-592), but a host that sets only
        // `SetForcedRole` must still be heard. The AI is not stored yet while it is being constructed: then there is none to read.
        if (ordered == static_cast<uint8>(BOT_ROLE_NONE))
            if (PlayerbotAI* botAi = PlayerbotAIStorage::Instance().GetAI(player->GetObjectGuid()))
                ordered = botAi->GetForcedRole() == 1 ? static_cast<uint8>(BOT_ROLE_TANK) :
                          botAi->GetForcedRole() == 2 ? static_cast<uint8>(BOT_ROLE_HEALER) :
                          botAi->GetForcedRole() ? static_cast<uint8>(BOT_ROLE_DPS) : static_cast<uint8>(BOT_ROLE_NONE);
        if (ordered != static_cast<uint8>(BOT_ROLE_NONE))
            return static_cast<BotRoles>(ordered);
    }
    // Spec tab 0 of a warrior is a damage role (AiFactory.cpp GetPlayerRoles(cls, tab): tab 2 tanks, the others dps).
    if (player && player->GetClass() == CLASS_WARRIOR)
        return BOT_ROLE_DPS;
    // A priest on tab 0 or 1 (discipline, holy: the only tabs the slice gives it) heals (AiFactory.cpp GetPlayerRoles(cls, tab): tab 2 is dps).
    if (player && player->GetClass() == CLASS_PRIEST)
        return BOT_ROLE_HEALER;
    return BOT_ROLE_NONE;
}

void AiFactory::AddDefaultCombatStrategies(Player* player, PlayerbotAI* const facade, Engine* combatEngine)
{
    if (player && player->GetClass() == CLASS_PRIEST)
        combatEngine->addStrategies("discipline", "offdps", "dps assist", "ranged", "buff", "cast time", NULL);
    else if (WarriorIsTank(player, facade))
    {
        // AiFactory.cpp:727 without "pull back" (the return leg of a pull is out of the slice); "close" is the melee approach the damage kit has too.
        combatEngine->addStrategies("protection", "tank assist", "pull", "close", "tank face", NULL);
        return;                                     // a tank never backs off the threat it holds (KeepsPartyThreatBackoff is false for a tank)
    }
    else
        combatEngine->addStrategies("arms", "dps assist", "close", "buff", NULL);

    // Applied after the class kit, as AiFactory.cpp:1009 does.
    if (KeepsPartyThreatBackoff(player, facade) && !combatEngine->HasStrategy("threat"))
        combatEngine->addStrategy("threat");
}

void AiFactory::AddDefaultNonCombatStrategies(Player* player, PlayerbotAI* const facade, Engine* nonCombatEngine)
{
    if (player && player->GetClass() == CLASS_PRIEST)
    {
        nonCombatEngine->addStrategies("discipline", "dps assist", "follow", "buff", "food", "loot", "default", NULL);
        return;
    }
    if (WarriorIsTank(player, facade))
    {
        // AiFactory.cpp:1445 plus the follow the slice's movement mode needs, food (the rest between pulls) and the engine's own "default".
        nonCombatEngine->addStrategies("protection", "tank assist", "follow", "food", "default", NULL);
        return;
    }
    nonCombatEngine->addStrategies("arms", "dps assist", "follow", "food", "loot", "default", NULL);
}

void AiFactory::AddDefaultDeadStrategies(Player* /*player*/, PlayerbotAI* const /*facade*/, Engine* /*deadEngine*/)
{
    // Death and resurrection are out of the slice (spec section 1).
}

void AiFactory::AddDefaultReactionStrategies(Player* player, PlayerbotAI* const facade, ReactionEngine* reactionEngine)
{
    // Reactions (AoE avoidance, potions, chat) are out of the slice. The class strategy is not: its reaction triggers are the
    // ones that move the bot between the engines ("combat start" -> "set combat state", "combat end" -> "set non combat state",
    // "death" -> "set dead state": ClassStrategy::InitReactionTriggers), exactly as the full factory adds it (AiFactory.cpp:2085,
    // a warrior on the arms tab). Without it the bot entered the combat engine (AttackAction calls OnCombatStarted itself) and
    // never left it (R8 observation d).
    // The tank's spec strategy, as AiFactory.cpp:2085 (a warrior on the protection tab).
    reactionEngine->addStrategy(player && player->GetClass() == CLASS_PRIEST ? "discipline" : (WarriorIsTank(player, facade) ? "protection" : "arms"));
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

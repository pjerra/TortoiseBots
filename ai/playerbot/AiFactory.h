#pragma once

#include "PlayerbotAI.h"

class Player;

namespace ai
{
    class AiObjectContext;
    class ReactionEngine;
    class Engine;
}

using namespace ai;

class AiFactory
{
public:
    static AiObjectContext* createAiObjectContext(Player* player, PlayerbotAI* ai);
	static Engine* createCombatEngine(Player* player, PlayerbotAI* const facade, AiObjectContext* AiObjectContext);
	static Engine* createNonCombatEngine(Player* player, PlayerbotAI* const facade, AiObjectContext* AiObjectContext);
    static Engine* createDeadEngine(Player* player, PlayerbotAI* const facade, AiObjectContext* AiObjectContext);
    static ReactionEngine* createReactionEngine(Player* player, PlayerbotAI* const facade, AiObjectContext* AiObjectContext);
    static void AddDefaultNonCombatStrategies(Player* player, PlayerbotAI* const facade, Engine* nonCombatEngine);
    static void AddDefaultDeadStrategies(Player* player, PlayerbotAI* const facade, Engine* deadEngine);
    static void AddDefaultCombatStrategies(Player* player, PlayerbotAI* const facade, Engine* engine);
    static void AddDefaultReactionStrategies(Player* player, PlayerbotAI* const facade, ReactionEngine* reactionEngine);
    // RandomBotNonCombatStrategies without its open-world adds (grind, travel,
    // tfish, rpg, rpg craft, wander) while the player is in a dungeon or raid.
    static std::string InstanceSafeNonCombatStrategies(Player* player, std::string const& strategies);

public:
    static int GetPlayerSpecTab(const Player* player);
    static std::map<uint32, int32> GetPlayerSpecTabs(const Player* player);
    static BotRoles GetPlayerRoles(uint8 cls, uint8 specTab);
    static BotRoles GetPlayerRoles(const Player* player);

    // Party back-off rule: a non-tank bot grouped with a real-player master,
    // outside battlegrounds, keeps the "threat" combat strategy. Every path
    // that re-applies the configured combat strategy string checks this after
    // it, since the shipped random-bot string carries -threat.
    static bool KeepsPartyThreatBackoff(Player* player, PlayerbotAI* const facade);
    // RandomBotNonCombatStrategies without its open-world adds (grind, travel,
    // tfish, rpg, rpg craft, wander) while the bot's master is a real player.
    static std::string RealMasterSafeNonCombatStrategies(PlayerbotAI* ai, std::string const& strategies);
    // The list with every "+grind/+travel/+tfish/+rpg/+rpg craft/+wander" entry removed.
    static std::string StripOpenWorldNonCombatStrategies(std::string const& strategies);
};

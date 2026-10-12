
#include "playerbot/playerbot.h"
#include "ClassicRaidDungeonStrategies.h"

using namespace ai;

void ZulgurubDungeonStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    (void)triggers;
}

void ZulgurubDungeonStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    (void)triggers;
}

void RuinsOfAhnqirajDungeonStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "start ossirian fight",
        NextAction::array(0, new NextAction("enable ossirian fight strategy", 100.0f), NULL)));
}

void RuinsOfAhnqirajDungeonStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    (void)triggers;
}

void AhnqirajTempleDungeonStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    // Boss protection potion (fire/shadow/nature per boss entry, see
    // ProtectionPotionPolicy.h): only once a listed boss is an attacker, and
    // below movement, dispels and every heal tier so survival comes first.
    triggers.push_back(new TriggerNode(
        "boss protection potion ready",
        NextAction::array(0, new NextAction("boss protection potion", ACTION_HIGH), NULL)));
}

void AhnqirajTempleDungeonStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    (void)triggers;
}

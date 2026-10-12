
#include "playerbot/playerbot.h"
#include "BattlegroundStrategy.h"

using namespace ai;

// Leave as soon as the match is over. Above "attack enemy player" and "invalid
// target" (ACTION_EMERGENCY): once the result is in, nothing is worth staying for,
// and the core accepts a leave in combat at that point (STATUS_WAIT_LEAVE). The
// combat engine needs the node as well, or a bot still fighting at the end stayed
// until the core's own end timer ported it out, about two minutes later.
static void AddBgLeaveTrigger(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "bg ended",
        NextAction::array(0, new NextAction("bg leave", ACTION_EMERGENCY + 5), NULL)));
}

void BattlegroundStrategy::InitNonCombatTriggers(std::list<TriggerNode*> &triggers)
{
    triggers.push_back(new TriggerNode(
        "bg waiting",
        NextAction::array(0, new NextAction("bg move to start", 1.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "player has flag",
        NextAction::array(0, new NextAction("jump::position bg objective", 3.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "bg active",
        NextAction::array(0, new NextAction("bg move to objective", 1.0f), NULL)));

    // Mount slot: just above the objective move, only while the objective is
    // over 40 yd away and the bot is on foot, and at most once per 5 s. The
    // old "bg active" node at 2.0 fired every tick and held a bot still while
    // its mount cast kept failing; the generic "very often" node at 1.0 ties
    // the move, and a tie goes to whichever basket is queued first.
    triggers.push_back(new TriggerNode(
        "bg objective far",
        NextAction::array(0, new NextAction("check mount state", 1.5f), NULL)));

    // Random "often" like mod-playerbots: on "very often" the re-pick won
    // nearly every visit and "bg move to objective" never got a turn.
    triggers.push_back(new TriggerNode(
        "often",
        NextAction::array(0, new NextAction("bg check objective", 10.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "bg active",
        NextAction::array(0, new NextAction("bg check flag", ACTION_HIGH), NULL)));

    AddBgLeaveTrigger(triggers);

    /*triggers.push_back(new TriggerNode(
        "enemy flagcarrier near",
        NextAction::array(0, new NextAction("attack enemy flag carrier", 80.0f), NULL)));*/

    /*triggers.push_back(new TriggerNode(
        "team flagcarrier near",
        NextAction::array(0, new NextAction("bg protect fc", 40.0f), NULL)));*/

    /*triggers.push_back(new TriggerNode(
        "player has flag",
        NextAction::array(0, new NextAction("bg move to objective", 90.0f), NULL)));*/
}

void WarsongStrategy::InitNonCombatTriggers(std::list<TriggerNode*> &triggers)
{
    triggers.push_back(new TriggerNode(
        "bg active",
        NextAction::array(0, new NextAction("bg check flag", 70.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "often",
        NextAction::array(0, new NextAction("bg use buff", 30.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "low health",
        NextAction::array(0, new NextAction("bg use buff", 30.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "low mana",
        NextAction::array(0, new NextAction("bg use buff", 30.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "enemy flagcarrier near",
        NextAction::array(0, new NextAction("attack enemy flag carrier", 80.0f), NULL)));

    // Friendly-flag-carrier bodyguard (SOC-P5): stick with our carrier when
    // nearby instead of leaving them undefended. Donor mod-playerbots
    // WarsongStrategy node; the value is WSG-only so this stays quiet
    // elsewhere.
    triggers.push_back(new TriggerNode(
        "team flagcarrier near",
        NextAction::array(0, new NextAction("bg protect fc", 40.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "player has flag",
        NextAction::array(0,
            new NextAction("jump::position bg objective", 80.5f),
            new NextAction("bg move to objective", 80.0f),
            NULL)));

    triggers.push_back(new TriggerNode(
        "player has flag",
        NextAction::array(0, new NextAction("rocket boots", 81.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "very often",
        NextAction::array(0, new NextAction("bg banner", 10.0f), NULL)));

    // Periodic forced objective re-pick (SOC-P6): about once a minute, stop
    // and drop a stale objective instead of walking into it. Donor
    // mod-playerbots WarsongStrategy node. The action itself stays out of
    // combat and off the flag carrier.
    triggers.push_back(new TriggerNode(
        "timer bg",
        NextAction::array(0, new NextAction("bg reset objective force", ACTION_EMERGENCY), NULL)));
}

void WarsongStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    InitNonCombatTriggers(triggers);
    AddBgLeaveTrigger(triggers);
}

void AlteracStrategy::InitNonCombatTriggers(std::list<TriggerNode*> &triggers)
{
    // Same forced re-pick watchdog as Warsong (SOC-P6, donor AlteracStrategy
    // node). Alterac's non-combat set was empty; the combat set keeps its
    // banner node below.
    triggers.push_back(new TriggerNode(
        "timer bg",
        NextAction::array(0, new NextAction("bg reset objective force", ACTION_EMERGENCY), NULL)));
}

void AlteracStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "very often",
        NextAction::array(0, new NextAction("bg banner", ACTION_NORMAL), NULL)));

    AddBgLeaveTrigger(triggers);
}

void ArathiStrategy::InitNonCombatTriggers(std::list<TriggerNode*> &triggers)
{
    triggers.push_back(new TriggerNode(
        "bg active",
        NextAction::array(0, new NextAction("bg check flag", 70.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "often",
        NextAction::array(0, new NextAction("bg use buff", 30.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "low health",
        NextAction::array(0, new NextAction("bg use buff", 30.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "low mana",
        NextAction::array(0, new NextAction("bg use buff", 30.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "very often",
        NextAction::array(0, new NextAction("bg banner", 10.0f), NULL)));
}

void ArathiStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    InitNonCombatTriggers(triggers);
    AddBgLeaveTrigger(triggers);
}


#include "playerbot/playerbot.h"
#include "UsePotionsStrategy.h"

using namespace ai;

class UsePotionsStrategyActionNodeFactory : public NamedObjectFactory<ActionNode>
{
public:
    UsePotionsStrategyActionNodeFactory()
    {
        creators["healthstone"] = &healthstone;
        creators["healing potion"] = &healing_potion;
        creators["dark rune"] = &dark_rune;
    }

private:
    ACTION_NODE_A(healthstone, "healthstone", "healing potion");

    ACTION_NODE_A(healing_potion, "healing potion", "use bandage");

    ACTION_NODE_A(dark_rune, "dark rune", "mana potion");
};

UsePotionsStrategy::UsePotionsStrategy(PlayerbotAI* ai) : Strategy(ai)
{
    actionNodeFactories.Add(std::make_unique<UsePotionsStrategyActionNodeFactory>());
}

void UsePotionsStrategy::InitCombatTriggers(std::list<TriggerNode*> &triggers)
{
    triggers.push_back(new TriggerNode(
        "critical health",
        NextAction::array(0, new NextAction("healthstone", ACTION_CRITICAL_HEAL + 1), NULL)));

    triggers.push_back(new TriggerNode(
        "critical health",
        NextAction::array(0, new NextAction("healing potion", ACTION_CRITICAL_HEAL + 0.5f), NULL)));

    triggers.push_back(new TriggerNode(
        "critical health",
        NextAction::array(0, new NextAction("whipper root tuber", ACTION_CRITICAL_HEAL), NULL)));

    triggers.push_back(new TriggerNode(
        "low health",
        NextAction::array(0, new NextAction("use bandage", ACTION_MEDIUM_HEAL), NULL)));

    // Dark rune is its own cooldown line next to the potion, so it arms at
    // the same medium threshold just below the potion. Live H9 (Patchwerk,
    // 7-min fight): at "low mana" + ACTION_HIGH every nuke outranked it -
    // mages pushed the row 7887 times and never once executed it while
    // holding 10 runes each.
    triggers.push_back(new TriggerNode(
        "medium mana",
        NextAction::array(0, new NextAction("dark rune", ACTION_EMERGENCY - 1), NULL)));

    // E11: proactive mana-potion arm at the medium threshold (donor
    // UsePotionsStrategy.cpp:36: medium mana -> mana potion EMERGENCY).
    triggers.push_back(new TriggerNode(
        "medium mana",
        NextAction::array(0, new NextAction("mana potion", ACTION_EMERGENCY), NULL)));
    // Long-fight top-up (generic, every mana class): fire the potion as
    // soon as the high-water mark (<65%) breaks so the 2 min cooldown
    // rolls early and is back for the next cycle. Live H8 (no Wisdom):
    // mages slid 100% -> 45% in 60 s before the <40% row armed, so the
    // first potion landed ~40 s late and every later cycle slipped with
    // it. isPossible still gates on cooldown + possession, so short
    // fights spend nothing.
    triggers.push_back(new TriggerNode(
        "high mana",
        NextAction::array(0, new NextAction("mana potion", ACTION_EMERGENCY), NULL)));
    triggers.push_back(new TriggerNode(
        "has poison debuff",
        NextAction::array(0, new NextAction("anti-venom", ACTION_DISPEL), NULL)));
}

void UsePotionsStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    InitCombatTriggers(triggers);
}
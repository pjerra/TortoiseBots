
#include "playerbot/playerbot.h"
#include "OnyxiasLairDungeonStrategies.h"

using namespace ai;

void OnyxiasLairDungeonStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "start onyxia fight",
        NextAction::array(0, new NextAction("enable onyxia fight strategy", 100.0f), NULL)));

    // Paladin resist auto-swap, as in Molten Core and Blackwing Lair:
    // Onyxia is on the fire list (Flame Breath, Fireball, Deep Breath).
    triggers.push_back(new TriggerNode(
        "boss wants fire aura",
        NextAction::array(0, new NextAction("swap fire resistance aura", ACTION_HIGH + 1), NULL)));

    // Boss protection potion (fire/shadow/nature per boss entry, see
    // ProtectionPotionPolicy.h): only once a listed boss is an attacker, and
    // below movement, dispels and every heal tier so survival comes first.
    triggers.push_back(new TriggerNode(
        "boss protection potion ready",
        NextAction::array(0, new NextAction("boss protection potion", ACTION_HIGH), NULL)));
}

void OnyxiaFightStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    // Phase 2 airborne: melee cannot reach; wands/shoot keep DPS on the
    // boss while whelp packs are the valid melee targets. Ranged spread
    // trims Fireball (18392) splash; universal flank keeps Deep Breath
    // lane clear via the reaction engine.
    triggers.push_back(new TriggerNode(
        "onyxia airborne",
        NextAction::array(0, new NextAction("shoot", ACTION_HIGH),
                             new NextAction("raid spread", ACTION_HIGH - 1), NULL)));
}

void OnyxiaFightStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end onyxia fight",
        NextAction::array(0, new NextAction("disable onyxia fight strategy", 100.0f), NULL)));
}

void OnyxiaFightStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end onyxia fight",
        NextAction::array(0, new NextAction("disable onyxia fight strategy", 100.0f), NULL)));
}

void OnyxiaFightStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    // Deep Breath outranks the generic flank: cast-triggered lane dodge to
    // the nearest safe zone beats cone geometry while a breath casts.
    triggers.push_back(new TriggerNode(
        "onyxia deep breath",
        NextAction::array(0, new NextAction("onyxia breath safe zone", ACTION_EMERGENCY + 5), NULL)));
}

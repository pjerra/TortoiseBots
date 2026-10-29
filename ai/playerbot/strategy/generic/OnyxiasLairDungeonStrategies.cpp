
#include "playerbot/playerbot.h"
#include "OnyxiasLairDungeonStrategies.h"
#include "playerbot/strategy/actions/OnyxiasLairDungeonActions.h"

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
    // Phase 2 airborne: the boss hovers out of melee reach at hover points far
    // from the raid, past the 60-yd sight ceiling of "current target" - the
    // air_target tactics row (ai_boss_tactic) keeps her a target for the
    // ranged bots out to the react distance, and the casters cast their own
    // rotation on her from the floor instead of a wand.
    // The row only spreads the raid: Fireball (18392) splash and the breath
    // lanes stay the melee's problem, and the universal flank keeps Deep
    // Breath lane clear via the reaction engine.
    triggers.push_back(new TriggerNode(
        "onyxia airborne",
        NextAction::array(0, new NextAction("raid spread", ACTION_HIGH), NULL)));

    // Classes without a ranged rotation (PlayerbotAI::IsRanged false: warrior,
    // rogue, paladin and a melee-spec kit) have no cast to send up at her, so
    // the wand stays theirs alone. Every caster class is excluded here, not
    // outgunned by a wand row above its rotation.
    triggers.push_back(new TriggerNode(
        "onyxia airborne wand",
        NextAction::array(0, new NextAction("shoot", ACTION_HIGH), NULL)));

    // Toggle (AiPlayerbot.OnyxiaWhelpsAndEggs, on by default; the triggers
    // stay silent while it is off). Phase 2 whelp duty for AoE-capable
    // ranged DPS, above the generic "shoot" row so the whelp becomes the
    // target before the wand fires.
    triggers.push_back(new TriggerNode(
        "onyxia whelps",
        NextAction::array(0, new NextAction("onyxia kill whelps", ACTION_HIGH + 1), NULL)));

    // Step out of an egg cave before its trap circle hatches whelps.
    triggers.push_back(new TriggerNode(
        "onyxia eggs near",
        NextAction::array(0, new NextAction("onyxia avoid eggs", ACTION_EMERGENCY), NULL)));
}

void OnyxiaFightStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new OnyxiaWhelpHoldMultiplier(ai));
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


#include "playerbot/playerbot.h"
#include "AttackEnemyPlayersStrategy.h"

using namespace ai;

// Relevance 55 like mod-playerbots (AttackEnemyPlayersStrategy, 79bd428): below
// the battleground objective nodes (flag-carrier run 80, chase the enemy carrier
// 80, check flag 70) and critical/medium heals, above dispels and plain movement.
// At ACTION_EMERGENCY the order outranked everything a battleground bot does, so a
// player it could not reach kept it standing in mid-field for minutes.
static float const kAttackEnemyPlayerRelevance = 55.0f;

void AttackEnemyPlayersStrategy::InitCombatTriggers(std::list<TriggerNode*> &triggers)
{
    triggers.push_back(new TriggerNode(
        "enemy player near",
        NextAction::array(0, new NextAction("attack enemy player", kAttackEnemyPlayerRelevance), NULL)));
}

void AttackEnemyPlayersStrategy::InitNonCombatTriggers(std::list<TriggerNode*> &triggers)
{
    triggers.push_back(new TriggerNode(
        "enemy player near",
        NextAction::array(0, new NextAction("attack enemy player", kAttackEnemyPlayerRelevance), NULL)));
}
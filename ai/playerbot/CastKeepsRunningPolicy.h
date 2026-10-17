#pragma once

// Cast-keeps-running policy (issue #1062: alt bots animate gathering but get
// no loot).
//
// The Mining / Herb Gathering cast breaks on any step, but the AI keeps
// ticking while it runs: "open loot" (8.0) had already cleared the loot
// target, so the next tick offered "update follow" (idle) or the loot chain's
// own reselect ("add gathering loot" re-adds the node, "loot" re-picks it,
// "open loot" re-fires and the core refuses SPELL_FAILED_SPELL_IN_PROGRESS).
// Either path moved the bot or burned one of the three open attempts, so a
// node beside a waiting master never finished its 3.2 s cast: animation and
// sound played, no loot window ever opened. mod-playerbots guards the same
// hole trigger-side (LootAction::Execute / OpenLootAction::Execute refuse
// while IsNonMeleeSpellCast, MoveToLootAction while loot is pending).
//
// Rule: while a non-melee cast is in progress, follow station-keeping and
// loot (re-)select/(re-)open stand down. The cast finishes, SMSG_LOOT_RESPONSE
// arrives, "store loot" takes the items. After the cast the next tick resumes
// normally, so a master that kept walking is followed with the loot in the
// bags instead of the cast broken halfway.
//
// The predicate below is pure (bools only) so it can be unit-tested; the
// call sites pass bot->IsNonMeleeSpellCasted(false, true, true) - the exact
// expression PlayerbotAI::CastSpell uses for its in-progress gate (channeled
// and auto-repeat casts excluded, same as the core's prepare() check).

namespace ai
{
    // Follow must not start (or re-issue) while the bot is casting: even a
    // no-op Follow() stops the cast before checking whether the bot needs to
    // move at all.
    inline bool FollowBlockedByCast(bool castingInProgress)
    {
        return castingInProgress;
    }

    // The loot chain must not (re-)select a target or (re-)open one while the
    // gather cast runs: the cast gate would refuse the re-cast and each
    // refusal counted toward the three-strike abandon, wedging the node
    // before the running cast finished.
    inline bool LootChainBlockedByCast(bool castingInProgress)
    {
        return castingInProgress;
    }
}

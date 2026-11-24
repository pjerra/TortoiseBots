// Standalone regression test for the startup pool reset's safety decisions. Exercises the pure
// rules without a database or a running core:
//   - a target is deleted only when the row is ready, the account is the pool's, the character is still on that account,
//     no human session holds it, it owns no auction, and it is not a live bot or Player in the world;
//   - a guild led by a target is refused while any member has no character row, is on an account outside the pool, or is
//     a character on a pool account without a ready pool row;
//   - planning order: off/already-applied skip, everything else refuses before any deletion;
//   - characters on pool accounts that are not targets are counted as kept.
//
// Build and run:
//   g++ -std=c++17 -O1 -Wall -Wextra -I. tools/test_pool_reset_safety_policy.cpp -o /tmp/test_pool_reset_safety
//   /tmp/test_pool_reset_safety

#include "ai/playerbot/policy/world/PoolResetSafetyPolicy.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace TortoiseBots::world;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

namespace
{

PoolResetTargetFacts ReadyFact()
{
    PoolResetTargetFacts facts;
    facts.rowReady = true;
    facts.accountTrusted = true;
    facts.sameAccount = true;
    facts.guidExists = true;
    return facts;
}

void TestTargetDecisions()
{
    // The ordinary case: a ready pool row, its own account, nothing live, no listing.
    CHECK(DecidePoolResetTarget(ReadyFact()) == PoolResetTargetAction::Delete);

    // Ownership facts are never overridden by anything else.
    PoolResetTargetFacts notReady = ReadyFact();
    notReady.rowReady = false;
    notReady.guidExists = false;
    CHECK(DecidePoolResetTarget(notReady) == PoolResetTargetAction::Refuse);
    PoolResetTargetFacts untrusted = ReadyFact();
    untrusted.accountTrusted = false;
    CHECK(DecidePoolResetTarget(untrusted) == PoolResetTargetAction::Refuse);
    PoolResetTargetFacts moved = ReadyFact();
    moved.sameAccount = false;
    CHECK(DecidePoolResetTarget(moved) == PoolResetTargetAction::Refuse);
    // A moved character that the database also reports missing is still a refusal: never "gone".
    PoolResetTargetFacts movedGone = moved;
    movedGone.guidExists = false;
    CHECK(DecidePoolResetTarget(movedGone) == PoolResetTargetAction::Refuse);

    // A human session is final even when the target would otherwise wait.
    PoolResetTargetFacts human = ReadyFact();
    human.humanSession = true;
    human.inWorld = true;
    CHECK(DecidePoolResetTarget(human) == PoolResetTargetAction::Refuse);

    // Already deleted: the registry row may go, nothing is deleted.
    PoolResetTargetFacts gone = ReadyFact();
    gone.guidExists = false;
    CHECK(DecidePoolResetTarget(gone) == PoolResetTargetAction::Gone);

    // An auction listing is refused before the busy states are considered.
    PoolResetTargetFacts auction = ReadyFact();
    auction.ownsAuctions = true;
    CHECK(DecidePoolResetTarget(auction) == PoolResetTargetAction::Refuse);

    // A live bot or Player is drained first.
    PoolResetTargetFacts bot = ReadyFact();
    bot.botRegistered = true;
    CHECK(DecidePoolResetTarget(bot) == PoolResetTargetAction::Wait);
    PoolResetTargetFacts inWorld = ReadyFact();
    inWorld.inWorld = true;
    CHECK(DecidePoolResetTarget(inWorld) == PoolResetTargetAction::Wait);

    // The text is what a status or a log can print.
    CHECK(std::string(PoolResetTargetActionText(PoolResetTargetAction::Delete)) == "delete");
    CHECK(std::string(PoolResetTargetActionText(PoolResetTargetAction::Refuse)) == "refuse");
}

PoolResetMemberFacts Member(std::uint64_t guid)
{
    PoolResetMemberFacts member;
    member.guid = guid;
    member.characterKnown = true;
    member.poolAccount = true;
    member.readyPoolRow = true;
    return member;
}

void TestGuildMembers()
{
    // Every member a ready pool character on a trusted pool account.
    std::vector<PoolResetMemberFacts> pool = { Member(1), Member(2) };
    CHECK(FirstPoolResetOutsider(pool) == nullptr);

    // A member with no character row (a dangling guild_member row) blocks the guild.
    std::vector<PoolResetMemberFacts> dangling = { Member(1), Member(2) };
    dangling[1].characterKnown = false;
    CHECK(FirstPoolResetOutsider(dangling) == &dangling[1]);

    // A real player's character on a normal account blocks it.
    std::vector<PoolResetMemberFacts> real = { Member(1), Member(2) };
    real[0].poolAccount = false;
    CHECK(FirstPoolResetOutsider(real) == &real[0]);

    // A character on a pool account without a ready row is somebody's: it blocks too.
    std::vector<PoolResetMemberFacts> untracked = { Member(1), Member(2) };
    untracked[1].readyPoolRow = false;
    CHECK(FirstPoolResetOutsider(untracked) == &untracked[1]);

    CHECK(PoolResetMemberOutsidePool(Member(9)) == false);
}

void TestPlanOrder()
{
    PoolResetPlanFacts plan;
    plan.settingOff = true;
    CHECK(DecidePoolResetPlan(plan) == PoolResetPlanAction::Skip);

    plan = PoolResetPlanFacts();
    plan.settingInvalid = true;
    char const* reason = nullptr;
    CHECK(DecidePoolResetPlan(plan, &reason) == PoolResetPlanAction::Refuse);
    CHECK(reason != nullptr);

    // The pool off is a skip, not an error.
    plan = PoolResetPlanFacts();
    CHECK(DecidePoolResetPlan(plan) == PoolResetPlanAction::Skip);

    plan = PoolResetPlanFacts();
    plan.poolEnabled = true;
    CHECK(DecidePoolResetPlan(plan) == PoolResetPlanAction::Refuse);   // no refill possible

    plan = PoolResetPlanFacts();
    plan.poolEnabled = true;
    plan.refillPossible = true;
    CHECK(DecidePoolResetPlan(plan) == PoolResetPlanAction::Refuse);   // state unanswered

    plan = PoolResetPlanFacts();
    plan.poolEnabled = true;
    plan.refillPossible = true;
    plan.stateAnswered = true;
    CHECK(DecidePoolResetPlan(plan) == PoolResetPlanAction::Skip);     // generation not pending

    plan = PoolResetPlanFacts();
    plan.poolEnabled = true;
    plan.refillPossible = true;
    plan.stateAnswered = true;
    plan.generationPending = true;
    plan.targetsAnswered = true;
    plan.accountsTrusted = true;
    CHECK(DecidePoolResetPlan(plan) == PoolResetPlanAction::Schedule);

    // Each single refusal blocks the schedule.
    PoolResetPlanFacts refuse = plan;
    refuse.noHumanSession = false;
    CHECK(DecidePoolResetPlan(refuse) == PoolResetPlanAction::Refuse);
    refuse = plan;
    refuse.noAuctions = false;
    CHECK(DecidePoolResetPlan(refuse) == PoolResetPlanAction::Refuse);
    refuse = plan;
    refuse.guildsClear = false;
    CHECK(DecidePoolResetPlan(refuse) == PoolResetPlanAction::Refuse);
    refuse = plan;
    refuse.accountsTrusted = false;
    CHECK(DecidePoolResetPlan(refuse) == PoolResetPlanAction::Refuse);
    refuse = plan;
    refuse.targetsAnswered = false;
    CHECK(DecidePoolResetPlan(refuse) == PoolResetPlanAction::Refuse);

    // A refusal always names a reason.
    reason = nullptr;
    DecidePoolResetPlan(refuse, &reason);
    CHECK(reason != nullptr);
}

void TestKeptCharacters()
{
    std::vector<std::uint64_t> onAccounts = { 10, 11, 12, 900 };
    std::vector<std::uint64_t> targets = { 10, 11 };
    CHECK(CountPoolResetKeptCharacters(onAccounts, targets) == 2);
    CHECK(CountPoolResetKeptCharacters({}, targets) == 0);
    CHECK(CountPoolResetKeptCharacters(onAccounts, {}) == 4);
    CHECK(CountPoolResetKeptCharacters({ 1, 2 }, { 1, 2 }) == 0);
}

} // namespace

int main()
{
    TestTargetDecisions();
    TestGuildMembers();
    TestPlanOrder();
    TestKeptCharacters();
    std::printf("OK: %d checks passed\n", checks);
    return 0;
}

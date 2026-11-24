// Standalone test of issue #14's decisions (forever/src/host/TbObjectUsePure.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_object_use_policy.cpp -o /tmp/test_object_use_policy && /tmp/test_object_use_policy
//
// The cases are the ones a live fill depends on: a scripted altar whose users the core never counts, a summoned ritual whose owner
// must keep the channel, the pick order (damage, then healers, tanks last) and every reason a bot is not picked (including one
// that already used the object), the release rules of a fill hold and of a named-use hold, how long a target remembers a released
// bot, the closed click queue with the fill off, and the chat words (`use`, `ritual`, `soulstone`) with their refusal order.
#include "forever/src/host/TbObjectUsePure.h"

#include <cstdio>
#include <string>
#include <vector>

namespace ou = TortoiseBots::objuse;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static ou::Candidate Bot(std::uint64_t guid, float distance)
{
    ou::Candidate bot;
    bot.guid = guid;
    bot.alive = true;
    bot.authorized = true;
    bot.distance = distance;
    return bot;
}

// The UBRS altar: three users, scripted, its own users never reach the core's counter.
static ou::FillFacts ScriptedAltar()
{
    ou::FillFacts facts;
    facts.casters = 3;
    return facts;
}

static void MissingCountsScriptedAltarClicks()
{
    ou::FillFacts facts = ScriptedAltar();
    CHECK(ou::MissingUsers(facts) == 3);          // nobody clicked yet

    facts.recordedUsers = 1;                      // the initiator's own click, still within 8 yd
    CHECK(ou::MissingUsers(facts) == 2);

    facts.heldBotsUsed = 1;                       // one held bot's use went in
    CHECK(ou::MissingUsers(facts) == 1);

    facts.walking = 1;                            // and one is on its way: not double counted
    CHECK(ou::MissingUsers(facts) == 0);

    ou::FillFacts counted;                        // a plain ritual: the core counts, the clicks add nothing
    counted.casters = 3;
    counted.uniqueUses = 2;
    CHECK(ou::Users(counted) == 2);
    CHECK(ou::MissingUsers(counted) == 1);

    counted.uniqueUses = 3;
    CHECK(ou::MissingUsers(counted) == 0);

    ou::FillFacts untouched;                      // casters 0: not a multi-user object
    CHECK(ou::MissingUsers(untouched) == 0);
}

static void OwnedRitualNeedsChannel()
{
    ou::FillFacts facts;
    facts.ownedRitual = true;
    facts.casters = 3;
    facts.uniqueUses = 1;                         // the summoning warlock counts at spawn (SpellEffects.cpp:4684-4688)

    facts.ownerChanneling = false;
    CHECK(ou::MissingUsers(facts) == 0);          // every use would be refused till the channel is back

    facts.ownerChanneling = true;
    CHECK(ou::MissingUsers(facts) == 2);          // the two missing clickers

    facts.uniqueUses = 3;
    CHECK(ou::Users(facts) == 3);
    CHECK(ou::MissingUsers(facts) == 0);
}

static void PickerTanksAndHealersLast()
{
    std::vector<ou::Candidate> bots;
    ou::Candidate tank = Bot(10, 2.0f);
    tank.isTank = true;
    ou::Candidate healer = Bot(11, 3.0f);
    healer.isHealer = true;
    ou::Candidate near = Bot(12, 5.0f);
    ou::Candidate far = Bot(13, 10.0f);
    bots = { tank, healer, near, far };

    std::vector<std::uint64_t> two = ou::PickFillers(bots, 2, ou::kDefaultRangeYd);
    CHECK(two.size() == 2);
    CHECK(two.size() == 2 && two[0] == 12 && two[1] == 13);

    std::vector<std::uint64_t> four = ou::PickFillers(bots, 4, ou::kDefaultRangeYd);
    CHECK(four.size() == 4);
    CHECK(four.size() == 4 && four[0] == 12 && four[1] == 13 && four[2] == 11 && four[3] == 10);

    std::vector<ou::Candidate> sameDistance = { Bot(21, 4.0f), Bot(20, 4.0f) };
    std::vector<std::uint64_t> ordered = ou::PickFillers(sameDistance, 2, ou::kDefaultRangeYd);
    CHECK(ordered.size() == 2 && ordered[0] == 20 && ordered[1] == 21);   // then guid
}

static void PickerSkipsCombatRunHeldUnauthorized()
{
    ou::Candidate combat = Bot(1, 1.0f);
    combat.inCombat = true;
    ou::Candidate dead = Bot(2, 1.0f);
    dead.alive = false;
    ou::Candidate held = Bot(3, 1.0f);
    held.held = true;
    ou::Candidate running = Bot(4, 1.0f);
    running.inRun = true;
    ou::Candidate stranger = Bot(5, 1.0f);
    stranger.authorized = false;
    ou::Candidate outOfRange = Bot(6, 31.0f);
    ou::Candidate clean = Bot(7, 20.0f);

    std::vector<ou::Candidate> bots = { combat, dead, held, running, stranger, outOfRange, clean };
    std::vector<std::uint64_t> picked = ou::PickFillers(bots, 5, ou::kDefaultRangeYd);
    CHECK(picked.size() == 1);
    CHECK(picked.size() == 1 && picked[0] == 7);

    CHECK(ou::CandidateUsable(clean, ou::kDefaultRangeYd));
    CHECK(!ou::CandidateUsable(combat, ou::kDefaultRangeYd));
    CHECK(!ou::CandidateUsable(dead, ou::kDefaultRangeYd));
    CHECK(!ou::CandidateUsable(held, ou::kDefaultRangeYd));
    CHECK(!ou::CandidateUsable(running, ou::kDefaultRangeYd));
    CHECK(!ou::CandidateUsable(stranger, ou::kDefaultRangeYd));
    CHECK(!ou::CandidateUsable(outOfRange, ou::kDefaultRangeYd));
}

// A bot that already used this object is not picked again for it ("once per GO per bot").
static void PickerSkipsUsedBot()
{
    ou::Candidate used = Bot(1, 1.0f);
    used.used = true;
    ou::Candidate fresh = Bot(2, 1.0f);

    std::vector<ou::Candidate> bots = { used, fresh };
    std::vector<std::uint64_t> picked = ou::PickFillers(bots, 2, ou::kDefaultRangeYd);
    CHECK(picked.size() == 1 && picked[0] == 2);
    CHECK(!ou::CandidateUsable(used, ou::kDefaultRangeYd));
}

// A target nobody is held on keeps its `used` record while the object still fills, so a released bot is not held again.
static void TargetMemoryOutlivesRelease()
{
    CHECK(ou::RemembersTarget(true, true, 0, ou::kTargetMemoryMs));
    CHECK(ou::RemembersTarget(true, true, ou::kTargetMemoryMs, ou::kTargetMemoryMs));       // at the bound still remembered
    CHECK(!ou::RemembersTarget(true, true, ou::kTargetMemoryMs + 1, ou::kTargetMemoryMs));  // older: a new ritual starts over
    CHECK(!ou::RemembersTarget(true, false, 0, ou::kTargetMemoryMs));                       // used up / not ready
    CHECK(!ou::RemembersTarget(false, true, 0, ou::kTargetMemoryMs));                       // gone (a summoned ritual ends with its channel)
}

// With the fill off the click queue stays empty however many uses arrive, and switching it off drops what was queued.
static void DisabledFillRecordsNothing()
{
    ou::ClickQueue<int> queue;
    queue.SetEnabled(false);
    std::uint32_t accepted = 0;
    for (int i = 0; i < 1000; ++i)
        if (queue.Record(i))
            ++accepted;
    CHECK(accepted == 0);
    CHECK(queue.items.empty());

    queue.SetEnabled(true);
    CHECK(queue.Record(1));
    CHECK(queue.items.size() == 1);
    std::vector<int> drained = queue.Drain();
    CHECK(queue.items.empty() && drained.size() == 1);

    CHECK(queue.Record(2));
    queue.SetEnabled(false);                                                               // the switch goes off with a use queued
    CHECK(queue.items.empty());                                                            // it is dropped, not replayed
    CHECK(!queue.Record(3));
    CHECK(queue.items.empty());
}

static void BattlegroundNeverFills()
{
    std::vector<ou::Candidate> bots;
    for (std::uint64_t guid = 1; guid <= 3; ++guid)
    {
        ou::Candidate bot = Bot(guid, 2.0f);
        bot.inBattleground = true;
        bots.push_back(bot);
    }
    CHECK(ou::PickFillers(bots, 3, ou::kDefaultRangeYd).empty());
    CHECK(!ou::CandidateUsable(bots[0], ou::kDefaultRangeYd));
}

static void HoldReleasesOnCombat()
{
    ou::HoldFacts fill;
    CHECK(!ou::JudgeHold(fill).release);          // a quiet hold stays
    fill.inCombat = true;
    ou::HoldVerdict verdict = ou::JudgeHold(fill);
    CHECK(verdict.release);
    CHECK(std::string(verdict.code) == "combat");
    fill.inCombat = false;
    fill.dead = true;
    CHECK(ou::JudgeHold(fill).release);
    fill.dead = false;
    fill.offMap = true;
    CHECK(ou::JudgeHold(fill).release);
}

static void HoldReleasesWhenObjectGone()
{
    ou::HoldFacts facts;
    facts.objectPresent = false;
    ou::HoldVerdict verdict = ou::JudgeHold(facts);
    CHECK(verdict.release && std::string(verdict.code) == "object-gone");

    facts = ou::HoldFacts();
    facts.objectReady = false;
    verdict = ou::JudgeHold(facts);
    CHECK(verdict.release && std::string(verdict.code) == "object-not-ready");

    facts = ou::HoldFacts();
    facts.ownedRitual = true;
    facts.ownerChanneling = false;
    verdict = ou::JudgeHold(facts);
    CHECK(verdict.release && std::string(verdict.code) == "channel-ended");

    facts = ou::HoldFacts();
    facts.ritualComplete = true;
    verdict = ou::JudgeHold(facts);
    CHECK(verdict.release && std::string(verdict.code) == "ritual-complete");

    facts = ou::HoldFacts();
    facts.ritualReset = true;
    verdict = ou::JudgeHold(facts);
    CHECK(verdict.release && std::string(verdict.code) == "ritual-reset");

    facts = ou::HoldFacts();
    facts.holdElapsed = true;
    verdict = ou::JudgeHold(facts);
    CHECK(verdict.release && std::string(verdict.code) == "timeout");
}

static void NamedUseHoldsUntilFollow()
{
    ou::HoldFacts named;
    named.named = true;
    named.objectPresent = false;                  // a named bot does not walk off because its object is gone
    named.objectReady = false;
    named.holdElapsed = true;
    CHECK(!ou::JudgeHold(named).release);

    named.releasedByCommand = true;
    ou::HoldVerdict verdict = ou::JudgeHold(named);
    CHECK(verdict.release && std::string(verdict.code) == "ordered");

    ou::HoldFacts kept;
    kept.named = true;
    CHECK(!ou::JudgeHold(kept).release);
    kept.inCombat = true;
    verdict = ou::JudgeHold(kept);
    CHECK(verdict.release && std::string(verdict.code) == "combat");
    kept.inCombat = false;
    kept.offMap = true;
    CHECK(ou::JudgeHold(kept).release);
}

static void BarePartyUseNotClaimed()
{
    ou::UseWord bare = ou::ParseObjectWord(true, "use altar", "Bot1");
    CHECK(!bare.claimed);                         // the engine's item use, as before

    ou::UseWord named = ou::ParseObjectWord(true, "Bot1 use altar", "Bot1");
    CHECK(named.claimed && named.party && !named.everyone);
    CHECK(named.what == "altar");

    ou::UseWord other = ou::ParseObjectWord(true, "Bot2 use altar", "Bot1");
    CHECK(!other.claimed);

    ou::UseWord all = ou::ParseObjectWord(true, "all use altar", "Bot1");
    CHECK(all.claimed && all.everyone);
    CHECK(all.what == "altar");

    ou::UseWord lower = ou::ParseObjectWord(true, "bot1 USE altar", "Bot1");
    CHECK(lower.claimed && lower.what == "altar");

    ou::UseWord whisper = ou::ParseObjectWord(false, "use 175706", "Bot1");
    CHECK(whisper.claimed && !whisper.party && !whisper.everyone);
    CHECK(whisper.what == "175706");

    CHECK(!ou::ParseObjectWord(false, "use", "Bot1").claimed);
    CHECK(!ou::ParseObjectWord(false, "ritual Bob", "Bot1").claimed);
    CHECK(!ou::ParseObjectWord(true, "come here", "Bot1").claimed);
}

static void NameMatchCaseAndEntry()
{
    CHECK(ou::NameMatches("Blackrock Altar", 175706, "altar"));
    CHECK(ou::NameMatches("Blackrock Altar", 175706, "BLACKROCK"));
    CHECK(ou::NameMatches("Blackrock Altar", 175706, "  Rock  "));
    CHECK(ou::NameMatches("Blackrock Altar", 175706, "175706"));
    CHECK(!ou::NameMatches("Blackrock Altar", 175706, "1757"));       // a partial entry is not an entry
    CHECK(!ou::NameMatches("Blackrock Altar", 175706, "1757061"));
    CHECK(!ou::NameMatches("Blackrock Altar", 175706, "altar of"));
    CHECK(!ou::NameMatches("Blackrock Altar", 175706, ""));
}

static void RitualRefusalOrder()
{
    ou::RitualFacts facts;
    facts.authorized = true;
    facts.warlock = true;
    facts.known = true;
    facts.targetInGroup = true;
    facts.canEnter = true;
    facts.casters = 3;

    ou::RitualVerdict verdict = ou::JudgeRitual(facts);
    CHECK(verdict.ok && std::string(verdict.code) == "ok");
    CHECK(verdict.ok && verdict.more == 2);       // `summoning <Name>: 2 more needed`

    auto codeIs = [](ou::RitualFacts const& facts, char const* code, bool ok = false)
    {
        ou::RitualVerdict v = ou::JudgeRitual(facts);
        return v.ok == ok && std::string(v.code) == code;
    };

    facts.authorized = false;
    CHECK(codeIs(facts, "not-yours"));
    facts.authorized = true;
    facts.warlock = false;
    CHECK(codeIs(facts, "not-warlock"));
    facts.warlock = true;
    facts.known = false;
    CHECK(codeIs(facts, "not-known"));
    facts.known = true;
    facts.inCombat = true;
    CHECK(codeIs(facts, "in-combat"));
    facts.inCombat = false;
    facts.battleground = true;
    CHECK(codeIs(facts, "battleground"));
    facts.battleground = false;
    facts.targetInGroup = false;
    CHECK(codeIs(facts, "no-target"));
    facts.targetInGroup = true;
    facts.targetIsSelf = true;
    CHECK(codeIs(facts, "no-target"));
    facts.targetIsSelf = false;
    facts.sameMapWithin40 = true;
    CHECK(codeIs(facts, "already-here"));
    facts.sameMapWithin40 = false;
    facts.canEnter = false;
    CHECK(codeIs(facts, "cannot-enter"));
}

static void SoulstoneBareTargetsSender()
{
    CHECK(ou::BareSoulstoneTarget(42, 0) == 42);          // bare, no target: the sender
    CHECK(ou::BareSoulstoneTarget(42, 99) == 99);         // bare, the sender's target

    ou::SoulstoneFacts facts;
    facts.authorized = true;
    facts.warlock = true;
    facts.hasStone = true;
    facts.targetExists = true;
    facts.targetAlive = true;
    facts.targetInGroup = true;
    facts.inRange = true;
    facts.spellReady = true;
    CHECK(ou::JudgeSoulstone(facts).ok);

    auto codeIs = [](ou::SoulstoneFacts const& facts, char const* code)
    {
        ou::SoulstoneVerdict v = ou::JudgeSoulstone(facts);
        return !v.ok && std::string(v.code) == code;
    };

    facts.authorized = false;
    CHECK(codeIs(facts, "not-yours"));
    facts.authorized = true;
    facts.warlock = false;
    CHECK(codeIs(facts, "not-warlock"));
    facts.warlock = true;
    facts.hasStone = false;
    CHECK(codeIs(facts, "no-stone"));
    facts.hasStone = true;
    facts.inCombat = true;
    CHECK(codeIs(facts, "in-combat"));
    facts.inCombat = false;
    facts.targetExists = false;
    CHECK(codeIs(facts, "no-target"));
    facts.targetExists = true;
    facts.targetAlive = false;
    CHECK(codeIs(facts, "dead"));
    facts.targetAlive = true;
    facts.targetInGroup = false;
    CHECK(codeIs(facts, "not-grouped"));
    facts.targetInGroup = true;
    facts.inRange = false;
    CHECK(codeIs(facts, "out-of-range"));
    facts.inRange = true;
    facts.spellReady = false;
    CHECK(codeIs(facts, "on-cooldown"));
}

int main()
{
    MissingCountsScriptedAltarClicks();
    OwnedRitualNeedsChannel();
    PickerTanksAndHealersLast();
    PickerSkipsCombatRunHeldUnauthorized();
    PickerSkipsUsedBot();
    TargetMemoryOutlivesRelease();
    DisabledFillRecordsNothing();
    BattlegroundNeverFills();
    HoldReleasesOnCombat();
    HoldReleasesWhenObjectGone();
    NamedUseHoldsUntilFollow();
    BarePartyUseNotClaimed();
    NameMatchCaseAndEntry();
    RitualRefusalOrder();
    SoulstoneBareTargetsSender();

    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}

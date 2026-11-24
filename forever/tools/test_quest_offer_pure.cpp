// Standalone test of the core-free half of the quest-target group offer (SPEC #42, round 1):
// forever/src/host/TbQuestOfferPure.h. Core-free: every fact is passed in, no server types.
//
//   g++ -std=c++17 -O1 -Wall -Wextra -I. -Iforever/src forever/tools/test_quest_offer_pure.cpp -o /tmp/test_quest_offer_pure && /tmp/test_quest_offer_pure
//
// The spec's failing cases, in order: OwnedBotNeverOffers, InstanceMapRefused, PlayerInGroupRefused,
// OtherFactionRefused, PlayerCooldownBlocksSecondBot, BotCooldownHolds, NearestBotWinsTieLowestGuid,
// VictimMemoryLasts20s, ExpiredInviteRecorded, ReleaseOnPlayerLeft, ReleaseOnMapChange,
// ReleaseAfterMaxMinutes. A mutation that lets an owned bot offer, accepts an instance map, counts the
// cooldowns per bot instead of per player, ignores the 20 s victim window, or releases on the wrong
// reason fails here.

#include "host/TbQuestOfferPure.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace qo = tbhost::quest_offer;

static int g_checks = 0;
static int g_fail = 0;

#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

// A player that passes every rule; `bot` is the matching pool bot.
static qo::PlayerFacts GoodPlayer()
{
    qo::PlayerFacts p;
    p.isReal = true;
    p.alive = true;
    p.inGroup = false;
    p.hasPendingInvite = false;
    p.dnd = false;
    p.onTaxi = false;
    p.ignoresBot = false;
    p.instanceableMap = false;
    p.needsTarget = true;
    p.faction = 469; // Alliance
    p.level = 5;
    return p;
}

static qo::BotFacts GoodBot(uint32_t guidLow = 7)
{
    qo::BotFacts b;
    b.guidLow = guidLow;
    b.faction = 469;
    b.level = 5;
    b.pool = true;
    b.free = true;
    b.owned = false;
    b.hasMaster = false;
    b.hasBrain = true;
    b.alive = true;
    b.inGroup = false;
    b.hasInvite = false;
    b.inCombat = false;
    b.victimEntry = 0;
    b.needsTarget = true;
    b.distanceYd = 12.0;
    b.rangeYd = double(qo::kDefaultRangeYd);
    return b;
}

static void OwnedBotNeverOffers()
{
    qo::PlayerFacts const player = GoodPlayer();
    uint32_t const entry = 6;

    CHECK(qo::BotEligible(GoodBot(), player, entry));

    qo::BotFacts owned = GoodBot();
    owned.owned = true;
    CHECK(!qo::BotEligible(owned, player, entry));
    // Every other precondition of the bot rule still refuses.
    qo::BotFacts notPool = GoodBot();
    notPool.pool = false;
    CHECK(!qo::BotEligible(notPool, player, entry));
    qo::BotFacts notFree = GoodBot();
    notFree.free = false;
    CHECK(!qo::BotEligible(notFree, player, entry));
    qo::BotFacts mastered = GoodBot();
    mastered.hasMaster = true;
    CHECK(!qo::BotEligible(mastered, player, entry));
    qo::BotFacts brainless = GoodBot();
    brainless.hasBrain = false;
    CHECK(!qo::BotEligible(brainless, player, entry));
    qo::BotFacts dead = GoodBot();
    dead.alive = false;
    CHECK(!qo::BotEligible(dead, player, entry));
    qo::BotFacts grouped = GoodBot();
    grouped.inGroup = true;
    CHECK(!qo::BotEligible(grouped, player, entry));
    qo::BotFacts invited = GoodBot();
    invited.hasInvite = true;
    CHECK(!qo::BotEligible(invited, player, entry));
    // Out of combat, or fighting E itself; a bot in combat with something else does not offer.
    qo::BotFacts otherFight = GoodBot();
    otherFight.inCombat = true;
    otherFight.victimEntry = 999;
    CHECK(!qo::BotEligible(otherFight, player, entry));
    qo::BotFacts sameFight = GoodBot();
    sameFight.inCombat = true;
    sameFight.victimEntry = entry;
    CHECK(qo::BotEligible(sameFight, player, entry));
    // Bots need the mob themselves, and must be in range.
    qo::BotFacts noNeed = GoodBot();
    noNeed.needsTarget = false;
    CHECK(!qo::BotEligible(noNeed, player, entry));
    qo::BotFacts far = GoodBot();
    far.distanceYd = double(qo::kDefaultRangeYd) + 0.5;
    CHECK(!qo::BotEligible(far, player, entry));
}

static void InstanceMapRefused()
{
    qo::PlayerFacts const player = GoodPlayer();
    CHECK(qo::PlayerEligible(player, player.faction));

    qo::PlayerFacts inInstance = player;
    inInstance.instanceableMap = true;
    CHECK(!qo::PlayerEligible(inInstance, player.faction));

    // The player must need the mob himself (an incomplete MONSTER objective or its quest loot).
    qo::PlayerFacts noNeed = player;
    noNeed.needsTarget = false;
    CHECK(!qo::PlayerEligible(noNeed, player.faction));
}

static void PlayerInGroupRefused()
{
    qo::PlayerFacts const player = GoodPlayer();

    qo::PlayerFacts grouped = player;
    grouped.inGroup = true;
    CHECK(!qo::PlayerEligible(grouped, player.faction));

    qo::PlayerFacts invited = player;
    invited.hasPendingInvite = true;
    CHECK(!qo::PlayerEligible(invited, player.faction));

    qo::PlayerFacts busy = player;
    busy.dnd = true;
    CHECK(!qo::PlayerEligible(busy, player.faction));
    qo::PlayerFacts taxi = player;
    taxi.onTaxi = true;
    CHECK(!qo::PlayerEligible(taxi, player.faction));
    qo::PlayerFacts ignoring = player;
    ignoring.ignoresBot = true;
    CHECK(!qo::PlayerEligible(ignoring, player.faction));
    qo::PlayerFacts dead = player;
    dead.alive = false;
    CHECK(!qo::PlayerEligible(dead, player.faction));
    qo::PlayerFacts mob = player;
    mob.isReal = false; // a bot is never offered to
    CHECK(!qo::PlayerEligible(mob, player.faction));
}

static void OtherFactionRefused()
{
    qo::PlayerFacts const player = GoodPlayer();
    qo::BotFacts const bot = GoodBot();
    CHECK(qo::PlayerEligible(player, bot.faction));

    qo::PlayerFacts hordePlayer = player;
    hordePlayer.faction = 67;
    CHECK(!qo::PlayerEligible(hordePlayer, bot.faction));
    CHECK(!qo::BotEligible(bot, hordePlayer, 6));

    // The spec's +/-5 levels: the bot must be level-close to the player.
    qo::PlayerFacts highPlayer = player;
    highPlayer.level = 10;
    CHECK(qo::BotEligible(GoodBot(), highPlayer, 6)); // 5 vs 10 = 5 apart
    highPlayer.level = 11;
    CHECK(!qo::BotEligible(GoodBot(), highPlayer, 6)); // 6 apart
    highPlayer.level = 1;                              // 4 apart, other side of the window
    CHECK(qo::BotEligible(GoodBot(), highPlayer, 6));
}

static void PlayerCooldownBlocksSecondBot()
{
    // The cooldown is kept for the player, not per bot: a second bot may not offer while it runs.
    qo::OfferRecord record = qo::RecordOffer(qo::OfferRecord{}, 1001, 5000);
    CHECK(record.hasOffered);
    CHECK(record.lastOfferSec == 5000);
    CHECK(record.invite.active);
    CHECK(!qo::CooldownOk(5000 + 29 * 60, record.lastOfferSec, record.hasOffered, 30));
    CHECK(qo::CooldownOk(5000 + 30 * 60, record.lastOfferSec, record.hasOffered, 30));
    // Whatever the outcome (declined, expired, ignored) the record blocks the next offer.
    qo::OfferRecord const ended = qo::EndInvite(record, 5000 + 60);
    CHECK(!ended.invite.active);
    CHECK(ended.hasOffered && ended.lastOfferSec == 5000);
    CHECK(!qo::CooldownOk(5000 + 60, ended.lastOfferSec, ended.hasOffered, 30));
    // A first offer is never blocked.
    CHECK(qo::CooldownOk(5000, 0, false, 30));
    // A clock that went backwards does not open the gate.
    CHECK(!qo::CooldownOk(4000, 5000, true, 30));
}

static void BotCooldownHolds()
{
    // The bot's own cooldown is shorter and independent of the player one.
    qo::BotOfferRecord bot = qo::RecordBotOffer(qo::BotOfferRecord{}, 91);
    CHECK(bot.hasOffered && bot.lastOfferSec == 91);
    CHECK(!qo::CooldownOk(91 + 9 * 60, bot.lastOfferSec, bot.hasOffered, 10));
    CHECK(qo::CooldownOk(91 + 10 * 60, bot.lastOfferSec, bot.hasOffered, 10));
}

static void NearestBotWinsTieLowestGuid()
{
    qo::BotFacts near1 = GoodBot(900);
    near1.distanceYd = 8.0;
    qo::BotFacts near2 = GoodBot(400);
    near2.distanceYd = 8.0;
    qo::BotFacts farther = GoodBot(100);
    farther.distanceYd = 20.0;

    std::vector<qo::BotFacts> const candidates = { farther, near1, near2 };
    CHECK(qo::PickBot(candidates) == 400); // nearest, then the lowest guid

    std::vector<qo::BotFacts> const onlyFar = { farther };
    CHECK(qo::PickBot(onlyFar) == 100);
    CHECK(qo::PickBot({}) == 0);
}

static void VictimMemoryLasts20s()
{
    uint32_t const entry = 6;
    qo::VictimMemory memory;
    CHECK(!qo::VictimMemoryValid(memory, entry, 100));

    memory = qo::RememberVictim(memory, entry, 100);
    CHECK(memory.has && memory.entry == entry && memory.seenSec == 100);
    CHECK(qo::VictimMemoryValid(memory, entry, 100));
    CHECK(qo::VictimMemoryValid(memory, entry, 120)); // 20 s after the last sighting still counts
    CHECK(!qo::VictimMemoryValid(memory, entry, 121));

    // Seeing the same victim again refreshes the window; a different entry replaces it.
    memory = qo::RememberVictim(memory, entry, 115);
    CHECK(qo::VictimMemoryValid(memory, entry, 134));
    CHECK(!qo::VictimMemoryValid(memory, entry, 136));
    memory = qo::RememberVictim(memory, 1234, 140);
    CHECK(memory.entry == 1234);
    CHECK(!qo::VictimMemoryValid(memory, entry, 140));
    // Forget never validates an empty entry.
    CHECK(!qo::VictimMemoryValid(qo::VictimMemory{}, 0, 140));
}

static void ExpiredInviteRecorded()
{
    qo::OfferRecord record = qo::RecordOffer(qo::OfferRecord{}, 1001, 700);
    uint32_t const expireSec = 60;
    CHECK(!qo::InviteExpired(record.invite, 700 + 59, expireSec));
    CHECK(qo::InviteExpired(record.invite, 700 + 60, expireSec));
    CHECK(!qo::InviteExpired(qo::PendingInvite{}, 900, expireSec));

    // Ending the invite keeps the offer: the player cooldown still runs from it.
    record = qo::EndInvite(record, 700 + 60);
    CHECK(!record.invite.active);
    CHECK(record.hasOffered && record.lastOfferSec == 700);
    CHECK(!qo::CooldownOk(700 + 60, record.lastOfferSec, record.hasOffered, 30));
}

static void ReleaseOnPlayerLeft()
{
    qo::ReleaseFacts facts;
    facts.inGroup = true;
    facts.sameMap = true;
    facts.playerInInstance = false;
    facts.farSec = 0;
    facts.botNeedsTarget = true;
    facts.groupedSec = 60;
    facts.maxMinutes = 30;
    CHECK(qo::ShouldRelease(facts) == qo::ReleaseReason::None);

    qo::ReleaseFacts left = facts;
    left.inGroup = false;
    CHECK(qo::ShouldRelease(left) == qo::ReleaseReason::PlayerLeft);
}

static void ReleaseOnMapChange()
{
    qo::ReleaseFacts facts;
    facts.inGroup = true;
    facts.sameMap = false;
    facts.playerInInstance = false;
    facts.farSec = 0;
    facts.botNeedsTarget = true;
    facts.groupedSec = 60;
    facts.maxMinutes = 30;
    CHECK(qo::ShouldRelease(facts) == qo::ReleaseReason::MapChange);

    qo::ReleaseFacts instance = facts;
    instance.sameMap = true;
    instance.playerInInstance = true;
    CHECK(qo::ShouldRelease(instance) == qo::ReleaseReason::InstanceMap);

    // Over 200 yd for 120 s, the bot leaves; at 119 s it stays.
    qo::ReleaseFacts far = facts;
    far.sameMap = true;
    far.farSec = 119;
    CHECK(qo::ShouldRelease(far) == qo::ReleaseReason::None);
    far.farSec = 120;
    CHECK(qo::ShouldRelease(far) == qo::ReleaseReason::TooFar);

    // The bot's own need ending is a release reason too.
    qo::ReleaseFacts done = facts;
    done.sameMap = true;
    done.botNeedsTarget = false;
    CHECK(qo::ShouldRelease(done) == qo::ReleaseReason::NeedEnded);
}

static void ReleaseAfterMaxMinutes()
{
    qo::ReleaseFacts facts;
    facts.inGroup = true;
    facts.sameMap = true;
    facts.playerInInstance = false;
    facts.farSec = 0;
    facts.botNeedsTarget = true;
    facts.groupedSec = 30 * 60 - 1;
    facts.maxMinutes = 30;
    CHECK(qo::ShouldRelease(facts) == qo::ReleaseReason::None);
    facts.groupedSec = 30 * 60;
    CHECK(qo::ShouldRelease(facts) == qo::ReleaseReason::MaxMinutes);
}

static void OfferText()
{
    // The whisper the player sees, and the line the bot says when it leaves.
    CHECK(qo::OfferLine("Kobold Vermin") == "We're both after Kobold Vermin. Want to group up?");
    CHECK(std::string(qo::kReleaseLine) == "Got what I needed, thanks!");
}

int main()
{
    OwnedBotNeverOffers();
    InstanceMapRefused();
    PlayerInGroupRefused();
    OtherFactionRefused();
    PlayerCooldownBlocksSecondBot();
    BotCooldownHolds();
    NearestBotWinsTieLowestGuid();
    VictimMemoryLasts20s();
    ExpiredInviteRecorded();
    ReleaseOnPlayerLeft();
    ReleaseOnMapChange();
    ReleaseAfterMaxMinutes();
    OfferText();

    if (g_fail)
    {
        std::printf("%d of %d checks FAILED\n", g_fail, g_checks);
        return 1;
    }
    std::printf("%d checks passed\n", g_checks);
    return 0;
}

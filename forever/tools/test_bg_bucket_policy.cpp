// Standalone test of BG1's bucket rules (ai/playerbot/policy/bg/BgQueuePolicy.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_bg_bucket_policy.cpp -o /tmp/test_bg_bucket_policy && /tmp/test_bg_bucket_policy
//
// The spec's cases (BATTLEGROUNDS-SPEC.md D4/D5/D9, section 4): the fill counts invited bots, the core's creation veto holds a
// match until both sides reach the target (and not a second longer once the deficit window passed), a bot-only creation is
// refused unless Bg.Autonomous, a module-queued bot leaves an orphaned bucket after the grace or on the hard timeout, a dead or
// fighting invitee still accepts, the caps refuse only once a limit dropped below the bots already held, and a leftover queue
// entry of a running match is not a second instance.
#include "ai/playerbot/policy/bg/BgQueuePolicy.h"

#include <cstdio>
#include <string>

namespace bg = TortoiseBots::bg;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static bg::BgBucketFacts Bucket()
{
    bg::BgBucketFacts facts;
    facts.target = 6;
    facts.humanWaiting = true;
    return facts;
}

static bg::BgQueueFacts Eligible()
{
    bg::BgQueueFacts facts;
    facts.pool = true;
    facts.free = true;
    facts.alive = true;
    facts.inBracket = true;
    facts.caps.maxInstances = 2;
    facts.caps.maxBots = 50;
    return facts;
}

int main()
{
    // Held = uninvited queue + invitations in flight, per side: a human's invite pending is not a hole to queue a second batch in.
    bg::BgBucketFacts facts = Bucket();
    facts.waiting[0] = 3;
    facts.invited[0] = 2;
    CHECK(bg::BgSideHeld(facts, 0) == 5);
    CHECK(bg::BgSideNeed(facts, 0) == 1);
    CHECK(bg::BgSideNeed(facts, 1) == 6);
    facts.waiting[0] = 4;
    facts.invited[0] = 2;
    CHECK(bg::BgSideHeld(facts, 0) == 6);
    CHECK(bg::BgSideNeed(facts, 0) == 0);
    facts.waiting[1] = 9;
    CHECK(bg::BgSideNeed(facts, 1) == 0);          // over the target is never a negative need

    // The veto: with a human waiting, creation waits until both sides reach the target.
    bg::BgBucketFacts oneSide = Bucket();
    oneSide.waiting[0] = 6;
    oneSide.waiting[1] = 1;
    CHECK(bg::BgVetoCreate(oneSide));
    oneSide.waiting[1] = 5;
    CHECK(bg::BgVetoCreate(oneSide));                // 5 + 0 invited: one bot short on horde
    oneSide.invited[1] = 1;
    CHECK(!bg::BgVetoCreate(oneSide));               // 6 each: let the core start it

    // ... but not forever: a side short for the whole deficit window releases the match to the core's own minimum.
    bg::BgBucketFacts deficit = Bucket();
    deficit.waiting[0] = 6;
    deficit.waiting[1] = 1;
    deficit.deficitDone = true;
    CHECK(!bg::BgVetoCreate(deficit));

    // A bucket with no human: the module's bots alone must not become an instance (D4.3, Bg.Autonomous = 0).
    bg::BgBucketFacts botsOnly = Bucket();
    botsOnly.humanWaiting = false;
    botsOnly.moduleBots = true;
    botsOnly.waiting[0] = 3;
    botsOnly.waiting[1] = 3;
    CHECK(bg::BgVetoCreate(botsOnly));
    botsOnly.autonomous = true;                      // the opt-in bot-only seeding lets it through
    CHECK(!bg::BgVetoCreate(botsOnly));
    botsOnly.autonomous = false;
    botsOnly.moduleBots = false;                     // a human's own premade leftovers: not the module's to hold
    CHECK(!bg::BgVetoCreate(botsOnly));

    // Orphan and timeout: a module bot leaves a bucket that lost its human after the grace, and leaves any queue on the hard
    // timeout; neither fires before its own time.
    CHECK(bg::kOrphanQueueGraceMs == 10000);
    CHECK(bg::kQueuedTimeoutMs == 3 * 180000);
    CHECK(bg::BgQueuedDrop(true, 0, 0) == bg::BgQueueDrop::None);
    CHECK(bg::BgQueuedDrop(true, bg::kOrphanQueueGraceMs, 1000) == bg::BgQueueDrop::None);
    CHECK(bg::BgQueuedDrop(false, bg::kOrphanQueueGraceMs - 1, 1000) == bg::BgQueueDrop::None);
    CHECK(bg::BgQueuedDrop(false, bg::kOrphanQueueGraceMs, 1000) == bg::BgQueueDrop::NoHuman);
    CHECK(bg::BgQueuedDrop(true, 0, bg::kQueuedTimeoutMs) == bg::BgQueueDrop::Timeout);
    CHECK(bg::BgQueuedDrop(false, 0, bg::kQueuedTimeoutMs) == bg::BgQueueDrop::Timeout);
    CHECK(bg::BgQueuedDrop(false, bg::kOrphanQueueGraceMs, bg::kQueuedTimeoutMs) == bg::BgQueueDrop::Timeout);

    // The names are the event's `why=` tokens: a dropped bot always says why.
    CHECK(std::string(bg::BgQueueDropWhy(bg::BgQueueDrop::None)).empty());
    CHECK(std::string(bg::BgQueueDropWhy(bg::BgQueueDrop::NoHuman)) == "no-human");
    CHECK(std::string(bg::BgQueueDropWhy(bg::BgQueueDrop::Timeout)) == "queue-timeout");
    for (bg::BgQueueDrop drop : { bg::BgQueueDrop::None, bg::BgQueueDrop::NoHuman, bg::BgQueueDrop::Timeout })
        CHECK(std::string(bg::BgQueueDropWhy(drop)).size() < 32);

    // At accept time the core revives a dead accepter (BattleGroundHandler.cpp:506) and combat does not block the port, so
    // neither is a reason to decline; the refusals that still matter do refuse.
    bg::BgQueueFacts accept = Eligible();
    CHECK(bg::BgAcceptEligible(accept));
    accept.alive = false;
    CHECK(bg::BgAcceptEligible(accept));             // dead: the core resurrects it
    accept = Eligible();
    accept.inCombat = true;
    CHECK(bg::BgAcceptEligible(accept));
    accept = Eligible();
    accept.pool = false;
    CHECK(!bg::BgAcceptEligible(accept));
    accept = Eligible();
    accept.free = false;                             // bound since it queued: the free-queue invite is stale
    CHECK(!bg::BgAcceptEligible(accept));
    accept = Eligible();
    accept.inRun = true;
    CHECK(!bg::BgAcceptEligible(accept));
    accept = Eligible();
    accept.inBattleground = true;
    CHECK(!bg::BgAcceptEligible(accept));
    accept = Eligible();
    accept.deserter = true;
    CHECK(!bg::BgAcceptEligible(accept));

    // The caps are inclusive and every held bot is counted already: at the limit is fine, one over is not.
    bg::BgQueueCaps caps;
    caps.maxInstances = 2;
    caps.maxBots = 50;
    caps.instancesWithBots = 2;
    caps.botsInBattlegrounds = 50;
    CHECK(!bg::BgAcceptCapsOver(caps));
    caps.botsInBattlegrounds = 51;
    CHECK(bg::BgAcceptCapsOver(caps));               // a lowered target after queuing
    caps.botsInBattlegrounds = 50;
    // An invitation enters an instance that already exists, so the instance cap is not asked again: the leftover queue entries
    // of a running match would otherwise outnumber it and decline every accept.
    caps.instancesWithBots = 3;
    CHECK(!bg::BgAcceptCapsOver(caps));

    // D9's instance cap, counted from the tracks (queued + invited + inside, each match once): a candidate in the
    // bucket the module already counts is not stopped by the instance cap, so a second human's queue fills to its target.
    auto track = [](std::uint32_t list, std::uint32_t bracket, std::uint32_t instance, bg::BgTrackState state)
    {
        bg::BgTrackFact fact;
        fact.listId = list;
        fact.bracket = bracket;
        fact.instance = instance;
        fact.state = state;
        return fact;
    };
    std::vector<bg::BgTrackFact> tracks;
    tracks.push_back(track(2, 1, 0, bg::BgTrackState::Queued));     // human A's bucket, one bot queued
    bg::BgQueueCaps second = bg::BgCapsOfTracks(tracks, 2, 50, 2, 2);
    CHECK(second.instancesWithBots == 1);
    CHECK(second.botsInBattlegrounds == 1);
    CHECK(!second.ownBucketCounted);
    bg::BgQueueFacts candidate = Eligible();
    candidate.caps = second;
    CHECK(bg::BgQueueEligible(candidate));                          // 1 < 2: human B's first bot is allowed
    tracks.push_back(track(2, 2, 0, bg::BgTrackState::Queued));     // now human B's own bucket is counted
    second = bg::BgCapsOfTracks(tracks, 2, 50, 2, 2);
    CHECK(second.instancesWithBots == 2);
    CHECK(second.ownBucketCounted);
    candidate.caps = second;
    CHECK(bg::BgQueueEligible(candidate));                          // its own bucket: the cap does not stop the fill
    bg::BgQueueCaps third = bg::BgCapsOfTracks(tracks, 2, 50, 2, 3);
    candidate.caps = third;
    CHECK(bg::BgQueueRefusalOf(candidate) == bg::BgRefusal::MaxInstances);   // a new bucket is refused

    // A running match is identified by its instance, never by its bracket: a human queuing in the same bracket after the
    // cap is taken needs a slot for the match his queue will create, so its first bot is refused, not waved through as "his own".
    std::vector<bg::BgTrackFact> running;
    running.push_back(track(2, 1, 77, bg::BgTrackState::Inside));
    running.push_back(track(2, 1, 77, bg::BgTrackState::Invited));
    bg::BgQueueCaps full = bg::BgCapsOfTracks(running, 1, 50, 2, 1);
    CHECK(full.instancesWithBots == 1);                             // invited and inside of one instance are one match
    CHECK(!full.ownBucketCounted);                                  // the bracket is not the queue
    candidate.caps = full;
    CHECK(bg::BgQueueRefusalOf(candidate) == bg::BgRefusal::MaxInstances);
    // Its queued reservation is a count of its own next to the running match, and the fill's own bucket is exempt.
    running.push_back(track(2, 1, 0, bg::BgTrackState::Queued));
    full = bg::BgCapsOfTracks(running, 2, 50, 2, 1);
    CHECK(full.instancesWithBots == 2);                             // instance 77 + the reservation for the next match
    CHECK(full.ownBucketCounted);
    candidate.caps = full;
    CHECK(bg::BgQueueEligible(candidate));
    // Bg.MaxBots still applies inside the own bucket.
    full = bg::BgCapsOfTracks(running, 2, 3, 2, 1);
    bg::BgQueueFacts over = Eligible();
    over.caps = full;
    CHECK(bg::BgQueueRefusalOf(over) == bg::BgRefusal::MaxBots);

    // The standard flow starts the match from the head groups and leaves the other bots queued until the core's next update. A queue entry whose humans are all inside (or invited to) an instance is a leftover of that match, not a second
    // instance, so a running match with its leftovers is one instance and its invitations stand at any cap.
    auto leftover = [&](std::uint32_t list, std::uint32_t bracket)
    {
        bg::BgTrackFact fact = track(list, bracket, 0, bg::BgTrackState::Queued);
        fact.demandPlaced = true;
        return fact;
    };
    std::vector<bg::BgTrackFact> ownMatch;
    ownMatch.push_back(track(2, 1, 77, bg::BgTrackState::Inside));
    ownMatch.push_back(track(2, 1, 77, bg::BgTrackState::Invited));
    ownMatch.push_back(leftover(2, 1));
    ownMatch.push_back(leftover(2, 1));
    bg::BgQueueCaps oneMatch = bg::BgCapsOfTracks(ownMatch, 1, 50, 2, 1);
    CHECK(oneMatch.instancesWithBots == 1);                         // the leftovers reserve nothing
    CHECK(oneMatch.botsInBattlegrounds == 4);                       // they are still held bots
    CHECK(!oneMatch.ownBucketCounted);
    CHECK(!bg::BgAcceptCapsOver(oneMatch));                         // cap 1: every invitation still stands
    // Two concurrent humans, each with his own match and leftovers, under the default cap of 2.
    ownMatch.push_back(track(3, 1, 88, bg::BgTrackState::Inside));
    ownMatch.push_back(leftover(3, 1));
    bg::BgQueueCaps twoMatches = bg::BgCapsOfTracks(ownMatch, 2, 50, 3, 1);
    CHECK(twoMatches.instancesWithBots == 2);
    CHECK(!bg::BgAcceptCapsOver(twoMatches));
    // A third human beyond the cap is still refused when his queue would create a third match.
    bg::BgQueueFacts thirdHuman = Eligible();
    thirdHuman.caps = bg::BgCapsOfTracks(ownMatch, 2, 50, 1, 2);
    CHECK(bg::BgQueueRefusalOf(thirdHuman) == bg::BgRefusal::MaxInstances);
    // A queue entry whose humans are still only queued is a reservation and counts: it may create the next match.
    bg::BgTrackFact waiting = track(2, 1, 0, bg::BgTrackState::Queued);
    ownMatch.push_back(waiting);
    bg::BgQueueCaps reserved = bg::BgCapsOfTracks(ownMatch, 2, 50, 2, 1);
    CHECK(reserved.instancesWithBots == 3);                         // 77 + 88 + the reservation
    CHECK(reserved.ownBucketCounted);
    candidate.caps = reserved;
    CHECK(bg::BgQueueEligible(candidate));                          // the fill's own bucket is never held against it

    // A human's own battleground holds a bound bot's queue only for the match the bot queued for: another map, or
    // another instance once the bot has one, is a queue the human already left.
    CHECK(bg::BgHumanInTrackMatch(2, 77, 2, 77));
    CHECK(bg::BgHumanInTrackMatch(2, 77, 2, 0));                    // no invitation yet: the same map is enough
    CHECK(!bg::BgHumanInTrackMatch(3, 77, 2, 0));                   // AV while the bot waits for WSG
    CHECK(!bg::BgHumanInTrackMatch(2, 78, 2, 77));                  // another instance than the bot's invitation
    CHECK(!bg::BgHumanInTrackMatch(0, 0, 2, 0));                    // not in a battleground at all

    // The deficit window is the spec's 180 s.
    CHECK(bg::kBgDeficitMs == 180000);

    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}

#pragma once

// The run leader (`.bot run`, docs/play/dungeon-run-leader.md): an opt-in
// driver that walks a bound party through a dungeon plan
// (ai/playerbot/DungeonPlanPolicy.h) by itself, behind
// AiPlayerbot.DungeonSelfClear (default 0).
//
// It gives the party ONLY the remote commander's own orders (`cmd move`,
// `move ... direct`, `pullback`, `rest`, `loot`, `open`, `use`, `ritual`, `gossip`), so every
// commander fix also fixes the run leader and the two cannot drift apart. The
// fights themselves are the bots' own combat AI (with the boss tactics table).
//
// This header is the whole decision: one Run per commander, advanced once a
// second by Tick() with a Facts snapshot that the world glue in
// commands/BotCommands.cpp reads from the party and the plan step's
// surroundings. Tick() answers with at most one order (the text after `.bot
// cmd `); the glue sends it as the commander and reports a refusal back with
// OrderRefused(). No game types here: tools/test_dungeon_run_policy.cpp runs it
// on its own.
//
// Safety rules, carried over from turtle-agent's remote driver (its plan
// stepper is the owner's MIT code; provenance in its PR):
//   - gather before a pull: every living bot within kGatherYd of the lead bot;
//   - the plan's cc units get one mage's `cc moon` before a pull (never the
//     pull target), its kill_first units are pulled first and focused in a
//     fight (a summoned healing ward too);
//   - walk in the plan's hops; before each hop, pull what stands near it, but
//     never what a later pull or boss step owns (OwnedByLaterStep);
//   - a pack or boss counts as done only when the lead bot stands within
//     kSeeYd of it and no living, hostile unit of it is left;
//   - kWipeDead dead (or all dead) regroups by itself once the fight is over:
//     revive, gather, then walk back over the plan's own move points (at most
//     kAutoRegroupsPerStep times a step, then it pauses);
//   - the owner's rule (2026-10-17): a fallen member (the lead bot too) is
//     waited for until it is alive and back with the party before the next
//     pull, boss, task or move (WaitForFallen), fight first, never mid-event;
//     a lead bot back at the entrance alone walks back while the living
//     party holds where it is (LeadRevivedAway);
//   - a refused gather is sent once more, then the run goes on without it;
//     at a gather's ceiling (and an approach hop's) a far straggler that
//     stands still or stalled (kSummonYd) is summoned once;
//   - a fallen bot alive again far from the lead bot, out of combat, is
//     summoned to it before its corpse run walks the dungeon, once per
//     revive; a failed summon walks back (SummonRevived);
//   - every wait has a ceiling (gather, walk, pull, fight, rest, task, object
//     wait, and kStepIdleCapMs per step); a ceiling pauses the run with the
//     reason, it never loops;
//   - any order from a player to the party pauses the run until `resume`;
//   - a party member coming back with attackers on it (more than kGatherYd,
//     at most kRunBackMaxYd away) keeps running to the party past the mobs
//     while the run waits for it (RunBack; the glue makes it a passive
//     follower of the lead bot); the party stays where it is and fights them
//     once it has arrived. Never during an event.

#include "../ai/playerbot/DungeonPlanPolicy.h"
#include "../ai/playerbot/RangedApproachPolicy.h"
#include "RemoteCommanderTasks.h"
#include "RezHoldPolicy.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace TortoiseBots {
namespace DungeonRun {

using ai::DungeonPlan;
using ai::PlanStep;
using ai::PlanStepKind;

constexpr std::uint32_t kTickMs = 1000;           // the glue ticks every run this often
constexpr std::uint32_t kWipeDead = 3;            // this many dead bots stop the step for a regroup
constexpr std::uint32_t kAutoRegroupsPerStep = 2; // regroups the run does by itself per step, then it pauses
constexpr std::uint32_t kGatherTries = 2;         // a gather refused this often: the run goes on without it
constexpr std::uint32_t kWalkBackRetries = 3;     // a walk-back point refused for a passing reason is sent again this often
constexpr float kArriveYd = 3.0f;                 // a walk is done this close to its point
constexpr float kStalledArriveYd = 6.0f;          // ... or this close once it stopped gaining ground (a point
                                                  // just off the mesh or on a ledge the bot cannot reach)
constexpr float kStandZTolYd = 4.5f;              // a stand this far above or below the lead bot or less is judged on the ground plane
constexpr float kSeeYd = 60.0f;                   // a pack, boss or spot is judged only from this close
constexpr float kApproachYd = 30.0f;              // an unseen pack is approached to this far from its centre
constexpr float kApproachHopYd = 40.0f;           // ... in navmesh path hops of at most this length
constexpr std::uint32_t kMaxApproachHops = 8;     // ... and at most this many without getting nearer, then pause
constexpr float kHopClearYd = 20.0f;              // an approach hop in sight of the pack is finished unless a unit is this close
constexpr float kApproachProgressYd = 5.0f;       // a hop that ends this much nearer than the nearest so far is progress
constexpr std::uint32_t kMaxWalkBackHops = 40;    // a walk back straight to its last point: hops before the replay
constexpr float kRouteSpotYd = 10.0f;             // a lead bot this close to where it died (or stood at a pause) is
                                                  // still on its route checkpoint's stretch (Run::routeKnown)
constexpr float kDetourMaxYd = 15.0f;             // an unreachable move point may be stood in for by a
                                                  // reachable one at most this far from it ...
constexpr float kDetourEndYd = 5.0f;              // ... whose navmesh path ends this close to it (DetourPointReachable)
constexpr float kGatherYd = 20.0f;                // every living bot this close to the lead before a pull
constexpr float kHopPullYd = 15.0f;               // units this close to the next hop are pulled first
constexpr std::uint32_t kGatherCapMs = 30000;
constexpr std::uint32_t kGatherComingMaxMs = 90000; // a straggler that keeps coming is waited for this long in all
constexpr float kSummonYd = 40.0f;                // at a gather's ceiling a bot this far that ...
constexpr float kStragglerDzYd = 8.0f;            // ... or this far up or down (another floor) ...
constexpr float kStragglerGainYd = 5.0f;          // ... came less than this closer since the last sample is summoned
constexpr float kStillYd = 2.0f;                   // a bot that moved less than this ...
constexpr std::uint32_t kStillMs = 6000;          // ... in this long stands still (stuck: summoned at a ceiling)
constexpr std::uint32_t kStuckSummonMs = 15000;   // in a regroup's gather: a far bot still this long is summoned at once
constexpr std::uint32_t kSummonWaitMs = 6000;     // a direct step's summoned follower is waited for this long (the summon takes 3 s)
// A direct step's straggler (refused too-far, or not with the lead bot at its gather's ceiling) is summoned again
// once more than kDirectResummonMs passed since its last summon of the step, or once it died and came back since
// then (DirectSummonAllowed), at most kDirectSummonCap summons a bot and step. The refusal pauses only after
// kDirectPauseSummons summons of it and kDirectPauseMs since the step's first refusal (DirectPauseDue); until then
// the run waits for it (the guard keeps going) and sends the step again no sooner than kDirectRetryMs.
constexpr std::uint32_t kDirectResummonMs = 30000;
constexpr std::uint32_t kDirectSummonCap = 3;
constexpr std::uint32_t kDirectPauseSummons = 2;
constexpr std::uint32_t kDirectPauseMs = 60000;
constexpr std::uint32_t kDirectRetryMs = 10000;
// A glue that reads the direct-step test (Facts::directJoinSupported) did not read it on the tick a direct order
// would go out (the step or the walk back's point changed within the tick before): the order waits a tick for the
// read, at most this many ticks in a row (a guard; then the ground rule alone, as for a glue without the test).
constexpr std::uint32_t kDirectJoinHolds = 10;
constexpr std::uint32_t kWalkRetryMs = 12000;     // no progress for this long: send the move again
constexpr std::uint32_t kShortStallMs = 3000;     // a plain walk within kStalledArriveYd that gains nothing this long arrived
constexpr std::uint32_t kWalkTries = 3;           // ... at most this often, then pause
constexpr float kWalkProgressYd = 2.0f;
// An exact waypoint (ai::kPlanFlagExact on a move row: the point a direct step
// after it is launched from; ExactWalkTo). The Wailing Caverns hop to Lord
// Serpentis works only from its foot (tests/wc-hop-probe-2, 2026-10-18): a
// direct step sent from a few yards short is refused no-los or lands short of
// the gap, over a 47 yd drop. Same numbers as turtle-agent's remote driver
// (internal/dungeon/remote.go tightenTo: directArrive, tightenStillYd,
// tightenProofHold, tightenBack).
constexpr float kExactArriveYd = 1.5f;            // the lead bot stands this close to the point ...
constexpr float kExactStillYd = 0.5f;             // ... and moved less than this from one spot ...
constexpr std::uint32_t kExactProofMs = 2000;     // ... for this long (2 s of looks, the lead not moving): proved
constexpr float kExactBackYd = 6.5f;              // a `cmd move` shorter than ~6 yd on the ground plane is not
                                                  // carried out (turtle-agent MinMove, ZF-1 run 4): a lead bot
                                                  // nearer than this is walked back along the plan's own points first
constexpr std::uint32_t kExactCapMs = kWalkRetryMs * kWalkTries; // the tighten's ceiling out of combat (36 s): then pause
constexpr std::uint32_t kPullNoFightMs = 25000;   // a pull that started no fight in this time failed
constexpr std::uint32_t kPullTriesPerUnit = 2;    // a unit two pulls could not start a fight with is passed
constexpr std::uint32_t kMaxPullsPerTarget = 3;  // a unit three fought pulls did not kill is out of reach
constexpr std::uint32_t kFruitlessPulls = 3;      // three pulls in a row that kill nothing: the rest of the pack is out of reach
// The pull stand (AiPlayerbot.DungeonRunPullStand, HoldForShot): `pullback`
// goes out only when the tank can shoot from where it stands, else it walks to
// a firing stand first. The margin is the module's own ReachPullAction margin
// (the walk of a ranged pull stops this much inside the shot's range).
constexpr float kShotMarginYd = 5.0f;
constexpr std::uint32_t kShotStandTries = 2;      // firing stands walked to per unit and step, then the order as before
constexpr std::uint32_t kShotStillWaitMs = 3000;  // a moving tank is waited for this long before the stand rule judges it
// A stand walk that gains nothing for kWalkRetryMs, or takes longer than this
// in all, is given up: the order goes out from where the tank is (never a pause).
constexpr std::uint32_t kShotWalkCapMs = kWalkRetryMs * kWalkTries;
constexpr std::uint32_t kMaxPullsPerStep = 20;
constexpr std::uint32_t kMaxWalkBackPulls = 8;    // pulls on the way of a walk back (their own count), then pause
// A lead bot on a navmesh island (IslandEscape). Zul'Farrak r3-19z20: the tank
// chased a Shadowhunter onto a ~3 x 3 yd patch of mesh at 1589 1046 that no
// path leaves; every walk-back point was refused or a zero-length hop, and the
// lead bot stood there 13 minutes, through a resume.
constexpr float kIslandSizeYd = 3.0f;             // that island's size (offline navmesh check, PLAN-ZF-ZUMRAH-WALKBACK B5)
constexpr float kGoodPathMinYd = 10.0f;           // a partial path that ends this close to the lead bot (short of its
                                                  // point) may only cross the island: from 1589.6 1046.9 the path to
                                                  // step 65 ended 3.05 yd away, on it. A good path is complete or ends farther.
static_assert(kGoodPathMinYd > kIslandSizeYd + kArriveYd, "a path that only crosses the island is no good path");
constexpr float kEscapeMaxYd = 10.0f;             // the straight hop off it goes to the last good spot this close ...
constexpr float kEscapeMaxVertical = 8.0f;        // ... and this far up or down (RemoteCommander::kDirectMaxVertical)
constexpr std::uint32_t kIslandEscapes = 2;       // hops off one island, then pause
constexpr std::uint32_t kEscapeSettleMs = 2000;   // the hop is over once the lead bot stands still this long ...
constexpr std::uint32_t kEscapeCapMs = 6000;      // ... or after this long
constexpr float kEscapeMovedYd = 2.0f;            // the glue refuses the hop when the lead bot moved this far since the read
// The straight line of the hop, checked before the lead bot is moved (CheckEscapeSegment):
constexpr float kSegmentStepYd = 2.0f;            // sampled every this far ...
constexpr std::size_t kSegmentSamples = 8;        // ... at most this many samples (the end point is the last)
constexpr float kSegmentGroundYd = 4.0f;          // a floor within this far up or down of the line at each sample
constexpr float kSegmentRiseYd = 2.0f;            // at most this height step from one sample to the next (up; down
                                                  // too, but for a ring spot whose path leads on: ...)
constexpr float kSegmentDropYd = kSegmentGroundYd; // ... at most this step down (Uldaman r16-19z34: a ramp's tip 2.1-2.5 yd
                                                  // above the floor it is not linked to)
constexpr float kEscapeHostileYd = 10.0f;         // no living hostile this close to the hop's end
// One partial path toward one point does not prove an island (a ledge, a closed
// door, a point off the mesh): IslandConfirmed wants a second sign as well.
constexpr float kIslandCellYd = 4.0f;             // the evidence is kept for the lead bot's cell of this size ...
constexpr float kEvidenceSpotYd = 2.0f;           // ... a second reading counts from this close to the first one's spot ...
constexpr float kEvidenceTargetYd = 10.0f;        // ... toward a point at least this far from the first one's point
constexpr std::uint64_t kGoodSpotFreshMs = 120000; // ... and, for the forward move check, read this recently (GoodSpotFresh)
constexpr float kGoodSpotProbeYd = 40.0f;         // the last good spot this close is probed (the glue: Facts::goodSpotCut)
constexpr std::uint32_t kWalkBackResends = 6;     // a walk-back point sent this often from one spot (kArriveYd) is passed
constexpr std::uint32_t kStepEscapes = 4;         // hops off an island one step may send in all (a good read in
                                                  // between gives kIslandEscapes again; the step ceiling is 300 s)
constexpr std::uint32_t kRegroupReviveMs = 240000; // after a resume: the dead must be up within this
constexpr std::uint32_t kRegroupGatherMs = 60000;  // ... and the party gathered within this after that
constexpr std::uint32_t kFightCapMs = 180000;
constexpr std::uint32_t kBossFightCapMs = 420000;
constexpr std::uint32_t kCeilingQuietMs = 5000; // a ceiling pause whose party stays out of combat this long goes on by itself
constexpr std::uint32_t kStaleCombatGraceMs = 20000; // a combat flag with nothing hostile engaged is waited out this long in all of one fight, then the fight ceiling runs again
constexpr std::uint32_t kStaleConfirmMs = 3000;      // ... and it must stand this long without a break before a wait_object is finished over it
constexpr std::uint32_t kRestCapMs = 60000;
constexpr std::uint32_t kRestRepeatMs = 20000;
constexpr std::uint32_t kPartyRezWaitMs = 15000;  // a regroup with a living rezzer (priest, paladin, shaman) waits this
                                                  // long for its resurrection before `revive party` releases the dead
constexpr std::uint32_t kLootFfaWaitMs = 20000;   // a loot step's free-for-all detour waits this long for a busy carrier
constexpr std::uint32_t kLootRestoreTries = 3;    // ... and orders the loot method back this many times (one a tick)
constexpr std::uint32_t kTaskResends = 2;         // loot, open, use: an interrupted or in-combat task is sent again this often
constexpr std::uint32_t kTaskCapMs = 90000;       // loot, open, use: the commander task's own budget is shorter
constexpr std::uint32_t kUseOnResends = 1;        // use_on: an answer that says the cast did not happen is sent again once
constexpr std::uint32_t kUseOnProofMs = 30000;    // use_on: after the ACK, the object's change or its boss must show this soon
constexpr float kUseOnSummonYd = 150.0f;          // use_on: how far around the lead bot the glue looks for the summoned boss
constexpr std::uint32_t kStepIdleCapMs = 300000;  // out-of-combat time one step may take
constexpr std::uint32_t kBossSearchMs = 60000;    // a boss not at his spot (a patrol) is waited for this long, then pause
// A pull step's patroller (WaitForPatroller; Uldaman r10-19z29 step 66): the
// Stone Steward 4860 walks a lane whose north end only is in the step's 10 yd
// radius, so the step said "pack cleared" in 0 s in r8, r9 and r10, and where
// the patrol was decided whether it came alone or into the Keeper's fight.
constexpr std::uint32_t kPatrolWaitMs = 60000;    // ... waited for this long at most (a shorter timeout_s of the step caps it)
constexpr float kPatrolSeeYd = 30.0f;             // the glue reads patrollers of the step's entries this far outside its radius
// A big pack's firing stand walk (BigPackWalkGate; Uldaman r10-19z29 step 79):
// the lead walked to the stand of a 9-joiner scorpid pack with the healer and
// the mage 27-30 yd behind on the ramp, and the pack woke on a dps first.
constexpr std::uint32_t kBigPackJoiners = 8;      // a pull with this many joiners expected or more is a big pack
// Scripted events (ritual, hold; docs/play/dungeon-run-leader.md "Events").
constexpr float kHoldReturnYd = 8.0f;             // out of a fight, a lead bot farther than this from the hold spot walks back
// ZF r12-19z29: the engage order sent the tank alone down the stairs while the four ranged stood on their
// places 36-60 yd off, and the freed crew died to the trolls with only the tank beside them. The Uldaman
// Keepers and Archaedas holds send the tank 15-20 yd after a keeper while the ranged keep their places
// (that is what the places are for), so the tank's distance from the spot alone never releases them: a
// living ranged bot (the healer too) must be this far from the living tank, out of heal and cast range,
// on the ground (the 3D distance less the height), for kHoldFollowTicks fight ticks in a row
// (ReleaseHoldStands).
constexpr float kHoldFollowRangedYd = 30.0f;
constexpr std::uint32_t kHoldFollowTicks = 2;
constexpr std::uint32_t kHoldIdlePullMs = 10000;  // an awake event unit that has not come for this long is pulled
constexpr std::uint32_t kEventStartMs = 30000;    // an encounter hold whose data does not leave "not started" in this time failed
// A hold done by its units (no done flag) whose units were all dead before it began (no unit seen alive in the radius, no
// awake one, corpses in sight): it ends this long after the first look at the spot, on a tick with the spot in sight (HoldStep; ZF r15-19z33: Bly's crew
// killed by the troll waves, "4 of 4 down" held the whole 300 s).
constexpr std::uint32_t kHoldAllDeadMs = 10000;
constexpr float kGossipWalkYd = 30.0f;            // a gossip step walks to its point first when the lead bot is farther than this
constexpr std::uint32_t kRitualNoReplyMs = 5000;  // a ritual whose bots are free again with no answer this long: pause
constexpr std::uint32_t kEventResetWaitMs = 60000; // before a ritual: an encounter still in progress or failed (a boss
                                                   // walking home) is waited for this long, then the ritual goes out
// Placing the party on a hold spot (HoldStands). Uldaman run 3 (19z12): the
// priest and the mage stood 12-20 yd from the tank on the Keepers' spot and a
// keeper killed them there while the tank held "stay". The ranged bots (healer
// too) stand behind the tank, on the side away from the event's mobs, in a
// row across that line; the others follow the lead bot as before.
constexpr float kHoldBehindYd = 6.0f;             // the ranged row this far behind the hold spot ...
constexpr float kHoldSpreadYd = 3.0f;             // ... its bots this far apart across the line ...
constexpr float kHoldGatherYd = 10.0f;            // ... and none farther than this from the spot (nor out of the radius)
constexpr float kHoldMobSideYd = 5.0f;            // the event's mobs' middle this far off the spot gives "away"
                                                  // (the four Stone Keepers ring the spot: 3.4 yd, no side)
// Guarding the party from a stand point (GuardTarget): the run leader holds the
// tank on "stay" at its pullback or walk-back point, where it does not go after
// a mob on someone else. Uldaman rerun 2026-10-17, keepers door pull 100 and the
// walk back at -10 337: adds killed the healer and the DPS 13-37 yd away while
// the tank stood 50 s at 53 % with nothing on it.
constexpr float kGuardYd = 40.0f;                 // party bots this close to the tank, hit by a mob, are guarded
constexpr std::uint32_t kGuardWaitMs = 4000;      // ... once mobs were hitting them (in melee reach, or standing and
                                                  // casting: not while a pullback's mobs still run in) for this long
constexpr std::uint32_t kGuardHealerWaitMs = 0;   // ... a mob hitting a healer at once: at pull 100 two Maulers
                                                  // took the healer from 73 % to dead in 4 s
constexpr std::uint32_t kGuardRepeatMs = 2000;    // a further pick-up no sooner than this after the last one
constexpr std::uint32_t kGuardOrdersPerFight = 3; // pick-up orders one fight may send (per budget: see below)
// The budget above starts over when a unit that was not in the fight joins it
// once the budget is spent (a summon lands, a mob crosses into the party).
// Deadmines r12-19z29 steps 90, 118 and 125: the three pick-ups went in the
// first seconds, then Remote-Controlled Golem summons, the Overseer, the
// Harvester and Gilnid joined and beat the healer 25-60 s while the tank sent
// nothing (13 of the run's 14 deaths). Bounded so that it cannot spam: once a
// refill happened, one unit gets at most kGuardOrdersPerUnit orders a fight
// (pick-ups and engages together, those before the refill counted; a unit at
// the cap is passed over for the next one; before any refill one unit may take
// the whole budget), and a fight at most kGuardOrdersFightCap in all.
// kGuardRepeatMs spaces every one of them.
constexpr std::uint32_t kGuardOrdersPerUnit = 2;
constexpr std::uint32_t kGuardOrdersFightCap = 12;
// The kill_first focus (PickKillFirst) sends every bot, the tank too, at its
// unit. Zul'Farrak r5-19z23 step 27: the camp's witch doctors aggroed on the
// tank from 20 yd (wipe 1: the focus took it into the camp, next to the north
// group, 13 mobs at once) and from 43 yd (wipe 2: a pet had pulled them). 15 yd
// is the stand's melee and close-caster reach, well inside the 20 yd at which
// the camp woke; a unit farther off is left to come to the tank (r3 lived that way).
constexpr float kKillFirstMaxYd = 15.0f;
// A kill_first focus every bot refused (OrderRefused: "No bot of the group
// accepted the focus order"; AttackAction refuses an evading target) leaves
// its unit out of the pick for this long, and the focus goes to the next one
// or to none. Deadmines r12-19z29 step 118, t 2210.9: the focused 622 3569 had
// turned to flee, all three living bots refused, and the focus stayed on it
// with no second order while it lived. After the wait it may be focused again.
constexpr std::uint32_t kFocusRefusedIgnoreMs = 8000;
// The healer's stand for a pull (the run leader's commander context): this far
// behind the lead bot's spot, on the line from the pull target through that spot
// (Uldaman run 4: the priest stood level with or ahead of the tank and died of
// the camp's first hits). Within heal range of the tank (40 yd), clear of the
// pack's first swings. The glue checks the point on the ground and the navmesh
// and falls back to half the distance, then to the spot itself.
constexpr float kHealerBackYd = 9.0f;
inline bool HealerBackPoint(float leadX, float leadY, float targetX, float targetY, float back, float& x, float& y)
{
    float const dx = leadX - targetX, dy = leadY - targetY;
    float const len = std::sqrt(dx * dx + dy * dy);
    if (len < 1.0f)
        return false; // standing on the target: no side is behind
    x = leadX + dx / len * back;
    y = leadY + dy / len * back;
    return true;
}

// After the fight the tank walks back to the spot it stood on at the first
// pick-up, not to the run's anchor (the last walk's end, the leg's start
// point when the leg's pulls have no move step): Uldaman run 3 (19z12,
// tests/uldaman-events-3) walked the tank 90 yd back to the baelog entrance
// after every guarded pull. Farther than this from that spot once the fight
// is over, it stays where the fight left it.
constexpr float kGuardReturnMaxYd = kGuardYd;
// A party member coming back with attackers runs on to the party (RunBack; the
// owner's rule of 2026-10-17). Uldaman 2026-10-17 (tests/uldaman-events-2):
// the revived priest and mage stopped to fight a scorpid at -174 311, 73-76 yd
// from the tank, and the regroup gave up with a bot 107.9 yd away; the rogues
// at the tank joined in by themselves once the mage came within 50 yd (the
// bots' own group help reaches SightDistance, 60 yd).
constexpr float kRunBackMaxYd = 120.0f;           // a member with attackers this far from the lead bot runs back
constexpr float kRunBackArriveYd = 12.0f;         // ... until it is this close: then it fights with the party
constexpr std::uint32_t kRunBackCapMs = 90000;    // one run back ends after this long
// A fallen bot revived far from the lead bot is summoned to it (SummonRevived)
// instead of walking the dungeon back. Uldaman run 3 (19z12, 10:45-10:54): the
// priest and the mage revived at the entrance (-229 46), ran about 270 yd back,
// died twice to Shrike Bats at -52..-80 359..375 (4 extra deaths), and the
// regroup paused at its 60 s gather ceiling with a bot 273.7 yd out; a `cmd
// summon` sent by hand right after each was alive, before the bats, had both at
// the altar within 20 s, while one sent in the bats' fight did nothing.
constexpr std::uint32_t kReviveSummonWaitMs = 10000; // a revived bot's summon (3 s) not at the lead bot after this
                                                     // long failed: it walks back
constexpr float kRegroupWalkYdPerS = 3.0f;        // the regroup's gather ceiling allows this pace (fights on the way)
constexpr std::uint32_t kRegroupGatherMaxMs = 180000; // ... from the farthest walker, up to this long

// Rest gate: it starts when someone is below a "start" line after a fight or
// before a step, and ends when everybody is at the "end" line (or at kRestCapMs).
struct RestLines
{
    std::uint32_t startHealth = 80, startHealerMana = 50, startMana = 35;
    std::uint32_t endHealth = 90, endHealerMana = 80, endMana = 60;
};

enum class State : std::uint8_t { Running, Paused, Done };
enum class Phase : std::uint8_t { Begin, Gather, Walk, AwaitPull, AwaitTask, Rest, Wait, Hold };

// A loot step's free-for-all detour (AiPlayerbot.DungeonRunLootFfa, TaskStep): Idle; Ordered: `lootmethod ffa`
// went out; Looting: the loot order is out under free-for-all; Restoring: the saved method was ordered back.
enum class LootFfa : std::uint8_t { Idle, Ordered, Looting, Restoring };

inline const char* StateName(State s)
{
    switch (s)
    {
        case State::Running: return "running";
        case State::Paused: return "paused";
        default: return "done";
    }
}

inline const char* PhaseName(Phase p)
{
    switch (p)
    {
        case Phase::Begin: return "begin";
        case Phase::Gather: return "gather";
        case Phase::Walk: return "walk";
        case Phase::AwaitPull: return "pull";
        case Phase::AwaitTask: return "task";
        case Phase::Rest: return "rest";
        case Phase::Hold: return "hold";
        default: return "wait";
    }
}

// A creature near the step, as the glue saw it this tick.
struct UnitFact
{
    std::uint64_t guid = 0;
    std::uint32_t entry = 0;
    float distLead = 0.0f;
    float x = 0.0f, y = 0.0f; // hold units and patrollers: where it stands (HoldStands: the side the event comes from)
    float z = 0.0f;           // ... patrollers only (OwnedByLaterStep)
    bool hostile = true;     // not friendly to the party (hostile or neutral): it may be pulled
    bool stealthed = false;
    bool evading = false;    // running home: not pulled this tick
    bool polymorphed = false; // a mage's Polymorph aura is on it (kPolymorphSpells)
    // Kill-first candidates (Facts::killFirst): in combat with a party member,
    // or summoned / owned (a healing ward) by a unit that is.
    bool engaged = false;
    std::uint64_t owner = 0;
    bool ownerEngaged = false;
    bool victimHealer = false; // its victim is a healer (Facts::partyAttackers, and a hold's units)
    bool victimLead = false;   // a hold's units: its victim is the lead bot (the tank holds it)
    // Facts::partyAttackers only:
    bool hitting = false;      // it hits its victim now: in melee reach of it, or standing and casting (not running in)
    bool victimPet = false;    // its victim is a party bot's pet (the lead bot's own too), not a bot (victimHealer false)
    float distSpot = -1.0f;    // ... during a hold: its distance from the hold spot (-1: not read)
    // Pull step units only, when Facts::joinerCeiling is set: how many other
    // creatures an estimate (ai::EstimateAggro) says come if this one is pulled.
    std::uint32_t joiners = 0;
    bool joinersPatrol = false; // ... and a lone patrol alone puts that over the ceiling
    bool joinersRead = false;   // the count above is this step's (ReadPullJoiners judged the unit; a held
                                // or off-index unit keeps its earlier read)
    // Pull step and hop units, when Facts::pullStand is set: the lead bot sees
    // it now (Unit::IsWithinLOSInMap).
    bool losLead = false;
};

// The mage's Polymorph (all ranks and forms): the aura the run waits for after
// a `cc moon`, and the spell a mage must know to be the run's crowd control.
constexpr std::uint32_t kPolymorphSpells[] = {118, 12824, 12825, 12826, 28270, 28271, 28272};
// In the fight a `cc moon` must show its aura within this much of the mage's
// own fight time (19z5: the mage waits for the tank's threat, walks into range
// and casts 1.5 s; Polymorphs landed 7-8 s into its fight), else once more,
// then noted.
constexpr std::uint32_t kCcLandMs = 8000;
// The aggro estimate's rest waits (Facts::joinerCeiling) a step: after that a pull goes out without one.
constexpr std::uint32_t kMaxJoinerRests = 3;

// A living bot of the party other than the lead bot.
struct MemberFact
{
    std::string name;
    float yd = 0.0f;        // from the lead bot (1000 on another map)
    bool polymorph = false; // a mage that knows Polymorph (kPolymorphSpells): the run's crowd control
    bool inCombat = false;
    float dz = 0.0f;        // height difference to the lead bot (another floor)
    std::uint32_t attackers = 0; // units attacking it (the core's attacker set of that bot, no scan)
    float x = 0.0f, y = 0.0f, z = 0.0f; // where it stands (the stand-still watch: UpdateStill)
    bool ranged = false;    // a ranged bot or a healer: it stands behind the tank in a hold (HoldStands)
    // With Facts::directJoinRead: the glue's own direct-step test for this bot (the one `cmd move ... direct`
    // refuses too-far on: a complete navmesh path of at most kDirectMaxGatherPath yd onto the lead bot's spot,
    // or within kDirectNearLead yd of it on its floor in its sight, or its own line to the point). Unread: true.
    bool withLead = true;
};

// What the glue reads once a tick for the run's current step.
struct Facts
{
    // The party (the commander's bound bots, the lead bot included).
    std::uint32_t total = 0, alive = 0, dead = 0;
    bool leadAlive = true;
    bool anyInCombat = false;
    bool leadInCombat = false;          // the lead bot itself is in a fight
    // anyInCombat, yet no living hostile is engaged with a bot (or its pet): a
    // combat flag left standing after the last kill, or a non-attackable add.
    // The fight timer does not run on it (Uldaman run 4, archaedas wait_object).
    bool staleCombat = false;
    float leadX = 0.0f, leadY = 0.0f, leadZ = 0.0f;
    bool leadMoving = false;            // the lead bot is moving now (Unit::IsMoving): an exact point's proof waits
    float maxMemberYd = 0.0f;           // the living bot farthest from the lead bot
    std::vector<MemberFact> members;    // the living bots, the lead bot too (at 0 yd)
    // The glue read MemberFact::withLead for every member this tick (only while DirectJoinWanted: a direct step
    // is the current one or the walk back's). The gate before a direct step then asks that test, not only the
    // ground distance (kGatherYd), so a bot on another floor within it never passes the gate and fails the step.
    bool directJoinRead = false;
    // The glue reads that test at all (the module's glue: always; the policy tests that do not set it, and any
    // legacy caller, keep the ground rule alone when it is not read).
    bool directJoinSupported = false;
    std::vector<std::string> deadNames; // the dead bots (ghosts too), the lead bot too
    // The lead bot has a navmesh path back to the run's anchor (Run::anchor*).
    // Read lazily: only on a tick after the policy asked for it
    // (Run::anchorPathWanted, set when a gather needs the answer).
    bool anchorPathRead = false;
    bool anchorReachable = true;
    // A walk back straight to its last point: the navmesh path the glue read
    // for the next hop passes an uncleared pull or boss group
    // (PathCrossesUnclearedPack); the shortcut is dropped for the replay.
    bool directPathCrossesPack = false;
    // In a fight, when the plan has kill_first: its living, not friendly units
    // from the bots' own attacker lists (a summoned healing ward included).
    std::vector<UnitFact> killFirst;
    std::uint32_t minHealthPct = 100;   // living bots
    std::uint32_t minHealerManaPct = 100;
    std::uint32_t minManaPct = 100;     // mana users that are not healers
    std::uint32_t eating = 0;           // living bots with a food or drink aura up (seated: standing ends it)
    bool rezzer = false;                // a living bot out of combat that can resurrect (priest, paladin, shaman)
    // AiPlayerbot.DungeonRunRezHold: the glue read the roster for the rez hold
    // (RezHoldPolicy.h) and the regroup asks it instead of the fixed kPartyRezWaitMs
    // wait on `rezzer`. Off (the default): rezHold false, rezMembers empty, the
    // old rule runs byte for byte.
    bool rezHold = false;
    std::vector<TortoiseBots::RezHold::Member> rezMembers; // every bot of the party, dead ones too
    bool rezInProgress = false;         // ... a party rezzer is casting a resurrection, or one waits to be accepted
    // The step.
    std::uint32_t team = 0;             // the party's team: ai::kPlanFlagAlliance or kPlanFlagHorde (0 unknown)
    bool seen = false;                  // the lead bot stands within kSeeYd of the step's point
    // pull/boss not seen yet: the next approach hop, a point of the lead bot's
    // navmesh path toward the step's point (kApproachHopYd along it at most,
    // kApproachYd short of the point). approachKnown false: no path.
    bool approachKnown = false;
    // The glue asked the navmesh for that hop this tick. It does not while a
    // walk or a regroup is under way, so on the tick such a walk ends no hop
    // is known yet without that meaning "no path" (Uldaman run 3, 19z12: baelog
    // pull 7 and the descent move paused with "no walkable path" on the tick
    // the walk back to the stand point ended, and again on the tick a resume's
    // regroup ended; both navmesh paths are complete).
    bool approachRead = true;
    float approachX = 0.0f, approachY = 0.0f, approachZ = 0.0f;
    // Read with that hop (and for a walk back's move point in sight with a path
    // of its own): the lead bot's navmesh path toward the point is partial and
    // ends within kGoodPathMinYd of it, short of the point, or is no path (LeadOnIsland:
    // IslandEscape), or it gets somewhere (LeadPathGood: the lead bot's spot is the last good one).
    bool leadIsland = false;
    bool leadPathGood = false;
    // ... and with leadIsland only: the path to the run's last good lead spot
    // (within kGoodSpotProbeYd and kEscapeMaxVertical) is incomplete or no path:
    // a known spot of the main mesh the lead bot cannot reach (GoodSpotProbeWanted).
    bool goodSpotCut = false;
    std::vector<UnitFact> units;        // pull/boss: living units of the step's entries within its radius
    // pull (not boss): living units of the step's entries that walk a
    // waypoint patrol (the core's default movement type), outside its radius
    // and within kPatrolSeeYd beyond it, none a later step owns
    // (OwnedByLaterStep). distSpot: from the step's point; x, y, z: where it is.
    std::vector<UnitFact> patrollers;
    std::vector<UnitFact> hopUnits;     // move: living hostile units within kHopPullYd of the point, not owned by a later step
    // boss: a dead unit (corpse) of the boss's entry within the step's radius
    // plus kSeeYd of its point (a pullback drags him toward the party), or
    // within kSeeYd of the lead bot; bossAlive: he stands in the step's units.
    bool bossDeadNearSpot = false;
    bool bossDeadNearLead = false;
    // ... and that corpse's guid and place (the nearest to the step's point when
    // there are several; 0: none seen), so a later `loot` step finds it where it
    // lies, not where the plan says he spawns (Revelosh dies 50-60 yd from his spot).
    std::uint64_t bossCorpseGuid = 0;
    float bossCorpseX = 0.0f, bossCorpseY = 0.0f, bossCorpseZ = 0.0f;
    int objectState = -1;               // wait_object, hold done by object: 0 ready, 1 active, 2 active_alt; -1 not found
    // hold (units: the living units of its `entries` within its radius, the
    // friendly, still dormant ones too):
    std::uint32_t eventDown = 0;        // dead units of its `entries` within its radius
    long long eventData = -1;           // done by instance data: the value now; -1 the map has no instance data
    std::uint32_t hostilesNear = 0;     // wait_clear: living hostiles within the radius
    std::string carrier;                // the bot that does object and item steps (the lead bot)
    // open / use_item: the step's inputs (its item, and `param` when set) are
    // split between bots, none carries them all: who holds what (PickItemCarrier).
    std::string itemsSplit;
    // use_on: the act of the carrier's task had started when its answer came (the cast began: an in-combat answer after it
    // is no sign the item did not take); the object's state differed from the one it had when the use began (ObjectEffectOf
    // at the answer); a unit of the boss row right after the use_on is in sight around the lead bot, alive or dead.
    bool taskActStarted = false;
    bool objectReacted = false;
    bool summonSeen = false;
    bool carrierHasItem = false;        // the step's `item` is in the carrier's bags
    bool carrierHasEntryItem = false;   // use_item: the item to use is in the bags
    bool carrierBusy = false;           // a commander task (loot/open/use) runs for the carrier
    // loot: read for the step's item, on the tick of an order and after a refusal.
    bool partyHasItem = false;          // some party bot (dead ones too) carries the step's `item`
    std::string itemHolder;             // ... the first such bot
    // ... and only after a refusal, from the remembered corpse:
    std::string lootTurn;               // the party bot whose round-robin turn the corpse is ("": none, not a bot)
    bool corpseChecked = false;         // the remembered corpse (by guid) was looked at
    bool corpseHasItem = false;         // ... and is in the map, still holding the step's item (not looted)
    int taskReply = 0;                  // the carrier's task answer since the run's order: 0 none, 1 ACK, 2 ERR
    std::string taskText;
    // AiPlayerbot.DungeonRunLootFfa (default 0): read every tick, only a loot step looks at them.
    bool lootFfaOn = false;             // the key is on
    int lootMethod = -1;                // the group's loot method (RemoteCommander::kLoot*), -1: no group
    bool leadLeadsGroup = false;        // the lead bot leads the group (`cmd lootmethod` needs it)
    // Guarding the party (GuardTarget), read in fights only:
    bool leadIsTank = false;            // the lead bot is a tank
    bool leadStaying = false;           // the lead bot holds "stay" (the run's move to a stand or walk-back point)
    bool leadStayKnown = false;         // ... and its stay point (set by the pullback or the run's move) on this map
    float leadStayX = 0.0f, leadStayY = 0.0f, leadStayZ = 0.0f;
    bool leadHit = false;               // a living mob with the lead bot as its victim is in melee reach of it
                                        // (a caster that turns on the tank from 30 yd is not the tank holding it)
    // ... and the nearest such mob (EngageTarget: the tank on "stay" fights nothing
    // until an attack order takes it off the stay); guid, entry, distLead, polymorphed,
    // evading and hitting set. leadHasAttacker is false when no mob is on the lead bot.
    UnitFact leadAttacker;
    bool leadHasAttacker = false;
    // Living, not friendly mobs whose victim is a party bot other than the lead
    // bot within kGuardYd of it; distLead from the lead bot, `hitting` and
    // `victimHealer` set.
    std::vector<UnitFact> partyAttackers;
    // The pull in flight (CheckReach), read in fights only: the lead bot's own
    // pull command is still reaching its target (the target has not come for
    // it, it has not turned back), and how many living, not friendly mobs have
    // a party bot as their victim (any of them, the lead bot too).
    bool leadReaching = false;
    std::uint32_t reachAggro = 0;
    // AiPlayerbot.DungeonRunAggroEstimate: the ceiling of expected joiners
    // (AiPlayerbot.DungeonRunAggroCeiling) while the estimate is on, 0 = off.
    // Off, the units carry no estimate and no pull ever waits for it.
    std::uint32_t joinerCeiling = 0;
    // AiPlayerbot.DungeonRunPullStand (HoldForShot): the key is on. Off (the
    // default) nothing below is read and the pull goes out as before, byte for byte.
    bool pullStand = false;
    // The lead bot's ranged weapon shot range (its PullStrategy::GetRange(),
    // pull action "shoot" only); 0 for a spell pull, a body pull ("reach pull":
    // no ranged weapon, skill, spell or ammo) or when it could not be read. At or
    // under kShotMarginYd it keeps the old order too (PullStandOn).
    float leadShotYd = 0.0f;
    // The firing stand the policy asked for (Run::shotStandFor), read this
    // tick: the lead bot's navmesh path to that unit walked back from the unit
    // end to the first point within leadShotYd - kShotMarginYd that sees it
    // (ShotStandOnPath). shotStandGuid is the unit it was read for (0: none
    // read this tick); shotStandKnown false: no such point, or the unit is gone.
    std::uint64_t shotStandGuid = 0;
    bool shotStandKnown = false;
    float shotStandX = 0.0f, shotStandY = 0.0f, shotStandZ = 0.0f;
    float shotStandYd = 0.0f;           // ... the stand's distance from the unit (centre to centre); it sees the unit
};

// A bot's own stand point in a hold (HoldStands): the glue walks it there and
// keeps it on "stay" until the list changes.
struct HoldStand
{
    std::string name;
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

// Where a boss's corpse lay when the boss step saw him down.
struct CorpseMark
{
    std::uint64_t guid = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

struct Run
{
    const DungeonPlan* plan = nullptr;
    std::size_t step = 0;
    State state = State::Running;
    Phase phase = Phase::Begin;
    std::string note;        // what it does now, or why it paused

    std::uint32_t phaseMs = 0;
    std::uint32_t stepIdleMs = 0;
    bool restDue = true;     // check the rest lines before the step
    int regroupDead = -1;    // the dead when the regroup was first read (-1 not yet): more than that is a new wipe
    std::uint32_t eatWaitMs = 0; // meal wait used since the last fight (kRestCapMs per fight: it cannot loop)
    bool restForEating = false; // the rest phase is the meal wait: it also ends when nobody eats any more
    bool stepRested = false; // a `rest` step has had its gate
    bool orderSent = false;  // task steps: the order went out
    std::uint32_t taskResends = 0; // task steps: orders sent again after an interrupted cast or an in-combat answer
    std::set<std::size_t> useOnRang; // use_on steps whose order was ACKed (or went unanswered) this run: never sent again; ResetStep keeps it
    long long dataAtOrder = -1; // a ritual: the hold's instance data when the order went out
    bool walkedThrough = false;
    // The target of the approach hop the walk goes to (hopWalk), to tell it from
    // another walk of the step (the stealth walk through a pack's centre).
    std::unordered_set<std::uint64_t> hopWaiveUnits; // the gather is left out for a pull of these units only (the step's units when a hop was abandoned)
    bool hopWalk = false;
    float hopX = 0.0f, hopY = 0.0f, hopZ = 0.0f;
    std::uint32_t approachHops = 0;     // approach hops since the party last got nearer to the step's point
    float approachBestYd = 0.0f;        // the nearest the lead bot has been to it (0: not set yet)
    // A move whose point the navmesh cannot reach walks to the nearest
    // reachable point near it instead (UseDetour).
    bool detour = false;
    float detourX = 0.0f, detourY = 0.0f, detourZ = 0.0f;
    bool fighting = false;
    std::uint32_t fightMs = 0;
    std::uint32_t staleMs = 0;          // all the time in this fight the combat flag stood with nothing hostile engaged (Facts::staleCombat)
    std::uint32_t staleRunMs = 0;       // ... and the time it has stood so without a break
    std::uint32_t restRepeatMs = 0;
    // The last point the run itself walked the party to (a move, an approach
    // hop, a stand point): a gather goes back there when a fight left the lead
    // bot off it (another floor, no path back), not where the lead bot stands.
    bool anchorKnown = false;
    float anchorX = 0.0f, anchorY = 0.0f, anchorZ = 0.0f;
    bool anchorPathWanted = false; // a gather needs the lead bot's path back read (next tick)
    // Pull and boss steps left without being cleared (skipped, out of reach):
    // a direct walk back keeps clear of them too.
    std::unordered_set<std::size_t> notCleared;
    // Walk.
    float walkX = 0.0f, walkY = 0.0f, walkZ = 0.0f;
    std::uint32_t walkTries = 0;
    std::uint32_t walkStillMs = 0;
    float walkBestYd = 0.0f;
    // An exact waypoint's tighten (ExactWalkTo): on once the walk to the point
    // ended (3 yd, or a stall within 6 yd); it brings the lead bot onto the
    // point and wants the stand-still proof before the step is done.
    bool exactOn = false;
    std::uint32_t exactMs = 0;          // the tighten's time (out of combat: the fight code runs instead)
    std::uint32_t exactRounds = 0;      // moves onto the point it sent
    std::uint32_t exactWaitMs = 0;      // the lead bot standing (not on the point) since the last order
    std::uint8_t exactStage = 0;        // 0 judging, 1 walking back to exactBack*, 2 walking onto the point
    float exactBackX = 0.0f, exactBackY = 0.0f, exactBackZ = 0.0f;
    bool exactLastSet = false;          // the lead bot's spot on the tighten's last tick (moving between looks)
    float exactLastX = 0.0f, exactLastY = 0.0f, exactLastZ = 0.0f;
    bool exactAnchorSet = false;        // the proof: one spot within kExactArriveYd of the point ...
    float exactAnchorX = 0.0f, exactAnchorY = 0.0f, exactAnchorZ = 0.0f;
    std::uint32_t exactStillMs = 0;     // ... stood on this long
    // The exact step whose proof stands (kept across NextStep: the direct step
    // after it reads it; cleared by a pause). npos: none.
    std::size_t exactProved = static_cast<std::size_t>(-1);
    // Pulls.
    std::uint64_t pullGuid = 0;
    std::uint32_t pulls = 0;
    std::unordered_map<std::uint64_t, std::uint32_t> pullTries;
    std::unordered_set<std::uint64_t> passed;
    bool pullHop = false;           // the pull out is a hop pull (a move's or the walk back's)
    std::size_t openAtLastPull = 0;
    std::uint32_t fruitlessPulls = 0;
    std::uint32_t bossWaitMs = 0;   // a boss step: time at the spot with the boss neither alive nor dead in sight
    bool bossSeenAlive = false;     // a boss step: the boss was among the step's living units
    std::uint32_t patrolWaitMs = 0; // a pull step: time spent waiting for a patroller outside the radius (WaitForPatroller)
    std::unordered_set<std::uint64_t> patrolSeen; // ... the patrollers this step saw (a pass of one is remembered run-wide)
    // A big pack's stand walk (BigPackWalkGate): the walk's unit, stand and
    // joiner count; the hold for the party, its time, and whether its ceiling
    // was spent (the walk then goes on without the gate).
    bool bigWalk = false;
    std::uint64_t bigWalkGuid = 0;
    float bigWalkX = 0.0f, bigWalkY = 0.0f, bigWalkZ = 0.0f;
    std::uint32_t bigJoiners = 0;
    bool bigHold = false;
    std::uint32_t bigHoldMs = 0;
    bool bigHoldSpent = false;
    std::string bigHoldNote;
    // The last joiner count the glue read per unit this step: a unit held for
    // its rest is not judged again (joinersRead false), the count stands.
    std::unordered_map<std::uint64_t, std::uint32_t> joinerSeen;
    std::uint32_t walkBackPulls = 0; // pulls on the way of the walk back (not counted in `pulls`)
    // Run-wide (kept across steps).
    std::unordered_set<std::uint64_t> unreachable;   // units given up on: never pulled again this run
    // Units that never came (PLAN-ZF-NOFIGHT; Zul'Farrak r3/r6, the island
    // Shadowhunter 81610): hop units only (a move's or the walk back's) whose
    // one pull drew no fight within kPullNoFightMs, or that the tank could not
    // shoot (PullUnit). Read only by the hop picks (PickPull `hop`), so a pull,
    // boss or hold step's own units are never left out by it. Cleared by every
    // regroup (BeginRegroup: a resume, a wipe recovery); a unit in it that turns
    // up as an attacker of the party is taken out at once (ForgetUnitsThatCame).
    std::unordered_set<std::uint64_t> neverCame;
    // Patrollers given up on (WaitForPatroller): the wait ran out, or a pull
    // step passed it after its pulls drew no fight. Run-wide, so a later step
    // listing the same entry does not wait for it again; cleared like neverCame
    // by every regroup (a resume, a wipe recovery).
    std::unordered_set<std::uint64_t> patrolGaveUp;
    // loot: the bot the order goes to ("": the carrier), the bots a refusal on the
    // loot rules has been tried with, and the refusal's pause text while the
    // next bot is tried.
    std::string taskBot;
    std::vector<std::string> lootTried;
    std::string lootRefusal;
    bool lootRights = false;            // the refusal was the loot rules (another member's turn ...)
    // The free-for-all detour of a loot step (LootFfaStart, LootFfaStep). lootFfaUsed: once per step (ResetStep).
    // lootFfaLive and lootRestore are run-wide: the group's method may be ffa because of this run, and was
    // lootRestore (RemoteCommander::kLoot*) before. A running tick with lootFfaLive and no detour in progress
    // orders it back (LootFfaRestoreTick); the glue does so whenever the run is not running.
    LootFfa lootFfa = LootFfa::Idle;
    bool lootFfaUsed = false;
    bool lootFfaLive = false;
    int lootRestore = -1;
    std::uint32_t lootRestoreTries = 0; // restore orders sent by the running-tick restore (LootFfaRestoreTick)
    std::uint32_t lootFfaMs = 0;        // time since the ffa order went out (while lootFfaLive)
    std::unordered_set<std::uint32_t> missing;       // items the run did not get (a skipped loot or use)
    std::unordered_map<std::uint32_t, CorpseMark> corpses; // boss entry -> the corpse seen when that boss step ended "boss down"
    std::uint32_t pendingMissing = 0;                 // the item a paused step failed to get (missing on skip)
    std::size_t skippedOpenStep = static_cast<std::size_t>(-1); // an `open` left out: the wait after it goes too
    // Regroup after a resume (revive the dead, gather), before the step.
    bool regroup = false;
    bool reviveSent = false;
    bool regroupGatherSent = false;
    std::uint32_t regroupMs = 0;
    // The rez hold (AiPlayerbot.DungeonRunRezHold, RezHoldPolicy.h): the time a resurrector could act
    // (restarted while one drinks), and the regroup clock's reading when the hold gave up and the revive
    // went out (the 240 s revive budget runs from there). Both stay 0 with the key off.
    std::uint32_t rezAbleMs = 0;
    std::uint32_t reviveBaseMs = 0;
    // The rez hold's nudge: the time the hold went on with a rezzer able and no cast under way, and whether the
    // lead bot was sent onto the body already (once per regroup). Both stay 0/false with the key off.
    std::uint32_t rezHoldMs = 0;
    bool rezNudged = false;
    // The order of that nudge, kept to tell its refusal (OrderRefused) from any other, and whether the module
    // answered it with a path refusal (the body cannot be walked to): the hold is then over and the revive goes out.
    std::string rezNudgeOrder;
    bool rezBodyUnreachable = false;
    void RestartRegroupClock()
    {
        regroupMs = 0;
        reviveBaseMs = 0;
    }
    // Gathers (a move to the lead bot's own spot): the one sent this tick, so a
    // refusal of it is known; refused kGatherTries times, it is left out.
    std::string gatherOrder;
    std::uint32_t gatherRefusals = 0;
    bool gatherWaived = false;
    std::unordered_set<std::string> summoned; // stragglers this step summoned already (once each)
    std::uint32_t gatherComingMs = 0; // the gather ceilings this step let a straggler that keeps coming pass (GatherCeiling)
    std::string summonDue; // a follower a direct step left behind (refused too-far): summoned on the next tick
    std::string summonWait; // ... the summon sent: the step waits for that bot at the lead bot (kSummonWaitMs at most)
    std::uint32_t summonWaitMs = 0;
    // Every summon of a bot this step (MarkSummoned): how many, and the run clock (runMs) of the last one. A
    // direct step's straggler may be summoned again by this record (DirectSummonAllowed), not once by `summoned`.
    struct StepSummon
    {
        std::uint32_t count = 0;
        std::uint64_t lastMs = 0;
    };
    std::unordered_map<std::string, StepSummon> stepSummons;
    std::unordered_map<std::string, std::uint64_t> deadSeenMs; // the run clock a bot was last seen dead (NoteDeadSeen), the whole run
    bool directRefusedOn = false;     // this step's direct step was refused too-far (or its gather hit the ceiling) ...
    std::uint64_t directRefusedMs = 0; // ... first at this run clock (DirectPauseDue)
    std::size_t directRefusedIdx = 0;  // ... at this plan point (DirectTroublePoint)
    std::uint32_t directJoinHolds = 0; // ticks a direct order was held for facts without the glue's test (kDirectJoinHolds)
    std::string directWaitFor;        // a refused direct step's straggler the run waits for (no summon allowed yet)
    std::uint32_t directWaitMs = 0;   // ... this long so far (kDirectRetryMs, then the step goes out again)
    std::unordered_map<std::string, float> memberYd; // each bot's distance to the lead at the gather's last sample
    // Revived bots summoned to the lead bot (SummonRevived), not yet with it:
    // the time since the summon went out. Not counted against the regroup's
    // gather ceiling; one that fails (refused, or not there in
    // kReviveSummonWaitMs) leaves the list and walks back.
    std::unordered_map<std::string, std::uint32_t> reviveSummons;
    std::uint32_t regroupGatherCapMs = kRegroupGatherMs; // the regroup's gather ceiling (RegroupGatherCap)
    // Each living bot's stand-still watch (UpdateStill, every tick): where it
    // stood when it last moved kStillYd, and how long it has stayed there.
    struct StillState
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        std::uint32_t ms = 0;
    };
    std::unordered_map<std::string, StillState> still;
    // The lead bot dying alone in a fight and coming back to life far from
    // where it fell (its own release and corpse run: the instance entrance)
    // while the others won the fight deep inside (LeadRevivedAway).
    bool leadSeen = false;
    float leadLastX = 0.0f, leadLastY = 0.0f, leadLastZ = 0.0f; // the living lead bot's last spot
    bool leadDied = false;
    // The owner's rule of 2026-10-17, "Party should wait if a party member
    // dies until it is back": every bot seen dead since the party was last
    // together. The run starts nothing new until each is alive and within
    // kGatherYd of the lead bot again (WaitForFallen).
    std::unordered_set<std::string> waitFor;
    // The living party held where it won the fight while the lead bot (and
    // the bots revived with it at the entrance) walk the plan's way back to
    // it (LeadRevivedAway): the glue keeps each named bot on stay at its spot,
    // and the run leaves them out of its gathers and summons (HoldView). A bot
    // drops out once the lead bot is within kGatherYd of it; the list ends with
    // the walk back, on a pause and with the step.
    std::vector<std::string> hold;
    // A regroup for a dead lead bot (WaitForFallen): once the lead bot is up
    // again more than kSeeYd from where it fell, the hold is filled before the
    // regroup gathers (19z11 03:24: the fight ended with the lead bot a ghost).
    bool holdDue = false;
    float leadDeathX = 0.0f, leadDeathY = 0.0f, leadDeathZ = 0.0f;
    // Crowd control and kill-first (the plan's cc_priority / cc_always / kill_first).
    std::unordered_set<std::uint64_t> ccSent; // units this step put a `cc moon` on (once each)
    std::uint64_t focusGuid = 0;              // the kill-first unit focused in this fight
    // Kill-first units whose focus was refused (kFocusRefusedIgnoreMs): unit ->
    // runMs until which PickKillFirst passes it over. focusUnstayed: such a
    // refusal this fight (CommanderFocus takes every bot off "stay" before it
    // asks them), so the guard keeps watching though focusGuid is cleared.
    std::unordered_map<std::uint64_t, std::uint64_t> focusRefused;
    bool focusUnstayed = false;
    // Aggro estimate (Facts::joinerCeiling): a pull expected to bring more
    // joiners than the ceiling waits once for the party to be rested.
    std::unordered_set<std::uint64_t> joinerHeld; // units already held once this step
    bool joinerRest = false;                  // the rest check is due before the pull
    bool restForJoiners = false;              // the rest phase is the joiner rest: its time is not step idle time
    std::uint32_t joinerRests = 0;            // joiner rests used this step (kMaxJoinerRests)
    std::string joinerNote;                   // "N joiners expected ..."
    // The pull stand (Facts::pullStand, HoldForShot): the unit the glue reads a
    // firing stand for on the next tick (0: none), the stands walked to per
    // unit this step, the stand walk under way (its unit and point), the time
    // a moving tank has been waited for, and how the last order's shot stood
    // (the order's note). shotStandPrep: a tick of this pull held for the
    // stand rule; the pull's fruitless count is not taken again until it goes out.
    std::uint64_t shotStandFor = 0;
    std::unordered_map<std::uint64_t, std::uint32_t> shotStandTries;
    bool shotStandWalk = false;
    std::uint64_t shotStandGuid = 0;
    float shotStandX = 0.0f, shotStandY = 0.0f, shotStandZ = 0.0f;
    std::uint32_t shotStillMs = 0;
    std::uint32_t shotWalkMs = 0;             // the stand walk's time (kShotWalkCapMs)
    bool shotStandPrep = false;
    std::uint64_t shotHoldGuid = 0;           // ... the unit the prep is for (another pick checks the way back)
    std::string shotHow;
    // HoldForShot ran on this tick. A tick it did not run on (a fight, a
    // gather, a dead member, a joiner rest, a regroup, another step kind) ends
    // the hold: the stand request, the walk and the wait are dropped (Tick).
    bool shotGateRan = false;
    std::uint64_t ccPending = 0;              // the `cc moon` sent, its Polymorph not seen yet
    bool ccResent = false;                    // ... sent a second time
    std::uint32_t ccWaitMs = 0;               // fight time since the mark (or its second sending)
    // Regroups the run started by itself at this step (a wipe or kWipeDead
    // dead), and the walk back over the plan's move points after one.
    std::uint32_t autoRegroups = 0;
    bool walkBackDue = false;    // walk back once the regroup is over
    bool walkBack = false;
    bool walkBackDirect = false; // straight to the last point in hops (before a replay of every point)
    std::size_t walkBackStart = 0; // the plan step the walk back began at (the straight target, or the replay's start)
    bool walkBackRestarted = false; // a replay that could not begin went back to the first point the route allows, once
    std::size_t walkBackIdx = 0; // the plan step whose point is walked now
    // The route checkpoint (run-wide: kept across steps, wipes and regroups):
    // the plan index of the move or direct step point the lead bot reached
    // last, its step done or a walk back's point (npos: none yet). The lead
    // bot moves only on the navmesh between such points, and the navmesh
    // never crosses a direct step's hole, so while `routeKnown` holds it has
    // crossed every direct step up to the checkpoint and none after it
    // (StartWalkBack). The run cannot vouch for that once the lead bot died
    // (a release comes back at the entrance) or the run paused (a player may
    // move it): `routeCheck` then holds the spot it stood on, and on its next
    // living tick the checkpoint is known again only if it stands within
    // kRouteSpotYd of that spot (raised where it fell, not moved).
    std::size_t checkpoint = static_cast<std::size_t>(-1);
    bool routeKnown = false;
    bool routeCheck = false;
    float routeSpotX = 0.0f, routeSpotY = 0.0f, routeSpotZ = 0.0f;
    std::uint32_t walkBackRefusals = 0; // passing refusals of that point
    // The island escape (IslandEscape): the living lead bot's last spot a
    // navmesh path left (Facts::leadPathGood; run-wide) and whether this tick's
    // read was such a path, the straight hops off the island sent this step, the
    // hop under way, and where the last walk-back point was given up (two in a
    // row given up from one spot, with no good path read: islandSuspect).
    bool goodLeadKnown = false;
    float goodLeadX = 0.0f, goodLeadY = 0.0f, goodLeadZ = 0.0f;
    std::uint64_t goodLeadMs = 0; // runMs when it was read (GoodSpotFresh)
    std::uint64_t runMs = 0;      // the run's own clock: every running tick's dt
    // The hop off an island under way was sent by MoveStep's check of the
    // forward run's own move point (its no-spot falls back like a walk back's).
    bool escapeFromMove = false;
    bool leadPathGoodNow = false;
    std::uint32_t islandEscapes = 0;
    bool escapeOn = false;
    std::uint32_t escapeMs = 0;
    bool passSpotKnown = false;
    float passX = 0.0f, passY = 0.0f, passZ = 0.0f;
    bool islandSuspect = false;
    // The island's evidence (IslandConfirmed): a single partial reading, the
    // lead bot's cell and spot and the point it was read toward; cleared when
    // the lead bot leaves the cell or a good path is read.
    bool islandEvidence = false;
    int evidenceCellX = 0, evidenceCellY = 0;
    float evidenceX = 0.0f, evidenceY = 0.0f, evidenceZ = 0.0f;
    float evidenceTX = 0.0f, evidenceTY = 0.0f, evidenceTZ = 0.0f;
    // A walk back's hop that found no spot (no-spot), and where: from there the
    // walk back goes on as before (points passed, its own ceilings).
    bool noSpotKnown = false; // ... or the forward run's move check's (escapeFromMove)
    float noSpotX = 0.0f, noSpotY = 0.0f, noSpotZ = 0.0f;
    std::uint32_t stepEscapes = 0; // hops off an island this step (kStepEscapes)
    std::uint32_t islandRefusals = 0; // path refusals taken as the island's second sign this step (RefusedFromIsland)
    // The walk-back point last sent, from where, and how often (kWalkBackResends).
    std::size_t resendIdx = static_cast<std::size_t>(-1);
    float resendX = 0.0f, resendY = 0.0f, resendZ = 0.0f;
    std::uint32_t resends = 0;
    // A scripted event's hold (Hold steps): its clock runs from the first look,
    // in fights too; a regroup or resume starts it over.
    bool eventOn = false;
    std::uint32_t eventResetWaitMs = 0; // a ritual waiting for its encounter to reset (kEventResetWaitMs)
    std::uint32_t eventMs = 0;
    bool eventStarted = false;   // the done data left "not started", or an event unit was seen awake
    bool holdUnitSeen = false;   // a units hold (no done flag): a unit of it stood alive in the radius at some tick of this hold
    bool holdArrived = false;    // the party stood on the hold spot once
    // The party's places on the hold spot (HoldStands), set once the lead bot
    // stands there out of a fight and again after each fight; the glue places
    // the named bots whenever holdStandsGen changes and lets them follow the
    // lead bot again once the list is empty (the step's end, a pause).
    std::vector<HoldStand> holdStands;
    std::uint32_t holdStandsGen = 0;
    bool holdPlaced = false;
    std::uint32_t holdReleases = 0; // times a hold's fight let the places go (ReleaseHoldStands)
    std::uint32_t holdFarTicks = 0; // fight ticks in a row with a ranged bot out of the tank's reach and the tank off the spot
    // The pause came from a player (an order to the party, `.bot run pause`),
    // not from the run: it never resumes by itself (HoldDoneWhilePaused).
    bool pausedByPlayer = false;
    // The pause is the fight ceiling's (kFightCapMs): once the party is out of
    // combat for kCeilingQuietMs the run goes on by itself, once per step
    // (CeilingFightOverWhilePaused). Any other pause, Resume and Skip clear it.
    bool ceilingPaused = false;
    std::uint32_t ceilingQuietMs = 0;
    std::size_t ceilingResumedStep = static_cast<std::size_t>(-1); // the step that has used its one self-resume (ResetStep keeps it)
    std::uint32_t eventIdleMs = 0; // out of a fight with an awake event unit standing
    std::uint32_t eventUp = 0, eventDown = 0, eventTotal = 0; // the event's units now: awake, dead, all seen
    // Run-wide counts for the report.
    std::uint32_t wipes = 0;     // the whole party dead, counted once each time
    bool wiped = false;
    std::uint32_t nearWipes = 0; // kWipeDead or more dead with someone alive, counted once each time
    bool nearWiped = false;
    bool eventRetry = false;     // an event failed this run: a ritual sent now is its retry
    std::unordered_set<std::size_t> eventRearmed; // start steps re-armed by a wipe regroup (once each)
    // Guarding the party from a stand point (GuardTarget): the time the party
    // has been hit with the tank idle on stay, the pick-up orders this fight
    // sent, and the walk back to the stand point after the fight.
    std::uint32_t guardMs = 0;
    std::uint32_t guardOrders = 0;
    std::uint32_t guardSinceOrderMs = 0; // fight time since the last pick-up
    bool guardReturn = false;
    // The tank's reach for a pull is cut off once a mob has aggro (CheckReach):
    // `stopReachDue` asks the glue to end the lead bot's pull, once per pull.
    bool stopReachDue = false;
    bool reachStopped = false;
    float guardX = 0.0f, guardY = 0.0f, guardZ = 0.0f; // the tank's spot at the fight's first pick-up
    // In a hold (HoldGuardTarget, HoldEngageTarget): the event's dead count at
    // the last reset of guardOrders (each unit of the event gets its own
    // orders), and whether an order took the tank off its stay on the hold
    // spot (out of the fight the hold puts it back on it).
    std::uint32_t guardEventDown = 0;
    bool holdUnstayed = false;
    std::uint64_t holdEngageGuid = 0; // the unit the last engage order sent the tank at
    std::uint64_t engageGuid = 0;     // the mob the last engage-on-arrival order (EngageTarget) sent the tank at
    std::uint32_t engageOrders = 0;   // engage orders this fight (kGuardOrdersPerFight at most; guardOrders is not touched)
    // The budget's refill outside a hold (CheckGuard): the units seen attacking
    // the party this fight (partyAttackers, the lead bot's attacker), the orders
    // of either kind sent at each, and all orders this fight, refills included
    // (kGuardOrdersPerUnit, kGuardOrdersFightCap).
    std::unordered_set<std::uint64_t> guardSeen;
    std::unordered_map<std::uint64_t, std::uint32_t> guardUnitOrders;
    std::uint32_t fightOrders = 0;
    bool guardRefilled = false; // a refill since the last order: that order's note says so
    std::uint32_t guardRefills = 0; // refills this fight: the per-unit cap applies only once there was one
    // Members running back to the party with attackers on them (RunBack): the
    // glue keeps each one a passive follower of the lead bot (it does not stop
    // to fight) while its name is here. Per bot: how long it has run, and its
    // stall sample.
    struct RunBackState
    {
        std::uint32_t ms = 0;
        std::uint32_t stillMs = 0;
        float bestYd = 0.0f;
    };
    std::vector<std::string> runBack;
    std::unordered_map<std::string, RunBackState> runBackState;
    std::unordered_set<std::string> runBackDone; // stalled or out of time in this wait: not again until it ends
    std::uint32_t runBacks = 0;  // run backs this run (the report)
};

inline const PlanStep* Current(const Run& r)
{
    return r.plan && r.step < r.plan->steps.size() ? &r.plan->steps[r.step] : nullptr;
}

// The hold a start step leads into: the current step is a ritual, open,
// use_item, gossip or move, and the next steps up to a hold are moves only. Its event
// may already be fighting (the Stone Keepers wake with the ritual's cast), so
// such a fight is bounded as the hold's. Null for none.
inline const PlanStep* EventHoldAhead(const Run& r)
{
    const PlanStep* s = Current(r);
    if (!s || (s->kind != PlanStepKind::Ritual && s->kind != PlanStepKind::Open && s->kind != PlanStepKind::UseItem &&
            s->kind != PlanStepKind::Gossip && s->kind != PlanStepKind::UseOn && s->kind != PlanStepKind::Move &&
            s->kind != PlanStepKind::MoveDirect))
        return nullptr;
    for (std::size_t i = r.step + 1; i < r.plan->steps.size(); ++i)
    {
        const PlanStep& next = r.plan->steps[i];
        if (next.kind == PlanStepKind::Hold)
            return &next;
        if (next.kind != PlanStepKind::Move && next.kind != PlanStepKind::MoveDirect)
            return nullptr;
    }
    return nullptr;
}

// The run is inside a scripted event or leading into one: a hold, its
// ritual or start step, the moves up to the hold, or an event already on.
// The event's quiet gaps between waves are not the end of a fight.
inline bool InEvent(const Run& r)
{
    const PlanStep* s = Current(r);
    return r.eventOn || (s && (s->kind == PlanStepKind::Hold || s->kind == PlanStepKind::Ritual)) ||
        EventHoldAhead(r) != nullptr;
}

// A ritual refused because its object is still active ("already in
// progress") after the event failed: the core leaves a summoning ritual active
// after its last use, so the retry cannot be started in this instance.
inline bool SpentRitual(const Run& r, const std::string& order, const std::string& answer)
{
    return r.eventRetry && order.compare(0, 7, "ritual ") == 0 && answer.compare(0, 4, "busy") == 0 &&
        answer.find("already in progress") != std::string::npos;
}

inline std::string SpentRitualNote(const std::string& what)
{
    return what + " refused: the altar is still active from the failed try, so it is spent; an instance reset is "
        "needed to retry the event (.bot run skip goes on without it)";
}

inline std::string Num(float v)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(v));
    return buf;
}

inline std::string MoveOrder(float x, float y, float z, bool direct)
{
    return "move " + Num(x) + " " + Num(y) + " " + Num(z) + (direct ? " direct" : "");
}

// A raw object guid as an order token: "0x" + 16 hex digits (RemoteCommander::ParseGuid).
inline std::string GuidArg(std::uint64_t guid)
{
    char buf[24];
    std::snprintf(buf, sizeof(buf), "0x%016llx", static_cast<unsigned long long>(guid));
    return buf;
}

inline std::string EntryArg(std::uint32_t entry, float x, float y, float z)
{
    return "entry:" + std::to_string(entry) + "@" + Num(x) + "," + Num(y) + "," + Num(z);
}

inline void EnterPhase(Run& r, Phase p)
{
    r.phase = p;
    r.phaseMs = 0;
}

// The pull stand's state of one step (HoldForShot).
inline void ResetShotStand(Run& r)
{
    r.shotStandFor = 0;
    r.shotStandTries.clear();
    r.shotStandWalk = false;
    r.shotStandGuid = 0;
    r.shotStandX = r.shotStandY = r.shotStandZ = 0.0f;
    r.shotStillMs = 0;
    r.shotWalkMs = 0;
    r.shotStandPrep = false;
    r.shotHoldGuid = 0;
    r.shotHow.clear();
}

// The big pack's stand walk gate is over (the pull went out, a fight, the step).
inline void EndBigWalk(Run& r)
{
    r.bigWalk = false;
    r.bigWalkGuid = 0;
    r.bigJoiners = 0;
    r.bigHold = false;
    r.bigHoldMs = 0;
    r.bigHoldSpent = false;
    r.bigHoldNote.clear();
}

// A tick the pull stand's gate did not run on ends its hold (the stand tries
// already spent stay counted): no stand request is left for the glue to
// answer, and the next pull checks the way back and counts as usual.
inline void EndShotHold(Run& r)
{
    r.shotStandFor = 0;
    r.shotStandWalk = false;
    r.shotStillMs = 0;
    r.shotWalkMs = 0;
    r.shotStandPrep = false;
}

inline void ClearRunBack(Run& r)
{
    r.runBack.clear();
    r.runBackState.clear();
    r.runBackDone.clear();
}

// The lead bot reached the point of plan step `idx` (a move or direct step
// done, or a walk back's point): the route checkpoint (Run::checkpoint).
inline void NoteCheckpoint(Run& r, std::size_t idx)
{
    r.checkpoint = idx;
    r.routeKnown = true;
    r.routeCheck = false;
}

// A walk to the point of plan step `idx` is done (WalkTo): its checkpoint,
// except a direct step's walk that stopped short of its point (WalkTo also
// ends a stalled walk within kStalledArriveYd, and a few yards short of a
// direct point can still be the near side of its hole).
inline void NoteReached(Run& r, const Facts& f, std::size_t idx)
{
    const PlanStep& p = r.plan->steps[idx];
    if (p.kind == PlanStepKind::MoveDirect && ai::PlanDistance(f.leadX, f.leadY, f.leadZ, p.x, p.y, p.z) > kArriveYd)
        return;
    NoteCheckpoint(r, idx);
}

// The run can no longer vouch that the lead bot stays on its checkpoint's
// stretch (it died, or the run paused): its last living spot is kept, and the
// next living tick compares (CheckRoute).
inline void SuspendRoute(Run& r)
{
    if (!r.routeKnown || r.routeCheck)
        return;
    if (!r.leadSeen)
    {
        r.routeKnown = false;
        return;
    }
    r.routeCheck = true;
    r.routeSpotX = r.leadLastX;
    r.routeSpotY = r.leadLastY;
    r.routeSpotZ = r.leadLastZ;
}

// Whether the route checkpoint tells where the lead bot is (Run::checkpoint):
// a point reached, nothing since that could move it off that stretch, and
// before the current step (an event that failed sends the run back to an
// earlier step; a checkpoint past the step says nothing about the way to it).
inline bool CheckpointKnown(const Run& r)
{
    return r.routeKnown && r.checkpoint != static_cast<std::size_t>(-1) && r.checkpoint < r.step;
}

// Whether plan step `idx` is a direct step the lead bot has not crossed (it is
// after the checkpoint, or the checkpoint is not known): a walk back never
// passes one on a failure (its hole would be skipped), it pauses instead.
inline bool DirectStillToCross(const Run& r, std::size_t idx)
{
    if (!r.plan || idx >= r.plan->steps.size() || r.plan->steps[idx].kind != PlanStepKind::MoveDirect)
        return false;
    return !(CheckpointKnown(r) && idx <= r.checkpoint);
}

inline void Pause(Run& r, const std::string& why)
{
    SuspendRoute(r); // a player may move the lead bot while the run is paused
    r.exactProved = static_cast<std::size_t>(-1); // ... so an exact point is proved again after a resume
    r.state = State::Paused;
    r.note = why;
    r.pausedByPlayer = false; // PausePlayer sets it after
    r.ceilingPaused = false; // the fight ceiling sets it after
    r.ceilingQuietMs = 0;
    ClearRunBack(r); // a paused run leaves every bot to its own AI
    r.hold.clear();
    r.holdDue = false;
    r.holdStands.clear();
    r.holdPlaced = false;
    r.holdFarTicks = 0;
}

// Clears an exact waypoint's tighten (not its standing proof, exactProved).
inline void ResetExact(Run& r)
{
    r.exactOn = false;
    r.exactMs = 0;
    r.exactRounds = 0;
    r.exactWaitMs = 0;
    r.exactStage = 0;
    r.exactLastSet = false;
    r.exactAnchorSet = false;
    r.exactStillMs = 0;
}

// Plan step `idx` is an exact waypoint (a move with ai::kPlanFlagExact).
inline bool IsExactPoint(const DungeonPlan* plan, std::size_t idx)
{
    return plan && idx < plan->steps.size() && plan->steps[idx].kind == PlanStepKind::Move &&
        (plan->steps[idx].flags & ai::kPlanFlagExact) != 0;
}

// The exact waypoint a direct step at `idx` is launched from: the step right
// before it when that is one, else npos.
inline std::size_t ExactLaunch(const DungeonPlan* plan, std::size_t idx)
{
    if (!plan || idx == 0 || idx >= plan->steps.size() || plan->steps[idx].kind != PlanStepKind::MoveDirect)
        return static_cast<std::size_t>(-1);
    return IsExactPoint(plan, idx - 1) ? idx - 1 : static_cast<std::size_t>(-1);
}

// A move_hop_exact (PlanStep::exactLaunch) with no exact move right before
// it in its leg: rows the exporter would never write. Its hop is not sent (the
// run pauses), as it would not be on a module that does not know the kind.
inline bool ExactLaunchBroken(const DungeonPlan* plan, std::size_t idx)
{
    if (!plan || idx >= plan->steps.size() || !plan->steps[idx].exactLaunch)
        return false;
    if (idx == 0)
        return true;
    return !IsExactPoint(plan, idx - 1) || plan->steps[idx - 1].leg != plan->steps[idx].leg;
}

inline std::string ExactLaunchBrokenNote(std::size_t idx)
{
    return "step " + std::to_string(idx + 1) + " is a move_hop_exact with no exact move (flags 64) right before it in " +
        "its leg: the hop is not sent; export the plan again (.bot run skip passes it)";
}

// Clears what belongs to one step (also on resume, so a resumed step gets
// fresh tries and ceilings).
inline void ResetStep(Run& r)
{
    EnterPhase(r, Phase::Begin);
    r.stepIdleMs = 0;
    r.restDue = true;
    r.eatWaitMs = 0;
    r.restForEating = false;
    r.stepRested = false;
    r.orderSent = false;
    r.taskResends = 0;
    r.taskBot.clear();
    r.lootTried.clear();
    r.lootRefusal.clear();
    r.lootRights = false;
    r.lootFfa = LootFfa::Idle; // lootFfaLive stays: the next running tick orders the method back (LootFfaRestoreTick)
    r.lootFfaUsed = false;
    r.dataAtOrder = -1;
    r.walkedThrough = false;
    r.approachHops = 0;
    r.approachBestYd = 0.0f;
    r.hopWalk = false;
    r.hopWaiveUnits.clear();
    r.detour = false;
    r.walkTries = 0;
    r.walkStillMs = 0;
    r.walkBestYd = 0.0f;
    r.walkX = r.walkY = r.walkZ = 0.0f;
    ResetExact(r);
    r.pullGuid = 0;
    r.pulls = 0;
    r.pullTries.clear();
    r.passed.clear();
    r.pullHop = false;
    r.openAtLastPull = 0;
    r.fruitlessPulls = 0;
    r.bossWaitMs = 0;
    r.bossSeenAlive = false;
    r.patrolWaitMs = 0;
    r.patrolSeen.clear();
    EndBigWalk(r);
    r.joinerSeen.clear();
    r.walkBackPulls = 0;
    r.gatherOrder.clear();
    r.gatherRefusals = 0;
    r.gatherWaived = false;
    r.summoned.clear();
    r.gatherComingMs = 0;
    r.summonDue.clear();
    r.summonWait.clear();
    r.summonWaitMs = 0;
    r.stepSummons.clear();
    r.directRefusedOn = false;
    r.directRefusedMs = 0;
    r.directRefusedIdx = 0;
    r.directJoinHolds = 0;
    r.directWaitFor.clear();
    r.directWaitMs = 0;
    r.memberYd.clear();
    r.ccSent.clear();
    r.joinerHeld.clear();
    r.joinerRest = false;
    r.restForJoiners = false;
    r.joinerRests = 0;
    r.joinerNote.clear();
    ResetShotStand(r);
    r.focusGuid = 0;
    r.focusRefused.clear();
    r.focusUnstayed = false;
    r.ccPending = 0;
    r.ccResent = false;
    r.ccWaitMs = 0;
    r.walkBackDue = false;
    r.walkBack = false;
    r.walkBackDirect = false;
    r.walkBackIdx = 0;
    r.walkBackStart = 0;
    r.walkBackRestarted = false;
    r.walkBackRefusals = 0;
    r.islandEscapes = 0; // a resume hops the lead bot off an island again
    r.escapeOn = false;
    r.escapeMs = 0;
    r.passSpotKnown = false;
    r.islandSuspect = false;
    r.islandEvidence = false;
    r.noSpotKnown = false;
    r.stepEscapes = 0;
    r.islandRefusals = 0;
    r.escapeFromMove = false;
    r.resendIdx = static_cast<std::size_t>(-1);
    r.resends = 0;
    r.eventOn = false;
    r.eventResetWaitMs = 0;
    r.eventMs = 0;
    r.eventStarted = false;
    r.holdUnitSeen = false;
    r.holdArrived = false;
    r.eventIdleMs = 0;
    r.eventUp = r.eventDown = r.eventTotal = 0;
    r.holdStands.clear();
    r.holdPlaced = false;
    r.holdFarTicks = 0;
    r.guardMs = 0;
    r.guardOrders = 0;
    r.engageGuid = 0;
    r.engageOrders = 0;
    r.guardSeen.clear();
    r.guardUnitOrders.clear();
    r.guardRefilled = false;
    r.guardRefills = 0;
    r.fightOrders = 0;
    r.guardReturn = false;
    r.stopReachDue = false;
    r.reachStopped = false;
    r.guardEventDown = 0;
    r.holdUnstayed = false;
    r.holdEngageGuid = 0;
    r.hold.clear();
    r.holdDue = false;
}

// "N wipe(s), M near wipe(s)", or "" for none.
inline std::string WipeCounts(const Run& r)
{
    std::string out;
    if (r.wipes)
        out = std::to_string(r.wipes) + (r.wipes == 1 ? " wipe" : " wipes");
    if (r.nearWipes)
        out += (out.empty() ? "" : ", ") + std::to_string(r.nearWipes) + (r.nearWipes == 1 ? " near wipe" : " near wipes");
    return out;
}

inline std::string DoneNote(const Run& r)
{
    std::string const counts = WipeCounts(r);
    return counts.empty() ? std::string("plan done") : "plan done (" + counts + ")";
}

// Each bot's distance to the lead bot now: the sample a gather's ceiling
// compares with (taken when the gather goes out and at each ceiling).
inline void SampleMembers(Run& r, const Facts& f)
{
    for (const MemberFact& m : f.members)
        r.memberYd[m.name] = m.yd;
}

// Each living bot's stand-still watch, fed every tick: a bot that moved
// kStillYd from its watch spot starts a fresh clock there; one that did not
// adds the tick. Bots not in the facts (dead, gone) are dropped, so a revived
// bot starts afresh.
inline void UpdateStill(Run& r, const Facts& f, std::uint32_t dt)
{
    for (auto it = r.still.begin(); it != r.still.end();)
    {
        bool present = false;
        for (const MemberFact& m : f.members)
            if (m.name == it->first)
            {
                present = true;
                break;
            }
        it = present ? std::next(it) : r.still.erase(it);
    }
    for (const MemberFact& m : f.members)
    {
        if (m.name.empty())
            continue;
        auto it = r.still.find(m.name);
        if (it == r.still.end() || ai::PlanDistance(m.x, m.y, m.z, it->second.x, it->second.y, it->second.z) >= kStillYd)
        {
            Run::StillState& st = r.still[m.name];
            st.x = m.x;
            st.y = m.y;
            st.z = m.z;
            st.ms = 0;
            continue;
        }
        it->second.ms += dt;
    }
}

// The bot has moved less than kStillYd for kStillMs.
inline bool IsStill(const Run& r, const std::string& name)
{
    auto it = r.still.find(name);
    return it != r.still.end() && it->second.ms >= kStillMs;
}

// A summon of `name` goes out this tick: once per step and regroup for the summons that keep that rule
// (Run::summoned), and counted with its time for a direct step's straggler (Run::stepSummons).
inline void MarkSummoned(Run& r, const std::string& name)
{
    r.summoned.insert(name);
    Run::StepSummon& s = r.stepSummons[name];
    ++s.count;
    s.lastMs = r.runMs;
}

// The run clock a bot was last seen dead (every tick): a bot that died and came back after its last summon is
// a new case for a direct step's summon (DirectSummonAllowed).
inline void NoteDeadSeen(Run& r, const Facts& f)
{
    for (const std::string& name : f.deadNames)
        r.deadSeenMs[name] = r.runMs;
}

// r12-19z29 Uldaman leg 5 (2026-10-22): a direct step's straggler summoned already this step (the mid-step
// revive's summon) was never summoned again, so its too-far refusal paused the run, and the paused run stopped
// guarding: two more deaths. It may be summoned again once more than kDirectResummonMs passed since its last
// summon of the step, or once it died and came back since then; never more than kDirectSummonCap times a step.
inline bool DirectSummonAllowed(const Run& r, const std::string& name)
{
    auto it = r.stepSummons.find(name);
    if (it == r.stepSummons.end() || it->second.count == 0)
        return true;
    if (it->second.count >= kDirectSummonCap)
        return false;
    auto dead = r.deadSeenMs.find(name);
    if (dead != r.deadSeenMs.end() && dead->second >= it->second.lastMs)
        return true;
    return r.runMs - it->second.lastMs > kDirectResummonMs;
}

// The step's walk points: what a walk back after a regroup replays.
inline bool IsMovePoint(const PlanStep& s)
{
    return s.kind == PlanStepKind::Move || s.kind == PlanStepKind::MoveDirect;
}

// The walk back's next point as WalkBack reaches it on its next tick: it passes the steps that are not walk
// points (a pull, a rest) in the same tick, so the facts must be read for the point after them. -1: none
// (the walk back is over, or none is on).
inline std::size_t NextWalkBackIdx(const Run& r)
{
    if (!r.walkBack || !r.plan)
        return static_cast<std::size_t>(-1);
    std::size_t i = r.walkBackIdx;
    while (i < r.step && i < r.plan->steps.size() && !IsMovePoint(r.plan->steps[i]))
        ++i;
    return i < r.step && i < r.plan->steps.size() ? i : static_cast<std::size_t>(-1);
}

// The point a direct step's trouble belongs to: the walk back's point, else the step's own.
inline std::size_t DirectTroublePoint(const Run& r)
{
    std::size_t const back = NextWalkBackIdx(r);
    return back != static_cast<std::size_t>(-1) ? back : r.step;
}

// The first too-far refusal (or direct gather ceiling) at a point starts the clock DirectPauseDue reads; a walk
// back that went on to another point starts it over there.
inline void NoteDirectTrouble(Run& r)
{
    std::size_t const point = DirectTroublePoint(r);
    if (r.directRefusedOn && r.directRefusedIdx == point)
        return;
    r.directRefusedOn = true;
    r.directRefusedMs = r.runMs;
    r.directRefusedIdx = point;
}

// A direct step's straggler pauses the run only after kDirectPauseSummons summons of it this step and
// kDirectPauseMs since the first refusal at this point, or after 2 x kDirectPauseMs whatever the summons (a
// straggler held in a fight is never summoned: the time bound); until then the run waits and keeps guarding.
inline bool DirectPauseDue(const Run& r, const std::string& name)
{
    if (!r.directRefusedOn)
        return false;
    auto it = r.stepSummons.find(name);
    std::uint32_t const summons = it == r.stepSummons.end() ? 0 : it->second.count;
    std::uint64_t const since = r.runMs - r.directRefusedMs;
    return (summons >= kDirectPauseSummons && since >= kDirectPauseMs) || since >= 2ull * kDirectPauseMs;
}

// The point of the direct step the gate before it guards, when there is one: the walk back's next point (as
// WalkBack reaches it: NextWalkBackIdx) when it is a direct one, else the current step when it is a direct step.
// Not while a walk is under way (the gate is not asked then). While this holds the glue reads
// MemberFact::withLead (one navmesh path per follower within kGatherYd) with that point for the bot's own line.
inline bool DirectJoinWanted(const Run& r, float& x, float& y, float& z)
{
    if (r.state != State::Running || !r.plan || r.phase == Phase::Walk)
        return false;
    const PlanStep* s = nullptr;
    std::size_t const back = NextWalkBackIdx(r);
    if (back != static_cast<std::size_t>(-1))
        s = &r.plan->steps[back];
    else
        s = Current(r);
    if (!s || s->kind != PlanStepKind::MoveDirect)
        return false;
    x = s->x;
    y = s->y;
    z = s->z;
    return true;
}

inline bool DirectJoinWanted(const Run& r)
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
    return DirectJoinWanted(r, x, y, z);
}

// The farthest living bot the glue's direct-step test finds not with the lead bot (only with
// Facts::directJoinRead), or none.
inline const MemberFact* NotWithLead(const Facts& f)
{
    if (!f.directJoinRead)
        return nullptr;
    const MemberFact* pick = nullptr;
    for (const MemberFact& m : f.members)
        if (!m.name.empty() && !m.withLead && (!pick || m.yd > pick->yd))
            pick = &m;
    return pick;
}

// The gate before a direct step: every living bot within kGatherYd of the lead bot and, when the glue read it,
// with the lead bot by the very test `cmd move ... direct` refuses on.
inline bool DirectGathered(const Facts& f)
{
    return f.maxMemberYd <= kGatherYd && !NotWithLead(f);
}

// The gate passed, but on facts without the glue's test although the glue reads it: the direct order waits for
// the next tick's read (no order, the note as it is), kDirectJoinHolds ticks at most. True: hold now.
inline bool HoldForDirectJoin(Run& r, const Facts& f)
{
    if (f.directJoinRead || !f.directJoinSupported)
    {
        r.directJoinHolds = 0;
        return false;
    }
    if (r.directJoinHolds >= kDirectJoinHolds)
        return false;
    ++r.directJoinHolds;
    return true;
}

// A gather's ceiling: the farthest straggler, a bot more than kSummonYd from
// the lead bot (or kStragglerDzYd up or down), or one that stands still more
// than kGatherYd away, that stands still now
// (IsStill) or came less than kStragglerGainYd closer since the last sample
// (stuck behind, not on its way), is summoned to the lead bot (`cmd summon`),
// once per bot and step (and per regroup), and the gather waits again. The
// stand-still test is what catches a bot that walked part of the way and then
// stuck: Uldaman 19z11, the revived priest gained more than 5 yd, then stood
// at the scorpid pit's navmesh hole and the regroup paused. stillOnly: only
// bots that stand still (an approach hop's ceiling: the walkers are on their
// way). False when there is none to summon: the caller pauses as before.
inline bool SummonStraggler(Run& r, const Facts& f, std::string& order, bool stillOnly = false)
{
    const MemberFact* pick = nullptr;
    bool pickStill = false;
    for (const MemberFact& m : f.members)
    {
        if (m.name.empty() || r.summoned.count(m.name))
            continue;
        // A bot in a fight, or with something on it, is never summoned, far or
        // not: `cmd summon` refuses a bot in combat, and one with attackers
        // would drag the encounter to the lead bot or be abandoned in it.
        if (m.inCombat || m.attackers > 0)
            continue;
        // Far: past kSummonYd, or on another floor. Not far, a bot past the
        // gather radius that stands still is stuck too: the gather (kGatherYd
        // 20 yd) waits for it while the summon skips it (kSummonYd 40), so a
        // bot that sticks 20-40 yd out is never summoned and the run pauses
        // (events-5 00:55:59, grimlok pull 93: Stoutster stood 34.9 yd away,
        // still, "the party did not gather within 30 s", twice).
        bool const far = m.yd > kSummonYd || m.dz > kStragglerDzYd;
        bool const still = IsStill(r, m.name);
        // A bot past the gather radius that stands still is stuck (a busy one
        // never gets here, see above), exactly as in a regroup
        // (SummonStuckInRegroup). The far rule below keeps its own tests.
        if (!far && !(still && m.yd > kGatherYd))
            continue;
        if (!still)
        {
            if (!far || stillOnly)
                continue;
            auto it = r.memberYd.find(m.name);
            if (it == r.memberYd.end() || it->second - m.yd >= kStragglerGainYd)
                continue;
        }
        if (!pick || m.yd > pick->yd)
        {
            pick = &m;
            pickStill = still;
        }
    }
    SampleMembers(r, f);
    if (!pick)
        return false;
    MarkSummoned(r, pick->name);
    order = "summon " + pick->name;
    r.note = "gather: " + pick->name + " stands " + Num(pick->yd) + " yd from the lead bot and " +
        (pickStill ? "has not moved for " + std::to_string(kStillMs / 1000) + " s" : std::string("does not come closer")) +
        ": summoned";
    return true;
}

// The ceiling of a pull's or a direct step's gather (kGatherCapMs). In order:
// SummonStraggler (a stuck or far bot, once each). Else the farthest bot, if it
// is still on its way (it is not still and came kStragglerGainYd closer since
// the last sample, the gather's start or the last ceiling), is waited for: the
// gather starts over for another kGatherCapMs, up to kGatherComingMaxMs in all
// (Run::gatherComingMs). Uldaman r4-19z21 slot 2 step 102: a bot 33.7 yd out,
// walking in at about 1 yd/s, paused the run at the 30 s ceiling. Else, that
// bot is summoned once (past the limit, or when it is walking but not coming)
// unless it is in a fight; and if none of that applies: the pause, with the
// text the caller gave.
// A direct step's gather (direct): the farthest bot may also be one within
// kGatherYd that the glue's test finds not with the lead bot (NotWithLead), and
// one summoned already this step is summoned again by DirectSummonAllowed; the
// pause waits for DirectPauseDue, the gather starts over meanwhile.
inline void GatherCeiling(Run& r, const Facts& f, std::string& order, const std::string& pauseText,
    bool direct = false)
{
    std::unordered_map<std::string, float> const sampled = r.memberYd; // SummonStraggler takes a new sample
    if (SummonStraggler(r, f, order))
    {
        EnterPhase(r, Phase::Gather);
        return;
    }
    const MemberFact* farthest = nullptr;
    for (const MemberFact& m : f.members)
        if (!m.name.empty() && m.yd > kGatherYd && (!farthest || m.yd > farthest->yd))
            farthest = &m;
    if (direct && !farthest)
        farthest = NotWithLead(f);
    if (direct && farthest)
    {
        NoteDirectTrouble(r);
        if (r.summoned.count(farthest->name))
        {
            if (!farthest->inCombat && !farthest->attackers && DirectSummonAllowed(r, farthest->name))
            {
                MarkSummoned(r, farthest->name);
                order = "summon " + farthest->name;
                r.note = "gather: " + farthest->name + " is not with the lead bot before a direct step (" +
                    Num(farthest->yd) + " yd): summoned again";
                EnterPhase(r, Phase::Gather);
                return;
            }
            if (!DirectPauseDue(r, farthest->name))
            {
                EnterPhase(r, Phase::Gather);
                r.note = "gathering: waiting for " + farthest->name + " to come before a direct step (" +
                    Num(farthest->yd) + " yd)";
                return;
            }
        }
    }
    if (farthest && !r.summoned.count(farthest->name))
    {
        auto it = sampled.find(farthest->name);
        bool const coming = !IsStill(r, farthest->name) && it != sampled.end() && it->second - farthest->yd >= kStragglerGainYd;
        if (coming && kGatherCapMs + r.gatherComingMs < kGatherComingMaxMs)
        {
            r.gatherComingMs += kGatherCapMs;
            EnterPhase(r, Phase::Gather);
            r.note = "gathering: " + farthest->name + " is coming (" + Num(farthest->yd) + " yd)";
            return;
        }
        if (!farthest->inCombat && !farthest->attackers)
        {
            MarkSummoned(r, farthest->name);
            order = "summon " + farthest->name;
            r.note = "gather: " + farthest->name + " stands " + Num(farthest->yd) + " yd from the lead bot and " +
                (coming ? std::string("did not come in time") : std::string("does not come closer")) + ": summoned";
            EnterPhase(r, Phase::Gather);
            return;
        }
    }
    Pause(r, pauseText);
}

// A regroup's gather: a living bot far from the lead bot (kSummonYd, or
// kStragglerDzYd up or down) that has not moved for kStuckSummonMs stood still
// where it is stuck (a navmesh pocket), so it is summoned at once instead of
// the gather's whole ceiling being waited out for it (events-5 01:48:43 ->
// 01:50:42: Stoutster stood 363 yd away, still, until the ceiling; the pit at
// 43,377 / -164,299 is a repeat spot). Once per bot per regroup (Run::summoned,
// the same set SummonStraggler uses), the farthest first, one a tick. The
// gather's clock starts over; the caller sends the order. False: none.
inline bool SummonStuckInRegroup(Run& r, const Facts& f, std::string& order)
{
    if (!f.leadAlive)
        return false;
    // The stand-still watch is older than the gather: it runs while the dead
    // are revived. The 15 s have to be the gather's own clock (regroupMs starts
    // over when the gather goes out and on every summon), or a bot that stood
    // still 120 yd away during the revives is summoned one tick after the
    // gather, before anyone could have seen it walk.
    if (r.regroupMs < kStuckSummonMs)
        return false;
    const MemberFact* pick = nullptr;
    for (const MemberFact& m : f.members)
    {
        if (m.name.empty() || m.name == f.carrier || r.summoned.count(m.name) || m.inCombat || m.attackers)
            continue;
        if (m.yd <= kSummonYd && m.dz <= kStragglerDzYd)
            continue;
        auto it = r.still.find(m.name);
        if (it == r.still.end() || it->second.ms < kStuckSummonMs)
            continue;
        if (!pick || m.yd > pick->yd)
            pick = &m;
    }
    if (!pick)
        return false;
    MarkSummoned(r, pick->name);
    order = "summon " + pick->name;
    r.RestartRegroupClock();
    r.note = "regroup: " + pick->name + " has not moved for " + std::to_string(kStuckSummonMs / 1000) + " s " +
        Num(pick->yd) + " yd away: summoned";
    return true;
}

// "waiting for Priest (dead), Mage (running back)": the bots the regroup waits
// for, the dead by name and the living ones still out of reach.
inline std::string WaitNote(const Run& r, const Facts& f)
{
    std::string out;
    auto add = [&](const std::string& name, const char* why)
    {
        out += (out.empty() ? "" : ", ") + name + " (" + why + ")";
    };
    for (const std::string& name : f.deadNames)
        add(name, "dead");
    for (const MemberFact& m : f.members)
        if (!m.name.empty() && (m.yd > kGatherYd || m.dz > kStragglerDzYd) &&
            (r.waitFor.count(m.name) || r.regroup))
            add(m.name, "running back");
    if (out.empty() && f.dead)
        out = std::to_string(f.dead) + " dead";
    return out.empty() ? std::string("waiting for the party") : "waiting for " + out;
}

// The regroup's gather ceiling, set when its gather goes out:
// kRegroupGatherMs, or the time the farthest bot needs to walk in at
// kRegroupWalkYdPerS, at most kRegroupGatherMaxMs (a revived bot whose summon
// fails walks the whole way).
inline std::uint32_t RegroupGatherCap(const Facts& f)
{
    float const ms = f.maxMemberYd / kRegroupWalkYdPerS * 1000.0f;
    if (ms <= static_cast<float>(kRegroupGatherMs))
        return kRegroupGatherMs;
    return ms >= static_cast<float>(kRegroupGatherMaxMs) ? kRegroupGatherMaxMs : static_cast<std::uint32_t>(ms);
}

// A revived bot's summon has failed: it walks back with the party's gather
// (the regroup's gather goes out once no summon is pending), and a gather
// already out starts its ceiling over for it.
inline void ReviveSummonFailed(Run& r, const std::string& name, const std::string& why)
{
    r.reviveSummons.erase(name);
    if (r.regroupGatherSent)
        r.RestartRegroupClock();
    r.note = "regroup: the summon of " + name + " " + why + ": it walks back";
}

// One second of the revived bots' summons (regroup only): a fallen bot
// (Run::waitFor) seen dead again may be summoned again after its next revive;
// one with the lead bot is done; one not there in kReviveSummonWaitMs failed.
inline void UpdateReviveSummons(Run& r, const Facts& f, std::uint32_t dt)
{
    for (const std::string& name : f.deadNames)
    {
        r.summoned.erase(name); // one summon per bot per revive
        r.reviveSummons.erase(name);
    }
    std::vector<std::string> failed;
    for (auto it = r.reviveSummons.begin(); it != r.reviveSummons.end();)
    {
        const MemberFact* m = nullptr;
        for (const MemberFact& x : f.members)
            if (x.name == it->first)
                m = &x;
        if (!m || (m->yd <= kGatherYd && m->dz <= kStragglerDzYd))
        {
            it = r.reviveSummons.erase(it);
            continue;
        }
        it->second += dt;
        if (it->second >= kReviveSummonWaitMs)
            failed.push_back(it->first);
        ++it;
    }
    for (const std::string& name : failed)
        ReviveSummonFailed(r, name, "did not bring it in " + std::to_string(kReviveSummonWaitMs / 1000) + " s");
}

// The owner's fix of 2026-10-17 (Uldaman run 3, kReviveSummonWaitMs): a
// fallen bot (Run::waitFor) alive again more than kSummonYd from the lead bot
// (or kStragglerDzYd up or down), out of combat and with no attackers, is
// summoned to the lead bot (`cmd summon`) on the first such tick, before its
// corpse run takes it through the dungeon; once per bot per revive (Run::
// summoned). Never while it fights (`cmd summon` refuses a bot in combat: it
// runs back, RunBack), never with the lead bot dead (it is the summon point),
// never a held bot (HoldView). The farthest first, one a tick. False: none.
inline bool SummonRevived(Run& r, const Facts& f, std::string& order)
{
    if (!f.leadAlive)
        return false;
    const MemberFact* pick = nullptr;
    for (const MemberFact& m : f.members)
    {
        if (m.name.empty() || m.name == f.carrier || !r.waitFor.count(m.name) || r.summoned.count(m.name) ||
            m.inCombat || m.attackers || (m.yd <= kSummonYd && m.dz <= kStragglerDzYd))
            continue;
        if (!pick || m.yd > pick->yd)
            pick = &m;
    }
    if (!pick)
        return false;
    MarkSummoned(r, pick->name);
    r.reviveSummons[pick->name] = 0;
    order = "summon " + pick->name;
    r.note = "regroup: " + pick->name + " is alive again " + Num(pick->yd) +
        " yd from the lead bot: summoned to it instead of walking back";
    return true;
}


// Where the lead bot stands against the run's anchor: on it (or no anchor),
// off it (more than kStragglerDzYd above or below: a fight chased it up a
// ridge; or no navmesh path back), or not known yet (away on the anchor's
// floor and the path back not read: asked for, the glue reads it next tick).
enum class AnchorState : std::uint8_t { On, Off, Unknown };

inline AnchorState CheckAnchor(Run& r, const Facts& f)
{
    if (!r.anchorKnown)
        return AnchorState::On;
    float const dz = f.leadZ > r.anchorZ ? f.leadZ - r.anchorZ : r.anchorZ - f.leadZ;
    if (dz > kStragglerDzYd)
        return AnchorState::Off;
    if (ai::PlanDistance(f.leadX, f.leadY, f.leadZ, r.anchorX, r.anchorY, r.anchorZ) <= kArriveYd)
        return AnchorState::On;
    if (!f.anchorPathRead)
    {
        r.anchorPathWanted = true;
        return AnchorState::Unknown;
    }
    return f.anchorReachable ? AnchorState::On : AnchorState::Off;
}

inline bool LeadOffAnchor(Run& r, const Facts& f)
{
    return CheckAnchor(r, f) == AnchorState::Off;
}

// Sends a gather: the lead bot holds where it stands and the others form up on
// it; a lead bot off the anchor walks back to the anchor first and the party
// gathers there (19z5 step 83: a gather on the lead's own spot pinned the party
// on a hilltop with no path down).
inline void SendGather(Run& r, const Facts& f, std::string& order)
{
    order = LeadOffAnchor(r, f) ? MoveOrder(r.anchorX, r.anchorY, r.anchorZ, false)
                                : MoveOrder(f.leadX, f.leadY, f.leadZ, false);
    r.gatherOrder = order;
    SampleMembers(r, f);
}

// Whether `order` is the gather this tick sent (the glue tells `cmd move` it
// is the run leader's gather on the lead bot's own spot).
inline bool IsGatherOrder(const Run& r, const std::string& order)
{
    return !r.gatherOrder.empty() && order == r.gatherOrder;
}

// The step the glue reads the step facts for (seen, approach, hopUnits): the
// move point a walk back replays, else the current step.
inline const PlanStep* FactStep(const Run& r)
{
    if (r.walkBack && r.plan && r.walkBackIdx < r.step && r.step < r.plan->steps.size())
        return &r.plan->steps[r.walkBackIdx];
    return r.plan && r.step < r.plan->steps.size() ? &r.plan->steps[r.step] : nullptr;
}

// OwnedByLaterStep's `from` for the hop pulls: the current step; during a walk
// back the step before it, so a pack of a step already done (back after a
// respawn) is pulled on the way, and the current step's own pack and later
// ones are left to their steps.
inline std::size_t OwnerFrom(const Run& r)
{
    return r.walkBack && r.step > 0 ? r.step - 1 : r.step;
}

// The glue's words on an `unreachable` answer when the refused path was the
// lead bot's own no-progress path (RemoteCommander::NoProgressFromLead): an
// incomplete path that ends where the lead bot stands. The source-side evidence
// RefusedFromIsland needs; a bare `unreachable` (no polygon at the point, a
// closed door, a point on a patch the path got well toward) does not carry it.
constexpr const char* kLeadNoProgressMark = "Every path from the lead bot ends where it stands";

inline bool LeadNoProgressRefusal(const std::string& answer)
{
    return answer.compare(0, 11, "unreachable") == 0 && answer.find(kLeadNoProgressMark) != std::string::npos;
}

// A refused move whose point cannot be walked to (the move's own verdicts:
// no path, off the mesh; a direct step's no ground or no sight line), as
// opposed to a refusal for the lead bot's state of the moment.
inline bool IsPathRefusal(const std::string& answer)
{
    for (const char* code : {"unreachable", "off-mesh", "no-ground", "no-los"})
        if (answer.compare(0, std::char_traits<char>::length(code), code) == 0)
            return true;
    return false;
}

// The waiver of a hop abandoned for a close unit ends with anything that
// changes the pull's situation (combat, a regroup, another target).
inline void DropHopWaiver(Run& r)
{
    if (r.hopWaiveUnits.empty())
        return;
    r.hopWaiveUnits.clear();
    r.gatherWaived = false;
}

inline void BeginRegroup(Run& r)
{
    DropHopWaiver(r);
    r.neverCame.clear(); // a resume or a wipe recovery judges the hop units afresh
    r.patrolGaveUp.clear(); // ... and the patrollers
    r.anchorKnown = false; // the party comes back to life elsewhere
    r.regroup = true;
    r.reviveSent = false;
    r.regroupGatherSent = false;
    r.RestartRegroupClock();
    r.rezAbleMs = 0;
    r.rezHoldMs = 0;
    r.rezNudged = false;
    r.rezNudgeOrder.clear();
    r.rezBodyUnreachable = false;
    r.regroupDead = -1;
    r.summoned.clear(); // one summon per bot per regroup (SummonStraggler)
    r.gatherComingMs = 0;
    r.reviveSummons.clear();
    r.regroupGatherCapMs = kRegroupGatherMs;
    r.leadDied = false; // the regroup revives and gathers whoever died
}

inline void NextStep(Run& r, const std::string& note)
{
    r.pendingMissing = 0;
    ++r.step;
    r.autoRegroups = 0;
    ResetStep(r);
    r.fightMs = 0; // a fight that goes on into the next step starts its ceiling over (the hold's 400 s were the wait step's)
    r.note = note;
    if (!Current(r))
    {
        r.state = State::Done;
        r.note = DoneNote(r);
    }
}

inline Run Start(const DungeonPlan* plan, std::size_t fromStep = 0)
{
    Run r;
    r.plan = plan;
    r.step = fromStep;
    ResetStep(r);
    r.note = "started";
    if (!Current(r))
    {
        r.state = State::Done;
        r.note = "plan has no steps";
    }
    return r;
}

inline std::string LowerCopy(std::string v)
{
    for (char& c : v)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return v;
}

// ---------------------------------------------------------------------------
// The run's own time limit (AiPlayerbot.DungeonRunMaxMinutes) and the notes of
// a stop. A run that hit its limit used to read "stopped by .bot run stop"
// (Uldaman run 2, 2026-10-17: the agent's 3 h cap sent a plain stop).
// ---------------------------------------------------------------------------

constexpr std::uint32_t kRunMaxMinutesDefault = 180; // the shipped template and the code fallback agree (3 h)

// maxMinutes 0 = no limit.
inline bool RunTimeLimitReached(std::uint64_t elapsedMs, std::uint32_t maxMinutes)
{
    return maxMinutes > 0 && elapsedMs >= static_cast<std::uint64_t>(maxMinutes) * 60000ull;
}

// "time limit reached (3h)", "time limit reached (90 min)".
inline std::string RunTimeLimitNote(std::uint32_t maxMinutes)
{
    std::string span = maxMinutes % 60 == 0 ? std::to_string(maxMinutes / 60) + "h"
                                            : std::to_string(maxMinutes) + " min";
    return "time limit reached (" + span + ")";
}

// The note of `.bot run stop [reason]`: no reason keeps the old text; a reason
// (the agent's own cap, "time limit reached (3h)") is shown as it came, cut to
// kRunStopReasonMax characters and reduced to printable ASCII without the
// protocol's separator.
constexpr std::size_t kRunStopReasonMax = 80;

inline std::string RunStopNote(const std::string& reason)
{
    std::string clean;
    for (char c : reason)
    {
        if (c == '|' || c < 0x20 || c > 0x7e)
            c = ' ';
        if (c == ' ' && (clean.empty() || clean.back() == ' '))
            continue;
        clean.push_back(c);
        if (clean.size() >= kRunStopReasonMax)
            break;
    }
    while (!clean.empty() && clean.back() == ' ')
        clean.pop_back();
    return clean.empty() ? std::string("stopped by .bot run stop") : "stopped: " + clean;
}

// ---------------------------------------------------------------------------
// `.bot run start [plan] [from [leg|step] <leg name|leg number|step number>]`
// ---------------------------------------------------------------------------

enum class StartFrom : std::uint8_t
{
    Beginning, // no `from`: the first step
    Leg,       // a leg's name or number (`from 4`, `from leg trogg_west`)
    Step,      // a step number as the status line shows it, from 1 (`from step 57`)
};

struct StartArgs
{
    bool ok = true;       // false: the words do not parse
    std::string plan;     // "" = the one plan of the lead bot's map
    StartFrom from = StartFrom::Beginning;
    std::string value;    // the leg name / number or the step number
};

// `arg` is the text after `start`, already lowercased. Words before `from` are
// the plan (at most one); after it an optional `leg` or `step`, then the value.
inline StartArgs ParseStartArgs(const std::string& arg)
{
    StartArgs out;
    std::vector<std::string> words;
    std::size_t i = 0;
    while (i < arg.size())
    {
        while (i < arg.size() && (arg[i] == ' ' || arg[i] == '\t'))
            ++i;
        std::size_t begin = i;
        while (i < arg.size() && arg[i] != ' ' && arg[i] != '\t')
            ++i;
        if (i > begin)
            words.push_back(arg.substr(begin, i - begin));
    }
    std::size_t at = 0;
    if (!words.empty() && words[0] != "from")
        out.plan = words[at++];
    if (at == words.size())
        return out;
    if (words[at] != "from")
    {
        out.ok = false; // a second word that is not `from`
        return out;
    }
    ++at;
    StartFrom kind = StartFrom::Leg;
    if (at < words.size() && (words[at] == "leg" || words[at] == "step"))
        kind = words[at++] == "leg" ? StartFrom::Leg : StartFrom::Step;
    if (at + 1 != words.size())
    {
        out.ok = false; // no value, or words after it
        return out;
    }
    out.from = kind;
    out.value = words[at];
    return out;
}

enum class FromResult : std::uint8_t
{
    Ok,
    NoSuchLeg,
    NoSuchStep,
};

inline bool AllDigits(const std::string& v)
{
    return !v.empty() && v.size() <= 5 && std::all_of(v.begin(), v.end(), [](char c) { return c >= '0' && c <= '9'; });
}

// A scripted event starts at its ritual, open, use_item or gossip step and runs on in the
// hold behind it (EventHoldAhead). A start that lands on the hold, or on a move
// leading into one, would never send the ritual: it goes back to the step that
// starts the event (the Uldaman Keepers' ritual ends leg 10, its hold opens
// leg 11). Anything else, or a hold with no such step before it, stays put.
inline std::size_t EventStartStep(const DungeonPlan& plan, std::size_t idx)
{
    auto const moveLike = [](PlanStepKind k) { return k == PlanStepKind::Move || k == PlanStepKind::MoveDirect; };
    std::size_t const n = plan.steps.size();
    if (idx >= n)
        return idx;
    PlanStepKind const kind = plan.steps[idx].kind;
    if (kind != PlanStepKind::Hold && !moveLike(kind))
        return idx;
    std::size_t j = idx;
    while (j < n && moveLike(plan.steps[j].kind))
        ++j;
    if (kind != PlanStepKind::Hold && !(j < n && plan.steps[j].kind == PlanStepKind::Hold))
        return idx;
    std::size_t k = idx;
    while (k > 0)
    {
        --k;
        PlanStepKind const back = plan.steps[k].kind;
        if (back == PlanStepKind::Ritual || back == PlanStepKind::Open || back == PlanStepKind::UseItem ||
            back == PlanStepKind::Gossip || back == PlanStepKind::UseOn)
            return k;
        if (!moveLike(back))
            return idx;
    }
    return idx;
}

// The index of the step the run starts at. `movedBack` is set when a start
// inside an event was moved back to the step that starts it.
inline FromResult ResolveStartStep(const DungeonPlan& plan, StartFrom from, const std::string& value,
    std::size_t& step, bool* movedBack = nullptr)
{
    step = 0;
    if (movedBack)
        *movedBack = false;
    if (from == StartFrom::Beginning)
        return FromResult::Ok;
    if (from == StartFrom::Step)
    {
        if (!AllDigits(value))
            return FromResult::NoSuchStep;
        std::size_t const n = static_cast<std::size_t>(std::atoi(value.c_str()));
        if (n < 1 || n > plan.steps.size())
            return FromResult::NoSuchStep;
        step = EventStartStep(plan, n - 1);
        if (movedBack)
            *movedBack = step != n - 1;
        return FromResult::Ok;
    }
    bool const numeric = AllDigits(value);
    int const number = numeric ? std::atoi(value.c_str()) : -1;
    for (std::size_t i = 0; i < plan.steps.size(); ++i)
    {
        std::uint8_t const leg = plan.steps[i].leg;
        if (numeric ? static_cast<int>(leg) == number : LowerCopy(plan.LegName(leg)) == value)
        {
            step = EventStartStep(plan, i);
            if (movedBack)
                *movedBack = step != i;
            return FromResult::Ok;
        }
    }
    return FromResult::NoSuchLeg;
}

// A start partway needs the whole party inside the instance: the steps before
// are not done and not checked, so a party that is outside (or scattered over
// two maps) would walk a dungeon plan from the middle in the wrong place.
enum class PartialStartVerdict : std::uint8_t
{
    Ok,
    NotInstance,    // the lead bot is not on an instance map
    PartyScattered, // a commanded bot is on another map or instance than the lead bot
};

inline PartialStartVerdict CheckPartialStart(bool leadOnInstanceMap, std::size_t members, std::size_t membersWithLead)
{
    if (!leadOnInstanceMap)
        return PartialStartVerdict::NotInstance;
    return membersWithLead < members ? PartialStartVerdict::PartyScattered : PartialStartVerdict::Ok;
}

// `.bot run resume`: the party regroups first (the dead are revived, the
// living gather on the lead bot), then the paused step starts over with fresh
// tries; a lead bot far from it walks back on the navmesh (approach hops).
inline bool Resume(Run& r)
{
    if (r.state != State::Paused || !Current(r))
        return false;
    r.state = State::Running;
    r.ceilingPaused = false;
    r.ceilingQuietMs = 0;
    r.fighting = false;
    r.autoRegroups = 0;
    ResetStep(r);
    BeginRegroup(r);
    r.walkBackDue = true; // a party revived at the entrance walks back the plan's way
    r.note = "resumed: regrouping";
    return true;
}

// `.bot run skip`: leaves the current step out and goes on running (from a
// pause too, with the regroup of a resume). An item the step failed to get is
// noted missing: later steps that need it are left out with a note instead
// of failing one after the other.
inline bool Skip(Run& r)
{
    const PlanStep* s = Current(r);
    if (r.state == State::Done || !s)
        return false;
    bool const wasPaused = r.state == State::Paused;
    if (r.pendingMissing)
        r.missing.insert(r.pendingMissing);
    if (s->kind == PlanStepKind::Open)
        r.skippedOpenStep = r.step;
    if (s->kind == PlanStepKind::Pull || s->kind == PlanStepKind::Boss)
        r.notCleared.insert(r.step);
    // The step's pull state goes with the step (NextStep -> ResetStep); a
    // target refused as invalid-target is already out of reach for the run.
    std::size_t const skipped = r.step + 1;
    r.state = State::Running;
    r.ceilingPaused = false;
    r.ceilingQuietMs = 0;
    NextStep(r, "skipped step " + std::to_string(skipped));
    if (wasPaused && r.state == State::Running)
        BeginRegroup(r);
    return true;
}

// The run was paused by a player (PausePlayer: an order to the party, `.bot run pause`).
inline bool PausedByPlayer(const Run& r)
{
    return r.state == State::Paused && r.pausedByPlayer;
}

// A pause from the fight ceiling (kFightCapMs) goes on by itself once the
// party has been out of combat for kCeilingQuietMs (Uldaman run 4, step 38: the
// fight ended 15 s after the pause and the run waited 470 s for a manual
// resume). Once per step: a second ceiling on the same step stays paused. A
// player's pause and every other pause never come back by themselves. The glue
// ticks it while the run is paused; true when the run resumed.
inline bool CeilingFightOverWhilePaused(Run& r, bool partyInCombat, std::uint32_t dt)
{
    if (r.state != State::Paused || !r.ceilingPaused || PausedByPlayer(r) || r.ceilingResumedStep == r.step)
        return false;
    if (partyInCombat)
    {
        r.ceilingQuietMs = 0;
        return false;
    }
    r.ceilingQuietMs += dt;
    if (r.ceilingQuietMs < kCeilingQuietMs)
        return false;
    r.ceilingResumedStep = r.step;
    if (!Resume(r))
        return false;
    r.note = "the fight past the ceiling is over: going on by itself (regrouping)";
    return true;
}

// A player's pause: only `.bot run resume` (or skip) goes on from it.
inline void PausePlayer(Run& r, const std::string& why)
{
    Pause(r, why);
    r.pausedByPlayer = true;
}

// Any order from a player to the party.
inline bool PauseForPlayer(Run& r, const std::string& order)
{
    if (r.state != State::Running)
        return false;
    PausePlayer(r, "player order (" + order + "): .bot run resume to go on");
    return true;
}

// The glue's order was refused (ERR): pause with the module's own words. A
// refused `rest` (nobody had food or drink to use) only notes it: the rest
// gate's own ceiling ends the wait. A refused gather is sent once more, then
// left out (the pull or the step goes on with the party as it stands); a
// refused point of a walk back is passed for the next one.
// The follower a refused direct step names: `cmd move ... direct` answers
// "too-far: <bot> is not with <lead> (...)" when a bot has no way onto the
// lead bot's line. Empty for any other answer.
inline std::string DirectStragglerName(const std::string& answer)
{
    static const std::string head = "too-far: ";
    static const std::string mid = " is not with ";
    if (answer.compare(0, head.size(), head) != 0)
        return std::string();
    std::size_t const at = answer.find(mid, head.size());
    if (at == std::string::npos || at == head.size())
        return std::string();
    return answer.substr(head.size(), at - head.size());
}

inline std::string MissingHint(const Run& r)
{
    return r.pendingMissing ? " (item " + std::to_string(r.pendingMissing) +
        " missing: .bot run skip leaves out the steps that need it)" : std::string();
}

// The module's refusals of a loot order that another bot, or the facts, may
// settle (BeginLootAct): the loot rules (another member's turn, already looted,
// not tapped by the group), and a corpse with nothing left on it.
constexpr const char* kLootRightsText = "nothing this bot may loot";
constexpr const char* kLootEmptyText = "Nothing is left to loot";

inline bool LootRefusalOpen(const std::string& text)
{
    return text.find(kLootRightsText) != std::string::npos || text.find(kLootEmptyText) != std::string::npos;
}

// A loot order was refused or failed: the step does not pause yet. The next
// tick (TaskStep, with the facts) counts it done if a party bot carries the
// item, tries the bot whose turn it is or another one for a refusal on the
// loot rules, and pauses only when none is left.
inline void LootRefused(Run& r, const std::string& text)
{
    r.lootRefusal = text;
    r.lootRights = text.find(kLootRightsText) != std::string::npos;
    r.orderSent = false;
    EnterPhase(r, Phase::Begin);
    r.note = text + ": looking for another bot to take it";
}

// The island escape (Zul'Farrak r3-19z20, kGoodPathMinYd). The glue's reading
// of the lead bot's navmesh path toward the point it is sent to: partial, ending
// within kGoodPathMinYd of the lead bot and more than kArriveYd short of the point,
// or no path at all toward a point off the lead bot. Uldaman r9-19z26 step 127: the
// tank stood on a ~3 x 8 yd patch of mesh at 132-139 / 296-304 (z -23.8, the floor
// round it -26.5); every partial path from 137.3 298.7 ended 0.0-0.2 yd from it,
// inside the server's smooth-path slop (PathFinder.cpp SMOOTH_PATH_SLOP 0.4), so
// BuildPointPath made a one-point path and answered NOPATH with the point itself
// as the end: read as partial-only, no reading said island and the escape never ran.
// A NOPATH is one sign like a partial reading; IslandConfirmed still wants a second.
inline bool LeadOnIsland(bool pathNoPath, bool pathIncomplete, float endFromLeadYd, float toPointYd)
{
    if (pathNoPath)
        return toPointYd > kArriveYd;
    return pathIncomplete && endFromLeadYd >= 0.0f && endFromLeadYd <= kGoodPathMinYd &&
        toPointYd > endFromLeadYd + kArriveYd;
}

// ... or a path that gets somewhere: complete, or ending more than kGoodPathMinYd from it.
inline bool LeadPathGood(bool pathNoPath, bool pathIncomplete, float endFromLeadYd)
{
    return !pathNoPath && (!pathIncomplete || endFromLeadYd > kGoodPathMinYd);
}

// A spot around the lead bot that the hop may go to (the glue's ring search):
// its navmesh path toward the point is complete (a start snapped onto the
// island has none), or partial and ending more than kGoodPathMinYd from the
// spot AND from the lead bot (Detour snaps a start up to 5 yd onto the nearest
// polygon: a spot off the mesh can snap onto the island itself).
inline bool RingSpotLeadsOn(bool pathNoPath, bool pathIncomplete, float endFromSpotYd, float endFromLeadYd)
{
    return !pathNoPath && (!pathIncomplete || (endFromSpotYd > kGoodPathMinYd && endFromLeadYd > kGoodPathMinYd));
}

// A candidate of the move's detour (the glue's NearestReachablePoint): the lead
// bot's navmesh path to it ends within kDetourEndYd of it, and, when partial,
// leads on as a ring spot's must (RingSpotLeadsOn; the start is the lead bot):
// it ends more than kGoodPathMinYd from the lead bot. r6-19z24: from the island
// the partial paths to 1587 1050 and 1594.3 1048 ended 3.6 and 1.0 yd from the
// lead bot, within 5 yd of the point: both moves were refused (CheckMove's
// no-progress rule), each a pause.
// Never a point within kArriveYd of the lead bot (r9-19z26 Uldaman step 124: the
// 10 yd ring point 136 298.7 lay 1.3 yd from the tank on its own rubble island,
// its path complete, and the step counted as 'arrived' 10.3 yd from the plan's
// point without a yard walked).
inline bool DetourPointReachable(bool pathNoPath, bool pathIncomplete, float endFromPointYd, float endFromLeadYd,
    float pointFromLeadYd)
{
    return pointFromLeadYd > kArriveYd && endFromPointYd <= kDetourEndYd &&
        RingSpotLeadsOn(pathNoPath, pathIncomplete, endFromLeadYd, endFromLeadYd);
}

// A candidate of the detour must also lead on by its OWN path toward the step's
// point (the glue's probe from the candidate to it; RingSpotLeadsOn, as a ring
// spot of the island escape must). Uldaman r13-19z30 slot 5: the lead stood in
// a closed 6 x 6 yd pit; the pit's own point 42.1 374.3 (3.2 yd away, its path
// from the lead complete) was taken as 'the nearest reachable point, 10.0 yd from
// the plan's' although its probe to 50.8 379.3 ended 1.1 yd from it, in the pit, and
// step 97's pullback was refused. A candidate within kArriveYd of the point has
// nothing to lead on to (the first candidate is the point itself): not asked.
inline bool DetourCandidateLeadsOn(bool probeNoPath, bool probeIncomplete, float probeEndFromCandidateYd,
    float probeEndFromLeadYd, float candidateFromPointYd)
{
    return candidateFromPointYd <= kArriveYd ||
        RingSpotLeadsOn(probeNoPath, probeIncomplete, probeEndFromCandidateYd, probeEndFromLeadYd);
}

// The glue probes the path to the last good lead spot (Facts::goodSpotCut)
// only when that spot is off the lead bot (kArriveYd), within kGoodSpotProbeYd
// and kEscapeMaxVertical.
inline bool GoodSpotProbeWanted(float yd, float dz)
{
    return yd > kArriveYd && yd <= kGoodSpotProbeYd && std::fabs(dz) <= kEscapeMaxVertical;
}

// The glue's reading of the forward run's own move point in sight (NoteLeadWay,
// for MoveStep's island check) costs a navmesh search: it runs only on a tick
// on which MoveStep can act on the move (running, no walk back or regroup, a
// move step in sight and off its point, phase Begin, nobody in combat), and one
// reading serves kMoveProbeCacheMs for the same step, target and
// kMoveProbeCellYd cell of the lead bot (MoveProbeHit).
constexpr float kMoveProbeCellYd = 2.0f;
constexpr std::uint64_t kMoveProbeCacheMs = 5000;

struct MoveProbeCache
{
    bool known = false;
    std::size_t step = 0;
    int cellX = 0, cellY = 0;
    float tx = 0.0f, ty = 0.0f, tz = 0.0f;
    std::uint64_t atMs = 0; // Run::runMs of the reading
    bool island = false;    // Facts::leadIsland it gave
    bool good = false;      // Facts::leadPathGood it gave
};

inline bool ForwardMoveProbeWanted(const Run& r, bool moveStep, bool seen, bool anyInCombat, float toPointYd)
{
    return r.state == State::Running && !r.walkBack && !r.regroup && moveStep && seen && !anyInCombat &&
        r.phase == Phase::Begin && toPointYd > kArriveYd;
}

inline int MoveProbeCell(float v)
{
    return static_cast<int>(std::floor(v / kMoveProbeCellYd));
}

inline bool MoveProbeHit(const MoveProbeCache& c, std::size_t step, float lx, float ly, float tx, float ty, float tz,
    std::uint64_t nowMs)
{
    return c.known && c.step == step && c.cellX == MoveProbeCell(lx) && c.cellY == MoveProbeCell(ly) && c.tx == tx &&
        c.ty == ty && c.tz == tz && nowMs - c.atMs < kMoveProbeCacheMs; // a clock before it wraps: a miss
}

inline void StoreMoveProbe(MoveProbeCache& c, std::size_t step, float lx, float ly, float tx, float ty, float tz,
    std::uint64_t nowMs, bool island, bool good)
{
    c.known = true;
    c.step = step;
    c.cellX = MoveProbeCell(lx);
    c.cellY = MoveProbeCell(ly);
    c.tx = tx;
    c.ty = ty;
    c.tz = tz;
    c.atMs = nowMs;
    c.island = island;
    c.good = good;
}

inline int IslandCell(float v)
{
    return static_cast<int>(std::floor(v / kIslandCellYd));
}

// The last good spot as the hop's end: farther than kArriveYd from the lead bot
// (nearer is the island itself), within kEscapeMaxYd and kEscapeMaxVertical.
inline bool GoodSpotFits(float lx, float ly, float lz, float gx, float gy, float gz)
{
    float const dx = gx - lx, dy = gy - ly;
    return ai::PlanDistance(lx, ly, lz, gx, gy, gz) > kArriveYd && std::sqrt(dx * dx + dy * dy) <= kEscapeMaxYd &&
        std::fabs(gz - lz) <= kEscapeMaxVertical;
}

// One sample of the hop's straight line: the point on the line, and what the
// glue read there (the floor's height, any liquid: water, slime, magma).
struct SegmentSample
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
    bool ground = false;
    float groundZ = 0.0f;
    bool liquid = false;
};

// The samples of the line from (ax,ay,az) to (bx,by,bz): every kSegmentStepYd,
// at most kSegmentSamples, the end point last.
inline std::vector<SegmentSample> EscapeSegmentSamples(float ax, float ay, float az, float bx, float by, float bz)
{
    float const dx = bx - ax, dy = by - ay;
    float const len = std::sqrt(dx * dx + dy * dy);
    std::size_t n = static_cast<std::size_t>(std::ceil(len / kSegmentStepYd));
    n = n < 1 ? 1 : (n > kSegmentSamples ? kSegmentSamples : n);
    std::vector<SegmentSample> out(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        float const t = static_cast<float>(i + 1) / static_cast<float>(n);
        out[i].x = ax + t * dx;
        out[i].y = ay + t * dy;
        out[i].z = az + t * (bz - az);
    }
    return out;
}

enum class SegmentVerdict : std::uint8_t { Ok, NoGround, Step, Climb, Liquid, Hostile };

inline const char* SegmentVerdictName(SegmentVerdict v)
{
    switch (v)
    {
        case SegmentVerdict::Ok: return "ok";
        case SegmentVerdict::NoGround: return "no ground under the line";
        case SegmentVerdict::Step: return "a height step on the line";
        case SegmentVerdict::Climb: return "too far up or down";
        case SegmentVerdict::Liquid: return "liquid on the line";
        default: return "a hostile near the end";
    }
}

// The straight line may be walked: a floor within kSegmentGroundYd of the line at
// every sample, no step over kSegmentRiseYd between samples (the lead bot's own
// height first), never more than kEscapeMaxVertical from where it starts, no
// liquid, and no hostile within kEscapeHostileYd of the end. A step down may be
// up to kSegmentDropYd when the spot's own path leads on (dropOk: a ring spot that
// passed RingSpotLeadsOn), so the lead bot is never dropped into a pit with no
// way out (Uldaman r13-19z30); a rise stays at kSegmentRiseYd, and so does a drop
// to the last good spot (its path toward this point was not read).
inline SegmentVerdict CheckEscapeSegment(float startZ, const std::vector<SegmentSample>& samples, bool hostileNearEnd,
    bool dropOk = false)
{
    if (hostileNearEnd)
        return SegmentVerdict::Hostile;
    if (samples.empty())
        return SegmentVerdict::NoGround;
    float prev = startZ;
    for (const SegmentSample& s : samples)
    {
        if (!s.ground || std::fabs(s.groundZ - s.z) > kSegmentGroundYd)
            return SegmentVerdict::NoGround;
        if (s.liquid)
            return SegmentVerdict::Liquid;
        if (s.groundZ - prev > kSegmentRiseYd || prev - s.groundZ > (dropOk ? kSegmentDropYd : kSegmentRiseYd))
            return SegmentVerdict::Step;
        if (std::fabs(s.groundZ - startZ) > kEscapeMaxVertical)
            return SegmentVerdict::Climb;
        prev = s.groundZ;
    }
    return SegmentVerdict::Ok;
}

// Every tick: the living lead bot's spot is the last good one when this tick's
// path read got somewhere; that also ends the count of hops off an island.
inline void NoteLeadPath(Run& r, const Facts& f)
{
    r.leadPathGoodNow = f.leadAlive && f.leadPathGood;
    // The island's evidence holds for the lead bot's cell; the no-spot mark for its spot.
    if (r.islandEvidence && (IslandCell(f.leadX) != r.evidenceCellX || IslandCell(f.leadY) != r.evidenceCellY))
        r.islandEvidence = false;
    if (r.noSpotKnown && ai::PlanDistance(f.leadX, f.leadY, f.leadZ, r.noSpotX, r.noSpotY, r.noSpotZ) > kArriveYd)
        r.noSpotKnown = false;
    if (!r.leadPathGoodNow)
        return;
    r.islandEvidence = false;
    r.islandSuspect = false; // a path that gets somewhere: no island here (a refusal's sign included)
    r.noSpotKnown = false;
    r.goodLeadKnown = true;
    r.goodLeadX = f.leadX;
    r.goodLeadY = f.leadY;
    r.goodLeadZ = f.leadZ;
    r.goodLeadMs = r.runMs;
    r.islandEscapes = 0;
}

// Sign (a) for the forward run's own move point (MoveStep): the last good lead
// spot counts only when it was read within kGoodSpotFreshMs of run time; an
// older one says nothing about where the lead bot stands now. The false positive
// left: a one-way drop within that time (a fall in a fight); the straight hop up
// to the good spot is then refused by CheckEscapeSegment (a rise of more than
// kSegmentRiseYd per sample), a no-spot, and the move goes out as before.
inline bool GoodSpotFresh(const Run& r)
{
    return r.goodLeadKnown && r.runMs - r.goodLeadMs <= kGoodSpotFreshMs;
}

// Whether the lead bot stands on a navmesh island on its way to (tx,ty,tz): this
// tick's reading toward it says so (Facts::leadIsland) AND a second sign does:
// (a) the last good lead spot nearby cannot be reached (Facts::goodSpotCut), or
// (b) an earlier reading from the same spot (kEvidenceSpotYd, same cell) toward
// a point at least kEvidenceTargetYd from this one said so too (r3-19z20: the
// points of steps 64, 65, 67, 68 and 70 all came back partial from 1589.6 1046.9).
// A single reading is kept as evidence and changes nothing: the move is sent,
// refused and passed, or the hop walked, as before. Or two walk-back points were
// given up in a row from one spot (Run::islandSuspect). Never where a walk back's
// hop found no spot (Run::noSpotKnown): the walk back goes on as before there.
inline bool IslandConfirmed(Run& r, const Facts& f, float tx, float ty, float tz, bool goodSpotCounts = true)
{
    if (r.noSpotKnown && ai::PlanDistance(f.leadX, f.leadY, f.leadZ, r.noSpotX, r.noSpotY, r.noSpotZ) <= kArriveYd)
    {
        r.islandSuspect = false;
        return false;
    }
    if (!f.leadIsland)
        return r.islandSuspect;
    int const cx = IslandCell(f.leadX), cy = IslandCell(f.leadY);
    bool const sameCell = r.islandEvidence && cx == r.evidenceCellX && cy == r.evidenceCellY;
    bool const second = sameCell &&
        ai::PlanDistance(f.leadX, f.leadY, f.leadZ, r.evidenceX, r.evidenceY, r.evidenceZ) <= kEvidenceSpotYd &&
        ai::PlanDistance(tx, ty, tz, r.evidenceTX, r.evidenceTY, r.evidenceTZ) >= kEvidenceTargetYd;
    if (!sameCell)
    {
        r.islandEvidence = true;
        r.evidenceCellX = cx;
        r.evidenceCellY = cy;
        r.evidenceX = f.leadX;
        r.evidenceY = f.leadY;
        r.evidenceZ = f.leadZ;
        r.evidenceTX = tx;
        r.evidenceTY = ty;
        r.evidenceTZ = tz;
    }
    return (f.goodSpotCut && goodSpotCounts) || second || r.islandSuspect;
}

// The lead bot stands on a navmesh island (IslandConfirmed)
// on its way to (tx,ty,tz): before any point is passed or a hop counted, it is
// moved alone in a straight line off it. The order "escape from lx ly lz [to x y
// z] toward tx ty tz" is the glue's own (RunIslandEscape), not a `cmd`: from the
// spot the facts were read at (the glue refuses it once the lead bot moved
// kEscapeMovedYd), to the last good spot (x y z) when GoodSpotFits, else to the
// nearest spot around the lead bot whose path leads on (RingSpotLeadsOn); in
// sight, on a line CheckEscapeSegment passes. kIslandEscapes hops a step (a
// resume gives fresh ones), then a pause that names the spot. A refusal that
// starts "wait" (a fight, the lead bot moved) is not counted.
inline void IslandEscape(Run& r, const Facts& f, float tx, float ty, float tz, const std::string& what,
    std::string& order)
{
    r.islandSuspect = false;
    r.escapeFromMove = false;
    std::string const spot = Num(f.leadX) + " " + Num(f.leadY) + " " + Num(f.leadZ);
    if (r.islandEscapes >= kIslandEscapes || r.stepEscapes >= kStepEscapes)
    {
        Pause(r, "the lead bot stands on a navmesh island at " + spot + ": no path toward " + what + " (" +
            std::to_string(r.stepEscapes >= kStepEscapes ? kStepEscapes : kIslandEscapes) +
            " straight hops off it did not help); move it off, then .bot run resume");
        return;
    }
    ++r.islandEscapes;
    ++r.stepEscapes;
    r.escapeOn = true;
    r.escapeMs = 0;
    EnterPhase(r, Phase::Begin);
    bool const good = r.goodLeadKnown && GoodSpotFits(f.leadX, f.leadY, f.leadZ, r.goodLeadX, r.goodLeadY, r.goodLeadZ);
    order = "escape from " + Num(f.leadX) + " " + Num(f.leadY) + " " + Num(f.leadZ) + " " +
        (good ? "to " + Num(r.goodLeadX) + " " + Num(r.goodLeadY) + " " + Num(r.goodLeadZ) + " " : std::string()) +
        "toward " + Num(tx) + " " + Num(ty) + " " + Num(tz);
    r.note = "the lead bot stands on a navmesh island at " + spot + " (no path toward " + what + "): a straight hop off it (" +
        std::to_string(r.islandEscapes) + " of " + std::to_string(kIslandEscapes) + ")";
}

// The hop off the island under way: no other order until the lead bot stood
// still kEscapeSettleMs (or kEscapeCapMs went by); the tick after it reads the way afresh.
inline bool EscapeWait(Run& r, const Facts& f, std::uint32_t dt)
{
    if (!r.escapeOn)
        return false;
    r.escapeMs += dt;
    if (r.escapeMs < kEscapeCapMs && (f.leadMoving || r.escapeMs < kEscapeSettleMs))
        return true;
    r.escapeOn = false;
    EnterPhase(r, Phase::Begin);
    return true;
}

// A walk-back point given up ("on to the next"), the lead bot at (x,y,z). The
// second one in a row given up from one spot (moved less than kArriveYd), with
// no path read there that got anywhere, is not passed: the lead bot is taken to
// stand on an island and is hopped off it first (IslandEscape, via
// islandSuspect). Not for a direct step's point (its line crosses a navmesh
// hole; no path is read for it). r3-19z20 passed 23 points in 13 minutes from one spot.
inline void PassWalkBackPoint(Run& r, float x, float y, float z, const std::string& why, bool direct = false)
{
    if (direct)
        r.islandSuspect = false; // a direct step's point says nothing about an island
    // Where a walk back's hop found no spot the points are passed as before
    // (2012dfee): no suspicion there, or the mark would clear it and the same
    // point would go out again every tick.
    bool const noSpotHere =
        r.noSpotKnown && ai::PlanDistance(x, y, z, r.noSpotX, r.noSpotY, r.noSpotZ) <= kArriveYd;
    bool const same = !direct && !noSpotHere && r.passSpotKnown &&
        ai::PlanDistance(x, y, z, r.passX, r.passY, r.passZ) <= kArriveYd;
    r.passSpotKnown = true;
    r.passX = x;
    r.passY = y;
    r.passZ = z;
    r.approachHops = 0;
    r.walkBackRefusals = 0;
    EnterPhase(r, Phase::Begin);
    if (same && !r.leadPathGoodNow)
    {
        r.islandSuspect = true;
        r.note = why + ": the second point given up from this spot: the lead bot is hopped off it first";
        return;
    }
    r.note = why + ": on to the next";
    ++r.walkBackIdx;
    r.gatherWaived = false;
    r.gatherRefusals = 0;
}

// ai::kPlanFlagGoneIsDone: a loot or open row with the flag and no item counts the object being gone as done. `answer`
// is the module's text for a refused order ("not-found: ...", the by-entry search found nothing spawned) or a failed
// task ("invalid-target: The object is gone.", it despawned on the bot's way), each led by its code.
inline bool GoneIsDone(const PlanStep* s, const std::string& answer)
{
    if (!s || (s->kind != PlanStepKind::Loot && s->kind != PlanStepKind::Open) || s->item ||
        !(s->flags & ai::kPlanFlagGoneIsDone))
        return false;
    return answer.rfind("not-found:", 0) == 0 || answer.rfind("invalid-target: The object is gone", 0) == 0;
}

inline void GoneStepDone(Run& r, const PlanStep& s)
{
    NextStep(r, std::string(ai::PlanStepKindName(s.kind)) + " " + std::to_string(s.entry) + ": object gone: counted as done");
}

// A gossip NPC the module found dead (cmd gossip, ZF r12-19z29 step 220: Weegli Blastfuse 7607 lay dead at
// the door): the module's no-npc text then carries "a dead one lies nearby: <name> (<entry>) lies dead N yd
// away and respawns in <time> (...)". None is alive within the order's 40 yd, and a resume sends the same
// order into the same search, so the first refusal pauses plainly. The words claim no more than the module
// found: an NPC of the entry farther away than 40 yd is not ruled out. "" when the answer does not carry the
// mark. `what` is the step's own words ("gossip 7607 option 1").
constexpr const char* kGossipNpcDeadMark = "lies dead";

// The dead NPC's words in the answer: "Sergeant Bly (7604)", "the NPC" when none. "" without the mark.
inline std::string GossipNpcDeadSubject(const std::string& answer)
{
    std::size_t const mark = answer.find(kGossipNpcDeadMark);
    if (mark == std::string::npos)
        return std::string();
    std::size_t from = answer.rfind(": ", mark);
    from = from == std::string::npos ? 0 : from + 2;
    std::string subject = answer.substr(from, mark - from);
    while (!subject.empty() && subject.back() == ' ')
        subject.pop_back();
    return subject.empty() ? std::string("the NPC") : subject;
}

inline std::string GossipNpcDeadNote(const std::string& what, const std::string& answer)
{
    std::size_t const mark = answer.find(kGossipNpcDeadMark);
    if (mark == std::string::npos)
        return std::string();
    std::string const subject = GossipNpcDeadSubject(answer);
    std::string respawn;
    std::string const key = "respawns in ";
    std::size_t const at = answer.find(key, mark);
    if (at != std::string::npos)
    {
        std::size_t const start = at + key.size();
        std::size_t end = answer.find_first_of("(.", start);
        end = end == std::string::npos ? answer.size() : end;
        std::string time = answer.substr(start, end - start);
        while (!time.empty() && time.back() == ' ')
            time.pop_back();
        if (!time.empty())
            respawn = ", respawns in " + time;
    }
    else if (answer.find("no respawn timer", mark) != std::string::npos)
        respawn = ", it never respawns";
    return what + " cannot be done: none alive within 40 yd, a dead one lies nearby (" + subject + respawn +
        "). .bot run resume will not help while it lies dead; .bot run skip goes on without it (a later step that "
        "needs this NPC may fail too), .bot run stop ends the run";
}

// ai::kPlanFlagDeadNpcIsDone: a gossip row with the flag counts the NPC being dead as done. `answer` is the module's text for the
// refused order or the failed task; the dead-NPC wording (GossipNpcDeadNote's mark) is what counts: any other refusal still pauses.
// True (and the step ended, with the note) when it applied.
inline bool GossipDeadIsDone(Run& r, const PlanStep* s, const std::string& answer)
{
    if (!s || s->kind != PlanStepKind::Gossip || !(s->flags & ai::kPlanFlagDeadNpcIsDone))
        return false;
    std::string name = GossipNpcDeadSubject(answer);
    if (name.empty())
        return false;
    // "Sergeant Bly (7604)" -> "Sergeant Bly": the step's own words already name the entry.
    std::string const entry = " (" + std::to_string(s->entry) + ")";
    if (name.size() > entry.size() && name.compare(name.size() - entry.size(), entry.size(), entry) == 0)
        name.erase(name.size() - entry.size());
    NextStep(r, "gossip " + std::to_string(s->entry) + ": " + name + " lies dead, nothing to talk to: the step is counted done "
        "(the plan marks a dead NPC as done)");
    return true;
}

// Uldaman r16-19z34 step 39: a guard return walked a partial path to its end and
// left the tank on the dead-end tip of a ramp, 2.4 yd above a floor the navmesh
// does not link it to. The walk back to the stand point was refused unreachable
// ("going on from here"), then the step's own move to the same point with no ring
// point either: a pause, and a resume paused again. The one reading said island,
// but neither refusal was a second sign (IslandConfirmed), so the hop off it
// never ran. Such a refusal now is one, but only with source-side evidence: an
// `unreachable` whose refused path ended where the lead bot stands
// (LeadNoProgressRefusal; the glue marks it). Only for a plain move step's own point (or the walk back to its
// stand), off a walk back and a detour, while the last path read got nowhere,
// nowhere a hop found no spot (noSpotKnown), within the hop caps (kIslandEscapes,
// kStepEscapes) and at most kStepEscapes times a step (a hop refused "wait" gives
// its caps back). Past them the refusal pauses or goes on as before.
inline bool PathRefusalIslandSign(const Run& r, const std::string& answer)
{
    const PlanStep* s = Current(r);
    bool const noSpotHere =
        r.noSpotKnown && ai::PlanDistance(r.leadLastX, r.leadLastY, r.leadLastZ, r.noSpotX, r.noSpotY, r.noSpotZ) <= kArriveYd;
    return r.state == State::Running && s && s->kind == PlanStepKind::Move && !r.walkBack && !r.detour &&
        LeadNoProgressRefusal(answer) && !r.leadPathGoodNow && !noSpotHere && !r.islandSuspect &&
        r.islandEscapes < kIslandEscapes && r.stepEscapes < kStepEscapes && r.islandRefusals < kStepEscapes;
}

inline void TakeIslandSign(Run& r)
{
    ++r.islandRefusals;
    r.islandSuspect = true; // the next Begin tick's MoveStep (or ApproachHop) hops it off (IslandConfirmed)
    EnterPhase(r, Phase::Begin);
}

// The glue: the step's own move refused unreachable and no detour point found
// (NearestReachablePoint). True when it was taken as the island's second sign
// (the next tick hops the lead bot off); false: OrderRefused, the pause, as before.
inline bool RefusedFromIsland(Run& r, const std::string& order, const std::string& answer)
{
    const PlanStep* s = Current(r);
    if (!s || order != MoveOrder(s->x, s->y, s->z, false) || !PathRefusalIslandSign(r, answer))
        return false;
    TakeIslandSign(r);
    // The guard's walk back to the step's own point is this same order: it is not sent again.
    if (r.guardReturn && order == MoveOrder(r.guardX, r.guardY, r.guardZ, false))
        r.guardReturn = false;
    r.note = "cmd " + order + " refused (" + answer + "), no reachable point near it either: the lead bot may stand on a " +
        "navmesh island: a straight hop off it first";
    return true;
}

inline void OrderRefused(Run& r, const std::string& order, const std::string& answer)
{
    // The hop off a navmesh island (IslandEscape) found no spot: counted; the
    // next tick tries again or pauses.
    if (order.compare(0, 7, "escape ") == 0)
    {
        r.escapeOn = false;
        EnterPhase(r, Phase::Begin);
        bool const wait = answer.compare(0, 4, "wait") == 0;
        if (wait && r.islandEscapes > 0)
            --r.islandEscapes; // a fight or a lead bot on the move: not a try
        if (wait && r.stepEscapes > 0)
            --r.stepEscapes;
        // A walk back's hop that found no spot: from this spot the walk back goes
        // on as before (IslandConfirmed), passing the point, bounded by its own
        // ceilings. The same for the forward run's move check (escapeFromMove):
        // the move (and its detour) goes out as before, not a second hop and a pause.
        bool const fromMove = r.escapeFromMove;
        r.escapeFromMove = false;
        if (!wait && ((r.walkBack && r.walkBackIdx < r.step) || fromMove))
        {
            r.noSpotKnown = true;
            r.noSpotX = r.leadLastX;
            r.noSpotY = r.leadLastY;
            r.noSpotZ = r.leadLastZ;
        }
        r.note = "the straight hop off the navmesh island was not made (" + answer + ")" +
            (wait ? ": not counted" : "");
        return;
    }
    // A revived bot's summon (SummonRevived): refused, it walks back.
    if (order.compare(0, 7, "summon ") == 0 && r.reviveSummons.count(order.substr(7)))
    {
        ReviveSummonFailed(r, order.substr(7), "was refused (" + answer + ")");
        return;
    }
    // The summon of a direct step's follower: `busy` (a summon of that bot is
    // pending already) keeps the wait; any other refusal ends it, so the step
    // goes out again at once (and pauses if that bot is named again).
    if (order.compare(0, 7, "summon ") == 0 && !r.summonWait.empty() && order.substr(7) == r.summonWait)
    {
        if (answer.compare(0, 4, "busy") != 0)
        {
            r.summonWait.clear();
            r.summonWaitMs = 0;
        }
        r.note = order + " refused: " + answer + (r.summonWait.empty() ? "" : ": waiting for it");
        return;
    }
    // The loot method orders of a loot step's free-for-all detour (LootFfaStep). A refused `ffa` ends the
    // detour: the step goes on the old way. A refused restore only notes it: the next tick sees the method
    // is not back, and the glue puts it back when the run pauses or stops.
    if (order == "lootmethod ffa" && r.lootFfa == LootFfa::Ordered)
    {
        r.lootFfa = LootFfa::Idle;
        r.lootFfaLive = false;
        r.note = "loot: free-for-all refused (" + answer + "): looking for another bot to take it";
        return;
    }
    if (order.compare(0, 11, "lootmethod ") == 0)
    {
        r.note = order + " refused: " + answer;
        return;
    }
    // A kill_first focus every bot refused (an evading, fleeing unit): the
    // focus is not left on it. Its unit is passed over kFocusRefusedIgnoreMs,
    // and the next tick's pick goes to the next kill_first unit, or to none
    // (the bots' own picks, the plan's order). It never pauses.
    if (order.compare(0, 6, "focus ") == 0)
    {
        std::uint64_t const guid = std::strtoull(order.c_str() + 6, nullptr, 10);
        r.focusRefused[guid] = r.runMs + kFocusRefusedIgnoreMs;
        r.focusUnstayed = true;
        if (r.focusGuid == guid)
            r.focusGuid = 0;
        r.note = order + " refused: " + answer + ": the focus is picked again (that unit left out " +
            std::to_string(kFocusRefusedIgnoreMs / 1000) + " s)";
        return;
    }
    if (order == "rest" || order == "revive party" || order.compare(0, 7, "summon ") == 0 ||
        order.compare(0, 3, "cc ") == 0 || order.compare(0, 7, "attack ") == 0)
    {
        // The rest gate, the regroup and the gather have their own ceilings;
        // crowd control and the tank's pick-up (GuardTarget) are help, the
        // fight goes on without them (a refused focus: above).
        r.note = order + " refused: " + answer;
        return;
    }
    // The rez hold's nudge (the lead bot walks onto a body). A path refusal (the ledge is not on the navmesh)
    // ends the hold: the revive and the summon of the revived bot go out instead of a pause. Any other refusal
    // only notes it; the hold's own budget ends it, and the nudge is not sent a second time.
    if (!r.rezNudgeOrder.empty() && order == r.rezNudgeOrder)
    {
        r.rezNudgeOrder.clear();
        if (IsPathRefusal(answer))
        {
            r.rezBodyUnreachable = true;
            r.note = "regroup: the lead bot cannot walk to the body (" + answer + "): reviving instead";
        }
        else
            r.note = "regroup: the walk to the body was refused (" + answer + ")";
        return;
    }
    if (!r.gatherOrder.empty() && order == r.gatherOrder)
    {
        r.gatherOrder.clear();
        bool const again = ++r.gatherRefusals < kGatherTries;
        r.gatherWaived = !again;
        if (r.regroup)
            r.regroupGatherSent = false;
        else
            EnterPhase(r, Phase::Begin);
        r.note = "gather refused (" + answer + "): " + (again ? "sending it once more" : "going on without it");
        return;
    }
    // The walk back to the stand point after a pick-up (GuardTarget): the
    // step goes on from where the lead bot stands; refused for want of a path
    // from it, the lead bot is hopped off first (PathRefusalIslandSign).
    if (r.guardReturn && order == MoveOrder(r.guardX, r.guardY, r.guardZ, false))
    {
        r.guardReturn = false;
        EnterPhase(r, Phase::Begin);
        if (PathRefusalIslandSign(r, answer))
        {
            TakeIslandSign(r);
            r.note = "back to the stand point refused (" + answer + "): the lead bot may stand on a navmesh island: " +
                "a straight hop off it first";
            return;
        }
        r.note = "back to the stand point refused (" + answer + "): going on from here";
        return;
    }
    const PlanStep* s = Current(r);
    // A direct step (the step's own or a walk back's) refused because a
    // follower has no way onto the lead bot's line and no straight line of its
    // own (it stands over the navmesh hole the step before crossed, or was left
    // behind): that follower is summoned to the lead bot, and the step goes out
    // again. Summoned again by DirectSummonAllowed (after kDirectResummonMs, or
    // after it died and came back; at most kDirectSummonCap a step), not once
    // per step: r12-19z29 the revive's summon used that once, and the refusal
    // paused the run. No summon allowed yet: the run waits for that bot
    // (directWaitFor, guarding on) until DirectPauseDue, then the pause below.
    if (order.size() > 7 && order.compare(order.size() - 7, 7, " direct") == 0)
    {
        std::string const name = DirectStragglerName(answer);
        if (!name.empty())
        {
            NoteDirectTrouble(r);
            if (DirectSummonAllowed(r, name))
            {
                MarkSummoned(r, name);
                r.summonDue = name;
                EnterPhase(r, Phase::Begin);
                r.note = "direct step: " + name + " is not with the lead bot: summoning it";
                return;
            }
            if (!DirectPauseDue(r, name))
            {
                r.directWaitFor = name;
                r.directWaitMs = 0;
                EnterPhase(r, Phase::Begin);
                auto const it = r.stepSummons.find(name);
                std::uint32_t const summons = it == r.stepSummons.end() ? 0 : it->second.count;
                r.note = "direct step: " + name + " is not with the lead bot (summoned " + std::to_string(summons) +
                    " times this step): waiting for it to come";
                return;
            }
        }
    }
    if (SpentRitual(r, order, answer))
    {
        Pause(r, SpentRitualNote("ritual " + std::to_string(s ? s->entry : 0)));
        return;
    }
    // A pull target the module refuses as such (invalid-target: not an enemy,
    // or the group cannot attack it; Zul'Farrak's Sul'lithuz Hatchlings 8130)
    // is left out for the run; the step goes on with what is left. Other
    // refusals of a pullback (a busy tank, no tank, the lead bot unavailable)
    // pause as before.
    if (order.compare(0, 9, "pullback ") == 0 && answer.compare(0, 14, "invalid-target") == 0)
    {
        std::uint64_t const guid = std::strtoull(order.c_str() + 9, nullptr, 10);
        r.unreachable.insert(guid);
        if (r.pullGuid == guid)
            r.pullGuid = 0;
        EnterPhase(r, Phase::Begin);
        r.note = "pull refused (" + answer + "): that unit is left out";
        return;
    }
    if (r.walkBack && r.plan && r.walkBackIdx < r.step)
    {
        const PlanStep& p = r.plan->steps[r.walkBackIdx];
        if (order == MoveOrder(p.x, p.y, p.z, p.kind == PlanStepKind::MoveDirect))
        {
            std::string const point = "walk back: the point of step " + std::to_string(r.walkBackIdx + 1);
            if (IsPathRefusal(answer) && DirectStillToCross(r, r.walkBackIdx))
            {
                Pause(r, point + " refused (" + answer + "): a direct step over a navmesh hole is not passed; " +
                    "gather the party at the point before it, then .bot run resume");
                return;
            }
            if (IsPathRefusal(answer))
            {
                // The point itself cannot be walked to now: the next one.
                PassWalkBackPoint(r, r.leadLastX, r.leadLastY, r.leadLastZ, point + " refused (" + answer + ")",
                    p.kind == PlanStepKind::MoveDirect);
                return;
            }
            // The lead bot cannot go right now (dead, teleporting, busy ...):
            // the same point again, a few times, then pause.
            if (++r.walkBackRefusals > kWalkBackRetries)
            {
                Pause(r, point + " refused " + std::to_string(r.walkBackRefusals) + " times (" + answer + ")");
                return;
            }
            r.note = point + " refused (" + answer + "): sending it again";
            EnterPhase(r, Phase::Begin);
            return;
        }
    }
    if (s && s->kind == PlanStepKind::UseItem && s->item && !r.missing.empty())
    {
        // A use that cannot work because an earlier item is missing (a
        // reagent): a consequence, not a second failure.
        r.missing.insert(s->item);
        NextStep(r, "use_item " + std::to_string(s->entry) + " refused (" + answer + "): an earlier item is missing; " +
            "item " + std::to_string(s->item) + " is noted missing too");
        return;
    }
    if ((order.compare(0, 5, "loot ") == 0 || order.compare(0, 5, "open ") == 0) && GoneIsDone(s, answer))
    {
        GoneStepDone(r, *s);
        return;
    }
    if (s && (s->kind == PlanStepKind::Loot || s->kind == PlanStepKind::UseItem) && s->item)
        r.pendingMissing = s->item;
    if (s && s->kind == PlanStepKind::Loot && order.compare(0, 5, "loot ") == 0 && LootRefusalOpen(answer))
    {
        LootRefused(r, "cmd " + order + " refused: " + answer);
        return;
    }
    if (s && s->kind == PlanStepKind::Gossip && order.compare(0, 7, "gossip ") == 0)
    {
        if (GossipDeadIsDone(r, s, answer))
            return;
        std::string const dead = GossipNpcDeadNote(
            "gossip " + std::to_string(s->entry) + " option " + std::to_string(s->param), answer);
        if (!dead.empty())
        {
            Pause(r, dead);
            return;
        }
    }
    Pause(r, "cmd " + order + " refused: " + answer + MissingHint(r));
}

inline bool NeedsRest(const Facts& f, const RestLines& l)
{
    return f.minHealthPct < l.startHealth || f.minHealerManaPct < l.startHealerMana || f.minManaPct < l.startMana;
}

inline bool Rested(const Facts& f, const RestLines& l)
{
    return f.minHealthPct >= l.endHealth && f.minHealerManaPct >= l.endHealerMana && f.minManaPct >= l.endMana;
}

// How far the lead bot is from a plain walk's point for the stalled arrival
// (kStalledArriveYd): the ground-plane distance while the point is no more than
// kStandZTolYd above or below it (a stand on a ledge a few yards off the lead
// bot's floor, which its horizontal reach covers), else the 3D distance (a real
// other floor). Ragefire Chasm: stands 3-4 yd above or below at 6.0-6.7 yd in
// 3D paused the run as stuck, 5.2-5.4 yd on the ground.
inline float StandDistance(float lx, float ly, float lz, float x, float y, float z)
{
    float const dz = lz > z ? lz - z : z - lz;
    if (dz <= kStandZTolYd)
        return std::sqrt((lx - x) * (lx - x) + (ly - y) * (ly - y));
    return ai::PlanDistance(lx, ly, lz, x, y, z);
}

// The executor's deadband for a `cmd move`: the order puts the lead bot on stay and
// runs "return to stay position", whose MoveToPositionAction::isUseful
// (ai/playerbot/strategy/actions/PositionAction.cpp) acts only when the bot is more
// than GetRange("follow") from the point on the ground; inside a dungeon or raid that
// range is AiPlayerbot.RaidFollowDistance (PlayerbotAI::GetRange "followraid",
// default 5.0 in PlayerbotAIConfig.cpp). A lead bot this close does not move at all.
// Conservative: the core's GetDistance2d subtracts the bot's bounding radius by
// default (Object.cpp WorldObject::GetDistance2d, SizeFactor::BoundingRadius) and
// ServerFacade::GetDistance2d rounds to 0.1 yd, so the executor's real deadband is
// about 5.0 + the radius (roughly 5.3-6.0 yd by race). The constant is the config
// default: a server with a larger RaidFollowDistance keeps the old pause for a lead
// bot between 5.0 yd and that value away.
constexpr float kFollowDeadbandYd = 5.0f;
// The lead bot and the point share a floor (a ramp, not a ledge over another
// floor) when the point is at most its ground distance plus this much above or
// below it: a slope of about 45 degrees. A slope bound, not navmesh evidence: a
// ledge over a second floor within that bound reads as the same floor.
constexpr float kSharedFloorRiseYd = 1.0f;

// A stalled plain walk the executor would not move: within kFollowDeadbandYd on the
// ground and on the point's floor (Zul'Farrak r16-19z34 step 37: 4.84 yd on the
// ground, 4.6 yd above, 6.6 yd in 3D; the guard's walk back paused the run).
inline bool InFollowDeadband(float lx, float ly, float lz, float x, float y, float z)
{
    float const ground = std::sqrt((lx - x) * (lx - x) + (ly - y) * (ly - y));
    float const dz = lz > z ? lz - z : z - lz;
    return ground <= kFollowDeadbandYd && dz <= ground + kSharedFloorRiseYd;
}

// Walks the party to a point. True once the lead bot is there; otherwise
// `order` may hold the move to send. Pauses after kWalkTries moves without
// progress. `plainWalk`: a Move step's walk or the guard's walk back to the stand
// (not a pull's, a hop's or an exact point's), which also counts a stall inside the
// executor's follow deadband (InFollowDeadband) as arrived.
inline bool WalkTo(Run& r, const Facts& f, float x, float y, float z, bool direct, std::uint32_t dt,
    std::string& order, bool plainWalk = false)
{
    float const dist = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, x, y, z);
    // The anchor is where the lead bot actually stands on arrival: the point,
    // or, for a stalled arrival short of it (a point off the mesh), its own spot.
    auto arrived = [&](float ax, float ay, float az)
    {
        r.anchorKnown = true;
        r.anchorX = ax;
        r.anchorY = ay;
        r.anchorZ = az;
        return true;
    };
    if (dist <= kArriveYd)
        return arrived(x, y, z);
    bool const fresh = r.phase != Phase::Walk || r.walkX != x || r.walkY != y || r.walkZ != z;
    if (fresh)
    {
        EnterPhase(r, Phase::Walk);
        r.walkX = x;
        r.walkY = y;
        r.walkZ = z;
        r.walkTries = 1;
        r.walkStillMs = 0;
        r.walkBestYd = dist;
        order = MoveOrder(x, y, z, direct);
        return false;
    }
    r.walkStillMs += dt;
    if (dist < r.walkBestYd - kWalkProgressYd)
    {
        r.walkBestYd = dist;
        r.walkStillMs = 0;
    }
    // A plain walk to a point close by (kStalledArriveYd) that gains nothing at
    // all is done after 3 s: a `cmd move` to a point 3-6 yd away does not move
    // the lead bot, and the 12 s rule made every such plan row idle 13 s (WC
    // run B: 20 rows of 12-15 s, 275 s of a 101 min run; rows 28-31 were 3-6 yd
    // hops). A direct step (over a navmesh hole) keeps the 12 s rule and its
    // checkpoint.
    // A plain walk judges the stalled arrival by StandDistance (a stand on a ledge);
    // a direct step keeps the 3D distance.
    float const stallYd = direct ? dist : StandDistance(f.leadX, f.leadY, f.leadZ, x, y, z);
    // A plain walk stalled inside the executor's follow deadband on the point's floor
    // (InFollowDeadband) is arrived as well: a `cmd move` cannot move the lead bot there,
    // so retrying only paused the run (Zul'Farrak r16-19z34 step 37, 6.6 yd in 3D).
    bool const deadband = plainWalk && InFollowDeadband(f.leadX, f.leadY, f.leadZ, x, y, z);
    if (!direct && r.walkStillMs >= kShortStallMs && (stallYd <= kStalledArriveYd || deadband))
        return arrived(f.leadX, f.leadY, f.leadZ);
    if (r.walkStillMs >= kWalkRetryMs && stallYd <= kStalledArriveYd)
        return arrived(f.leadX, f.leadY, f.leadZ);
    if (r.walkStillMs >= kWalkRetryMs)
    {
        if (r.walkTries >= kWalkTries)
        {
            Pause(r, "the lead bot is stuck " + Num(dist) + " yd from " + Num(x) + " " + Num(y) + " " + Num(z));
            return false;
        }
        ++r.walkTries;
        r.walkStillMs = 0;
        order = MoveOrder(x, y, z, direct);
    }
    return false;
}

inline float GroundDistance(float ax, float ay, float bx, float by)
{
    return std::sqrt((ax - bx) * (ax - bx) + (ay - by) * (ay - by));
}

// Where a lead bot standing too near exact waypoint `idx` to be moved onto it
// (kExactBackYd on the ground plane: the module carries out no shorter move)
// walks back to first: the latest plan point before it on the run's own route
// that is a plain move, at least kExactBackYd from the lead bot and from the
// waypoint on the ground plane and within kSeeYd of the waypoint. Never past a
// direct step (back over a hole). False: none.
inline bool ExactBackPoint(const Run& r, const Facts& f, std::size_t idx, float& x, float& y, float& z)
{
    const PlanStep& p = r.plan->steps[idx];
    for (std::size_t i = idx; i-- > 0;)
    {
        const PlanStep& q = r.plan->steps[i];
        if (q.kind == PlanStepKind::MoveDirect)
            return false;
        if (q.kind != PlanStepKind::Move)
            continue;
        if (GroundDistance(q.x, q.y, f.leadX, f.leadY) < kExactBackYd || GroundDistance(q.x, q.y, p.x, p.y) < kExactBackYd)
            continue;
        if (ai::PlanDistance(q.x, q.y, q.z, p.x, p.y, p.z) > kSeeYd)
            continue;
        x = q.x;
        y = q.y;
        z = q.z;
        return true;
    }
    return false;
}

// Walks the party to exact waypoint `idx` (ai::kPlanFlagExact) and proves the
// lead bot stands on it. True once proved: within kExactArriveYd of the point,
// not moving, and on one spot (kExactStillYd) for kExactProofMs; the step is
// then done and Run::exactProved names it. The rules of the plain walk do not
// end it: its 3 yd arrival and its stalled arrival (6 yd) only start the
// tighten. The tighten orders only while the lead bot stands (never over the
// move that carries it), after kShortStallMs of standing: a lead bot farther
// than kExactBackYd on the ground plane is sent onto the point again; a nearer
// one walks back to ExactBackPoint, then onto the point (a `cmd move` that
// short is not carried out). At most kWalkTries moves onto the point, and no
// proof within kExactCapMs out of combat pauses the run: the step after it (a
// direct step) is never sent from short of the point.
inline bool ExactWalkTo(Run& r, const Facts& f, std::size_t idx, std::uint32_t dt, std::string& order)
{
    const PlanStep& p = r.plan->steps[idx];
    std::string const what = "the exact point of step " + std::to_string(idx + 1) + " (" + Num(p.x) + " " +
        Num(p.y) + " " + Num(p.z) + ")";
    if (!r.exactOn)
    {
        if (!WalkTo(r, f, p.x, p.y, p.z, false, dt, order))
            return false;
        ResetExact(r);
        r.exactOn = true;
        EnterPhase(r, Phase::Walk);
        r.walkX = p.x;
        r.walkY = p.y;
        r.walkZ = p.z;
    }
    r.exactMs += dt;
    float const dist = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, p.x, p.y, p.z);
    bool const moved = r.exactLastSet &&
        ai::PlanDistance(f.leadX, f.leadY, f.leadZ, r.exactLastX, r.exactLastY, r.exactLastZ) > kExactStillYd;
    bool const moving = f.leadMoving || moved;
    r.exactLastSet = true;
    r.exactLastX = f.leadX;
    r.exactLastY = f.leadY;
    r.exactLastZ = f.leadZ;
    // The proof: looks on one spot within kExactArriveYd, the lead bot not moving.
    if (dist <= kExactArriveYd && !moving)
    {
        if (!r.exactAnchorSet ||
            ai::PlanDistance(f.leadX, f.leadY, f.leadZ, r.exactAnchorX, r.exactAnchorY, r.exactAnchorZ) > kExactStillYd)
        {
            r.exactAnchorSet = true;
            r.exactAnchorX = f.leadX;
            r.exactAnchorY = f.leadY;
            r.exactAnchorZ = f.leadZ;
            r.exactStillMs = 0;
        }
        else
            r.exactStillMs += dt;
        if (r.exactStillMs >= kExactProofMs)
        {
            ResetExact(r);
            r.exactProved = idx;
            r.anchorKnown = true;
            r.anchorX = p.x;
            r.anchorY = p.y;
            r.anchorZ = p.z;
            return true;
        }
        r.note = "on " + what + ": proving the lead bot stands still on it";
        return false;
    }
    r.exactAnchorSet = false;
    r.exactStillMs = 0;
    if (r.exactMs >= kExactCapMs)
    {
        Pause(r, what + ": the lead bot was not proved to stand on it within " + std::to_string(kExactCapMs / 1000) +
            " s (" + Num(dist) + " yd away, " + (moving ? "still moving" : "not on the point") + "); the next step " +
            "is not sent from short of it: put the lead bot on the point (within " + Num(kExactArriveYd) +
            " yd), then .bot run resume");
        return false;
    }
    if (moving)
    {
        r.exactWaitMs = 0;
        r.note = "walking onto " + what;
        return false;
    }
    r.exactWaitMs += dt;
    if (r.exactWaitMs < kShortStallMs)
    {
        r.note = "walking onto " + what + " (" + Num(dist) + " yd away)";
        return false;
    }
    // Walked back to the back point (or stopped on the way): onto the point.
    if (r.exactStage == 1 &&
        GroundDistance(f.leadX, f.leadY, p.x, p.y) >= kExactBackYd - kExactArriveYd && r.exactRounds < kWalkTries)
    {
        ++r.exactRounds;
        r.exactStage = 2;
        r.exactWaitMs = 0;
        order = MoveOrder(p.x, p.y, p.z, false);
        r.note = "walking onto " + what + " from the point behind it";
        return false;
    }
    if (r.exactRounds >= kWalkTries)
    {
        r.note = "waiting on " + what + " (" + Num(dist) + " yd away)";
        return false;
    }
    float bx = 0.0f, by = 0.0f, bz = 0.0f;
    if (GroundDistance(f.leadX, f.leadY, p.x, p.y) < kExactBackYd && ExactBackPoint(r, f, idx, bx, by, bz))
    {
        r.exactStage = 1;
        r.exactBackX = bx;
        r.exactBackY = by;
        r.exactBackZ = bz;
        r.exactWaitMs = 0;
        order = MoveOrder(bx, by, bz, false);
        r.note = what + ": the lead bot stands " + Num(dist) + " yd off it, too near for a move onto it: back to " +
            Num(bx) + " " + Num(by) + " " + Num(bz) + " on the plan's way first";
        return false;
    }
    ++r.exactRounds;
    r.exactStage = 2;
    r.exactWaitMs = 0;
    order = MoveOrder(p.x, p.y, p.z, false);
    r.note = "walking onto " + what + " again (" + Num(dist) + " yd away)";
    return false;
}

// The unit to pull now: hostile, not evading, not passed, not stealthed;
// nearest to the lead bot. When `entry` is not 0 only that entry. Null for none.
// `hop`: a hop pick (Facts::hopUnits), which also leaves out Run::neverCame.
inline const UnitFact* PickPull(const Run& r, const std::vector<UnitFact>& units, std::uint32_t entry,
    bool hop = false)
{
    const UnitFact* best = nullptr;
    for (const UnitFact& u : units)
    {
        if (!u.hostile || u.evading || u.stealthed || r.passed.count(u.guid) || r.unreachable.count(u.guid))
            continue;
        if (hop && r.neverCame.count(u.guid))
            continue;
        if (entry && u.entry != entry)
            continue;
        if (!best || u.distLead < best->distLead)
            best = &u;
    }
    return best;
}

// Units the step still has to deal with: hostile, alive (the glue lists only
// living ones), not passed. Evading ones count (they come back).
inline std::size_t OpenUnits(const Run& r, const std::vector<UnitFact>& units)
{
    std::size_t n = 0;
    for (const UnitFact& u : units)
        if (u.hostile && !r.passed.count(u.guid) && !r.unreachable.count(u.guid))
            ++n;
    return n;
}

// Whether a unit may be pulled or crowd-controlled now (PickPull's rules).
inline bool Pullable(const Run& r, const UnitFact& u)
{
    return u.hostile && !u.evading && !u.stealthed && !r.passed.count(u.guid) && !r.unreachable.count(u.guid);
}

// The party's crowd control: the first living mage that knows Polymorph, "" none.
inline std::string CcBot(const Facts& f)
{
    for (const MemberFact& m : f.members)
        if (m.polymorph && !m.name.empty())
            return m.name;
    return std::string();
}

// In the fight after a `cc moon`: the Polymorph aura must show on that unit
// within kCcLandMs. Not there: the mark once more, then noted as not landed.
// Returns true when it sent an order.
inline bool CheckCc(Run& r, const Facts& f, std::uint32_t dt, std::string& order)
{
    if (!r.ccPending)
        return false;
    const UnitFact* held = nullptr;
    for (const UnitFact& x : f.units)
        if (x.guid == r.ccPending)
            held = &x;
    if (!held || held->polymorphed)
    {
        if (held)
            r.note = "fighting: " + std::to_string(held->entry) + " is polymorphed";
        r.ccPending = 0;
        return false;
    }
    // Counted only while the crowd control itself is in the fight.
    std::string const bot = CcBot(f);
    for (const MemberFact& m : f.members)
        if (m.name == bot && m.inCombat)
            r.ccWaitMs += dt;
    if (r.ccWaitMs < kCcLandMs)
        return false;
    if (!r.ccResent && !bot.empty())
    {
        r.ccResent = true;
        r.ccWaitMs = 0;
        r.note = "fighting: the crowd control on " + std::to_string(held->entry) + " did not land: once more";
        order = "cc moon " + std::to_string(held->guid) + " " + bot;
        return true;
    }
    r.note = "fighting: cc did not land on " + std::to_string(held->entry);
    r.ccPending = 0;
    return false;
}

// Whether a unit this step crowd-controlled is still alive among the step's
// units (one mage holds one Polymorph).
inline bool CcHeld(const Run& r, const Facts& f)
{
    for (const UnitFact& u : f.units)
        if (r.ccSent.count(u.guid))
            return true;
    return false;
}

// The pack member the run crowd-controls before a pull of `pullGuid`: the
// best-ranked of the plan's cc entries (cc_priority, then cc_always) among
// the step's units, never the pull target, not one already done this step,
// and none while an earlier one is still alive. Null without a mage in the
// party or a plan list. Never the boss of a boss step or one of its own adds
// (the step's entries, killed first in its kill order): Uldaman's cc lists
// (20261021174717) name the Geomancer 4853 that Grimlok's step lists and the
// 7030 of Galgann's, and a polymorphed add of the kill order holds the fight
// up (turtle-agent remote_cc.go never sheeps the active boss's `first` adds).
inline bool BossStepUnit(const PlanStep* s, std::uint32_t entry)
{
    if (!s || s->kind != PlanStepKind::Boss)
        return false;
    return entry == s->entry || std::find(s->entries.begin(), s->entries.end(), entry) != s->entries.end();
}

inline const UnitFact* PickCc(const Run& r, const Facts& f, std::uint64_t pullGuid)
{
    if (!r.plan || CcBot(f).empty() || CcHeld(r, f))
        return nullptr;
    const PlanStep* const step = Current(r);
    const UnitFact* best = nullptr;
    int bestRank = 0;
    for (const UnitFact& u : f.units)
    {
        if (u.guid == pullGuid || !Pullable(r, u) || r.ccSent.count(u.guid) || BossStepUnit(step, u.entry))
            continue;
        int const rank = r.plan->CcRank(u.entry);
        if (rank < 0)
            continue;
        if (!best || rank < bestRank || (rank == bestRank && u.distLead < best->distLead))
        {
            best = &u;
            bestRank = rank;
        }
    }
    return best;
}

// The pull stand rule is on for this pull (AiPlayerbot.DungeonRunPullStand):
// the key, a ranged weapon shot the glue could read with more range than the
// margin (the glue reads no stand below that), and not inside a scripted event.
inline bool PullStandOn(const Run& r, const Facts& f)
{
    return f.pullStand && f.leadShotYd > kShotMarginYd && !InEvent(r);
}

// The tank's shot can go out from where it stands: the unit in sight and
// within the shot's range less the module's reach margin.
inline bool ShootableFromLead(const Facts& f, const UnitFact& u)
{
    return u.losLead && u.distLead <= f.leadShotYd - kShotMarginYd;
}

// A pack's pull target (a pull step): a kill_first unit first (best rank,
// then nearest), else the nearest; never the unit the crowd control is for,
// or a crowd-controlled one, while another can be pulled. Null for none.
// With the pull stand on, among units of the same rank one the tank can shoot
// from where it stands goes before a nearer one it cannot.
inline const UnitFact* PickPackPull(const Run& r, const Facts& f)
{
    const UnitFact* cc = PickCc(r, f, 0);
    const UnitFact* best = nullptr;
    const UnitFact* held = nullptr;
    int bestRank = -1;
    bool const stand = PullStandOn(r, f);
    for (const UnitFact& u : f.units)
    {
        if (!Pullable(r, u))
            continue;
        if ((cc && u.guid == cc->guid) || r.ccSent.count(u.guid))
        {
            if (!held || u.distLead < held->distLead)
                held = &u;
            continue;
        }
        int const rank = r.plan ? r.plan->KillFirstRank(u.entry) : -1;
        bool better = !best;
        if (best)
        {
            if (rank >= 0 && (bestRank < 0 || rank < bestRank))
                better = true;
            else if (rank == bestRank && stand && ShootableFromLead(f, u) != ShootableFromLead(f, *best))
                better = ShootableFromLead(f, u);
            else if (rank == bestRank && u.distLead < best->distLead)
                better = true;
        }
        if (better)
        {
            best = &u;
            bestRank = rank;
        }
    }
    return best ? best : held;
}

// Whether the kill_first focus is bounded by distance: on a pull or move
// step outside a scripted event. A boss step and an event are not (Zum'rah's
// Ward 7785 is focused wherever it stands while the tank holds his adds).
inline bool KillFirstBounded(const Run& r)
{
    const PlanStep* s = Current(r);
    return s && !InEvent(r) &&
        (s->kind == PlanStepKind::Pull || s->kind == PlanStepKind::Move || s->kind == PlanStepKind::MoveDirect);
}

// The focused unit is gone: no longer among this fight's kill_first units (it
// died: the glue lists living ones only) or crowd-controlled by the run. Then
// there is nothing left to take the focus back from.
inline bool FocusGone(const Run& r, const Facts& f)
{
    if (r.ccSent.count(r.focusGuid))
        return true;
    for (const UnitFact& u : f.killFirst)
        if (u.guid == r.focusGuid)
            return false;
    return true;
}

// In a fight: the kill_first unit to focus (best rank, then nearest; not a
// crowd-controlled one). On a pull or move step (KillFirstBounded) only one
// within kKillFirstMaxYd of the lead bot (the tank): the focus sends every bot
// at it, and Zul'Farrak r5-19z23 step 27 lost the party twice to a focus on a
// witch doctor 20 and 43 yd off, in its camp. A farther one is left to come
// in; the bots fight with their own picks in the meantime. The bound is for a
// new focus only: the unit already focused stays eligible at any distance, so
// a better-ranked one walking back and forth across the bound switches the
// focus once, not every tick. There is no order that takes a focus back (the
// skull and every bot's attack stay), so a held focus is never dropped for
// distance: a focused unit that walks back to its camp still draws the party.
// Boss steps and events: any distance, as before. Null for none.
inline const UnitFact* PickKillFirst(const Run& r, const Facts& f)
{
    if (!r.plan)
        return nullptr;
    bool const bounded = KillFirstBounded(r);
    const UnitFact* best = nullptr;
    int bestRank = 0;
    for (const UnitFact& u : f.killFirst)
    {
        if (!u.hostile || u.evading || u.stealthed || r.ccSent.count(u.guid))
            continue;
        // Its focus was refused a moment ago (kFocusRefusedIgnoreMs).
        auto const refused = r.focusRefused.find(u.guid);
        if (refused != r.focusRefused.end() && r.runMs < refused->second)
            continue;
        // Only this fight's: engaged with the party, or the ward of one that is.
        if (!u.engaged && !(u.owner && u.ownerEngaged))
            continue;
        if (bounded && u.guid != r.focusGuid && u.distLead > kKillFirstMaxYd)
            continue;
        int const rank = r.plan->KillFirstRank(u.entry);
        if (rank < 0)
            continue;
        if (!best || rank < bestRank || (rank == bestRank && u.distLead < best->distLead))
        {
            best = &u;
            bestRank = rank;
        }
    }
    return best;
}

// The firing stand on the lead bot's navmesh path to a unit (the glue's
// read for Run::shotStandFor): `path` runs from the lead bot (front) toward the
// unit, `range` is Facts::leadShotYd - kShotMarginYd, `hasLos(point)` whether
// the unit is in sight from a point (at the core's sight heights).
//   - Beyond range: the ranged reach's own choice (ai::SelectRangedApproachPoint,
//     ReachTargetAction::MoveIntoRangeAlongPath): the path walked back from the
//     unit end to the first point within range that sees it, or the nearest
//     point toward it in sight when none within range does.
//   - Within range but out of sight (that choice leaves this to the chase): the
//     first point of the path, from the lead bot on, nearer the unit and in sight.
// Every stand sees the unit; none found: known false (the order goes out as before).
struct ShotStand
{
    bool known = false;
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

template <typename LosFn>
inline ShotStand ShotStandOnPath(const std::vector<ai::ApproachPoint>& path, const ai::ApproachPoint& unit, float range,
    LosFn hasLos)
{
    ShotStand stand;
    if (path.size() < 2 || range <= 0.0f)
        return stand;
    ai::RangedApproachChoice const choice = ai::SelectRangedApproachPoint(path, unit, range, hasLos);
    if (choice.valid)
    {
        stand.known = true;
        stand.x = choice.point.x;
        stand.y = choice.point.y;
        stand.z = choice.point.z;
        return stand;
    }
    float const startSq = ai::ApproachDistanceSq(path.front(), unit);
    if (startSq > range * range)
        return stand; // beyond range and no point nearer in sight
    for (std::size_t i = 1; i < path.size(); ++i)
        if (ai::ApproachDistanceSq(path[i], unit) < startSq && hasLos(path[i]))
        {
            stand.known = true;
            stand.x = path[i].x;
            stand.y = path[i].y;
            stand.z = path[i].z;
            return stand;
        }
    return stand;
}

// The joiners the aggro estimate expects for a pull of `u`: this tick's read,
// else the last one this step (a unit held for its joiner rest is not judged
// again by the glue). 0 with the estimate off.
inline std::uint32_t ExpectedJoiners(const Run& r, const UnitFact& u)
{
    if (u.joinersRead)
        return u.joiners;
    auto const it = r.joinerSeen.find(u.guid);
    return it == r.joinerSeen.end() ? 0 : it->second;
}

// The pull stand (AiPlayerbot.DungeonRunPullStand; PLAN-PULLBACK-RANGE F1):
// `pullback` goes out only when the tank's shot can go out from where it
// stands. In the WC runs of 19z17 the tank, ordered from a spot it could not
// shoot from, walked to a firing position by itself and never fired from
// there (0 shots in 33 such pulls; 16 of 16 from a stand). So, before the
// order:
//   - a moving tank is waited for up to kShotStillWaitMs (again after each
//     stand walk);
//   - a unit out of sight or beyond leadShotYd - kShotMarginYd: the glue
//     reads a firing stand for it (one tick, Run::shotStandFor), and the party
//     walks there (WalkTo); a walk with no headway for kWalkRetryMs, or longer
//     than kShotWalkCapMs, gives the stand up and orders from here (never a
//     pause); at most kShotStandTries stands per unit and step;
//   - no stand found, or the stands used up: the order as before.
// True while the pull waits (`order` may hold the move); false: the order
// goes out now, and Run::shotHow says how its shot stood.
inline bool HoldForShot(Run& r, const Facts& f, const UnitFact& u, std::uint32_t dt, std::string& order)
{
    r.shotGateRan = true;
    // A stand request is answered (or dropped) on the very next tick: the
    // glue reads at most one stand per request.
    bool const standRead = r.shotStandFor == u.guid && f.shotStandGuid == u.guid;
    r.shotStandFor = 0;
    if (!PullStandOn(r, f))
    {
        r.shotHow = f.leadShotYd > kShotMarginYd ? "no stand rule in an event" : "no ranged weapon pull: stand rule off";
        return false;
    }
    std::string const what = std::to_string(u.entry) + " " + GuidArg(u.guid);
    std::uint32_t& tries = r.shotStandTries[u.guid];
    auto hold = [&](const std::string& note)
    {
        r.shotStandPrep = true;
        r.shotHoldGuid = u.guid;
        r.note = note;
        return true;
    };
    // The stand walk for this unit: on to its end. A walk something else
    // broke off (a gather) ends here too; its stand counted already.
    if (r.shotStandWalk && r.shotStandGuid == u.guid)
    {
        r.shotStandWalk = false;
        if (r.phase == Phase::Walk && r.walkX == r.shotStandX && r.walkY == r.shotStandY && r.walkZ == r.shotStandZ)
        {
            // Stuck (no headway for kWalkRetryMs short of the point) or too
            // long: the stand is given up before WalkTo would resend the move
            // or pause the run, and the order goes out from here.
            float const dist = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, r.shotStandX, r.shotStandY, r.shotStandZ);
            bool const gaining = dist < r.walkBestYd - kWalkProgressYd;
            r.shotWalkMs += dt;
            if ((!gaining && r.walkStillMs + dt >= kWalkRetryMs && dist > kStalledArriveYd) ||
                r.shotWalkMs >= kShotWalkCapMs)
            {
                // Held one tick more, without the hold's pass on the way back:
                // the next tick checks the anchor from where the tank got stuck
                // (another floor: the gather back first), then the order goes out.
                tries = kShotStandTries;
                r.shotWalkMs = 0;
                EnterPhase(r, Phase::Begin);
                r.shotHow = "firing stand unreachable: ordering from here";
                r.shotStandPrep = false;
                r.note = "firing stand unreachable for " + what + ": checking the way back before the order";
                return true;
            }
            if (!WalkTo(r, f, r.shotStandX, r.shotStandY, r.shotStandZ, false, dt, order))
            {
                if (r.state != State::Running)
                    return true; // stuck: WalkTo paused the run with its reason
                r.shotStandWalk = true;
                return hold("to firing stand " + std::to_string(tries) + " for " + what);
            }
            EnterPhase(r, Phase::Begin); // arrived (the anchor is the stand now)
        }
        r.shotStillMs = 0;
    }
    if (f.leadMoving && r.shotStillMs < kShotStillWaitMs)
    {
        r.shotStillMs += dt;
        return hold("waiting for the tank to stand still before pulling " + what);
    }
    bool const moving = f.leadMoving;
    if (ShootableFromLead(f, u))
    {
        r.shotHow = std::string(tries ? "shot from firing stand " + std::to_string(tries) : "shot from where it stands") +
            (moving ? ", still moving" : "");
        return false;
    }
    if (tries >= kShotStandTries)
    {
        if (r.shotHow.compare(0, 24, "firing stand unreachable") != 0)
            r.shotHow = "firing stands used up (" + std::to_string(tries) + ")";
        return false;
    }
    if (standRead)
    {
        if (!f.shotStandKnown)
        {
            tries = kShotStandTries;
            r.shotHow = "no firing stand found";
            return false;
        }
        if (ai::PlanDistance(f.leadX, f.leadY, f.leadZ, f.shotStandX, f.shotStandY, f.shotStandZ) <= kArriveYd)
        {
            // The stand is where the tank stands: walking gains nothing.
            tries = kShotStandTries;
            r.shotHow = "the firing stand is where it stands";
            return false;
        }
        ++tries;
        r.shotStandWalk = true;
        r.shotStandGuid = u.guid;
        r.shotStandX = f.shotStandX;
        r.shotStandY = f.shotStandY;
        r.shotStandZ = f.shotStandZ;
        r.shotStillMs = 0;
        r.shotWalkMs = 0;
        // A big pack (kBigPackJoiners): the walk is watched every tick by
        // BigPackWalkGate, so the lead bot never walks into reach first.
        EndBigWalk(r);
        std::uint32_t const joiners = ExpectedJoiners(r, u);
        if (joiners >= kBigPackJoiners)
        {
            r.bigWalk = true;
            r.bigWalkGuid = u.guid;
            r.bigWalkX = f.shotStandX;
            r.bigWalkY = f.shotStandY;
            r.bigWalkZ = f.shotStandZ;
            r.bigJoiners = joiners;
        }
        WalkTo(r, f, f.shotStandX, f.shotStandY, f.shotStandZ, false, dt, order);
        return hold("to firing stand " + std::to_string(tries) + " for " + what + " (" + Num(f.shotStandYd) +
            " yd from it; was " + Num(u.distLead) + " yd, los " + (u.losLead ? "yes" : "no") + ")");
    }
    r.shotStandFor = u.guid; // the glue reads it on the next tick
    return hold("reading a firing stand for " + what);
}

// Gathers the party, crowd-controls the plan's cc target of the pack (one
// `cc moon <guid> <mage>`, once per unit and step), then sends `pullback <guid>`.
// With the pull stand on (HoldForShot), the tank is put where it can shoot first.
// `hop`: a move's hop pull (the walk back's are hop pulls too). Such a unit is
// not the step's target: with the pull stand on, one the tank still cannot
// shoot once the stand rule gave up is left out of the hop picks (neverCame),
// with no order: the pullback would walk the tank at it (ReachPullAction), and
// in r6-19z24 step 64 that walk, the engage on arrival and the chase put the
// tank on the island of 81610 (PLAN-ZF-NOFIGHT). The judgement is made from
// where the tank stands now: a unit around a corner is left too, and fights
// when the party walks by. After a regroup cleared the set, the same rule
// leaves a still unshootable unit out again without any pull.
inline void PullUnit(Run& r, const Facts& f, const UnitFact& u, std::string& order, const UnitFact* cc = nullptr,
    bool walkBack = false, std::uint32_t dt = kTickMs, bool hop = false)
{
    hop = hop || walkBack;
    // The owner's wait rule at the pull itself (Uldaman 19z11: 17 of 63 pulls
    // went out with a member dead, 15 with the healer, two of them into
    // wipes): no pull while a bot is dead, outside a scripted event. Tick's
    // wait (WaitForFallen) regroups first; this holds if a death comes in
    // between.
    if (f.dead > 0 && !InEvent(r))
    {
        r.note = WaitNote(r, f) + ": no pull while a party member is dead";
        return;
    }
    if (!r.hopWaiveUnits.empty() && !r.hopWaiveUnits.count(u.guid))
        DropHopWaiver(r); // another target than the units the hop was abandoned for: gather as usual
    if (r.shotStandPrep && r.shotHoldGuid != u.guid)
        r.shotStandPrep = false; // the pull stand held another unit: this one checks the way back
    // A pull the stand rule holds checked the way back on its first tick; its
    // stand walk is driven every tick, not every other one while the path back
    // is read (shotStandPrep is only set on consecutive gate ticks).
    AnchorState const anchor = r.gatherWaived || r.shotStandPrep ? AnchorState::On : CheckAnchor(r, f);
    if (anchor == AnchorState::Unknown && r.phase != Phase::Gather)
    {
        r.note = "checking the way back to " + Num(r.anchorX) + " " + Num(r.anchorY) + " " + Num(r.anchorZ);
        return; // the glue reads the path next tick
    }
    if ((f.maxMemberYd > kGatherYd || anchor == AnchorState::Off) && !r.gatherWaived)
    {
        if (r.phase != Phase::Gather)
        {
            EnterPhase(r, Phase::Gather);
            r.note = anchor == AnchorState::Off ? "gathering back at " + Num(r.anchorX) + " " + Num(r.anchorY) + " " +
                Num(r.anchorZ) + " before a pull (the fight left the lead bot off the way)"
                                         : std::string("gathering before a pull");
            SendGather(r, f, order);
        }
        else if (r.phaseMs >= kGatherCapMs)
        {
            GatherCeiling(r, f, order, "the party did not gather within " + std::to_string(kGatherCapMs / 1000) +
                " s (a bot is " + Num(f.maxMemberYd) + " yd from the lead bot)");
        }
        return;
    }
    if (!walkBack && r.pulls >= kMaxPullsPerStep)
    {
        Pause(r, std::to_string(kMaxPullsPerStep) + " pulls did not clear the step");
        return;
    }
    if (walkBack && r.walkBackPulls >= kMaxWalkBackPulls)
    {
        Pause(r, "walk back: " + std::to_string(kMaxWalkBackPulls) + " pulls on the way did not clear it");
        return;
    }
    if (cc && cc->guid != u.guid && !r.ccSent.count(cc->guid))
    {
        // The mark goes out before the pull; the mage's Polymorph comes with
        // the fight (its `cc` strategy is a combat one), where the fight gate
        // checks the aura (CheckCc).
        r.ccSent.insert(cc->guid);
        r.ccPending = cc->guid;
        r.ccResent = false;
        r.ccWaitMs = 0;
        // Still getting the pull ready: the next tick's pull is not counted
        // as a second pull attempt (FightStep's fruitless count).
        EnterPhase(r, Phase::Gather);
        std::string const bot = CcBot(f);
        r.note = "crowd control on " + std::to_string(cc->entry) + " (" + bot + ") before the pull";
        order = "cc moon " + std::to_string(cc->guid) + " " + bot;
        return;
    }
    // The pull stand (off: false at once, nothing changed): before the gather
    // waiver is spent, so a stand walk from a hop in sight keeps it.
    if (f.pullStand)
    {
        auto const done = r.pullTries.find(u.guid);
        if ((done == r.pullTries.end() || done->second < kMaxPullsPerTarget) && HoldForShot(r, f, u, dt, order))
            return;
    }
    // A gather left out holds for this pull only: the next one gathers again.
    r.gatherWaived = false;
    r.hopWaiveUnits.clear();
    r.gatherRefusals = 0;
    if (hop && PullStandOn(r, f) && !ShootableFromLead(f, u))
    {
        r.neverCame.insert(u.guid);
        r.note = "hop unit " + std::to_string(u.entry) + " " + GuidArg(u.guid) +
            " passed: the tank cannot shoot it from here (" + r.shotHow + "), and a hop pull never walks to it";
        r.shotStandPrep = false;
        r.shotStandFor = 0;
        r.shotStandWalk = false;
        r.shotStillMs = 0;
        r.shotHow.clear();
        EnterPhase(r, Phase::Begin);
        return;
    }
    std::uint32_t& tries = r.pullTries[u.guid];
    if (tries >= kMaxPullsPerTarget)
    {
        // Each pull started a fight, yet the unit lives: it evades, leashes
        // home or stands where the party cannot reach. Never loop on it.
        r.unreachable.insert(u.guid);
        r.note = "unit " + std::to_string(u.entry) + " survived " + std::to_string(kMaxPullsPerTarget) +
            " pulls: left as out of reach";
        return;
    }
    ++tries;
    ++(walkBack ? r.walkBackPulls : r.pulls);
    r.pullGuid = u.guid;
    r.pullHop = hop;
    r.reachStopped = false;
    EndBigWalk(r);
    EnterPhase(r, Phase::AwaitPull);
    r.note = "pulling " + std::to_string(u.entry);
    order = "pullback " + std::to_string(u.guid);
    if (f.pullStand)
    {
        // Each order its own status line (the trace keeps only changes): the
        // target's guid, the how-many-th order to it, and how its shot stood.
        r.note += " " + GuidArg(u.guid) + " (order " + std::to_string(tries) + "; " + r.shotHow + ")";
        r.shotStandPrep = false;
        r.shotStandFor = 0;
        r.shotStandWalk = false;
        r.shotStillMs = 0;
        r.shotHow.clear();
    }
    // The aggro estimate (Facts::joinerCeiling): the note says how many joiners
    // the estimate expects with this pull, or that the facts carried no count
    // this step (the glue reads it only for units it judged; a unit held or out
    // of the index keeps its earlier read).
    if (f.joinerCeiling > 0)
        r.note += u.joinersRead ? "; " + std::to_string(u.joiners) + " joiners expected"
                                : std::string("; joiners not read");
}

// The step's pull is out: wait for the fight (the fight gate takes over) or
// give the pull up after kPullNoFightMs. A hop unit that never came is left
// out of the hop picks at once (Run::neverCame: the agent's one pullback); a
// step's own unit only for the step, after kPullTriesPerUnit such pulls.
inline void AwaitPull(Run& r)
{
    if (r.phaseMs < kPullNoFightMs)
        return;
    auto it = r.pullTries.find(r.pullGuid);
    if (it != r.pullTries.end() && it->second >= kPullTriesPerUnit)
    {
        r.passed.insert(r.pullGuid);
        if (r.patrolSeen.count(r.pullGuid))
            r.patrolGaveUp.insert(r.pullGuid); // a patroller that never came: not waited for again this run
    }
    if (r.pullHop)
        r.neverCame.insert(r.pullGuid);
    r.pullGuid = 0;
    r.pullHop = false;
    EnterPhase(r, Phase::Begin);
}

// Walks toward a step whose point is out of sight, one navmesh hop at a time
// (the glue's approach point). A hop is fixed when its walk starts; once there
// the next tick asks for the next one. Bounded by kMaxApproachHops.
inline void ApproachHop(Run& r, const Facts& f, const PlanStep& s, std::uint32_t dt, std::string& order,
    const std::string& what)
{
    r.note = "approaching the " + what;
    if (r.phase == Phase::Walk)
    {
        if (WalkTo(r, f, r.walkX, r.walkY, r.walkZ, false, dt, order))
            EnterPhase(r, Phase::Begin);
        return;
    }
    if (!f.approachKnown && !f.approachRead)
    {
        r.note += ": reading the way"; // the next tick asks the navmesh
        return;
    }
    if (IslandConfirmed(r, f, s.x, s.y, s.z))
    {
        IslandEscape(r, f, s.x, s.y, s.z, "the " + what, order);
        return;
    }
    // A hop that ends where the lead bot stands goes nowhere: no path, not a hop.
    if (!f.approachKnown ||
        ai::PlanDistance(f.leadX, f.leadY, f.leadZ, f.approachX, f.approachY, f.approachZ) <= kArriveYd)
    {
        Pause(r, "no walkable path toward the " + what + " at " + Num(s.x) + " " + Num(s.y) + " " + Num(s.z));
        return;
    }
    // The cap counts hops that did not bring the lead bot nearer: a long way
    // walked in hops of kApproachHopYd is not stopped by it, a loop is.
    float const toStep = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, s.x, s.y, s.z);
    if (r.approachBestYd <= 0.0f)
        r.approachBestYd = toStep;
    else if (toStep < r.approachBestYd - kApproachProgressYd)
    {
        r.approachBestYd = toStep;
        r.approachHops = 0;
    }
    if (++r.approachHops > kMaxApproachHops)
    {
        // Before the pause, a bot that stands still far behind is summoned
        // to the lead bot (once each), so a resume starts with the party
        // together. The lead bot's own way stays the reason for the pause.
        // Uldaman 19z11 02:50: the priest stood on a navmesh pocket at 43 371
        // while the lead bot hopped from the entrance.
        if (SummonStraggler(r, f, order, true))
        {
            r.note = "approach: " + r.note.substr(std::string("gather: ").size()) + " before the pause";
            return;
        }
        Pause(r, std::to_string(kMaxApproachHops) + " approach hops did not bring the party within " +
            Num(kSeeYd) + " yd of the " + what + " (nearest it got: " + Num(r.approachBestYd) + " yd)");
        return;
    }
    r.hopWalk = true;
    r.hopX = f.approachX;
    r.hopY = f.approachY;
    r.hopZ = f.approachZ;
    WalkTo(r, f, f.approachX, f.approachY, f.approachZ, false, dt, order);
}

// After a regroup the run started by itself the party stands where it came
// back to life (the instance entrance after a corpse run). It walks the plan's
// own move points again, in order, from the one nearest the lead bot up to the
// current step: the way the run came, with no new path search. Nothing to walk
// when the current step's point is nearer than every earlier move point.
// A point of a navmesh path (the glue's PathFinder points).
struct PathPoint
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

// The margin past a pull group's radius a direct walk back keeps from it.
constexpr float kPackClearanceYd = 10.0f;

inline float SegmentDistance(const PathPoint& a, const PathPoint& b, float px, float py, float pz)
{
    float const dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
    float const len2 = dx * dx + dy * dy + dz * dz;
    float t = len2 > 0.0f ? ((px - a.x) * dx + (py - a.y) * dy + (pz - a.z) * dz) / len2 : 0.0f;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return ai::PlanDistance(a.x + t * dx, a.y + t * dy, a.z + t * dz, px, py, pz);
}

// Whether a path passes within a pull or boss group's radius plus
// kPackClearanceYd of its point, for every group not cleared yet: the step
// `from` and later ones, and earlier ones left without being cleared
// (`notCleared`: skipped, out of reach), of the party's team. The whole path
// counts, its last leg too. The shortest navmesh path back
// ignores packs: a straight walk past an uncleared one body-pulls it.
inline bool PathCrossesUnclearedPack(const DungeonPlan& plan, std::size_t from, const std::vector<PathPoint>& path,
    std::uint32_t team, const std::unordered_set<std::size_t>& notCleared = {})
{
    for (std::size_t i = 0; i < plan.steps.size(); ++i)
    {
        if (i < from && !notCleared.count(i))
            continue; // cleared before
        const PlanStep& g = plan.steps[i];
        if ((g.kind != PlanStepKind::Pull && g.kind != PlanStepKind::Boss) || !ai::StepForTeam(g, team))
            continue;
        float const reach = g.radius + kPackClearanceYd;
        if (path.size() == 1 && ai::PlanDistance(path[0].x, path[0].y, path[0].z, g.x, g.y, g.z) <= reach)
            return true;
        for (std::size_t k = 1; k < path.size(); ++k)
            if (SegmentDistance(path[k - 1], path[k], g.x, g.y, g.z) <= reach)
                return true;
    }
    return false;
}

// The lowest plan index a walk back may start at: the checkpoint when it is
// known (a point before it can lie behind a hole the lead bot crossed since),
// else 0 (as at the entrance: before every hole).
inline std::size_t ReplayFloor(const Run& r)
{
    return CheckpointKnown(r) ? r.checkpoint : 0;
}

// The first direct step before the current step that the lead bot has not
// crossed (npos for none): after the checkpoint when it is known, else the
// plan's first direct step.
inline std::size_t FirstUncrossedDirect(const Run& r)
{
    std::size_t const from = CheckpointKnown(r) ? r.checkpoint + 1 : 0;
    for (std::size_t i = from; i < r.step && i < r.plan->steps.size(); ++i)
        if (r.plan->steps[i].kind == PlanStepKind::MoveDirect)
            return i;
    return static_cast<std::size_t>(-1);
}

// The last move point in [ReplayFloor, hole), before the direct step `hole`:
// where a walk back stops its navmesh hops and replays the plan's own points
// over the hole. `hole` itself when no move point is before it.
inline std::size_t LastPointBeforeDirect(const Run& r, std::size_t hole)
{
    std::size_t start = hole;
    for (std::size_t i = ReplayFloor(r); i < hole; ++i)
        if (IsMovePoint(r.plan->steps[i]))
            start = i;
    return start;
}

// The first move point at or after ReplayFloor, before the current step (npos
// for none): where a replay that cannot begin starts over (WalkBack).
inline std::size_t FirstReplayPoint(const Run& r)
{
    for (std::size_t i = ReplayFloor(r); i < r.step && i < r.plan->steps.size(); ++i)
        if (IsMovePoint(r.plan->steps[i]))
            return i;
    return static_cast<std::size_t>(-1);
}

// The walk back first goes straight to its start point (in navmesh hops,
// StartWalkBack); when that fails it replays every point from the one nearest
// the lead bot (ReplayWalkBack), but only among the points the route allows:
// not before the checkpoint when it is known, and not past the first direct
// step not crossed yet (that direct step is then always replayed, even from a
// lead bot nearer the step than any point). True when there is one.
inline bool ReplayWalkBack(Run& r, const Facts& f)
{
    r.walkBackDirect = false;
    r.walkBack = false;
    r.approachHops = 0;
    const PlanStep* cur = Current(r);
    if (!cur)
        return false;
    std::size_t const hole = FirstUncrossedDirect(r);
    std::size_t const ceiling = hole != static_cast<std::size_t>(-1) ? hole : r.step;
    float best = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, cur->x, cur->y, cur->z);
    bool found = false;
    if (hole != static_cast<std::size_t>(-1))
    {
        r.walkBackIdx = LastPointBeforeDirect(r, hole);
        best = -1.0f;
        found = true;
    }
    for (std::size_t i = ReplayFloor(r); i < ceiling; ++i)
    {
        const PlanStep& p = r.plan->steps[i];
        if (!IsMovePoint(p))
            continue;
        float const d = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, p.x, p.y, p.z);
        if (best < 0.0f || d < best)
        {
            best = d;
            r.walkBackIdx = i;
            found = true;
        }
    }
    if (!found)
        return false;
    r.walkBack = true;
    r.walkBackStart = r.walkBackIdx;
    EnterPhase(r, Phase::Begin);
    return true;
}

// Whether the plan's points from `from` up to the current step hold a direct
// step: a navmesh hole (Uldaman's scorpid pit) the navmesh hops of a straight
// walk back cannot cross; only the plan's own points cross it.
inline bool DirectStepOnWay(const Run& r, std::size_t from)
{
    for (std::size_t i = from; i < r.step && i < r.plan->steps.size(); ++i)
        if (r.plan->steps[i].kind == PlanStepKind::MoveDirect)
            return true;
    return false;
}

// The plan's move point nearest the lead bot before the current step (npos
// for none). Proximity only: on a folded route it can lie past a hole the lead
// bot has not crossed, so no walk back starts from it (StartWalkBack).
inline std::size_t NearestMovePoint(const Run& r, const Facts& f)
{
    std::size_t idx = static_cast<std::size_t>(-1);
    float best = 0.0f;
    for (std::size_t i = 0; i < r.step; ++i)
    {
        const PlanStep& p = r.plan->steps[i];
        if (!IsMovePoint(p))
            continue;
        float const d = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, p.x, p.y, p.z);
        if (idx == static_cast<std::size_t>(-1) || d < best)
        {
            best = d;
            idx = i;
        }
    }
    return idx;
}

// After a regroup: the way back to the current step. The rule (route topology,
// never straight-line proximity: Uldaman's route doubles back, and a point past
// the scorpid pit lies nearer the entrance than points before it):
//   - Every direct step the lead bot has not crossed is replayed, in plan
//     order. It has crossed exactly those up to the route checkpoint while
//     that is known (Run::checkpoint: the last point it reached, kept across
//     wipes; lost on a death or a pause unless it is back on the same spot);
//     otherwise none (a release comes back at the entrance, before every hole).
//   - With such a direct step: straight (navmesh hops) to the last move point
//     before the first of them, not before the checkpoint, then the plan's
//     points from there (the hops over the hole, the rest). Uldaman: step 39
//     and the pit hops, not step 1 (run 4: 528 yd through Baelog's wing).
//   - Without one (no direct step in the plan, or all of them crossed): as
//     before, no walk back when it stands on the last move point before the
//     step or within kApproachYd of the step's own point and nearer to it than
//     to that move point (Galgann pull 108, 2026-10-16: a straight line from
//     the entrance is shorter to the pack than to its stand yet has no path),
//     else straight to the last move point in navmesh hops.
// With no direct step left to cross, standing on the last move point sets the
// checkpoint there.
inline void StartWalkBack(Run& r, const Facts& f)
{
    r.walkBackDue = false;
    r.walkBack = false;
    r.walkBackDirect = false;
    const PlanStep* cur = Current(r);
    if (!cur)
        return;
    std::size_t last = r.step;
    for (std::size_t i = 0; i < r.step; ++i)
        if (IsMovePoint(r.plan->steps[i]))
            last = i;
    if (last == r.step)
        return;
    const PlanStep& p = r.plan->steps[last];
    float const toLast = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, p.x, p.y, p.z);
    // The holes first: on a folded route the spot a lead bot comes back at can
    // lie on a later point (the last one too) before the holes it has to cross.
    std::size_t const hole = FirstUncrossedDirect(r);
    if (hole != static_cast<std::size_t>(-1))
    {
        r.walkBack = true;
        r.walkBackIdx = LastPointBeforeDirect(r, hole);
        r.walkBackDirect = r.walkBackIdx < hole; // a navmesh point first; a direct step is replayed as such
        r.walkBackStart = r.walkBackIdx;
        r.walkBackRestarted = false;
        r.approachHops = 0;
        r.note = "walk back: a direct step is on the way (step " + std::to_string(hole + 1) + "): " +
            (r.walkBackDirect ? "straight to step " + std::to_string(r.walkBackIdx + 1) + ", then the plan's points"
                              : std::string("the plan's points from it"));
        EnterPhase(r, Phase::Begin);
        return;
    }
    if (toLast <= kArriveYd)
    {
        NoteCheckpoint(r, last);
        return; // on the last point already
    }
    float const toStep = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, cur->x, cur->y, cur->z);
    if (toStep <= kApproachYd && toStep <= toLast)
        return; // back near the step already
    r.walkBack = true;
    r.walkBackDirect = true;
    r.walkBackIdx = last;
    r.walkBackStart = last;
    r.walkBackRestarted = false;
    r.approachHops = 0;
    EnterPhase(r, Phase::Begin);
}

// On to the next walk-back point, with fresh tries, hops and gather waiver.
inline void NextWalkBackPoint(Run& r)
{
    r.islandSuspect = false;
    ++r.walkBackIdx;
    r.walkBackRefusals = 0;
    r.approachHops = 0;
    r.gatherWaived = false;
    r.gatherRefusals = 0;
    EnterPhase(r, Phase::Begin);
}

// A bound on any walk back, whatever its state (the final check on d7e94c3b:
// after a hop that found no spot one point went out every tick, 3000 ticks):
// each try at the current point (its move sent, or its way found missing) from
// one spot (kArriveYd) is counted; past kWalkBackResends the point is passed.
// True when it was.
inline bool ResendBound(Run& r, const Facts& f)
{
    if (r.resendIdx == r.walkBackIdx &&
        ai::PlanDistance(f.leadX, f.leadY, f.leadZ, r.resendX, r.resendY, r.resendZ) <= kArriveYd)
        ++r.resends;
    else
    {
        r.resendIdx = r.walkBackIdx;
        r.resendX = f.leadX;
        r.resendY = f.leadY;
        r.resendZ = f.leadZ;
        r.resends = 1;
    }
    if (r.resends <= kWalkBackResends)
        return false;
    r.resends = 0;
    r.note = "walk back: the point of step " + std::to_string(r.walkBackIdx + 1) + " was tried " +
        std::to_string(kWalkBackResends) + " times from this spot: on to the next";
    NextWalkBackPoint(r); // clears the island suspicion too
    return true;
}

// One second of the walk back. True while it walks (the tick ends there);
// false once it is over. Each point is replayed as its move step would be:
// the hostiles within kHopPullYd of it (the glue's hopUnits, read for this
// point: FactStep) are pulled first, a direct one gathers first, then a
// WalkTo (bounded by kWalkTries). One point a tick: the facts are read for it.
inline bool WaitForFallen(Run& r, const Facts& f);

// A dead bot first (the owner's wait rule, 2026-10-17): no walk back point and
// no direct step goes out while a party member is dead. A direct step takes
// the lead bot's line, and a dead bot cannot join it. Uldaman run 4, 14:03:
// the rogue died on his corpse run 120 yd away after the regroup was over;
// the walk back pushed on to the pit's direct step, named a bot that stood
// with the lead bot, and paused with the rogue still dead 9 minutes later.
// The ordinary wait (WaitForFallen) only runs between steps; this asks for
// the same regroup (revive party, gather, a stuck straggler summoned) on any
// tick before such an order. True when the regroup starts. Not in a fight,
// a scripted event or a regroup.
inline bool FallenFirst(Run& r, const Facts& f)
{
    if (f.dead == 0 || r.regroup || f.anyInCombat || InEvent(r))
        return false;
    return WaitForFallen(r, f);
}

inline bool WalkBack(Run& r, const Facts& f, std::uint32_t dt, std::string& order)
{
    if (!r.walkBack)
        return false;
    if (r.walkBackIdx < r.step && FallenFirst(r, f))
        return true;
    while (r.walkBackIdx < r.step && !IsMovePoint(r.plan->steps[r.walkBackIdx]))
        ++r.walkBackIdx;
    if (r.walkBackIdx >= r.step)
    {
        r.walkBack = false;
        r.stepIdleMs = 0;
        r.note = "walked back to step " + std::to_string(r.step + 1);
        EnterPhase(r, Phase::Begin);
        return true; // the next tick reads the facts for the step itself
    }
    if (r.phase == Phase::AwaitPull)
    {
        AwaitPull(r);
        if (r.phase == Phase::AwaitPull)
            return true;
    }
    if (const UnitFact* hopUnit = PickPull(r, f.hopUnits, 0, true))
    {
        PullUnit(r, f, *hopUnit, order, nullptr, true, dt);
        if (r.state == State::Running)
            r.note = "walk back: " + r.note;
        return true;
    }
    const PlanStep& p = r.plan->steps[r.walkBackIdx];
    // A point out of sight (more than kSeeYd: beyond ReactDistance the move
    // itself is refused) is approached in navmesh hops, as a resume does;
    // no path toward it at all: it is passed for the next one.
    if (!f.seen && !(r.phase == Phase::Walk && r.walkX == p.x && r.walkY == p.y && r.walkZ == p.z))
    {
        std::string const what = "the point of step " + std::to_string(r.walkBackIdx + 1);
        if (r.phase == Phase::Walk)
        {
            r.note = "walk back: approaching " + what;
            if (WalkTo(r, f, r.walkX, r.walkY, r.walkZ, false, dt, order))
                EnterPhase(r, Phase::Begin);
            return true;
        }
        // Never at a direct step's point: the lead bot may stand at its launch
        // spot, and DirectStillToCross decides there.
        if (p.kind != PlanStepKind::MoveDirect && IslandConfirmed(r, f, p.x, p.y, p.z))
        {
            IslandEscape(r, f, p.x, p.y, p.z, what, order);
            return true;
        }
        if (r.walkBackDirect && f.directPathCrossesPack)
        {
            // The shortest way passes a pack not cleared yet: the plan's own
            // points instead (the way the run came, its packs cleared).
            if (ReplayWalkBack(r, f))
                r.note = "walk back: the straight way to " + what + " passes an uncleared pack: replaying the plan's points";
            else
                r.note = "walk back: the straight way to " + what + " passes an uncleared pack";
            return true;
        }
        std::uint32_t const cap = r.walkBackDirect ? kMaxWalkBackHops : kMaxApproachHops;
        if (!f.approachKnown && !f.approachRead)
        {
            r.note = "walk back: approaching " + what + ": reading the way"; // the next tick asks the navmesh
            return true;
        }
        // A hop that ends where the lead bot stands goes nowhere: no path, not a hop.
        bool const noWay = !f.approachKnown ||
            ai::PlanDistance(f.leadX, f.leadY, f.leadZ, f.approachX, f.approachY, f.approachZ) <= kArriveYd;
        if ((noWay || r.approachHops >= cap) && ResendBound(r, f))
            return true;
        if (noWay || r.approachHops >= cap)
        {
            std::string const why = noWay ? "no walkable path toward " + what
                                                     : std::to_string(cap) + " hops did not reach " + what;
            if (r.walkBackDirect)
            {
                // Straight there failed: replay the plan's points instead.
                if (ReplayWalkBack(r, f))
                    r.note = "walk back: " + why + ": replaying the plan's points";
                else
                    r.note = "walk back: " + why;
                return true;
            }
            // A replay that cannot even begin (its first point has no walkable
            // path from where the lead bot stands) goes back to the first point
            // the route allows (ReplayFloor: the checkpoint when known, else
            // the plan's first point), once.
            std::size_t const first = FirstReplayPoint(r);
            if (!r.walkBackRestarted && r.walkBackIdx == r.walkBackStart && first != static_cast<std::size_t>(-1) &&
                first < r.walkBackIdx)
            {
                r.walkBackRestarted = true;
                r.walkBackIdx = r.walkBackStart = first;
                r.approachHops = 0;
                r.note = "walk back: " + why + ": replaying from the first point (step " + std::to_string(first + 1) + ")";
                return true;
            }
            if (DirectStillToCross(r, r.walkBackIdx))
            {
                Pause(r, "walk back: " + why + ": a direct step over a navmesh hole is not passed; " +
                    "gather the party at the point before it, then .bot run resume");
                return true;
            }
            PassWalkBackPoint(r, f.leadX, f.leadY, f.leadZ, "walk back: " + why);
            return true;
        }
        ++r.approachHops;
        r.note = "walk back: approaching " + what;
        WalkTo(r, f, f.approachX, f.approachY, f.approachZ, false, dt, order);
        return true;
    }
    bool const direct = p.kind == PlanStepKind::MoveDirect;
    // An exact waypoint on the way back is replayed as its step is (ExactWalkTo),
    // and a direct step launched from one waits for its proof (MoveStep).
    bool const reachedStraightTarget = r.walkBackDirect && r.walkBackIdx == r.walkBackStart;
    if (IsExactPoint(r.plan, r.walkBackIdx))
    {
        r.note = "walk back: to the exact point of step " + std::to_string(r.walkBackIdx + 1);
        if (ExactWalkTo(r, f, r.walkBackIdx, dt, order))
        {
            NoteCheckpoint(r, r.walkBackIdx);
            NextWalkBackPoint(r);
            if (reachedStraightTarget)
                r.walkBackDirect = false;
        }
        return true;
    }
    if (ExactLaunchBroken(r.plan, r.walkBackIdx))
    {
        Pause(r, "walk back: " + ExactLaunchBrokenNote(r.walkBackIdx));
        return true;
    }
    std::size_t const launch = ExactLaunch(r.plan, r.walkBackIdx);
    if (launch != static_cast<std::size_t>(-1) &&
        !(r.phase == Phase::Walk && r.walkX == p.x && r.walkY == p.y && r.walkZ == p.z) &&
        ai::PlanDistance(f.leadX, f.leadY, f.leadZ, p.x, p.y, p.z) > kArriveYd)
    {
        const PlanStep& l = r.plan->steps[launch];
        if (r.exactProved != launch ||
            ai::PlanDistance(f.leadX, f.leadY, f.leadZ, l.x, l.y, l.z) > kExactArriveYd)
        {
            r.exactProved = static_cast<std::size_t>(-1);
            r.note = "walk back: to the exact point of step " + std::to_string(launch + 1) + " before its direct step";
            if (ExactWalkTo(r, f, launch, dt, order))
                EnterPhase(r, Phase::Begin);
            return true;
        }
    }
    if (direct && r.phase != Phase::Walk && !DirectGathered(f) && !r.gatherWaived)
    {
        if (r.phase != Phase::Gather)
        {
            EnterPhase(r, Phase::Gather);
            r.note = "walk back: gathering before a direct step";
            SendGather(r, f, order);
        }
        else if (r.phaseMs >= kGatherCapMs)
        {
            GatherCeiling(r, f, order, "the party did not gather within " + std::to_string(kGatherCapMs / 1000) +
                " s before a direct step (a bot is " + Num(f.maxMemberYd) + " yd from the lead bot)", true);
        }
        return true;
    }
    if (direct && r.phase != Phase::Walk && HoldForDirectJoin(r, f))
        return true;
    if (!direct && r.phase != Phase::Walk && ai::PlanDistance(f.leadX, f.leadY, f.leadZ, p.x, p.y, p.z) > kArriveYd &&
        IslandConfirmed(r, f, p.x, p.y, p.z))
    {
        IslandEscape(r, f, p.x, p.y, p.z, "the point of step " + std::to_string(r.walkBackIdx + 1), order);
        return true;
    }
    // A bound whatever the state: the same point sent more than kWalkBackResends
    // times from one spot is passed (ResendBound).
    if (!(r.phase == Phase::Walk && r.walkX == p.x && r.walkY == p.y && r.walkZ == p.z) && ResendBound(r, f))
        return true;
    r.note = "walk back: to the point of step " + std::to_string(r.walkBackIdx + 1);
    if (WalkTo(r, f, p.x, p.y, p.z, direct, dt, order))
    {
        // The straight target is reached: from here the plan's own points go
        // on (the hole's direct steps, then the rest), each one a hop of its
        // own, not a straight walk with the straight walk's cap.
        NoteReached(r, f, r.walkBackIdx);
        NextWalkBackPoint(r);
        if (reachedStraightTarget)
            r.walkBackDirect = false;
    }
    return true;
}

// The approach hop the pack came in sight during is walked to its end first
// (Ragefire 19z13, trogg cave pull 13: the pull's gather broke the hop off, the
// anchor was still the stand at the top of the ramp, more than kStragglerDzYd
// above, so the gather "went back" to it and the next hop came forward again:
// eight times, then the hop cap). Arriving at the hop's point makes it the
// anchor (WalkTo). Not when a unit of the step stands within kHopClearYd of the
// lead bot: then the hop is over and the pull goes out from here, with the gather
// (and the gather back to the old anchor) left out for this pull.
inline bool FinishHopInSight(Run& r, const Facts& f, std::uint32_t dt, std::string& order, const char* what)
{
    if (r.phase != Phase::Walk || !r.hopWalk || r.walkX != r.hopX || r.walkY != r.hopY || r.walkZ != r.hopZ)
        return false;
    for (const UnitFact& u : f.units)
        if (u.hostile && u.distLead < kHopClearYd)
        {
            // The hop is over; the pull goes from where the lead bot stands. The
            // party is neither gathered beside the pack nor sent back to the old
            // anchor for this pull: the followers stay behind (PullUnit clears
            // the waiver when the pull goes out).
            EnterPhase(r, Phase::Begin);
            r.hopWalk = false;
            r.gatherWaived = true;
            for (const UnitFact& each : f.units)
                r.hopWaiveUnits.insert(each.guid);
            return false;
        }
    r.note = std::string("approaching the ") + what;
    if (!WalkTo(r, f, r.hopX, r.hopY, r.hopZ, false, dt, order))
        return true;
    EnterPhase(r, Phase::Begin); // arrived (the anchor is set): on with the pull
    return false;
}

// The big pack's firing stand walk (Run::bigWalk, set by HoldForShot when a
// pull with kBigPackJoiners or more joiners expected walks to a stand). Uldaman
// r10-19z29 step 79: the stand walk was driven only on the ticks the pull stand
// ran; on the ticks between (another unit's joiner rest, a way-back read) the
// lead bot walked on unwatched to a stand 10 yd from the scorpids while the
// healer and the mage were 27-30 yd behind on the ramp, and the gather came
// after it stood there. This gate runs on every out-of-combat tick of the walk:
// a living bot more than kGatherYd from both the lead bot and the stand stops
// the lead bot where it stands (a gather on its own spot) until every bot is
// that close, at most kGatherCapMs; then the walk goes on with "proceeding
// anyway". It keeps the pull stand's walk state while it holds. True: this
// tick is the gate's (`order` may hold the gather or the walk's move).
inline bool BigPackWalkGate(Run& r, const Facts& f, std::uint32_t dt, std::string& order)
{
    if (!r.bigWalk)
        return false;
    bool const walking = r.phase == Phase::Walk && r.walkX == r.bigWalkX && r.walkY == r.bigWalkY && r.walkZ == r.bigWalkZ;
    if (!walking)
    {
        EndBigWalk(r); // the walk ended (arrived, given up, a gather took over): the usual gates from here
        return false;
    }
    std::string const what = "big pack (" + std::to_string(r.bigJoiners) + " joiners expected, " +
        std::to_string(kBigPackJoiners) + " or more)";
    // The pull stand's walk is the gate's while it holds and as it resumes: a
    // tick the stand rule did not run on would drop it (EndShotHold).
    auto keepStandWalk = [&]
    {
        r.shotGateRan = true;
        r.shotStandWalk = true;
        r.shotStandGuid = r.bigWalkGuid;
        r.shotStandX = r.bigWalkX;
        r.shotStandY = r.bigWalkY;
        r.shotStandZ = r.bigWalkZ;
        r.shotStandPrep = true;
        r.shotHoldGuid = r.bigWalkGuid;
    };
    auto walkOn = [&](const std::string& note)
    {
        r.bigHold = false;
        r.bigHoldMs = 0;
        keepStandWalk();
        float const dist = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, r.bigWalkX, r.bigWalkY, r.bigWalkZ);
        r.walkStillMs = 0;
        r.walkBestYd = dist;
        order = MoveOrder(r.bigWalkX, r.bigWalkY, r.bigWalkZ, false);
        r.note = note;
        return true;
    };
    const MemberFact* behind = nullptr;
    if (!r.bigHoldSpent)
        for (const MemberFact& m : f.members)
            if (!m.name.empty() && m.yd > kGatherYd &&
                ai::PlanDistance(m.x, m.y, m.z, r.bigWalkX, r.bigWalkY, r.bigWalkZ) > kGatherYd &&
                (!behind || m.yd > behind->yd))
                behind = &m;
    if (!behind)
    {
        if (r.bigHold)
            return walkOn(what + ": the party gathered, on to the firing stand");
        return false; // together: the pull stand drives the walk as usual
    }
    if (!r.bigHold)
    {
        // The lead bot holds where it stands; the others form up on it.
        r.bigHold = true;
        r.bigHoldMs = 0;
        r.bigHoldNote = what + ": the firing stand walk waits for the party (" + behind->name + " " +
            Num(behind->yd) + " yd behind)";
        order = MoveOrder(f.leadX, f.leadY, f.leadZ, false);
        r.gatherOrder = order;
        SampleMembers(r, f);
        keepStandWalk();
        r.note = r.bigHoldNote;
        return true;
    }
    r.bigHoldMs += dt;
    if (r.bigHoldMs >= kGatherCapMs)
    {
        r.bigHoldSpent = true;
        return walkOn(what + ": the party did not gather within " + std::to_string(kGatherCapMs / 1000) + " s (" +
            behind->name + " " + Num(behind->yd) + " yd behind): proceeding anyway");
    }
    keepStandWalk();
    r.note = r.bigHoldNote;
    return true;
}

// A pull step whose units are all gone may still have one of its entries
// walking a patrol outside the radius (Facts::patrollers). Uldaman r10-19z29
// step 66: the Stone Steward's lane reaches the 10 yd radius only at its north
// end; the step said "pack cleared" in 0 s, and the Steward came into the
// Keeper's fight two steps later. A hostile patroller is waited for, like a
// boss away from his spot (kBossSearchMs), up to kPatrolWaitMs or the step's
// shorter timeout_s, until it walks into the radius: there it is one of the
// step's units and the usual pick takes it (the way back, the aggro estimate,
// the fruitless count). Never in a fight, never for a friendly unit, one a
// later step owns (OwnedByLaterStep) or one the run gave up on (Run::
// unreachable, Run::patrolGaveUp: the wait ran out, or its pulls drew no
// fight); none seen: the step is cleared as before. True: the step waits;
// false: it is cleared (`timedOut` says the wait ran out).
inline bool WaitForPatroller(Run& r, const Facts& f, const PlanStep& s, std::uint32_t dt, std::string& timedOut)
{
    if (s.kind != PlanStepKind::Pull || f.anyInCombat)
        return false;
    const UnitFact* p = nullptr;
    for (const UnitFact& u : f.patrollers)
    {
        if (!u.hostile || r.passed.count(u.guid) || r.unreachable.count(u.guid) || r.patrolGaveUp.count(u.guid))
            continue;
        if (r.plan && ai::OwnedByLaterStep(*r.plan, r.step, u.entry, u.x, u.y, u.z, f.team))
            continue;
        r.patrolSeen.insert(u.guid);
        if (!p || u.distLead < p->distLead)
            p = &u;
    }
    if (!p)
        return false;
    std::uint32_t const capMs = s.timeoutS && s.timeoutS * 1000u < kPatrolWaitMs ? s.timeoutS * 1000u : kPatrolWaitMs;
    std::string const what = std::to_string(p->entry) + " " + GuidArg(p->guid);
    if (r.patrolWaitMs >= capMs)
    {
        r.patrolGaveUp.insert(p->guid);
        timedOut = "; the patroller " + what + " did not come within " + std::to_string(capMs / 1000) + " s";
        return false;
    }
    r.patrolWaitMs += dt;
    r.note = "waiting for the patroller " + what + " (outside the " + Num(s.radius) + " yd radius; up to " +
        std::to_string(capMs / 1000) + " s)";
    return true;
}

// pull and boss steps: approach until seen, then pull in order until nothing
// hostile of the step is left. `order` lists the entries in kill order (empty:
// any entry, nearest first).
inline void FightStep(Run& r, const Facts& f, const PlanStep& s, const std::vector<std::uint32_t>& killOrder,
    std::uint32_t dt, std::string& order)
{
    if (r.phase == Phase::AwaitPull)
    {
        AwaitPull(r);
        if (r.phase == Phase::AwaitPull)
            return;
    }
    // A step for the other team (flags 2/4):
    //   - a pack is skipped without a look, unless the party already stands
    //     near it and a unit of it is not friendly;
    //   - a boss is always looked at first, and skipped only when every unit
    //     of its encounter there is friendly to the party. A hostile or
    //     neutral one is fought (Uldaman's Lost Dwarves carry team horde in
    //     the plan, yet were hostile to an Alliance party in a live run).
    if (!ai::StepForTeam(s, f.team))
    {
        bool notFriendly = false;
        for (const UnitFact& u : f.units)
            notFriendly = notFriendly || (u.hostile && !r.passed.count(u.guid));
        bool const skip = s.kind == PlanStepKind::Boss ? (f.seen && !notFriendly) : (!f.seen || !notFriendly);
        if (skip)
        {
            NextStep(r, s.kind != PlanStepKind::Boss ? "pack is for the other team: skipped"
                : f.units.empty() ? "boss is for the other team and not here: skipped"
                                  : "boss is for the other team and friendly to this party: skipped");
            return;
        }
    }
    if (!f.seen)
    {
        ApproachHop(r, f, s, dt, order, s.kind == PlanStepKind::Boss ? "boss" : "pack");
        return;
    }
    if (FinishHopInSight(r, f, dt, order, s.kind == PlanStepKind::Boss ? "boss" : "pack"))
        return;
    for (const UnitFact& u : f.units)
        if (u.joinersRead)
            r.joinerSeen[u.guid] = u.joiners;
    if (BigPackWalkGate(r, f, dt, order))
        return;
    if (s.kind == PlanStepKind::Boss)
        for (const UnitFact& u : f.units)
            r.bossSeenAlive = r.bossSeenAlive || u.entry == s.entry;
    const UnitFact* pick = nullptr;
    if (killOrder.empty())
        pick = PickPackPull(r, f);
    for (std::uint32_t entry : killOrder)
        if (!pick)
            pick = PickPull(r, f.units, entry);
    if (pick)
    {
        r.bossWaitMs = 0;
        // The way back to the anchor not known yet: read it first (one tick),
        // before this pull counts as one.
        // (A hold for another unit than the pick is caught by PullUnit's own check of the way back.)
        if (r.phase != Phase::Gather && !r.gatherWaived && !r.shotStandPrep && CheckAnchor(r, f) == AnchorState::Unknown)
        {
            r.note = "checking the way back to " + Num(r.anchorX) + " " + Num(r.anchorY) + " " + Num(r.anchorZ);
            return;
        }
        // The aggro estimate (off at joinerCeiling 0): more joiners than the
        // ceiling and the party not rested yet waits once per unit, on the
        // rest wait that already exists (TickBody), no movement. At most
        // kMaxJoinerRests such waits a step (counted when a rest really starts);
        // after that the pull goes out.
        std::string budgetNote;
        if (f.joinerCeiling > 0 && pick->joiners > f.joinerCeiling && !r.joinerHeld.count(pick->guid))
        {
            if (r.joinerRests >= kMaxJoinerRests)
                budgetNote = std::to_string(pick->joiners) + " joiners expected, rest budget used";
            else
            {
                r.joinerHeld.insert(pick->guid);
                r.joinerRest = true;
                r.joinerNote = std::to_string(pick->joiners) + " joiners expected" +
                    (pick->joinersPatrol ? " (a patrol is the cause)" : "");
                r.note = r.joinerNote;
                return;
            }
        }
        // Pulls that kill nothing, three in a row: the rest of the pack is
        // out of reach (a pit, a ledge). It is left, not counted cleared.
        std::size_t const open = OpenUnits(r, f.units);
        // A tick the pull stand held (HoldForShot) is the same pull, not another one.
        if (r.phase != Phase::Gather && !r.shotStandPrep)
        {
            if (r.pulls > 0 && open >= r.openAtLastPull)
                ++r.fruitlessPulls;
            else if (r.pulls > 0)
                r.fruitlessPulls = 0;
            r.openAtLastPull = open;
        }
        if (r.fruitlessPulls >= kFruitlessPulls)
        {
            for (const UnitFact& u : f.units)
                if (u.hostile && !r.passed.count(u.guid))
                    r.unreachable.insert(u.guid);
            r.notCleared.insert(r.step);
            NextStep(r, std::string(s.kind == PlanStepKind::Boss ? "boss" : "pack") + " skipped, not cleared: " +
                std::to_string(open) + " units out of reach after " + std::to_string(kFruitlessPulls) +
                " pulls that killed nothing");
            return;
        }
        PullUnit(r, f, *pick, order, PickCc(r, f, pick->guid), false, dt);
        if (!budgetNote.empty() && r.phase == Phase::AwaitPull)
            r.note = f.pullStand ? budgetNote + "; " + r.note : budgetNote;
        return;
    }
    if (OpenUnits(r, f.units) > 0)
    {
        // Only evading or stealthed units left: let them settle (the step's
        // idle ceiling bounds this), and walk through a stealthed pack.
        r.note = "waiting for the pack to settle";
        bool const stealthOnly = [&] {
            for (const UnitFact& u : f.units)
                if (u.hostile && !r.passed.count(u.guid) && !u.stealthed)
                    return false;
            return true;
        }();
        if (stealthOnly && WalkTo(r, f, s.x, s.y, s.z, false, dt, order))
            NextStep(r, "walked through a stealthed pack");
        return;
    }
    if ((s.flags & ai::kPlanFlagAmbush) && !r.walkedThrough)
    {
        // An ambush pack shows itself only when the party walks into it.
        r.note = "walking through an ambush pack";
        if (WalkTo(r, f, s.x, s.y, s.z, false, dt, order))
            r.walkedThrough = true;
        if (!r.walkedThrough)
            return;
    }
    std::size_t outOfReach = 0;
    bool friendlyLeft = false;
    for (const UnitFact& u : f.units)
    {
        if (u.hostile && r.unreachable.count(u.guid))
            ++outOfReach;
        friendlyLeft = friendlyLeft || !u.hostile;
    }
    std::string const what = s.kind == PlanStepKind::Boss ? "boss" : "pack";
    if (outOfReach)
    {
        r.notCleared.insert(r.step);
        NextStep(r, what + " skipped, not cleared: " + std::to_string(outOfReach) + " units out of reach");
    }
    else if (friendlyLeft)
        NextStep(r, what + " is friendly to this party: left alone");
    else if (s.kind != PlanStepKind::Boss)
    {
        std::string timedOut;
        if (WaitForPatroller(r, f, s, dt, timedOut))
            return;
        NextStep(r, "pack cleared" + timedOut);
    }
    else if (f.bossDeadNearSpot || (r.bossSeenAlive && f.bossDeadNearLead))
    {
        // Remember where he fell: a later `loot` step for this entry goes to the
        // corpse by its guid (TaskStep), not to the plan's spawn point.
        if (f.bossCorpseGuid)
            r.corpses[s.entry] = CorpseMark{f.bossCorpseGuid, f.bossCorpseX, f.bossCorpseY, f.bossCorpseZ};
        NextStep(r, "boss down");
    }
    else
    {
        // Not seen alive and not seen dead: he is away (a patrol, Theka in
        // Zul'Farrak), not killed. The plan rows carry no patrol path: the
        // party waits at his spot for him to come by, then pauses.
        r.bossWaitMs += dt;
        r.note = "boss " + std::to_string(s.entry) + " not at his spot: waiting for him";
        if (r.bossWaitMs >= kBossSearchMs)
            Pause(r, "boss " + std::to_string(s.entry) + " not found: not alive or dead within " + Num(s.radius) +
                " yd of " + Num(s.x) + " " + Num(s.y) + " " + Num(s.z) + " for " +
                std::to_string(kBossSearchMs / 1000) + " s (.bot run skip if he is already dead)");
    }
}

inline void MoveStep(Run& r, const Facts& f, const PlanStep& s, std::uint32_t dt, std::string& order)
{
    if (r.phase == Phase::AwaitPull)
    {
        AwaitPull(r);
        if (r.phase == Phase::AwaitPull)
            return;
    }
    if (const UnitFact* hopUnit = PickPull(r, f.hopUnits, 0, true))
    {
        PullUnit(r, f, *hopUnit, order, nullptr, false, dt, true);
        return;
    }
    // Far away (after a wipe the lead bot comes back from the entrance; a
    // long plan hop): walk back on the navmesh in hops first.
    if (!r.detour && !f.seen)
    {
        ApproachHop(r, f, s, dt, order, "next point");
        return;
    }
    // A direct step launched from an exact waypoint goes out only while the
    // proof of that waypoint stands: the lead bot proved on it (that step, or
    // this one after a resume or a fight) and still within kExactArriveYd.
    // Otherwise the tighten runs again here first (ExactWalkTo); the direct
    // walk under way (its own point in walkX) is not interrupted.
    if (ExactLaunchBroken(r.plan, r.step))
    {
        Pause(r, ExactLaunchBrokenNote(r.step));
        return;
    }
    std::size_t const launch = ExactLaunch(r.plan, r.step);
    bool const directUnderWay = r.phase == Phase::Walk && r.walkX == s.x && r.walkY == s.y && r.walkZ == s.z;
    bool const crossed = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, s.x, s.y, s.z) <= kArriveYd;
    if (launch != static_cast<std::size_t>(-1) && !directUnderWay && !crossed)
    {
        const PlanStep& l = r.plan->steps[launch];
        if (r.exactProved != launch ||
            ai::PlanDistance(f.leadX, f.leadY, f.leadZ, l.x, l.y, l.z) > kExactArriveYd)
        {
            r.exactProved = static_cast<std::size_t>(-1);
            if (ExactWalkTo(r, f, launch, dt, order))
                EnterPhase(r, Phase::Begin); // proved: the next tick gathers and sends the direct step
            return;
        }
    }
    // The forward run's own move point in sight (r6-19z24 Zul'Farrak steps 64/65:
    // the hop pull's fight left the tank on the navmesh island at 1588-1591
    // 1044-1047, 6-9 yd from the point, and both `cmd move`s were refused): the
    // same rule as the walk back's (IslandConfirmed, two signs) before the move
    // goes out (out of sight ApproachHop asks it, unless a detour is on). Never
    // for a direct step (its line crosses a navmesh hole), nor while a walk is under way.
    if (s.kind == PlanStepKind::Move && r.phase != Phase::Walk &&
        ai::PlanDistance(f.leadX, f.leadY, f.leadZ, s.x, s.y, s.z) > kArriveYd &&
        IslandConfirmed(r, f, s.x, s.y, s.z, GoodSpotFresh(r)))
    {
        IslandEscape(r, f, s.x, s.y, s.z, "the point of step " + std::to_string(r.step + 1), order);
        r.escapeFromMove = r.escapeOn; // its no-spot falls back to the move (OrderRefused)
        return;
    }
    if (s.kind == PlanStepKind::MoveDirect && r.phase != Phase::Walk && FallenFirst(r, f))
        return;
    // A direct step takes the lead bot's line: the party gathers on it first
    // (`cmd move ... direct` refuses while a bot has no way to the lead's spot).
    if (s.kind == PlanStepKind::MoveDirect && r.phase != Phase::Walk && !DirectGathered(f) && !r.gatherWaived)
    {
        if (r.phase != Phase::Gather)
        {
            EnterPhase(r, Phase::Gather);
            r.note = "gathering before a direct step";
            SendGather(r, f, order);
        }
        else if (r.phaseMs >= kGatherCapMs)
        {
            GatherCeiling(r, f, order, "the party did not gather within " + std::to_string(kGatherCapMs / 1000) +
                " s before a direct step (a bot is " + Num(f.maxMemberYd) + " yd from the lead bot)", true);
        }
        return;
    }
    if (s.kind == PlanStepKind::MoveDirect && r.phase != Phase::Walk && HoldForDirectJoin(r, f))
        return;
    if (r.detour)
    {
        r.note = "walking around: the plan's point is off the walkable way";
        if (WalkTo(r, f, r.detourX, r.detourY, r.detourZ, false, dt, order))
        {
            // A navmesh walk to a stand-in point: for a direct step it proves
            // no crossing of its hole, so the checkpoint stays where it was.
            if (s.kind != PlanStepKind::MoveDirect)
                NoteCheckpoint(r, r.step);
            NextStep(r, "arrived at the nearest reachable point, " +
                Num(ai::PlanDistance(r.detourX, r.detourY, r.detourZ, s.x, s.y, s.z)) + " yd from the plan's");
        }
        return;
    }
    if (IsExactPoint(r.plan, r.step))
    {
        r.note = "walking to an exact point";
        if (ExactWalkTo(r, f, r.step, dt, order))
        {
            NoteCheckpoint(r, r.step);
            NextStep(r, "arrived: the lead bot stands on the exact point");
        }
        return;
    }
    r.note = "walking";
    if (WalkTo(r, f, s.x, s.y, s.z, s.kind == PlanStepKind::MoveDirect, dt, order, true))
    {
        NoteReached(r, f, r.step);
        NextStep(r, "arrived");
    }
}

// The glue found that the current move step's point cannot be reached (its
// `cmd move` was refused unreachable or off-mesh) and that (x,y,z), within
// kDetourMaxYd of it, can: the step walks there instead, by the navmesh path
// around whatever is in the way. False (nothing changes) when the step is not
// a move, a detour is already on, or the point is too far from the plan's.
inline bool UseDetour(Run& r, float x, float y, float z)
{
    const PlanStep* s = Current(r);
    if (!s || r.state != State::Running || r.detour ||
        (s->kind != PlanStepKind::Move && s->kind != PlanStepKind::MoveDirect))
        return false;
    // An exact waypoint has no stand-in: a direct step launched from beside it
    // is the very thing the flag forbids. The refusal pauses the run.
    if (IsExactPoint(r.plan, r.step) || ExactLaunch(r.plan, r.step) != static_cast<std::size_t>(-1))
        return false;
    if (ai::PlanDistance(x, y, z, s->x, s->y, s->z) > kDetourMaxYd)
        return false;
    r.detour = true;
    r.detourX = x;
    r.detourY = y;
    r.detourZ = z;
    EnterPhase(r, Phase::Begin);
    return true;
}

// open / use_item: the bags of one bot, for PickItemCarrier.
struct BagFact
{
    std::string name;
    std::uint64_t guid = 0;          // the bot's guid (OutstandingCarrierVerdict)
    std::vector<std::uint32_t> held; // the step's input items this bot carries
    std::string away;                // why it cannot take the task ("dead", "not in the instance"); "": it can
};

// The bot an open / use_item goes to: the lead bot when it carries every input
// of the step (its item, the item to use, and the second item in `param`),
// else the first available bot that does. None does: the lead bot, and `split`
// says who holds what (an unavailable bot: item N is with X, who is dead) when
// someone holds the step's first input. Only the step's own inputs count,
// never loot a bot kept for other reasons; a dead bot or one off the lead
// bot's map is never chosen.
inline std::string PickItemCarrier(const std::vector<BagFact>& bags, const std::vector<std::uint32_t>& inputs,
    const std::string& lead, std::string* split = nullptr)
{
    auto holdsAll = [&](const BagFact& b)
    {
        for (std::uint32_t item : inputs)
            if (item && std::find(b.held.begin(), b.held.end(), item) == b.held.end())
                return false;
        return true;
    };
    std::string best;
    for (const BagFact& b : bags)
        if (b.away.empty() && holdsAll(b) && (best.empty() || b.name == lead))
            best = b.name;
    if (!best.empty())
        return best;
    if (split && !inputs.empty())
    {
        bool first = false;
        for (const BagFact& b : bags)
            first = first || std::find(b.held.begin(), b.held.end(), inputs[0]) != b.held.end();
        if (first)
            for (const BagFact& b : bags)
            {
                std::string has;
                for (std::uint32_t item : b.held)
                    has += (has.empty() ? "" : ", ") + std::to_string(item);
                if (has.empty())
                    continue;
                *split += std::string(split->empty() ? "" : "; ") +
                    (b.away.empty() ? b.name + " holds " + has
                                    : std::string("item ") + has + " is with " + b.name + ", who is " + b.away);
            }
    }
    return lead;
}

// open / use_item while its order is out (r.orderSent): the bot the order went
// to (`carrierRaw`, the glue's slot) keeps the step, so its busy state, its
// reply and the made item are read from that bot even after the use consumed
// the inputs (Uldaman: the Medallion 7740 and the Shaft 7741 used by a bot
// that is not the lead bot become the Staff 7733 in its bags; no bot carries
// the inputs any more). The carrier is never picked again while the order is
// out: when that bot left the party, is dead or off the lead bot's map, the
// order is retired (RetireOrder) before the facts are read.
enum class CarrierVerdict : std::uint8_t
{
    NoOrder, // no order out (or a new one, carrierRaw 0): PickItemCarrier picks
    Pinned,  // the order's bot is there: it is the carrier (`name`)
    Retire,  // the order's bot left, died or is off the map: RetireOrder
};

inline CarrierVerdict OutstandingCarrierVerdict(bool orderSent, std::uint64_t carrierRaw,
    const std::vector<BagFact>& bags, std::string* name = nullptr)
{
    if (!orderSent || !carrierRaw)
        return CarrierVerdict::NoOrder;
    for (const BagFact& b : bags)
        if (b.guid == carrierRaw && b.away.empty())
        {
            if (name)
                *name = b.name;
            return CarrierVerdict::Pinned;
        }
    return CarrierVerdict::Retire;
}

// An outstanding open / use_item whose bot is gone (CarrierVerdict::Retire):
// the order is dropped like a refused loot (LootRefused), with no answer read
// from it, and the step goes out again from Begin to a fresh pick, where the
// usual checks apply (a dead bot holding the inputs: the split note; the
// fallen-member rules of a step not awaiting a task).
inline void RetireOrder(Run& r)
{
    r.orderSent = false;
    EnterPhase(r, Phase::Begin);
    r.note = "the bot of the order left the party, died or is off the map: the order goes out again";
}

// loot: the next bot to try after a refusal on the loot rules: the one whose
// round-robin turn the corpse is (f.lootTurn), then each living party bot not
// tried yet. Never a bot off the lead bot's map (MemberFact::yd 1000). "" for none.
inline std::string NextLootBot(const Run& r, const Facts& f)
{
    auto tried = [&](const std::string& name)
    {
        return std::find(r.lootTried.begin(), r.lootTried.end(), name) != r.lootTried.end();
    };
    if (!f.lootTurn.empty() && !tried(f.lootTurn))
        for (const MemberFact& m : f.members)
            if (m.name == f.lootTurn && m.yd < 1000.0f)
                return f.lootTurn;
    for (const MemberFact& m : f.members)
        if (!m.name.empty() && !tried(m.name) && m.yd < 1000.0f)
            return m.name;
    return std::string();
}

// loot: no bot is left to try (or the refusal was not about the loot rules).
inline void LootGivenUp(Run& r, const Facts& f, const PlanStep& s)
{
    std::string text = r.lootRefusal;
    if (s.item && f.corpseChecked && !f.corpseHasItem)
        text = "loot: item " + std::to_string(s.item) + " not on corpse and not in any bag (" + text + ")";
    else if (r.lootRights && r.lootTried.size() > 1)
    {
        text += " (tried";
        for (const std::string& name : r.lootTried)
            text += " " + name;
        text += ")";
    }
    Pause(r, text + MissingHint(r));
}

// loot: the order for `bot` (and the status note for it). The boss corpse remembered from his step is
// named by guid (the loot task walks to it); otherwise the plan's entry@point search.
inline std::string LootOrderText(Run& r, const PlanStep& s, const std::string& bot)
{
    std::string const kind = ai::PlanStepKindName(s.kind);
    auto const corpse = r.corpses.find(s.entry);
    std::string const target = corpse != r.corpses.end() && corpse->second.guid
        ? GuidArg(corpse->second.guid) : EntryArg(s.entry, s.x, s.y, s.z);
    if (corpse != r.corpses.end() && corpse->second.guid)
        r.note = kind + " by " + bot + ": the corpse of " + std::to_string(s.entry) + " at " +
            Num(corpse->second.x) + " " + Num(corpse->second.y) + " " + Num(corpse->second.z);
    return "loot " + target + " " + bot + (s.item ? " " + std::to_string(s.item) : "");
}

// loot, AiPlayerbot.DungeonRunLootFfa: the corpse's item is not the carrier's to take under the group's
// loot rules (another member's turn; Revelosh's Shaft of Tsol went to the warlock, 2026-10-21). Before the
// next bot is tried, the lead bot (the group's leader) sets free-for-all, the carrier loots, and the method
// goes back. Once per step; the lead bot must lead the group and the method must be one to restore.
inline bool LootFfaWanted(const Run& r, const Facts& f, const PlanStep& s)
{
    return f.lootFfaOn && f.leadLeadsGroup && s.item && !r.lootFfaUsed && r.lootFfa == LootFfa::Idle &&
        f.lootMethod > RemoteCommander::kLootFfa && f.lootMethod <= RemoteCommander::kLootNeedBeforeGreed;
}

inline std::string LootRestoreOrder(const Run& r)
{
    return std::string("lootmethod ") + RemoteCommander::LootMethodName(r.lootRestore);
}

// Starts the detour: the order is `lootmethod ffa`; the method to restore is read now.
inline void LootFfaStart(Run& r, const Facts& f, std::string& order)
{
    r.lootFfaUsed = true;
    r.lootFfaLive = true;
    r.lootRestoreTries = 0;
    r.lootFfaMs = 0;
    r.lootRestore = f.lootMethod;
    r.lootFfa = LootFfa::Ordered;
    r.taskBot.clear();
    r.note = std::string("loot: another member's turn: free-for-all loot for the carrier (the method goes back to ") +
        RemoteCommander::LootMethodName(f.lootMethod) + ")";
    order = "lootmethod ffa";
}

// The detour's later ticks; true when the tick is spent (an order sent, or waiting). False: go on with the
// usual loot step (the detour ended, or never took).
inline bool LootFfaStep(Run& r, const Facts& f, const PlanStep& s, std::string& order)
{
    switch (r.lootFfa)
    {
        case LootFfa::Ordered:
        {
            // A refusal of the order ended it already (OrderRefused). Free-for-all must have taken, or the
            // carrier is no better off: the old way (the next bot).
            if (f.lootMethod != RemoteCommander::kLootFfa)
            {
                // Not free-for-all (the order did not take, or a player set another method): nothing to put back.
                r.lootFfa = LootFfa::Idle;
                r.lootFfaLive = false;
                r.note = "loot: free-for-all did not take: looking for another bot to take it";
                return false;
            }
            if (f.carrierHasItem || f.partyHasItem)
            {
                r.lootFfa = LootFfa::Restoring;
                order = LootRestoreOrder(r);
                r.note = "loot: the item is in a bag already: the loot method goes back";
                return true;
            }
            if (f.carrierBusy)
            {
                // Bounded: the group must not stay on free-for-all for a carrier that never frees up. On the cap
                // the next running tick orders the method back (LootFfaRestoreTick) and the step goes the old way.
                if (r.lootFfaMs >= kLootFfaWaitMs)
                {
                    r.lootFfa = LootFfa::Idle;
                    r.note = "loot: " + f.carrier + " did not finish an earlier order in " +
                        std::to_string(kLootFfaWaitMs / 1000) + " s: the loot method goes back";
                    return false;
                }
                r.note = "loot: waiting for " + f.carrier + " to finish an earlier order";
                return true;
            }
            r.orderSent = true;
            EnterPhase(r, Phase::AwaitTask);
            r.lootTried.push_back(f.carrier);
            r.lootFfa = LootFfa::Looting;
            r.note = "loot by " + f.carrier + " (free-for-all)";
            order = LootOrderText(r, s, f.carrier);
            return true;
        }
        case LootFfa::Looting:
            // The task's answer (ACK or ERR) or its end, whichever comes first: the method goes back before
            // the answer is read. The task cap pauses the step in the usual code (the glue restores then).
            if (f.taskReply == 0 && f.carrierBusy)
                return false;
            r.lootFfa = LootFfa::Restoring;
            order = LootRestoreOrder(r);
            r.note = "loot: the carrier's loot is answered: the loot method goes back";
            return true;
        case LootFfa::Restoring:
            // The restore order went out last tick: the next running tick reads the method and, if it is not
            // back, orders it again (LootFfaRestoreTick).
            r.lootFfa = LootFfa::Idle;
            return false;
        case LootFfa::Idle:
            break;
    }
    return false;
}

// Every running tick, whatever the step: the group's loot method is ffa because of this run's detour and no
// detour tick is in charge (a regroup after a wipe, a lead death or a fight cap reset the step with the method
// still ffa; a restore that was refused or did not take). Orders the saved method back, one order a tick, at most
// kLootRestoreTries times; then the run pauses and a human sets it (the glue tries once more as the run pauses).
// The flag ends when the facts read a method that is not ffa. True: the tick is spent.
inline bool LootFfaRestoreTick(Run& r, const Facts& f, std::string& order)
{
    if (!r.lootFfaLive || r.lootFfa != LootFfa::Idle)
        return false;
    if (f.lootMethod != RemoteCommander::kLootFfa)
    {
        // Not ffa any more (restored, or a player's choice), or no group to restore.
        r.lootFfaLive = false;
        r.lootRestoreTries = 0;
        return false;
    }
    if (r.lootRestoreTries >= kLootRestoreTries)
    {
        Pause(r, "the loot method of the lead bot's group is still free-for-all after " +
            std::to_string(kLootRestoreTries) + " orders to put it back to " + RemoteCommander::LootMethodName(r.lootRestore) +
            " (AiPlayerbot.DungeonRunLootFfa): set it with .bot cmd lootmethod " +
            RemoteCommander::LootMethodName(r.lootRestore) + ", then .bot run resume");
        return true;
    }
    ++r.lootRestoreTries;
    order = LootRestoreOrder(r);
    r.note = "the loot method goes back to " + std::string(RemoteCommander::LootMethodName(r.lootRestore)) +
        " (try " + std::to_string(r.lootRestoreTries) + ")";
    return true;
}

// A task answer that is worth one more try: the cast was cut (an interrupted opening, the party's fight), or the
// bot or the object was busy. The fight is handled first by the run (it holds the tick), then the same order goes out.
inline bool TaskResendable(const std::string& text)
{
    auto starts = [&](const std::string& prefix) { return text.compare(0, prefix.size(), prefix) == 0; };
    return starts("in-combat:") || starts("busy:") || starts("failed: The opening cast was interrupted");
}

// The first sentence of a module answer, for a note ("2.5 s" does not end it: a full stop ends it when a space or
// the end follows).
inline std::string FirstSentence(const std::string& text)
{
    std::size_t const at = text.find(". ");
    std::string out = at == std::string::npos ? text : text.substr(0, at);
    while (!out.empty() && out.back() == '.')
        out.pop_back();
    return out;
}

// loot, open, use_item: one commander task for the carrier, then its result.
inline void TaskStep(Run& r, const Facts& f, const PlanStep& s, std::string& order)
{
    std::string const kind = ai::PlanStepKindName(s.kind);
    if (s.kind == PlanStepKind::Loot && r.lootFfa != LootFfa::Idle && LootFfaStep(r, f, s, order))
        return;
    if (!r.orderSent)
    {
        if ((s.kind == PlanStepKind::Loot || s.kind == PlanStepKind::UseItem) && s.item && f.carrierHasItem)
        {
            NextStep(r, kind + ": " + f.carrier + " already carries item " + std::to_string(s.item));
            return;
        }
        // A loot step is done when any party bot has the item (the quest item of
        // one member is looted by that member, or already was: Uldaman 19z13,
        // Revelosh 7741), before the order and after a refusal.
        if (s.kind == PlanStepKind::Loot && s.item && f.partyHasItem)
        {
            NextStep(r, kind + ": " + (f.itemHolder.empty() ? f.carrier : f.itemHolder) + " already carries item " +
                std::to_string(s.item));
            return;
        }
        if (s.kind == PlanStepKind::Loot && !r.lootRefusal.empty())
        {
            std::string next;
            // The item gone from the corpse (and in no bag): no bot can take it.
            if (r.lootRights && !(s.item && f.corpseChecked && !f.corpseHasItem))
            {
                if (LootFfaWanted(r, f, s))
                {
                    LootFfaStart(r, f, order);
                    return;
                }
                next = NextLootBot(r, f);
            }
            if (next.empty())
            {
                LootGivenUp(r, f, s);
                return;
            }
            r.taskBot = next;
        }
        if ((s.kind == PlanStepKind::Open || s.kind == PlanStepKind::UseItem) && !f.itemsSplit.empty())
        {
            // The items the step needs sit in different bags (a loot step passed
            // on another bot's bags): one bot must carry them all.
            Pause(r, kind + ": no bot carries every item it needs: " + f.itemsSplit);
            return;
        }
        if (s.kind == PlanStepKind::Open && s.item && !f.carrierHasItem)
        {
            if (r.missing.count(s.item))
            {
                std::size_t const here = r.step;
                NextStep(r, "open " + std::to_string(s.entry) + " left out: it needs item " + std::to_string(s.item) +
                    ", which the run did not get");
                r.skippedOpenStep = here;
                return;
            }
            Pause(r, "open: " + f.carrier + " does not carry item " + std::to_string(s.item) + " (the lock needs it)");
            return;
        }
        if (s.kind == PlanStepKind::UseItem && !f.carrierHasEntryItem)
        {
            if (r.missing.count(s.entry))
            {
                if (s.item)
                    r.missing.insert(s.item);
                NextStep(r, "use_item " + std::to_string(s.entry) + " left out: the run did not get that item");
                return;
            }
            r.pendingMissing = s.item;
            Pause(r, "use_item: " + f.carrier + " does not carry item " + std::to_string(s.entry));
            return;
        }
        if (f.carrierBusy)
        {
            r.note = kind + ": waiting for " + f.carrier + " to finish an earlier order";
            return;
        }
        r.orderSent = true;
        EnterPhase(r, Phase::AwaitTask);
        std::string const bot = s.kind == PlanStepKind::Loot && !r.taskBot.empty() ? r.taskBot : f.carrier;
        if (s.kind == PlanStepKind::Loot)
            r.lootTried.push_back(bot);
        r.note = kind + " by " + bot;
        if (s.kind == PlanStepKind::Loot)
            order = LootOrderText(r, s, bot);
        else if (s.kind == PlanStepKind::Open)
            order = "open " + EntryArg(s.entry, s.x, s.y, s.z) + " " + f.carrier;
        else
            order = "use " + std::to_string(s.entry) + " " + f.carrier;
        return;
    }
    bool const consequence = s.kind == PlanStepKind::UseItem && s.item && !r.missing.empty();
    if (f.taskReply == 2)
    {
        if (consequence)
        {
            r.missing.insert(s.item);
            NextStep(r, "use_item failed (" + f.taskText + "): an earlier item is missing; item " +
                std::to_string(s.item) + " is noted missing too");
            return;
        }
        if (GoneIsDone(&s, f.taskText))
        {
            GoneStepDone(r, s);
            return;
        }
        if (s.kind != PlanStepKind::Open && s.item)
            r.pendingMissing = s.item;
        if (s.kind == PlanStepKind::Loot && LootRefusalOpen(f.taskText))
        {
            LootRefused(r, kind + " failed: " + f.taskText);
            return;
        }
        if (TaskResendable(f.taskText) && r.taskResends < kTaskResends)
        {
            // An interrupted cast or an in-combat answer: the run leader fights first (it already does), then the
            // same order goes out again, to the same bot (taskBot and lootTried stay).
            ++r.taskResends;
            r.orderSent = false;
            EnterPhase(r, Phase::Begin);
            r.note = kind + ": " + FirstSentence(f.taskText) + ": sending it again (" + std::to_string(r.taskResends) +
                " of " + std::to_string(kTaskResends) + ")";
            return;
        }
        Pause(r, kind + " failed: " + f.taskText + MissingHint(r));
        return;
    }
    if (f.carrierBusy)
    {
        if (r.phaseMs >= kTaskCapMs)
            Pause(r, kind + ": " + f.carrier + " did not finish in " + std::to_string(kTaskCapMs / 1000) + " s");
        return;
    }
    if (s.kind != PlanStepKind::Open && s.item && !f.carrierHasItem && !(s.kind == PlanStepKind::Loot && f.partyHasItem))
    {
        if (consequence)
        {
            r.missing.insert(s.item);
            NextStep(r, "use_item did not make item " + std::to_string(s.item) +
                " (an earlier item is missing): it is noted missing too");
            return;
        }
        r.pendingMissing = s.item;
        Pause(r, kind + " done, but item " + std::to_string(s.item) + " is not in " + f.carrier +
            "'s bags (.bot run skip leaves out the steps that need it)");
        return;
    }
    NextStep(r, kind + " done");
}

// ---------------------------------------------------------------------------
// Scripted events (docs/play/dungeon-run-leader.md "Events"): a start step
// (`ritual` here; `open` and `use_item` start events too), then a `hold` that
// stands on a spot and fights what the event sends until its done condition.
// ---------------------------------------------------------------------------

// The bots of a ritual: the carrier (the lead bot) first, then the nearest
// living bots of the party, `want` in all (fewer when the party has fewer).
inline std::vector<std::string> RitualBots(const Facts& f, std::uint32_t want)
{
    std::vector<const MemberFact*> others;
    for (const MemberFact& m : f.members)
        if (!m.name.empty() && m.name != f.carrier)
            others.push_back(&m);
    std::sort(others.begin(), others.end(), [](const MemberFact* a, const MemberFact* b) { return a->yd < b->yd; });
    std::vector<std::string> names;
    if (!f.carrier.empty() && want)
        names.push_back(f.carrier);
    for (const MemberFact* m : others)
        if (names.size() < want)
            names.push_back(m->name);
    return names;
}

// ritual: `cmd ritual` for `param` bots (the commander's own order: they walk
// to the object, use it in turn and hold still until it has cast), then its
// one answer. An ERR pauses with the module's words; it never waits blind.
inline void RitualStep(Run& r, const Facts& f, const PlanStep& s, std::string& order)
{
    std::string const what = "ritual " + std::to_string(s.entry);
    std::uint32_t const want = s.param ? s.param : ai::kPlanDefaultRitualBots;
    if (!r.orderSent)
    {
        // The event this ritual starts, when its hold reads an encounter state
        // (the glue reads that data on the ritual step too).
        const PlanStep* hold = EventHoldAhead(r);
        if (hold && (hold->flags & ai::kPlanFlagDoneData) && (hold->flags & ai::kPlanFlagEncounter) && f.eventData >= 0)
        {
            if (f.eventData == static_cast<long long>(hold->param))
            {
                NextStep(r, what + " left out: the event is done already (data " + std::to_string(hold->entry) +
                    " reads " + std::to_string(hold->param) + ")");
                return;
            }
            // Still in progress or failed (Archaedas walking home after a
            // reset): a ritual now would start a dead event. Wait for the
            // reset, bounded (the Stone Keepers stay at fail for good).
            bool const busy = f.eventData == 1 || f.eventData == static_cast<long long>(ai::kPlanEncounterFail);
            if (busy && r.eventResetWaitMs < kEventResetWaitMs)
            {
                r.eventResetWaitMs += kTickMs;
                r.note = what + ": waiting for the encounter to reset (data " + std::to_string(hold->entry) + " reads " +
                    std::to_string(f.eventData) + ")";
                return;
            }
        }
        if (f.carrierBusy)
        {
            r.note = what + ": waiting for " + f.carrier + " to finish an earlier order";
            return;
        }
        std::vector<std::string> const bots = RitualBots(f, want);
        if (bots.size() < want)
        {
            Pause(r, what + " needs " + std::to_string(want) + " living bots; " + std::to_string(bots.size()) +
                " are up (revive the party, then .bot run resume)");
            return;
        }
        r.orderSent = true;
        r.dataAtOrder = f.eventData;
        EnterPhase(r, Phase::AwaitTask);
        order = "ritual " + EntryArg(s.entry, s.x, s.y, s.z);
        std::string list;
        for (const std::string& name : bots)
        {
            order += " " + name;
            list += (list.empty() ? "" : ", ") + name;
        }
        r.note = what + ": starting the event (" + list + ")";
        return;
    }
    if (f.taskReply == 2)
    {
        Pause(r, SpentRitual(r, "ritual ", f.taskText) ? SpentRitualNote(what) : what + " failed: " + f.taskText);
        return;
    }
    if (f.taskReply == 1)
    {
        NextStep(r, what + " done: the event started");
        return;
    }
    if (f.carrierBusy)
    {
        if (r.phaseMs >= kTaskCapMs)
            Pause(r, what + ": the bots did not finish in " + std::to_string(kTaskCapMs / 1000) + " s");
        return;
    }
    if (r.phaseMs >= kRitualNoReplyMs)
        Pause(r, what + ": the bots are free again but the ritual gave no answer (.bot run resume tries it again)");
}

// gossip: the lead bot talks to creature `entry` and picks option `param` (the
// commander's `cmd gossip <entry> <bot> <option>`: the bot walks to the nearest
// living NPC of the entry within 40 yd of itself, talks to it and picks the
// option, one answer). The lead bot walks to the step's point first when it is
// farther than kGossipWalkYd from it (as a move would: approach hops out of
// sight, a plain walk in sight), so the NPC is within the order's search range.
// ACK ends the step; an ERR pauses with the module's own words (an NPC that
// AiPlayerbot.BotGossipNpcs does not list is the order's `not-allowed`, which
// names that key); no answer within the step's timeout pauses too. A step
// right before a hold counts as that event's start step (EventHoldAhead).
inline void GossipStep(Run& r, const Facts& f, const PlanStep& s, std::uint32_t dt, std::string& order)
{
    std::string const what = "gossip " + std::to_string(s.entry) + " option " + std::to_string(s.param);
    if (!r.orderSent)
    {
        float const dist = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, s.x, s.y, s.z);
        bool const direct = r.phase == Phase::Walk && r.walkX == s.x && r.walkY == s.y && r.walkZ == s.z;
        bool const hop = r.phase == Phase::Walk && !direct;
        if (hop || (!direct && dist > kGossipWalkYd && !f.seen))
        {
            ApproachHop(r, f, s, dt, order, "NPC");
            return;
        }
        if (direct || dist > kGossipWalkYd)
        {
            r.note = what + ": walking to the NPC";
            if (!WalkTo(r, f, s.x, s.y, s.z, false, dt, order))
                return;
        }
        if (f.carrierBusy)
        {
            r.note = what + ": waiting for " + f.carrier + " to finish an earlier order";
            return;
        }
        r.orderSent = true;
        EnterPhase(r, Phase::AwaitTask);
        order = "gossip " + std::to_string(s.entry) + " " + f.carrier + " " + std::to_string(s.param);
        r.note = what + " by " + f.carrier;
        return;
    }
    if (f.taskReply == 2)
    {
        if (GossipDeadIsDone(r, &s, f.taskText))
            return;
        std::string const dead = GossipNpcDeadNote(what, f.taskText);
        Pause(r, dead.empty() ? what + " failed: " + f.taskText : dead);
        return;
    }
    if (f.taskReply == 1)
    {
        NextStep(r, what + " done");
        return;
    }
    if (r.phaseMs >= s.timeoutS * 1000)
    {
        Pause(r, what + ": no answer in " + std::to_string(s.timeoutS) + " s (.bot run resume tries it again)");
        return;
    }
    r.note = what + ": waiting for the answer";
}

// use_on: the carrier (the bot of the party that carries item `item`, the lead
// bot first) uses that item ON game object `entry` at x,y,z: the commander's
// `use <item> <bot> entry:<obj>@x,y,z`, which walks the bot to the object and
// casts the item's spell at it (Zul'Farrak's gong 141832: the Mallet of
// Zul'Farrak 9240's spell 11434 opens its lock and the gong's event summons
// Gahz'rilla; a plain `open` of the gong does nothing).
//
// The use is made ONCE a run. A second use summons a second Gahz'rilla, which
// the instance kills at once (instance_zulfarrak.cpp OnCreatureCreate:
// DisappearAndDie once the encounter is in progress), leaving an invisible
// corpse at the pit that a boss row may take for his death. So:
//   - an answer that says the cast did not happen (in combat, busy, an
//     interrupted cast) is sent again once (kUseOnResends), and only when the
//     act had not started (Facts::taskActStarted);
//   - a boss row's boss already in sight (alive or dead, Facts::summonSeen)
//     ends the step without a use, before the order and after an ERR;
//   - once an ACK came, the step is latched for the run (Run::useOnRang, kept
//     through ResetStep: a wipe or the lead bot's death replays the step, never
//     the use);
//   - the ACK alone is not taken as done: the object must have reacted
//     (Facts::objectReacted) or the boss must show within kUseOnProofMs, else
//     the run pauses saying so.
// An ERR pauses with the module's text; the item in a dead or absent bot's
// bags (Facts::itemsSplit) pauses naming that bot; no carrier with the item
// pauses before any order (the item is the operator's to give: the run leader
// has no GM step).
inline void UseOnStep(Run& r, const Facts& f, const PlanStep& s, std::string& order)
{
    std::string const what = "use_on " + std::to_string(s.entry) + " with item " + std::to_string(s.item);
    const PlanStep* next = r.plan && r.step + 1 < r.plan->steps.size() ? &r.plan->steps[r.step + 1] : nullptr;
    std::string const boss = next && next->kind == PlanStepKind::Boss ? "boss " + std::to_string(next->entry) : std::string();
    bool const seen = f.summonSeen && !boss.empty();
    if (!r.orderSent)
    {
        if (seen)
        {
            r.useOnRang.insert(r.step);
            NextStep(r, what + ": " + boss + " is here already (alive or dead): not used again");
            return;
        }
        if (r.useOnRang.count(r.step))
        {
            Pause(r, what + ": used once already this run (its order was ACKed) and " +
                (boss.empty() ? std::string("nothing shows it took") : boss + " is not in sight") +
                ": not used again (a second use summons a second boss that the instance kills at once, leaving an " +
                "invisible corpse); .bot run skip goes on");
            return;
        }
        if (!f.itemsSplit.empty())
        {
            Pause(r, what + ": " + f.itemsSplit + " (revive or bring that bot, then .bot run resume)");
            return;
        }
        if (!f.carrierHasItem)
        {
            Pause(r, what + ": no bot of the party carries item " + std::to_string(s.item) + " (" + f.carrier +
                " was checked first): give it to the lead bot (GM .additem " + std::to_string(s.item) +
                " with the bot selected), then .bot run resume");
            return;
        }
        if (f.carrierBusy)
        {
            r.note = what + ": waiting for " + f.carrier + " to finish an earlier order";
            return;
        }
        r.orderSent = true;
        EnterPhase(r, Phase::AwaitTask);
        order = "use " + std::to_string(s.item) + " " + f.carrier + " " + EntryArg(s.entry, s.x, s.y, s.z);
        r.note = what + " by " + f.carrier;
        return;
    }
    if (f.taskReply == 2)
    {
        if (seen)
        {
            r.useOnRang.insert(r.step);
            NextStep(r, what + ": " + boss + " is here (the use took before the answer " + FirstSentence(f.taskText) +
                "): not used again");
            return;
        }
        if (TaskResendable(f.taskText) && !f.taskActStarted && r.taskResends < kUseOnResends)
        {
            ++r.taskResends;
            r.orderSent = false;
            EnterPhase(r, Phase::Begin);
            r.note = what + ": " + FirstSentence(f.taskText) + ": sending it again (" + std::to_string(r.taskResends) +
                " of " + std::to_string(kUseOnResends) + ")";
            return;
        }
        Pause(r, what + " failed: " + f.taskText +
            (f.taskActStarted ? " (the cast had started: not sent again; look whether it took before .bot run resume)"
                              : std::string()));
        return;
    }
    if (f.taskReply == 1)
    {
        r.useOnRang.insert(r.step);
        if (f.objectReacted || seen)
        {
            NextStep(r, what + " done" + (f.objectReacted ? (f.taskText.empty() ? std::string() : ": " + f.taskText)
                                                          : ": " + boss + " is here"));
            return;
        }
        if (r.phase != Phase::Wait)
            EnterPhase(r, Phase::Wait);
        if (r.phaseMs >= kUseOnProofMs)
        {
            Pause(r, what + ": the order was ACKed but object " + std::to_string(s.entry) + " showed no change" +
                (boss.empty() ? std::string() : " and " + boss + " did not show") + " within " +
                std::to_string(kUseOnProofMs / 1000) + " s: the item did not take (it is not used again this run; " +
                ".bot run skip goes on)");
            return;
        }
        r.note = what + ": ACKed; waiting for the object or " + (boss.empty() ? std::string("its event") : boss) +
            " to show it took";
        return;
    }
    if (r.phaseMs >= kTaskCapMs)
    {
        // The use may have taken without an answer: latched like an ACK, never sent again this run.
        r.useOnRang.insert(r.step);
        Pause(r, what + ": no answer in " + std::to_string(kTaskCapMs / 1000) + " s: not used again this run (it may " +
            "have taken); .bot run skip goes on");
        return;
    }
    r.note = what + (f.carrierBusy ? ": " + f.carrier + " is on it" : std::string(": waiting for the answer"));
}

// The event's units now (the glue lists the living ones within the hold's
// radius, the friendly, still dormant ones too, and counts the dead).
// Whether a unit of a hold's radius counts as one of the event's own: awake
// (hostile), or of the hold's first entry (the event's named mobs: the four
// Stone Keepers, Archaedas), dormant ones too. The dormant statues of the other
// entries in the hall (Archaedas's room holds some fifty Earthen Guardians and
// Custodians, 2026-10-16 live run: "0/53 down") count only once they wake.
inline bool EventUnitCounts(const PlanStep& s, const UnitFact& u)
{
    return u.hostile || s.entries.empty() || u.entry == s.entries.front();
}

inline void CountEvent(Run& r, const Facts& f, const PlanStep& s)
{
    std::uint32_t up = 0, counted = 0;
    for (const UnitFact& u : f.units)
    {
        up += u.hostile ? 1 : 0;
        counted += EventUnitCounts(s, u) ? 1 : 0;
    }
    r.eventUp = up;
    r.eventDown = f.eventDown;
    r.eventTotal = counted + f.eventDown;
    // A hold done by its units (no done flag) starts once one of them is seen
    // awake, in a fight too: r14-19z32 Bly turned hostile and was in the fight
    // on the same tick, HoldStep (the only place that marked it) never saw him
    // awake, and the hold waited 300 s twice with the crew dead ("3 of 3
    // down"). A data or object hold keeps its own start rule (HoldStep).
    if (up && !(s.flags & (ai::kPlanFlagDoneData | ai::kPlanFlagDoneObject)))
        r.eventStarted = true;
    // A unit alive in the radius at any tick (friendly or not) rules out the "all dead before the hold" end (HoldStep).
    if (!f.units.empty())
        r.holdUnitSeen = true;
}

// "2 of 4 down, 1 awake", or "" before any unit of the event was seen.
inline std::string EventProgress(const Run& r)
{
    if (!r.eventTotal)
        return std::string();
    return std::to_string(r.eventDown) + " of " + std::to_string(r.eventTotal) + " down" +
        (r.eventUp ? ", " + std::to_string(r.eventUp) + " awake" : std::string());
}

// The step that started the event a hold waits on: the nearest ritual, open,
// use_item or gossip before it, with only moves, rests and waits between. npos for none.
inline std::size_t EventStartStep(const Run& r)
{
    if (!r.plan)
        return static_cast<std::size_t>(-1);
    for (std::size_t i = r.step; i-- > 0;)
    {
        PlanStepKind const k = r.plan->steps[i].kind;
        if (k == PlanStepKind::Ritual || k == PlanStepKind::Open || k == PlanStepKind::UseItem ||
            k == PlanStepKind::Gossip || k == PlanStepKind::UseOn)
            return i;
        if (k != PlanStepKind::Move && k != PlanStepKind::MoveDirect && k != PlanStepKind::Rest &&
            k != PlanStepKind::WaitObject && k != PlanStepKind::WaitClear)
            break;
    }
    return static_cast<std::size_t>(-1);
}

// The event reset (or never started): the run goes back to the step that
// starts it and pauses, so `.bot run resume` regroups and starts it again.
inline void EventFailed(Run& r, const std::string& why)
{
    std::size_t const start = EventStartStep(r);
    std::string again = ": .bot run resume waits for it again, .bot run skip goes on without it";
    if (start != static_cast<std::size_t>(-1))
    {
        r.step = start;
        r.autoRegroups = 0;
        ResetStep(r);
        again = ": .bot run resume regroups and starts it again at step " + std::to_string(start + 1) + " (" +
            ai::PlanStepKindName(r.plan->steps[start].kind) + ")";
    }
    r.fighting = false;
    r.eventRetry = true;
    Pause(r, "event failed (" + why + ")" + again);
}

// A wipe regroup inside a hold whose encounter has reset: the encounter is not
// done, so waiting on the spot would wait for an event nobody restarted
// (events-5: the wipe at 01:38:23 was followed by a regroup and a walk back to
// the hold step, the hold then held 30 s for data 2 to leave 0 and paused
// "event failed (data 2 did not leave 0)" at 01:45:12 -- 6.8 min, and
// unattended that pause hits the 10 min cap and the run fails). The run goes
// back to the step that starts the event instead, once per event per run
// (Run::eventRearmed): a second wipe in the same event regroups and pauses as
// it does today. r.eventRetry is set as EventFailed does, so a `busy` altar is
// reported as spent. False: nothing re-armed.
inline bool RearmEventOnWipe(Run& r, const Facts& f)
{
    const PlanStep* s = Current(r);
    if (!s || s->kind != PlanStepKind::Hold)
        return false;
    if (!(s->flags & ai::kPlanFlagDoneData) || !(s->flags & ai::kPlanFlagEncounter))
        return false;
    // Only a RESET encounter: NOT_STARTED (0) or FAIL (2). IN_PROGRESS (1) is a
    // live event (a wipe with the fight still on, a boss walking home between
    // waves): dropping it and sending the ritual into a running event breaks
    // the event, so the run holds on the spot as it does today. DONE (the
    // hold's param) ends the step before this; -1 is no instance data at all.
    if (f.eventData != static_cast<long long>(ai::kPlanEncounterNotStarted) &&
        f.eventData != static_cast<long long>(ai::kPlanEncounterFail))
        return false;
    std::size_t const start = EventStartStep(r);
    if (start == static_cast<std::size_t>(-1) || r.eventRearmed.count(start))
        return false;
    r.eventRearmed.insert(start);
    r.step = start;
    r.eventRetry = true;
    return true;
}

// The hold's done condition from what the glue reads on every tick, with the
// spot in sight or not, in a fight, a regroup or a pause too (instance data is
// map-wide; the object counts when it is within the lead bot's reach). Uldaman
// run 3 (19z12): Archaedas died 12 s after the lead bot; the hold then rested,
// hopped toward its spot from where the lead bot came back to life and paused
// after 8 hops, and the agent's pause cap ended the run FAILED with data 2 at 3
// (DONE) and the vault door 124369 open. "" when not done, else the note.
inline std::string HoldDoneNote(const PlanStep& s, const Facts& f)
{
    if (s.kind != PlanStepKind::Hold)
        return std::string();
    if (s.flags & ai::kPlanFlagDoneData)
    {
        if (f.eventData >= 0 && f.eventData == static_cast<long long>(s.param))
            return "event done: data " + std::to_string(s.entry) + " reads " + std::to_string(s.param);
    }
    else if ((s.flags & ai::kPlanFlagDoneObject) && f.objectState == static_cast<int>(s.param))
        return "event done: object " + std::to_string(s.entry) + " is in state " + std::to_string(s.param);
    return std::string();
}

// A paused run whose current step is a hold whose event is done (the glue
// reads HoldDoneNote's facts while paused too): the step ends, and the run
// resumes by itself with a resume's regroup (revive, gather or summon, walk
// back), so a pause never outlives the event it waited on (Uldaman run 3: the
// agent's 10-minute pause cap ended the run FAILED with Archaedas dead). A
// pause for a player's order stays a pause: the step ends, the note says the
// event is done, and `.bot run resume` goes on. True when the step ended.
inline bool HoldDoneWhilePaused(Run& r, const Facts& f)
{
    const PlanStep* s = Current(r);
    if (r.state != State::Paused || !s)
        return false;
    std::string const done = HoldDoneNote(*s, f);
    if (done.empty())
        return false;
    bool const byPlayer = PausedByPlayer(r);
    std::string const why = r.note;
    r.state = State::Running;
    r.fighting = false;
    NextStep(r, done);
    if (r.state == State::Done)
        return true;
    if (byPlayer)
    {
        r.state = State::Paused;
        // The note keeps its "player order" head: the glue lets the held bots
        // go without forming them up over the player's order (ReleaseRunHold).
        r.note = why + "; " + done + " meanwhile: .bot run resume goes on with step " + std::to_string(r.step + 1);
        return true;
    }
    BeginRegroup(r);
    r.walkBackDue = true;
    r.note = done + " while paused (" + why + "): resumed by itself, regrouping";
    return true;
}

// The places of the party on a hold spot (kHoldBehindYd): every living ranged
// bot (a healer too) but the lead bot, in a row kHoldBehindYd behind the spot
// on the side away from the event's mobs (the middle of its named mobs, the
// hold's first entry, else of its awake ones), kHoldSpreadYd apart across that
// line, each at most kHoldGatherYd from the spot and inside the hold's radius.
// Units that ring the spot (no side from their middle): the side whose row is
// nowhere nearer to a unit than the spot is, else the tank's own spot. No
// units seen: the side the party came from (the nearest earlier move point).
// The others follow the lead bot (the tank) on the spot. Empty when no side
// can be told or no ranged bot lives.
inline std::vector<HoldStand> HoldStands(const Run& r, const Facts& f, const PlanStep& s)
{
    std::vector<HoldStand> out;
    std::vector<std::string> names;
    for (const MemberFact& m : f.members)
        if (m.ranged && !m.name.empty() && m.name != f.carrier)
            names.push_back(m.name);
    if (names.empty())
        return out;
    std::sort(names.begin(), names.end());
    // The side the event comes from.
    float bx = 0.0f, by = 0.0f;
    bool side = false;
    for (int pass = 0; pass < 2 && !side; ++pass)
    {
        float cx = 0.0f, cy = 0.0f;
        std::uint32_t n = 0;
        for (const UnitFact& u : f.units)
            if (pass == 0 ? (!s.entries.empty() && u.entry == s.entries.front()) : u.hostile)
            {
                cx += u.x;
                cy += u.y;
                ++n;
            }
        if (!n)
            continue;
        float const dx = cx / n - s.x, dy = cy / n - s.y;
        float const len = std::sqrt(dx * dx + dy * dy);
        if (len >= kHoldMobSideYd)
        {
            bx = -dx / len;
            by = -dy / len;
            side = true;
        }
    }
    float const reach = s.radius > 0.0f ? std::min(kHoldGatherYd, s.radius) : kHoldGatherYd;
    // The row for a side: kHoldBehindYd behind the spot along (ux, uy).
    auto row = [&](float ux, float uy)
    {
        std::vector<HoldStand> stands;
        for (std::size_t i = 0; i < names.size(); ++i)
        {
            float const across = (static_cast<float>(i) - 0.5f * static_cast<float>(names.size() - 1)) * kHoldSpreadYd;
            float dx = ux * kHoldBehindYd - uy * across, dy = uy * kHoldBehindYd + ux * across;
            float const len = std::sqrt(dx * dx + dy * dy);
            if (len > reach)
            {
                dx *= reach / len;
                dy *= reach / len;
            }
            stands.push_back(HoldStand{names[i], s.x + dx, s.y + dy, s.z});
        }
        return stands;
    };
    if (side)
        return row(bx, by);
    if (!f.units.empty())
    {
        // The event's units ring the spot (the four Stone Keepers: their middle
        // 3.4 yd off it), and the script wakes each onto the NEAREST player: a
        // row toward any side the party came from may stand nearer to a statue
        // than the tank does. Of 24 sides, the one whose row keeps every stand
        // at least as far from every unit as the spot is, with the widest
        // margin; none such: the ranged stand on the tank's own spot.
        float bestMargin = -1.0f;
        for (int k = 0; k < 24; ++k)
        {
            float const a = static_cast<float>(k) * 3.14159265f / 12.0f;
            std::vector<HoldStand> stands = row(std::cos(a), std::sin(a));
            float margin = 1e9f;
            for (const HoldStand& st : stands)
                for (const UnitFact& u : f.units)
                {
                    float const fromStand = std::sqrt((st.x - u.x) * (st.x - u.x) + (st.y - u.y) * (st.y - u.y));
                    float const fromSpot = std::sqrt((s.x - u.x) * (s.x - u.x) + (s.y - u.y) * (s.y - u.y));
                    margin = std::min(margin, fromStand - fromSpot);
                }
            if (margin >= 0.0f && margin > bestMargin)
            {
                bestMargin = margin;
                out = std::move(stands);
            }
        }
        if (out.empty())
            for (const std::string& name : names)
                out.push_back(HoldStand{name, s.x, s.y, s.z});
        return out;
    }
    for (std::size_t i = r.step; r.plan && i-- > 0;)
    {
        const PlanStep& p = r.plan->steps[i];
        if (p.kind != PlanStepKind::Move && p.kind != PlanStepKind::MoveDirect)
            continue;
        float const dx = p.x - s.x, dy = p.y - s.y;
        float const len = std::sqrt(dx * dx + dy * dy);
        if (len >= kHoldMobSideYd)
            return row(dx / len, dy / len);
    }
    return out;
}

// Whether two stand lists name the same bots (positions aside: the event's
// mobs wake and move, the places stay until the party changes).
inline bool SameStandNames(const std::vector<HoldStand>& a, const std::vector<HoldStand>& b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].name != b[i].name)
            return false;
    return true;
}

// hold: walks to the spot, then stands there and lets the bots fight what the
// event sends (no pulls of its own, but for an awake unit of the event that
// does not come within kHoldIdlePullMs), walks back to the spot after a fight
// that took it more than kHoldReturnYd away, and ends on the done condition.
// A failed event goes back to its start step (EventFailed); `timeout_s` is the
// event's ceiling, fights included.
inline void HoldStep(Run& r, const Facts& f, const PlanStep& s, std::uint32_t dt, std::string& order)
{
    if (r.phase == Phase::AwaitPull)
    {
        AwaitPull(r);
        if (r.phase == Phase::AwaitPull)
            return;
    }
    if (!f.seen)
    {
        ApproachHop(r, f, s, dt, order, "event spot");
        return;
    }
    r.eventOn = true;
    CountEvent(r, f, s);
    std::string const progress = EventProgress(r);
    std::string const tail = progress.empty() ? std::string() : " (" + progress + ")";
    std::string const data = "data " + std::to_string(s.entry);
    if (s.flags & ai::kPlanFlagDoneData)
    {
        if (f.eventData < 0)
        {
            Pause(r, "event: the lead bot's map has no instance data to read (" + data + ")");
            return;
        }
        if (f.eventData == static_cast<long long>(s.param))
        {
            NextStep(r, "event done: " + data + " reads " + std::to_string(s.param) + tail);
            return;
        }
        if (s.flags & ai::kPlanFlagEncounter)
        {
            bool const notStarted = f.eventData == static_cast<long long>(ai::kPlanEncounterNotStarted);
            bool const failed = f.eventData == static_cast<long long>(ai::kPlanEncounterFail);
            if (!notStarted && !failed)
                r.eventStarted = true;
            if (failed)
            {
                EventFailed(r, data + " reads 2, fail: the encounter reset" + tail);
                return;
            }
            if (notStarted && r.eventStarted)
            {
                EventFailed(r, data + " is back to 0, not started: the encounter reset" + tail);
                return;
            }
            if (notStarted && r.eventMs >= kEventStartMs)
            {
                EventFailed(r, data + " did not leave 0, not started, in " + std::to_string(kEventStartMs / 1000) +
                    " s: the start did not take");
                return;
            }
        }
    }
    else if (s.flags & ai::kPlanFlagDoneObject)
    {
        if (f.objectState == static_cast<int>(s.param))
        {
            NextStep(r, "event done: object " + std::to_string(s.entry) + " is in state " + std::to_string(s.param) + tail);
            return;
        }
    }
    else
    {
        if (r.eventUp)
            r.eventStarted = true;
        if (r.eventStarted && f.units.empty())
        {
            NextStep(r, "event done: every unit of it is dead" + tail);
            return;
        }
        // Every unit lay dead before the hold began (r15-19z33: Bly's crew killed by the troll waves, "4 of 4 down" for 300 s): the
        // event can never start. Only kHoldAllDeadMs after the FIRST look at the spot (eventMs runs from that look and keeps running
        // while the spot is out of sight; this code runs only on a tick with the spot in sight), with corpses in sight (eventDown),
        // no unit ever alive in the radius this hold (holdUnitSeen) and none awake (eventStarted); units that are merely not spawned
        // or not visible yet show no corpses and keep the 300 s ceiling.
        if (!r.eventStarted && !r.holdUnitSeen && f.units.empty() && r.eventDown > 0 && r.eventMs >= kHoldAllDeadMs)
        {
            NextStep(r, "event done: none of its units is alive (they died before the hold)" + tail);
            return;
        }
    }
    if (r.eventMs >= s.timeoutS * 1000u)
    {
        Pause(r, "event not done after " + std::to_string(s.timeoutS) + " s" + tail +
            ": .bot run resume waits again, .bot run skip goes on");
        return;
    }
    // Hold the spot: walk there first, and back after a fight that took the
    // lead bot away (the bots chase in a fight; the run brings them back).
    float const fromSpot = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, s.x, s.y, s.z);
    // A guard order took the tank off its stay in the fight (CheckGuard): it
    // goes back on the spot (the move puts it on "stay" there again), even
    // when the fight left it on the spot (a walk sends no move there), or its
    // own movement takes over between the event's units.
    if (r.holdUnstayed && fromSpot <= kArriveYd)
    {
        r.holdUnstayed = false;
        r.holdArrived = true;
        r.note = "event: the tank back on the hold spot" + tail;
        order = MoveOrder(s.x, s.y, s.z, false);
        return;
    }
    if (!r.holdArrived || fromSpot > kHoldReturnYd || r.holdUnstayed)
    {
        r.holdUnstayed = false;
        if (!WalkTo(r, f, s.x, s.y, s.z, false, dt, order))
        {
            if (r.state == State::Running)
                r.note = (r.holdArrived ? "event: back to the hold spot" : "event: walking to the hold spot") + tail;
            return;
        }
        r.holdArrived = true;
    }
    if (r.phase != Phase::Hold)
        EnterPhase(r, Phase::Hold);
    // The party's places on the spot (HoldStands): set on arrival, again when
    // the living party changes (a revived bot).
    {
        std::vector<HoldStand> stands = HoldStands(r, f, s);
        if (!r.holdPlaced || !SameStandNames(stands, r.holdStands))
        {
            r.holdPlaced = true;
            r.holdStands = std::move(stands);
            ++r.holdStandsGen;
            r.holdFarTicks = 0;
        }
    }
    // An awake unit of the event that does not come is pulled after a while.
    const UnitFact* idle = PickPull(r, f.units, 0);
    r.eventIdleMs = idle ? r.eventIdleMs + dt : 0;
    if (idle && r.eventIdleMs >= kHoldIdlePullMs)
    {
        r.eventIdleMs = 0;
        PullUnit(r, f, *idle, order);
        return;
    }
    r.note = "event: holding the spot" + tail;
}

// Guarding the party from a stand point. The run leader's moves put the tank
// (the lead bot) on "stay" at a pullback stand or a walk-back point, and a
// staying bot does not go after a mob that hits someone else. In a fight, when
// mobs are hitting party bots within kGuardYd of the tank (in melee reach of
// them, or standing and casting: a pullback's mobs still running in do not
// count), the one nearest the tank is the one it goes to pick up
// (`attack <guid> <tank>`, which ends its stay); after the fight the run walks
// it back to the stand point. A tank that holds a mob of its own in melee
// reach picks up only a mob hitting a healer (Uldaman pull 100: a Brawler on
// the tank while a Geomancer and two Maulers killed the mage and the healer).
// A mob on a party pet (victimPet) is picked up too, but only when no mob on a
// bot qualifies (Uldaman Pause 2: a Scorpid on a pet 15 yd off ran the fight
// past the ceiling), and within the same kGuardYd bound.
// Once the guard took the tank off its stay this fight, it keeps watching:
// the next pick-up needs no stay. Never a polymorphed or evading mob. A
// player's own order to the party pauses the run, so this never overrides the
// commander. An engage on arrival (EngageTarget) also keeps it watching, and
// so does a kill_first focus while it is on (Run::focusGuid, cleared once its
// unit is dead or crowd-controlled and no other is left to focus): the focus order takes every bot,
// the tank too, off "stay" (Zul'Farrak r5-19z23 wipe 2: no pick-up in a whole
// fight while mobs killed the priest). Its pick-ups move only the tank and go
// under the same cap and spacing; the focus is re-sent only when its unit
// changes, so the two do not alternate. Null for none.
//
// Inside a scripted event only in its hold (InHoldArea), and only mobs within
// the hold's radius of its spot (HoldGuardTarget): the moves and the start
// step leading into a hold, and anything outside its radius, stay untouched.
// A unit that had kGuardOrdersPerUnit orders this fight (outside a hold), once
// the fight's budget was refilled: passed over. Before any refill the budget
// of kGuardOrdersPerFight governs alone, and one unit may take all of it (pull
// 100: one Mauler on the healer got 3 pick-ups; the cold review of 2026-10-22).
// The orders a unit had before the refill count.
inline bool GuardUnitCapped(const Run& r, std::uint64_t guid)
{
    if (r.guardRefills == 0)
        return false;
    auto const it = r.guardUnitOrders.find(guid);
    return it != r.guardUnitOrders.end() && it->second >= kGuardOrdersPerUnit;
}

inline const UnitFact* GuardPick(const Facts& f, float holdRadius, const Run* capped = nullptr)
{
    const UnitFact* best = nullptr;
    for (const UnitFact& u : f.partyAttackers)
    {
        if (!u.hostile || u.evading || u.polymorphed || !u.hitting || u.distLead > kGuardYd)
            continue;
        if (capped && GuardUnitCapped(*capped, u.guid))
            continue;
        if (holdRadius > 0.0f && (u.distSpot < 0.0f || u.distSpot > holdRadius))
            continue;
        if (f.leadHit && !u.victimHealer)
            continue;
        // A mob on a party pet comes after every mob on a bot, whatever the
        // distances (Uldaman Pause 2: a Scorpid on a pet 15 yd from the tank).
        if (best && best->victimPet != u.victimPet)
        {
            if (u.victimPet)
                continue;
            best = nullptr;
        }
        if (!best || u.distLead < best->distLead)
            best = &u;
    }
    return best;
}

// The current step is a hold whose event is on and the lead bot, a living
// tank, stands within its radius of the spot: the hold step itself (not the
// ritual or the moves before it). Null otherwise.
inline const PlanStep* InHoldArea(const Run& r, const Facts& f)
{
    const PlanStep* s = Current(r);
    if (!s || s->kind != PlanStepKind::Hold || !r.eventOn || !f.leadAlive || !f.leadIsTank || f.carrier.empty())
        return nullptr;
    if (ai::PlanDistance(f.leadX, f.leadY, f.leadZ, s->x, s->y, s->z) > s->radius)
        return nullptr;
    return s;
}

inline const UnitFact* GuardTarget(const Run& r, const Facts& f)
{
    // Watching: fightOrders too, so a refill (guardOrders and engageOrders back
    // to 0) does not end the watch the earlier orders started.
    if (!f.leadAlive || !f.leadIsTank ||
        !(f.leadStaying || r.guardOrders > 0 || r.fightOrders > 0 || r.engageGuid != 0 || r.focusGuid != 0 ||
            r.focusUnstayed) ||
        f.carrier.empty() || InEvent(r))
        return nullptr;
    return GuardPick(f, 0.0f, &r);
}

// The guard inside a hold (Uldaman Keepers, 19z12 attempt 1: the tank held
// "stay" on the hold spot at 102 270 while a Stone Keeper killed the priest
// at 119 266, 17 yd away, and the mage fled at 16 %). The same rules as
// GuardTarget (melee reach or casting, a healer first, the tank's own mob in
// melee reach), bounded to the hold: only mobs within the hold's radius of
// its spot, so the tank never leaves the hold area for a pick-up and never
// goes after a dormant statue (it hits nobody).
inline const UnitFact* HoldGuardTarget(const Run& r, const Facts& f)
{
    // Watching: on stay, or taken off it by a hold order (holdUnstayed) this
    // fight; guardOrders starts over with each death, holdUnstayed does not.
    const PlanStep* s = InHoldArea(r, f);
    if (!s || !(f.leadStaying || r.guardOrders > 0 || r.holdUnstayed))
        return nullptr;
    return GuardPick(f, s->radius);
}

// The tank on "stay" on the hold spot does not fight at all: an attack
// action is a movement action, and a staying bot's movement actions are not
// useful (19z12 attempt 1: Trian's "tank assist" was useless and "select new
// target" failed 1871 times in 100 s; it never swung at the keepers, only
// taunted). So once an awake unit of the event is in the fight (hostile, in
// combat, within the radius: the hold's units), the tank on stay is sent at
// one (`attack`, which ends its stay) and fights it where it stands; out of
// the fight the hold puts it back on the spot. The healer's attacker first,
// then one on someone else, then one on the tank itself, each the nearest.
//
// Once a hold order took it off its stay this fight (holdUnstayed), its own
// AI fights and the run only sends it at a unit nobody holds: never one
// already on the tank, nor the one it was sent at last, and while the tank
// holds a unit (one on it, or a mob hitting it in melee reach) only one that
// hits the healer. Switching costs the threat it built: with two units in the
// fight (Archaedas and a waking statue every 10 s) a "the one it is not on"
// rule sent it A, B, A every 2 s.
inline const UnitFact* HoldEngageTarget(const Run& r, const Facts& f)
{
    if (!(f.leadStaying || r.holdUnstayed) || !InHoldArea(r, f))
        return nullptr;
    auto const awake = [](const UnitFact& u)
    {
        return u.hostile && u.engaged && !u.evading && !u.polymorphed && u.distLead <= kGuardYd;
    };
    bool holding = f.leadHit;
    for (const UnitFact& u : f.units)
        if (awake(u) && u.victimLead)
            holding = true;
    auto const rank = [](const UnitFact& u) { return u.victimHealer ? 0 : u.victimLead ? 2 : 1; };
    const UnitFact* best = nullptr;
    for (const UnitFact& u : f.units)
    {
        if (!awake(u))
            continue;
        if (!f.leadStaying && (u.victimLead || u.guid == r.holdEngageGuid || (holding && !u.victimHealer)))
            continue;
        if (!best || rank(u) < rank(*best) || (rank(u) == rank(*best) && u.distLead < best->distLead))
            best = &u;
    }
    return best;
}

// Engage on arrival (PLAN-THREAT-265, M1a). The tank on "stay" at a stand
// fights nothing, so a mob that reaches it, or one hitting the healer, waited
// for the guard's wait rule or for nothing (RFC 19z19 step 25: idle 21 s with a
// Trogg on the mage and the rogue 2-4 yd away; Uldaman 19z19 step 85: three
// mobs on it and no target for 14 s). While the lead bot, a living tank, is on
// stay outside an event, the one to send it at at once: a healer's attacker
// (hitting, not polymorphed or evading, within kGuardYd, the nearest), else the
// nearest mob on the tank itself in melee reach (Facts::leadAttacker). A DPS's
// attacker is not engaged here: it stays with GuardTarget's wait, so a mob
// that will come to the tank does not draw it from its stand. Null for none.
inline const UnitFact* EngageTarget(const Run& r, const Facts& f)
{
    if (!f.leadAlive || !f.leadIsTank || !f.leadStaying || InEvent(r) || f.carrier.empty())
        return nullptr;
    const UnitFact* best = nullptr;
    for (const UnitFact& u : f.partyAttackers)
    {
        if (!u.victimHealer || !u.hostile || u.evading || u.polymorphed || !u.hitting || u.distLead > kGuardYd)
            continue;
        if (GuardUnitCapped(r, u.guid))
            continue;
        if (!best || u.distLead < best->distLead)
            best = &u;
    }
    if (best)
        return best;
    if (f.leadHasAttacker && f.leadAttacker.hostile && !f.leadAttacker.evading && !f.leadAttacker.polymorphed &&
        !GuardUnitCapped(r, f.leadAttacker.guid))
        return &f.leadAttacker;
    return nullptr;
}

// A hold's fight in which a living ranged bot (the healer too) is more than kHoldFollowRangedYd from the
// living tank on the ground, and the tank is more than kHoldReturnYd off the spot, for kHoldFollowTicks
// fight ticks in a row (Zul'Farrak r12-19z29: the tank 30-60 yd down the stairs, the four on their places
// 36-40 yd from it, out of heal range): the places are let go and the glue's release (SyncHoldStands ->
// FormUpOnLead) makes the placed bots follow the tank. A tank that goes after a keeper with the ranged
// within reach of it keeps them in place (the Uldaman Keepers and Archaedas holds). The release is one-way
// within the fight: HoldStep does not run while the fight branch does, so the places are set again only when
// the fight is over and HoldStep has the tank back on the spot (holdPlaced false). The dead lead bot is not
// followed (the rez and regroup rules apply). Only the hold's own fight: another step or a hold whose event
// is not on touches nothing. True when the places were let go.
inline bool ReleaseHoldStands(Run& r, const Facts& f)
{
    const PlanStep* s = Current(r);
    if (!s || s->kind != PlanStepKind::Hold || !r.eventOn || !f.leadAlive || !f.leadIsTank || r.holdStands.empty())
    {
        r.holdFarTicks = 0;
        return false;
    }
    float const fromSpot = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, s->x, s->y, s->z);
    float farthest = 0.0f;
    for (const MemberFact& m : f.members)
    {
        if (!m.ranged || m.yd >= 1000.0f)
            continue;
        farthest = std::max(farthest, std::sqrt(std::max(0.0f, m.yd * m.yd - m.dz * m.dz)));
    }
    if (!(fromSpot > kHoldReturnYd) || !(farthest > kHoldFollowRangedYd))
    {
        r.holdFarTicks = 0;
        return false;
    }
    if (++r.holdFarTicks < kHoldFollowTicks)
        return false;
    r.holdFarTicks = 0;
    r.holdStands.clear();
    r.holdPlaced = false;
    ++r.holdStandsGen;
    ++r.holdReleases;
    r.note = "event: the tank is " + Num(fromSpot) + " yd off the spot, the ranged " + Num(farthest) +
        " yd behind the tank: the ranged leave their places and follow it (" + std::to_string(r.holdReleases) + ")";
    return true;
}

// The note's tail for the first order after a refill (CheckGuard), once.
inline std::string RefillNote(Run& r)
{
    if (!r.guardRefilled)
        return std::string();
    r.guardRefilled = false;
    return "; a new unit joined: the orders start over";
}

// One fight tick of the guard: mobs hitting the party for kGuardWaitMs (a
// healer: at once) send the pick-up, at most kGuardOrdersPerFight a fight and
// kGuardRepeatMs apart. In a hold the tank on stay is sent at the event's
// awake unit at once (HoldEngageTarget), and the order count starts over with
// each unit of the event that dies (a hold is one long fight: the four
// Keepers one after the other). True when `order` holds it.
inline bool CheckGuard(Run& r, const Facts& f, std::uint32_t dt, std::string& order)
{
    r.guardSinceOrderMs += dt;
    bool const hold = InHoldArea(r, f) != nullptr;
    if (hold && r.eventDown != r.guardEventDown)
    {
        r.guardEventDown = r.eventDown;
        r.guardOrders = 0;
    }
    if (!hold)
    {
        // A unit not seen in this fight before joins it (a summon, a mob that
        // crossed into the party) once a budget is spent: both budgets start
        // over (kGuardOrdersPerFight each), under kGuardOrdersFightCap. A unit
        // that joins while a budget is left is served by that budget. The
        // spacing (kGuardRepeatMs after the last order) is kept: fightOrders,
        // not the budgets, says whether an order went out this fight.
        bool joined = false;
        auto const see = [&r, &joined](const UnitFact& u)
        {
            if (u.hostile && r.guardSeen.insert(u.guid).second)
                joined = true;
        };
        for (const UnitFact& u : f.partyAttackers)
            see(u);
        if (f.leadHasAttacker)
            see(f.leadAttacker);
        if (joined && (r.guardOrders >= kGuardOrdersPerFight || r.engageOrders >= kGuardOrdersPerFight) &&
            r.fightOrders < kGuardOrdersFightCap)
        {
            r.guardOrders = 0;
            r.engageOrders = 0;
            r.guardRefilled = true;
            ++r.guardRefills;
        }
        // Engage on arrival: the first order of the fight (of either kind)
        // without a wait, every other one (another mob, or after a pick-up) no
        // sooner than kGuardRepeatMs after the last order of either kind, and
        // at most kGuardOrdersPerFight engage orders in all. An `attack` the bot holds
        // (stunned, feared) leaves the stay on, and the nearest mob changes:
        // without the spacing and the cap an order went out every tick. Not
        // one of the pick-ups (guardOrders).
        const UnitFact* arrival = EngageTarget(r, f);
        if (arrival && arrival->guid != r.engageGuid && r.engageOrders < kGuardOrdersPerFight &&
            r.fightOrders < kGuardOrdersFightCap &&
            (r.fightOrders == 0 || r.guardSinceOrderMs >= kGuardRepeatMs))
        {
            r.engageGuid = arrival->guid;
            ++r.engageOrders;
            ++r.fightOrders;
            ++r.guardUnitOrders[arrival->guid];
            r.guardMs = 0;
            r.guardSinceOrderMs = 0; // a pick-up follows it no sooner than kGuardRepeatMs
            if (!r.guardReturn)
            {
                // The stand it leaves, as at the first pick-up.
                r.guardReturn = true;
                r.guardX = f.leadStayKnown ? f.leadStayX : f.leadX;
                r.guardY = f.leadStayKnown ? f.leadStayY : f.leadY;
                r.guardZ = f.leadStayKnown ? f.leadStayZ : f.leadZ;
            }
            r.note = "fighting: the tank engages " + std::to_string(arrival->entry) + " on arrival (" +
                Num(arrival->distLead) + " yd)" + RefillNote(r);
            order = "attack " + std::to_string(arrival->guid) + " " + f.carrier;
            return true;
        }
    }
    const UnitFact* u = hold ? HoldGuardTarget(r, f) : GuardTarget(r, f);
    const UnitFact* engage = hold && !u ? HoldEngageTarget(r, f) : nullptr;
    if (!u && !engage)
    {
        r.guardMs = 0;
        return false;
    }
    if (u)
        r.guardMs += dt;
    std::uint32_t const wait = engage ? 0u : u->victimHealer ? kGuardHealerWaitMs : kGuardWaitMs;
    if ((u && r.guardMs < wait) || r.guardOrders >= kGuardOrdersPerFight ||
        (!hold && r.fightOrders >= kGuardOrdersFightCap) ||
        ((r.guardOrders > 0 || (!hold && r.fightOrders > 0)) && r.guardSinceOrderMs < kGuardRepeatMs))
        return false;
    r.guardMs = 0;
    r.guardSinceOrderMs = 0;
    ++r.guardOrders;
    if (!hold)
    {
        ++r.fightOrders;
        ++r.guardUnitOrders[u->guid];
    }
    if (hold)
    {
        // The hold walks it back to the spot (HoldStep), not the guard's return.
        r.holdUnstayed = true;
        if (engage)
        {
            r.holdEngageGuid = engage->guid;
            r.note = "event: the tank takes " + std::to_string(engage->entry) + " (" + Num(engage->distLead) + " yd)";
            order = "attack " + std::to_string(engage->guid) + " " + f.carrier;
            return true;
        }
    }
    else if (!r.guardReturn)
    {
        // The first pick-up: the tank's stay point (its stand), not where it
        // is now: a tank on stay may still be dragging a pullback in (Uldaman
        // run 3: 18 yd short of the stand, in the corridor). Its own spot only
        // when no stay point is known.
        r.guardReturn = true;
        r.guardX = f.leadStayKnown ? f.leadStayX : f.leadX;
        r.guardY = f.leadStayKnown ? f.leadStayY : f.leadY;
        r.guardZ = f.leadStayKnown ? f.leadStayZ : f.leadZ;
    }
    r.note = std::string(hold ? "event" : "fighting") + ": the tank picks up " + std::to_string(u->entry) + " off " +
        (u->victimHealer ? "the healer" : u->victimPet ? "a pet" : "the party") + " (" + Num(u->distLead) + " yd)" +
        RefillNote(r);
    order = "attack " + std::to_string(u->guid) + " " + f.carrier;
    return true;
}

// The tank's reach for a pull stops as soon as a mob has aggro. Uldaman run 4
// (2026-10-17, sentinel pull 2): the tank kept walking toward the west Relic
// Hunters to reach its pull target, 15.7 yd from the Sentinel, after the camp
// was awake, and pulled six mobs with the boss. While the lead bot is still
// reaching (Facts::leadReaching) and any mob has a party bot as its victim
// (Facts::reachAggro), the glue ends the lead bot's pull (Run::stopReachDue,
// once per pull): the tank stands where it is and the fight is the party's.
// True when it asks.
inline bool CheckReach(Run& r, const Facts& f)
{
    if (r.reachStopped || !f.leadReaching || f.reachAggro == 0 || !f.leadAlive || InEvent(r))
        return false;
    r.reachStopped = true;
    r.stopReachDue = true;
    r.note = "fighting: the pull stops, " + std::to_string(f.reachAggro) +
        (f.reachAggro == 1 ? " mob has" : " mobs have") + " aggro already";
    return true;
}

// Whether a bot is kept to heals (PlayerbotAI::RunHealOnly): only a healer
// that is in the party of a running run and is not its lead bot. The glue asks
// it of every bot of every running run once a second; whoever fails it gets
// its own AI back (a hire removed mid-run, a role change, a pause, the end).
inline bool HealerKeptToHeals(bool runRunning, bool inRunParty, bool isLead, bool isHealer)
{
    return runRunning && inRunParty && !isLead && isHealer;
}

// Whether the glue puts the lead bot back on "stay" now (RFC r3-19z20, step
// 22): an attack or pull order ends the lead's stay (RelaxTacticalMovement)
// and nothing gave it back after the fight, so while the party rested the
// lead ran the idle "formation station" walk, 16 yd into the lava basin, and
// the party followed it in. While the run rests, the lead holds where it
// stands; the next step's own order (a move puts it on "stay" at its point,
// a pull or attack ends the stay again) takes over when the rest ends. Asked
// once a tick; true only while the lead is off "stay", so it is sent once.
// A lead standing in magma (Player::IsInMagma) is not held there: it must be
// free to walk out (the movement guard lets a bot in magma leave it).
inline bool LeadStayDueWhileResting(Run const& r, bool leadAlive, bool leadInCombat, bool leadStaying,
    bool leadInMagma)
{
    return r.state == State::Running && r.phase == Phase::Rest && leadAlive && !leadInCombat && !leadStaying &&
        !leadInMagma;
}

// A party member coming back with attackers on it. The owner's rule
// (2026-10-17): "the party members running back to party should just run past
// the mobs and try to run to the party, when its with the party and gets help
// it can stop and fight with them."
//
// Only while the run waits for its party: a regroup (revive, gather), a
// step's gather, or a walk back. Never during an event (a hold, or the steps
// leading into one: the Keepers, Archaedas); a player's order pauses the run
// (and ends every run back) anyway.
inline bool RunBackAllowed(const Run& r)
{
    const PlanStep* s = Current(r);
    if (!s || s->kind == PlanStepKind::Hold || r.eventOn || EventHoldAhead(r))
        return false;
    return r.regroup || r.phase == Phase::Gather || r.walkBack;
}

// One second of the run backs. A living bot other than the lead bot, more
// than kGatherYd and at most kRunBackMaxYd from it, with attackers, runs on to
// the party (Run::runBack: the glue keeps it a passive follower of the lead
// bot, so it does not turn on its attackers) until it is within
// kRunBackArriveYd; there the party's own group help takes its attackers on
// (SightDistance). The party sends no order meanwhile: it does not walk out to
// meet it. A bot that stops gaining ground (kStragglerGainYd in kWalkRetryMs:
// the Uldaman scorpid pit floor has no navmesh path out) or runs longer than
// kRunBackCapMs is let go; the gather's summon of a stalled straggler takes it
// from there. A bot is listed only while the lead bot is out of combat. True
// while someone runs back: the run waits (no regroup or step order), but its
// fight handling (the wipe regroup, crowd control, kill_first, the ceilings)
// still runs.
inline bool RunBack(Run& r, const Facts& f, std::uint32_t dt)
{
    bool const allowed = RunBackAllowed(r) && f.leadAlive && (r.regroup || (f.dead < kWipeDead && f.alive > 0));
    if (!allowed)
    {
        ClearRunBack(r);
        return false;
    }
    if (r.regroup && !r.reviveSent && f.dead > 0)
        return false; // the revive goes out first
    auto member = [&](const std::string& name) -> const MemberFact*
    {
        for (const MemberFact& m : f.members)
            if (m.name == name)
                return &m;
        return nullptr;
    };
    // The running ones: arrived, dead, stalled or out of time end their run.
    for (std::size_t i = 0; i < r.runBack.size();)
    {
        std::string const name = r.runBack[i];
        const MemberFact* m = member(name);
        Run::RunBackState& st = r.runBackState[name];
        st.ms += dt;
        st.stillMs += dt;
        bool done = !m || m->yd <= kRunBackArriveYd;
        if (!done)
        {
            if (m->yd < st.bestYd - kStragglerGainYd)
            {
                st.bestYd = m->yd;
                st.stillMs = 0;
            }
            if (st.stillMs >= kWalkRetryMs || st.ms >= kRunBackCapMs)
            {
                r.runBackDone.insert(name);
                done = true;
            }
        }
        if (done)
        {
            r.runBackState.erase(name);
            r.runBack.erase(r.runBack.begin() + static_cast<std::ptrdiff_t>(i));
        }
        else
            ++i;
    }
    // New ones, only while the lead bot is in no fight of its own: a healer
    // standing back in the party's fight is not called in (and keeps healing).
    for (const MemberFact& m : f.members)
    {
        if (f.leadInCombat)
            break;
        if (m.name.empty() || m.name == f.carrier || !m.attackers || m.yd <= kGatherYd || m.yd > kRunBackMaxYd ||
            r.runBackDone.count(m.name) ||
            std::find(r.runBack.begin(), r.runBack.end(), m.name) != r.runBack.end())
            continue;
        r.runBack.push_back(m.name);
        Run::RunBackState st;
        st.bestYd = m.yd;
        r.runBackState[m.name] = st;
        ++r.runBacks;
    }
    if (r.runBack.empty())
        return false;
    std::string note;
    for (const std::string& name : r.runBack)
    {
        const MemberFact* m = member(name);
        std::uint32_t const n = m ? m->attackers : 0;
        note += (note.empty() ? "regroup: " : "; ") + name + " running back with " + std::to_string(n) +
            (n == 1 ? " attacker" : " attackers");
    }
    r.note = note;
    return true;
}

// The out-of-combat ceiling of a step: kStepIdleCapMs, or a wait's own
// timeout plus a minute to get there when that is longer.
// The lead bot's death, seen on any tick outside a regroup (a regroup
// revives and gathers whoever died): where it fell is its last living spot.
inline void NoteLeadDeath(Run& r, const Facts& f)
{
    for (const std::string& name : f.deadNames)
        r.waitFor.insert(name);
    // The lead bot dead during a regroup comes back at the entrance after a
    // `revive party`: the regroup walks the plan's way back afterwards.
    if (!f.leadAlive && r.regroup)
        r.walkBackDue = true;
    if (!f.leadAlive)
        SuspendRoute(r); // it comes back where it fell, or at the entrance
    if (f.leadAlive && r.routeCheck)
    {
        // Alive again after a death or a pause: still on the checkpoint's
        // stretch only where it was (a resurrection on its corpse); anywhere
        // else (the entrance after a release, a player's move) it is not known.
        r.routeCheck = false;
        r.routeKnown = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, r.routeSpotX, r.routeSpotY, r.routeSpotZ) <=
            kRouteSpotYd;
    }
    if (f.leadAlive)
    {
        r.leadSeen = true;
        r.leadLastX = f.leadX;
        r.leadLastY = f.leadY;
        r.leadLastZ = f.leadZ;
        return;
    }
    if (r.leadDied || r.regroup || !r.leadSeen)
        return;
    r.leadDied = true;
    r.leadDeathX = r.leadLastX;
    r.leadDeathY = r.leadLastY;
    r.leadDeathZ = r.leadLastZ;
}

// The lead bot died alone in a fight (fewer than kWipeDead dead) and came
// back to life by itself more than kSeeYd from where it fell: its own release
// and corpse run put it at the instance entrance while the others won the
// fight deep inside. Uldaman 19z11 02:48: no regroup followed, the step went
// on approaching the pack from the entrance and paused after 8 hops at the
// scorpid pit's navmesh hole. Handled as the wipe recovery (regroup: gather
// on the lead bot, summon a stuck straggler at the ceiling; then the walk back
// over the plan's own points and direct steps), which exists and is proven;
// summoning the lead bot to the party would need a new summon target (`cmd
// summon` brings a bot to the lead bot, not the lead bot to a bot). Counted
// against kAutoRegroupsPerStep; past that it pauses. A lead bot raised where
// it fell (a party resurrection) needs nothing.
// The living party stays where it won the fight: only the revived few walk
// back (19z11 03:24: the three alive deep in the hall were summoned to the
// entrance by hand and the whole party walked the way again). Held: every
// living bot more than kGatherYd from the lead bot and within kSeeYd of where
// the lead bot fell.
inline void FillHold(Run& r, const Facts& f)
{
    r.hold.clear();
    for (const MemberFact& m : f.members)
        if (!m.name.empty() && m.yd > kGatherYd &&
            ai::PlanDistance(m.x, m.y, m.z, r.leadDeathX, r.leadDeathY, r.leadDeathZ) <= kSeeYd)
            r.hold.push_back(m.name);
}

// The lead bot fell inside an event's hold (and is dead, or came back to life
// since): with the fight over, the hold cannot go on without it, so the run
// regroups (revive, gather or hold the party, walk back) instead of hopping
// toward the spot from wherever the lead bot is (Uldaman run 3, 19z12).
inline bool LeadLostInHold(const Run& r, const Facts& f)
{
    const PlanStep* s = Current(r);
    return s && s->kind == PlanStepKind::Hold && r.eventOn && (!f.leadAlive || r.leadDied);
}

inline bool LeadRevivedAway(Run& r, const Facts& f)
{
    if (!r.leadDied || !f.leadAlive || f.anyInCombat)
        return false;
    bool const lostInHold = LeadLostInHold(r, f);
    r.leadDied = false;
    if (r.regroup || (InEvent(r) && !lostInHold) ||
        ai::PlanDistance(f.leadX, f.leadY, f.leadZ, r.leadDeathX, r.leadDeathY, r.leadDeathZ) <= kSeeYd)
        return false;
    if (r.autoRegroups >= kAutoRegroupsPerStep)
    {
        Pause(r, "the lead bot died and came back " + Num(ai::PlanDistance(f.leadX, f.leadY, f.leadZ, r.leadDeathX,
            r.leadDeathY, r.leadDeathZ)) + " yd from where it fell (" + std::to_string(kAutoRegroupsPerStep) +
            " regroups by itself at this step did not hold): .bot run resume");
        return true;
    }
    ++r.autoRegroups;
    r.fighting = false;
    ResetStep(r);
    BeginRegroup(r);
    r.walkBackDue = true;
    FillHold(r, f);
    r.note = "the lead bot died and came back to life away from the party: regrouping by itself (" +
        std::to_string(r.autoRegroups) + " of " + std::to_string(kAutoRegroupsPerStep) + " at this step)" +
        (r.hold.empty() ? std::string() : ", the party holds while it walks back");
    return true;
}

// The owner's wait rule: drops the fallen that are back (alive and within
// kGatherYd of the lead bot), then starts the regroup when one is not. The
// lead bot dead itself: the regroup's revive brings it back (at the entrance
// after a release) and the walk back follows; counted against
// kAutoRegroupsPerStep, past that it pauses. False when nobody is waited for.
inline bool WaitForFallen(Run& r, const Facts& f)
{
    for (auto it = r.waitFor.begin(); it != r.waitFor.end();)
    {
        bool back = false;
        for (const MemberFact& m : f.members)
            if (m.name == *it)
            {
                back = m.yd <= kGatherYd && m.dz <= kStragglerDzYd;
                break;
            }
        it = back ? r.waitFor.erase(it) : std::next(it);
    }
    if (r.waitFor.empty() && f.dead == 0 && f.leadAlive)
        return false;
    bool const lead = !f.leadAlive;
    if (lead)
    {
        if (r.autoRegroups >= kAutoRegroupsPerStep)
        {
            Pause(r, "the lead bot is dead: revive it, then .bot run resume (" +
                std::to_string(kAutoRegroupsPerStep) + " regroups by itself at this step did not hold)");
            return true;
        }
        ++r.autoRegroups;
    }
    std::string const note = WaitNote(r, f);
    bool const deathSpotKnown = r.leadDied;
    // The regroup's gather overrides a walk under way: the step starts over
    // from its beginning once the party is back (a fresh move, not a wait for
    // the walk's retry clock).
    EnterPhase(r, Phase::Begin);
    BeginRegroup(r);
    // A loot step's free-for-all detour waiting for its carrier (LootFfa::Ordered) ends here: this regroup does not
    // reset the step, and the running-tick restore (LootFfaRestoreTick) only runs with no detour in progress.
    // lootFfaLive stays, so the method is ordered back on the next tick.
    r.lootFfa = LootFfa::Idle;
    if (lead)
    {
        r.walkBackDue = true;
        r.holdDue = deathSpotKnown; // the hold waits for the lead bot to be up (regroup)
    }
    r.note = note + (lead ? ": the lead bot is dead, reviving and gathering before the step goes on"
                          : ": reviving and gathering before the step goes on");
    return true;
}

inline std::uint32_t StepIdleCapMs(const PlanStep& s)
{
    bool const wait = s.kind == PlanStepKind::WaitObject || s.kind == PlanStepKind::WaitClear ||
        s.kind == PlanStepKind::Hold;
    std::uint32_t const own = wait ? s.timeoutS * 1000 + 60000 : 0;
    return own > kStepIdleCapMs ? own : kStepIdleCapMs;
}

// One second of the run. Returns the order to send now (the text after
// `.bot cmd `), or "" for none.
// The held party (Run::hold): a bot drops out once dead, gone or within
// kGatherYd of the lead bot; the list ends once the walk back is over.
inline void UpdateHold(Run& r, const Facts& f)
{
    if (!r.walkBack && !r.walkBackDue && !r.regroup)
    {
        r.hold.clear();
        return;
    }
    r.hold.erase(std::remove_if(r.hold.begin(), r.hold.end(), [&](const std::string& name)
        {
            // The bot a direct step summoned (summonWait) stays held until it
            // arrives (kArriveYd), so a timed-out summon 3-20 yd off keeps it held.
            float const near = name == r.summonWait ? kArriveYd : kGatherYd;
            for (const MemberFact& m : f.members)
                if (m.name == name)
                    return m.yd <= near;
            return true;
        }), r.hold.end());
}

// The facts without the held bots: gathers, the gather and regroup ceilings
// and their summons look at the bots walking back only. A held bot a direct
// step summoned (Run::summonWait) is shown, so the summon's wait sees it
// arrive (uldaman-events-4, 14:03 and 14:39: hidden, the wait ended at once,
// the step went out before the 3 s summon landed and the walk back paused at
// the scorpid pit); it stays in the hold until it stands at the lead bot.
inline Facts HoldView(const Run& r, const Facts& f)
{
    Facts g = f;
    g.members.clear();
    g.maxMemberYd = 0.0f;
    for (const MemberFact& m : f.members)
    {
        if (m.name != r.summonWait && std::find(r.hold.begin(), r.hold.end(), m.name) != r.hold.end())
            continue;
        g.members.push_back(m);
        g.maxMemberYd = std::max(g.maxMemberYd, m.yd);
    }
    return g;
}

inline std::string TickBody(Run& r, const Facts& f, std::uint32_t dt, const RestLines& lines);

inline std::string Tick(Run& r, const Facts& f, std::uint32_t dt = kTickMs, const RestLines& lines = RestLines())
{
    r.shotGateRan = false;
    std::string order;
    bool held = false;
    if (r.state == State::Running && !r.hold.empty())
    {
        UpdateHold(r, f);
        if (!r.hold.empty())
        {
            order = TickBody(r, HoldView(r, f), dt, lines);
            held = true;
        }
    }
    if (!held)
        order = TickBody(r, f, dt, lines);
    if (!r.shotGateRan)
        EndShotHold(r); // the pull stand's hold ends with any tick its gate did not run on
    return order;
}

// A unit in Run::neverCame that turns up as an attacker of the party (on a
// bot, on the lead bot, or a kill_first unit in the fight) came after all: it
// is taken out of the set at once, so the hop picks see it again.
inline void ForgetUnitsThatCame(Run& r, const Facts& f)
{
    if (r.neverCame.empty())
        return;
    for (const UnitFact& u : f.partyAttackers)
        r.neverCame.erase(u.guid);
    for (const UnitFact& u : f.killFirst)
        r.neverCame.erase(u.guid);
    if (f.leadHasAttacker)
        r.neverCame.erase(f.leadAttacker.guid);
}

inline std::string TickBody(Run& r, const Facts& f, std::uint32_t dt, const RestLines& lines)
{
    std::string order;
    if (r.state != State::Running)
        return order;
    const PlanStep* s = Current(r);
    if (!s)
    {
        r.state = State::Done;
        r.note = DoneNote(r);
        return order;
    }
    r.runMs += dt;
    r.phaseMs += dt;
    ForgetUnitsThatCame(r, f);
    if (r.eventOn)
        r.eventMs += dt; // an event's clock runs in its fights too
    r.gatherOrder.clear(); // only this tick's gather can be refused next
    if (f.anchorPathRead)
        r.anchorPathWanted = false;
    UpdateStill(r, f, dt);
    NoteDeadSeen(r, f);
    NoteLeadDeath(r, f);
    NoteLeadPath(r, f);
    if (r.lootFfaLive)
    {
        r.lootFfaMs += dt;
        if (LootFfaRestoreTick(r, f, order))
            return order;
    }

    // The stale flag's clocks: one snapshot of "nothing hostile engaged" proves
    // nothing (a guardian, a scripted fight), so the grace is spent once per
    // combat and a wait step is finished over it only when it stands unbroken.
    // They end with the combat (any early return below included), not with a
    // step finished over the flag.
    if (!f.anyInCombat)
    {
        r.staleMs = 0;
        r.staleRunMs = 0;
    }
    else if (f.staleCombat)
    {
        r.staleMs += dt;
        r.staleRunMs += dt;
    }
    else
        r.staleRunMs = 0;

    // A hold whose event is done ends on any tick (HoldDoneNote): in a fight,
    // a regroup, a rest or an approach too, the spot in sight or not.
    if (s->kind == PlanStepKind::Hold)
    {
        std::string const done = HoldDoneNote(*s, f);
        if (!done.empty())
        {
            if (f.seen)
                CountEvent(r, f, *s);
            std::string const progress = EventProgress(r);
            NextStep(r, done + (progress.empty() ? std::string() : " (" + progress + ")"));
            s = Current(r);
            if (!s)
                return order;
        }
    }

    // A wipe (the whole party dead) counts once, however long it lasts.
    bool const allDead = f.total > 0 && f.alive == 0;
    if (allDead && !r.wiped)
        ++r.wipes;
    r.wiped = allDead;
    // A near wipe (kWipeDead or more dead, someone still alive) counts once
    // too; one that ends in a wipe counts as both.
    bool const nearWipe = f.dead >= kWipeDead && f.alive > 0;
    // The latch holds until fewer than kWipeDead are dead: a near wipe that
    // turns into a wipe, and the revive one by one after it, is one event.
    if (nearWipe && !r.nearWiped)
        ++r.nearWipes;
    if (nearWipe)
        r.nearWiped = true;
    else if (f.dead < kWipeDead)
        r.nearWiped = false;

    // A party member coming back with attackers runs on to the party; the run
    // waits for it where it stands (RunBack): no regroup or step order, but
    // the fight handling below still runs.
    bool const runBackWait = RunBack(r, f, dt);
    std::string const runBackNote = runBackWait ? r.note : std::string();

    // After a resume or a skip from a pause: the dead get up, the living
    // gather on the lead bot, then the step goes on.
    // A fight during the regroup (a pack met on the corpse run) is fought
    // first: the combat block below bounds it, and the regroup goes on after.
    // Deaths in it that make a wipe hand over to the regroup by itself.
    if (r.regroup && r.regroupDead < 0)
        r.regroupDead = static_cast<int>(f.dead);
    if (r.regroup && !f.anyInCombat && (f.dead >= kWipeDead || allDead) &&
        static_cast<int>(f.dead) > r.regroupDead)
        r.regroup = false;
    if (r.regroup && !runBackWait && !f.anyInCombat)
    {
        r.regroupMs += dt;
        UpdateReviveSummons(r, f, dt);
        // A regroup for a dead lead bot: once it is up away from where it fell
        // (a release: the entrance), the living party there holds, before any
        // gather; from the next tick the regroup looks at the others only.
        if (r.holdDue && f.leadAlive)
        {
            r.holdDue = false;
            if (ai::PlanDistance(f.leadX, f.leadY, f.leadZ, r.leadDeathX, r.leadDeathY, r.leadDeathZ) > kSeeYd)
            {
                FillHold(r, f);
                if (!r.hold.empty())
                {
                    r.note = "regroup: the lead bot is up away from the party: the party holds while it walks back";
                    return order;
                }
            }
        }
        if (f.dead > 0 || !f.leadAlive)
        {
            // A living priest, paladin or shaman raises the dead where they
            // lie: `revive party` (a release and a corpse run) waits for it.
            // A nudge the module answered as unwalkable (OrderRefused) ends the wait: the revive goes out now.
            if (f.rezHold && !r.rezBodyUnreachable)
            {
                // The rez hold decides (RezHoldPolicy.h): hold for a resurrection,
                // wait for the resurrector's mana, or fall through to the revive.
                TortoiseBots::RezHold::Inputs hold;
                hold.holdMs = r.regroupMs;
                hold.ableMs = r.rezAbleMs;
                hold.rezInProgress = f.rezInProgress;
                hold.partyEngaged = f.anyInCombat;
                hold.reviveSent = r.reviveSent;
                TortoiseBots::RezHold::Result const verdict = TortoiseBots::RezHold::Decide(hold, f.rezMembers);
                using TortoiseBots::RezHold::Verdict;
                using TortoiseBots::RezHold::Reason;
                // The budget of a rezzer that could act runs while it could; a drink restarts it.
                if (verdict.verdict == Verdict::WaitMana)
                    r.rezAbleMs = 0;
                else if (verdict.reason == Reason::Recovering || verdict.reason == Reason::Casting)
                    r.rezAbleMs += dt;
                if (verdict.verdict == Verdict::Hold || verdict.verdict == Verdict::WaitMana)
                {
                    auto nameOf = [&](int idx) -> std::string
                    {
                        return idx >= 0 && static_cast<std::size_t>(idx) < f.rezMembers.size()
                            ? f.rezMembers[static_cast<std::size_t>(idx)].name : std::string();
                    };
                    std::string who = nameOf(verdict.rezzerIdx);
                    if (!who.empty() && verdict.targetIdx >= 0)
                        who += " for " + nameOf(verdict.targetIdx);
                    // How far the rezzer stands from the body and whether it sees it: the 2026-10-20 Uldaman
                    // hold ran 63 s on a priest that never got into sight of a body 20 yd away.
                    float rezzerYd = 0.0f;
                    bool rezzerSees = false;
                    bool const viewed = verdict.verdict == Verdict::Hold &&
                        TortoiseBots::RezHold::RezzerView(verdict, f.rezMembers, rezzerYd, rezzerSees);
                    r.note = "regroup: " + WaitNote(r, f) + ", " +
                        (verdict.verdict == Verdict::Hold ? std::string("a resurrection in the party first")
                                                          : std::string("the resurrector drinks first")) +
                        " (" + TortoiseBots::RezHold::ReasonName(verdict.reason) +
                        (who.empty() ? std::string() : ": " + who) +
                        (viewed ? ", " + nameOf(verdict.rezzerIdx) + " " + Num(rezzerYd) + " yd from the body, sight " +
                                      (rezzerSees ? "yes" : "no")
                                : std::string()) + ")";
                    SummonRevived(r, f, order);
                    // A hold that went kRezNudgeMs with a rezzer able and no cast: the lead bot walks onto the body
                    // (once), and the party follows it into the rezzer's reach and sight.
                    if (verdict.verdict == Verdict::WaitMana || f.rezInProgress)
                        r.rezHoldMs = 0;
                    else if (verdict.reason == Reason::Recovering)
                    {
                        r.rezHoldMs += dt;
                        int const t = verdict.targetIdx;
                        if (order.empty() && !r.rezNudged && r.rezHoldMs >= TortoiseBots::RezHold::kRezNudgeMs &&
                            f.leadAlive && t >= 0 && static_cast<std::size_t>(t) < f.rezMembers.size() &&
                            f.rezMembers[static_cast<std::size_t>(t)].placed)
                        {
                            TortoiseBots::RezHold::Member const& body = f.rezMembers[static_cast<std::size_t>(t)];
                            if (ai::PlanDistance(f.leadX, f.leadY, f.leadZ, body.bodyX, body.bodyY, body.bodyZ) >
                                TortoiseBots::RezHold::kRezNearYd)
                            {
                                r.rezNudged = true;
                                order = MoveOrder(body.bodyX, body.bodyY, body.bodyZ, false);
                                r.rezNudgeOrder = order;
                                r.note += "; the lead bot walks to the body";
                            }
                        }
                    }
                    return order;
                }
            }
            else if (f.rezzer && !r.reviveSent && !r.rezBodyUnreachable && r.regroupMs < kPartyRezWaitMs)
            {
                r.note = "regroup: " + WaitNote(r, f) + ", a resurrection in the party first";
                SummonRevived(r, f, order);
                return order;
            }
            r.note = "regroup: " + WaitNote(r, f) + ", reviving";
            if (!r.reviveSent || r.regroupMs % 60000 < dt)
            {
                if (!r.reviveSent && f.rezHold)
                    r.reviveBaseMs = r.regroupMs; // the hold's time is not taken from the corpse run
                r.reviveSent = true;
                r.rezHoldMs = 0;
                r.rezNudged = false;
                r.rezNudgeOrder.clear();
                r.rezBodyUnreachable = false;
                order = "revive party";
            }
            else
                SummonRevived(r, f, order); // one already up far away: before its corpse run
            if (r.regroupMs > kRegroupReviveMs + r.reviveBaseMs)
                Pause(r, "regroup: " + std::to_string(f.dead) + " still dead after " +
                    std::to_string(kRegroupReviveMs / 1000) + " s (cmd revive <bot>, then .bot run resume)");
            return order;
        }
        if (f.maxMemberYd > kGatherYd && !f.anyInCombat && !r.gatherWaived)
        {
            // A revived bot far away is summoned before the gather walks it.
            if (SummonRevived(r, f, order))
                return order;
            r.note = "regroup: " + WaitNote(r, f) + ", gathering on the lead bot" +
                (r.reviveSummons.empty() ? "" : " (a summon on its way)");
            if (!r.regroupGatherSent && !r.reviveSummons.empty())
                return order; // the gather goes out once the summons are in (or failed)
            if (!r.regroupGatherSent)
            {
                r.regroupGatherSent = true;
                r.RestartRegroupClock();
                r.regroupGatherCapMs = RegroupGatherCap(f);
                SendGather(r, f, order);
            }
            // A bot being summoned does not count against the ceiling.
            else if (r.reviveSummons.empty())
            {
                // A far bot that has not moved at all is stuck (a navmesh
                // pocket), not walking: it is summoned at once, before the
                // ceiling is waited out for it (SummonStuckInRegroup).
                if (SummonStuckInRegroup(r, f, order))
                    return order;
                if (r.regroupMs > r.regroupGatherCapMs)
                {
                    if (SummonStraggler(r, f, order))
                        r.RestartRegroupClock();
                    else
                        Pause(r, "regroup: the party did not gather within " +
                            std::to_string(r.regroupGatherCapMs / 1000) + " s (a bot is " + Num(f.maxMemberYd) +
                            " yd from the lead bot)");
                }
            }
            return order;
        }
        r.regroup = false;
        r.reviveSummons.clear();
        r.gatherWaived = false;
        r.gatherRefusals = 0;
        r.waitFor.clear(); // everyone alive and gathered: the fallen are back
        r.leadDied = false;
        r.holdDue = false;
        r.note = "regrouped";
        if (r.walkBackDue)
        {
            StartWalkBack(r, f);
            if (r.walkBack)
                return order; // the next tick reads the facts for the first point
        }
    }

    // A wipe or kWipeDead dead: once the fight is over the run regroups by
    // itself (revive, gather, walk back), kAutoRegroupsPerStep times a step;
    // then it pauses for a regroup by hand.
    bool const wipe = f.dead >= kWipeDead || allDead;
    bool const autoRegroup = wipe && r.autoRegroups < kAutoRegroupsPerStep;
    if (wipe && !autoRegroup)
    {
        Pause(r, std::to_string(f.dead) + " dead: regroup (cmd revive party), then .bot run resume (" +
            std::to_string(kAutoRegroupsPerStep) + " regroups by itself at this step did not hold)");
        return order;
    }
    if (autoRegroup && !f.anyInCombat)
    {
        ++r.autoRegroups;
        r.fighting = false;
        // A wipe inside a hold whose event is not done: the event reset, so the
        // regroup goes back to its start step (RearmEventOnWipe).
        std::size_t const rearmed = RearmEventOnWipe(r, f) ? r.step : static_cast<std::size_t>(-1);
        ResetStep(r);
        BeginRegroup(r);
        r.walkBackDue = true;
        r.note = std::to_string(f.dead) + " dead (" + WipeCounts(r) + " this run): regrouping by itself (" +
            std::to_string(r.autoRegroups) + " of " + std::to_string(kAutoRegroupsPerStep) + " at this step)" +
            (rearmed == static_cast<std::size_t>(-1) ? std::string()
                                                     : "; the event starts again at step " +
                    std::to_string(rearmed + 1) + " (" + ai::PlanStepKindName(r.plan->steps[rearmed].kind) + ")");
        return order;
    }
    // The owner's rule (2026-10-17): "Party should wait if a party member
    // dies until it is back." Once the fight is over, any death (the lead bot
    // too, one in a walk, one that released and runs back) holds the next
    // pull, boss, task or move until the bot is alive and back with the party:
    // the regroup (a resurrection in the party, `revive party`, the gather, a
    // stuck straggler summoned at its ceiling, then a pause with the reason).
    // Not inside a scripted event (it cannot be left mid-way: the wait comes
    // after it), not during a commander task, and not while a member runs
    // back with attackers (RunBack). The lead bot dead alone came back far
    // from where it fell: the wipe recovery (LeadRevivedAway). In a fight the
    // others fight on; more dead make it a wipe and the run regroups by itself.
    if (!autoRegroup && !f.anyInCombat && !r.regroup && !runBackWait && (!InEvent(r) || LeadLostInHold(r, f)) &&
        r.phase != Phase::AwaitTask && r.phase != Phase::Wait)
    {
        if (LeadRevivedAway(r, f))
            return order;
        if (WaitForFallen(r, f))
            return order;
    }

    // A wait_object whose object already is in the wanted state is done, in a
    // fight or not (a leftover combat flag must not hold it): Uldaman run 4,
    // Archaedas down, the vault door 124369 active, "a fight lasted more than
    // 180 s" at the wait step.
    // Only on a stale flag: a real fight keeps its handling and its timer.
    bool const objectDone = s->kind == PlanStepKind::WaitObject && r.staleRunMs >= kStaleConfirmMs &&
        f.objectState >= 0 && f.objectState == static_cast<int>(s->param);

    // Fights are the bots' own; the run only bounds them.
    if (f.anyInCombat && !objectDone)
    {
        // The ritual answered and its event is fighting already (the first
        // Stone Keeper wakes with the cast): the fight is the hold's.
        // An interrupted answer counts too while the event reads in progress:
        // the altar going back to ready as the keepers woke is the event
        // starting (Uldaman run 4, 19z13: the tank stood on stay all fight).
        bool eventRunning = false;
        if (s->kind == PlanStepKind::Ritual && r.orderSent && f.taskReply == 2)
        {
            const PlanStep* ahead = EventHoldAhead(r);
            // Only the module's typed `interrupted` answer, and only when the data was not in progress already
            // when the order went out (a ritual sent after the reset wait ran out): then this order started it.
            eventRunning = ahead && (ahead->flags & ai::kPlanFlagDoneData) && (ahead->flags & ai::kPlanFlagEncounter) &&
                f.eventData == 1 && r.dataAtOrder != 1 && f.taskText.compare(0, 12, "interrupted:") == 0;
        }
        if (s->kind == PlanStepKind::Ritual && r.orderSent && (f.taskReply == 1 || eventRunning))
        {
            NextStep(r, "ritual " + std::to_string(s->entry) + " done: the event started");
            s = Current(r);
            if (!s)
                return order;
            if (s->kind == PlanStepKind::Hold)
                r.eventOn = true;
        }
        if (!r.fighting)
        {
            r.fighting = true;
            r.joinerRest = false; // a fight is on: the aggro estimate's rest wait is moot
            EndBigWalk(r);        // ... and so is the big pack's stand walk gate
            r.fightMs = 0;
            DropHopWaiver(r);
        }
        // A stale flag (nothing hostile engaged) is waited out for a grace
        // period; past it the fight ceiling runs again, so a flag that never
        // clears still ends in a pause.
        bool const staleGrace = f.staleCombat && r.staleMs <= kStaleCombatGraceMs;
        if (!staleGrace)
            r.fightMs += dt;
        // A hold's fights are the event (one wave after the other, no pause
        // between): its own timeout bounds them when that is longer.
        // A start step's fight (or a move's) before its hold is the event's too.
        const PlanStep* const event = s->kind == PlanStepKind::Hold ? s : EventHoldAhead(r);
        std::uint32_t const holdCap = event && event->timeoutS * 1000u > kBossFightCapMs ? event->timeoutS * 1000u
                                                                                       : kBossFightCapMs;
        std::uint32_t const cap = event ? holdCap
                                : s->kind == PlanStepKind::Boss ? kBossFightCapMs : kFightCapMs;
        if (r.fightMs > cap && autoRegroup)
        {
            // kWipeDead or more dead and one left fighting past the ceiling:
            // a wipe, not a long fight. The regroup starts.
            ++r.autoRegroups;
            r.fighting = false;
            ResetStep(r);
            BeginRegroup(r);
            r.walkBackDue = true;
            r.note = std::to_string(f.dead) + " dead and the fight went past " + std::to_string(cap / 1000) +
                " s (" + WipeCounts(r) + " this run): regrouping by itself (" + std::to_string(r.autoRegroups) +
                " of " + std::to_string(kAutoRegroupsPerStep) + " at this step)";
            return order;
        }
        if (r.fightMs > cap && !staleGrace)
        {
            Pause(r, "a fight lasted more than " + std::to_string(cap / 1000) + " s");
            r.ceilingPaused = true;
            r.ceilingQuietMs = 0;
            return order;
        }
        r.note = autoRegroup ? std::to_string(f.dead) + " dead: fighting on, a regroup follows"
               : !f.leadAlive ? std::string("the lead bot is dead: the others fight on") : std::string("fighting");
        if (runBackWait && !autoRegroup)
            r.note = runBackNote;
        // A hold done by its units whose fight began before any HoldStep tick
        // (its crew hostile at once): the event is on, so it is counted and an
        // awake unit marks it started (CountEvent); else it waited to timeout_s
        // with every unit dead (Codex review of 81b9fc71). Data and object
        // holds keep HoldStep's start.
        if (s->kind == PlanStepKind::Hold && f.seen &&
            !(s->flags & (ai::kPlanFlagDoneData | ai::kPlanFlagDoneObject)))
            r.eventOn = true;
        if (s->kind == PlanStepKind::Hold && r.eventOn && f.seen)
        {
            CountEvent(r, f, *s);
            std::string const progress = EventProgress(r);
            if (!autoRegroup && f.leadAlive)
                r.note = "event: fighting" + (progress.empty() ? std::string() : " (" + progress + ")");
        }
        ReleaseHoldStands(r, f);
        CheckReach(r, f);
        if (CheckCc(r, f, dt, order))
            return order;
        if (CheckGuard(r, f, dt, order))
            return order;
        // The plan's kill_first unit of the fight (a healer, its healing ward)
        // is focused: skull on it, every bot attacks it. Again only when the
        // best one changes (it died, or a ward came up).
        // None left and the focused unit is gone (FocusGone: dead or
        // crowd-controlled): the focus is over, and with it the guard's reason
        // to keep watching (GuardTarget). One still alive (evading, say) keeps it.
        if (const UnitFact* k = PickKillFirst(r, f))
        {
            if (k->guid != r.focusGuid)
            {
                r.focusGuid = k->guid;
                r.note = "fighting: kill " + std::to_string(k->entry) + " first";
                order = "focus " + std::to_string(k->guid);
            }
        }
        else if (r.focusGuid != 0 && FocusGone(r, f))
            r.focusGuid = 0;
        return order;
    }
    if (runBackWait)
        return order;
    if (r.fighting)
    {
        // Back to the step's start: it looks at what is left, and rests first.
        r.fighting = false;
        r.restDue = true;
        r.eatWaitMs = 0;
        r.pullGuid = 0;
        r.focusGuid = 0;
        r.focusRefused.clear();
        r.focusUnstayed = false;
        r.guardMs = 0;
        r.guardOrders = 0;
        r.engageGuid = 0;
        r.engageOrders = 0;
        r.guardSeen.clear();
        r.guardUnitOrders.clear();
        r.guardRefilled = false;
        r.guardRefills = 0;
        r.fightOrders = 0;
        r.reachStopped = false;
        // A fight may have moved the lead bot: an exact point's tighten starts
        // over and its proof is taken again before a direct step goes out.
        ResetExact(r);
        r.exactProved = static_cast<std::size_t>(-1);
        if (r.phase != Phase::AwaitTask && r.phase != Phase::Wait)
            EnterPhase(r, Phase::Begin);
    }

    // After a pick-up (CheckGuard): the tank walks back to the spot it stood
    // on at the first pick-up, with the party, before the step goes on; one
    // the fight took it far from is left (it stays near the fight). The
    // arrival ends the tick: this tick's facts were read during the walk,
    // without the navmesh query for an unseen step's approach (the glue skips
    // it while the run walks), so going on at once paused on "no walkable
    // path toward the pack" (Uldaman run 3, baelog pull 7).
    if (r.guardReturn)
    {
        if (InEvent(r) || r.phase == Phase::AwaitTask || r.phase == Phase::Wait)
            r.guardReturn = false;
        else if (r.phase != Phase::Walk &&
            ai::PlanDistance(f.leadX, f.leadY, f.leadZ, r.guardX, r.guardY, r.guardZ) > kGuardReturnMaxYd)
        {
            // It stays near the fight: that is the party's spot now, so a
            // later gather does not walk back to an old anchor far behind.
            r.guardReturn = false;
            r.anchorKnown = true;
            r.anchorX = f.leadX;
            r.anchorY = f.leadY;
            r.anchorZ = f.leadZ;
        }
        else if (WalkTo(r, f, r.guardX, r.guardY, r.guardZ, false, dt, order, true))
        {
            r.guardReturn = false;
            EnterPhase(r, Phase::Begin);
            return order;
        }
        else
        {
            if (r.state == State::Running)
                r.note = "back to the stand point after picking up the party's attackers";
            return order;
        }
    }

    // A walk back is bounded point by point (WalkTo), not by the step's ceiling.
    // The joiner rest (aggro estimate) is bounded by kRestCapMs and kMaxJoinerRests a step, and does not count
    // toward the step's idle ceiling: several loud packs in one pull step must not pause the run.
    if (!r.walkBack && !(r.phase == Phase::Rest && r.restForJoiners))
        r.stepIdleMs += dt;
    if (r.stepIdleMs > StepIdleCapMs(*s))
    {
        Pause(r, "step " + std::to_string(r.step + 1) + " (" + ai::PlanStepKindName(s->kind) + ") took more than " +
            std::to_string(StepIdleCapMs(*s) / 1000) + " s out of combat");
        return order;
    }

    // The hop off a navmesh island under way (IslandEscape): nothing else goes out.
    if (EscapeWait(r, f, dt))
    {
        r.note = "the lead bot hops off the navmesh island";
        return order;
    }

    // Rest gate.
    if (r.phase == Phase::Rest)
    {
        if (r.restForEating)
            r.eatWaitMs += dt;
        if (Rested(f, lines) || r.phaseMs >= kRestCapMs ||
            (r.restForEating && (f.eating == 0 || r.eatWaitMs >= kRestCapMs)))
        {
            r.restForEating = false;
            EnterPhase(r, Phase::Begin);
        }
        else
        {
            r.restRepeatMs += dt;
            if (r.restRepeatMs >= kRestRepeatMs)
            {
                r.restRepeatMs = 0;
                order = "rest";
            }
            return order;
        }
    }
    // A pull that brings many joiners (FightStep): the party is rested to the
    // end lines first, never inside an event.
    if (r.joinerRest)
    {
        r.joinerRest = false;
        if (!Rested(f, lines) && !InEvent(r))
        {
            EnterPhase(r, Phase::Rest);
            r.restForEating = false;
            r.restForJoiners = true;
            ++r.joinerRests; // a slot is spent by a rest that really starts, not by a pick the party was rested for
            r.restRepeatMs = 0;
            r.note = "resting: " + r.joinerNote;
            order = "rest";
            return order;
        }
    }
    if (r.phase == Phase::Begin && (r.restDue || (s->kind == PlanStepKind::Rest && !r.stepRested)))
    {
        bool const forced = s->kind == PlanStepKind::Rest && !r.stepRested;
        r.restDue = false;
        r.stepRested = r.stepRested || s->kind == PlanStepKind::Rest;
        if (forced || NeedsRest(f, lines))
        {
            EnterPhase(r, Phase::Rest);
            r.restForEating = false;
            r.restForJoiners = false;
            r.restRepeatMs = 0;
            r.note = "resting";
            order = "rest";
            return order;
        }
    }
    // A bot that sat down to eat or drink after the fight is resting even
    // above the start lines (the tank at 82 % ate before Uldaman pull 17 and
    // the pull order found it seated): the run waits until nobody eats or
    // everyone is at the end lines. Looked at on every tick before the pull
    // goes out (Begin, Gather: bots sit down a tick after the fight ends),
    // kRestCapMs of waiting per fight.
    if ((r.phase == Phase::Begin || r.phase == Phase::Gather) && r.eatWaitMs < kRestCapMs)
    {
        if (f.eating > 0 && !Rested(f, lines) && !InEvent(r))
        {
            EnterPhase(r, Phase::Rest);
            r.restForJoiners = false;
            r.restForEating = true;
            r.restRepeatMs = 0;
            r.note = "resting: " + std::to_string(f.eating) + " eating or drinking";
            return order;
        }
    }

    // A follower a direct step left behind (OrderRefused): summoned before the
    // step goes out again. The module's direct check counts every bot, a held
    // one too (Run::hold); the wait below then sees it through HoldView, which
    // shows the bot being waited for. It stays held until it arrives, so a
    // refused or timed-out summon leaves it held where it was.
    if (!r.summonDue.empty())
    {
        order = "summon " + r.summonDue;
        r.summonWait = r.summonDue;
        r.summonWaitMs = 0;
        r.summonDue.clear();
        return order;
    }
    // ... then waits for that bot to stand at the lead bot (the summon is the
    // 3 s `.bot summon` flow: a step sent at once still finds it away), at most
    // kSummonWaitMs; then the step goes out again.
    if (!r.summonWait.empty())
    {
        const MemberFact* member = nullptr;
        for (const MemberFact& m : f.members)
            if (m.name == r.summonWait)
                member = &m;
        r.summonWaitMs += dt;
        if (member && member->yd > kArriveYd && r.summonWaitMs < kSummonWaitMs)
        {
            r.note = "direct step: waiting for " + r.summonWait + " (summoned)";
            return order;
        }
        bool const stillHeld = std::find(r.hold.begin(), r.hold.end(), r.summonWait) != r.hold.end();
        r.summonWait.clear();
        r.summonWaitMs = 0;
        EnterPhase(r, Phase::Begin);
        // A held bot that did not come (timed out, gone): it stays held; these
        // facts still show it, so the step goes out on the next tick, without it.
        if (stillHeld)
            return order;
    }
    // A refused direct step's straggler no summon is allowed for yet
    // (OrderRefused): the step goes out again after kDirectRetryMs, or as soon
    // as the glue's test finds it with the lead bot. No pause meanwhile: a
    // fight on the way runs the guard (CheckGuard) as on any other tick.
    if (!r.directWaitFor.empty())
    {
        const MemberFact* member = nullptr;
        for (const MemberFact& m : f.members)
            if (m.name == r.directWaitFor)
                member = &m;
        r.directWaitMs += dt;
        bool const arrived = member && f.directJoinRead && member->withLead && member->yd <= kGatherYd;
        if (!arrived && r.directWaitMs < kDirectRetryMs) // a held bot is not in these facts: the full wait
        {
            r.note = "direct step: waiting for " + r.directWaitFor + " to come to the lead bot";
            return order;
        }
        r.directWaitFor.clear();
        r.directWaitMs = 0;
        EnterPhase(r, Phase::Begin);
    }

    if (WalkBack(r, f, dt, order))
        return order;

    switch (s->kind)
    {
        case PlanStepKind::Move:
        case PlanStepKind::MoveDirect:
            MoveStep(r, f, *s, dt, order);
            break;
        case PlanStepKind::Pull:
            FightStep(r, f, *s, {}, dt, order);
            break;
        case PlanStepKind::Boss:
        {
            std::vector<std::uint32_t> killOrder = s->entries;
            killOrder.push_back(s->entry);
            FightStep(r, f, *s, killOrder, dt, order);
            break;
        }
        case PlanStepKind::Loot:
        case PlanStepKind::Open:
        case PlanStepKind::UseItem:
            TaskStep(r, f, *s, order);
            break;
        case PlanStepKind::WaitObject:
            if (r.skippedOpenStep != static_cast<std::size_t>(-1) && r.step == r.skippedOpenStep + 1)
            {
                NextStep(r, "wait for object " + std::to_string(s->entry) + " left out: the open before it was left out");
                break;
            }
            if (r.phase != Phase::Wait)
                EnterPhase(r, Phase::Wait);
            if (f.objectState == static_cast<int>(s->param))
                NextStep(r, "object " + std::to_string(s->entry) + " is in the state the plan waits for");
            else if (r.phaseMs >= s->timeoutS * 1000)
                Pause(r, "waited " + std::to_string(s->timeoutS) + " s for object " + std::to_string(s->entry) +
                    (f.objectState < 0 ? " (not found near its point)" : " (state " + std::to_string(f.objectState) + ")"));
            else
                r.note = "waiting for object " + std::to_string(s->entry);
            break;
        case PlanStepKind::WaitClear:
            if (!f.seen)
            {
                ApproachHop(r, f, *s, dt, order, "spot to watch");
                break;
            }
            if (r.phase != Phase::Wait)
                EnterPhase(r, Phase::Wait);
            if (f.hostilesNear == 0)
                NextStep(r, "no hostile left near the spot");
            else if (r.phaseMs >= s->timeoutS * 1000)
                Pause(r, "waited " + std::to_string(s->timeoutS) + " s; " + std::to_string(f.hostilesNear) +
                    " hostiles still near the spot");
            else
                r.note = "waiting for the spot to clear";
            break;
        case PlanStepKind::Rest:
            NextStep(r, "rested");
            break;
        case PlanStepKind::Ritual:
            RitualStep(r, f, *s, order);
            break;
        case PlanStepKind::Gossip:
            GossipStep(r, f, *s, dt, order);
            break;
        case PlanStepKind::UseOn:
            UseOnStep(r, f, *s, order);
            break;
        case PlanStepKind::Hold:
            HoldStep(r, f, *s, dt, order);
            break;
        default:
            Pause(r, std::string("step kind ") + ai::PlanStepKindName(s->kind) +
                " is not supported yet: do it by hand, then .bot run skip and .bot run resume");
            break;
    }
    return order;
}

// The run's phase as the status line and the dashboard show it: during an
// event's hold, how far the event is ("hold 1/4 down", "fight 2/4 down": units
// of the event dead of all seen), else the phase's name.
inline std::string PhaseText(const Run& r)
{
    const PlanStep* s = Current(r);
    if (s && s->kind == PlanStepKind::Hold && r.eventOn && r.eventTotal && r.state == State::Running)
        return std::string(r.fighting ? "fight " : "hold ") + std::to_string(r.eventDown) + "/" +
            std::to_string(r.eventTotal) + " down";
    return PhaseName(r.phase);
}

// The status line for the commander:
//   TBM:RUN|<plan>|<state>|<step>/<steps>|<leg>|<kind>|<phase>|<note>
// <step> counts from 1 (0 before the first, steps+1 is never sent: a done run
// shows the last). '|' and line breaks in the note become spaces.
inline std::string StatusLine(const Run& r)
{
    std::size_t const total = r.plan ? r.plan->steps.size() : 0;
    std::size_t const shown = total == 0 ? 0 : (r.step < total ? r.step + 1 : total);
    const PlanStep* s = r.plan && shown ? &r.plan->steps[shown - 1] : nullptr;
    std::string note = r.note;
    for (char& c : note)
        if (c == '|' || c == '\n' || c == '\r')
            c = ' ';
    std::string leg = s ? r.plan->LegName(s->leg) : std::string();
    if (leg.empty() && s)
        leg = std::to_string(s->leg);
    return "TBM:RUN|" + (r.plan ? r.plan->name : std::string("-")) + "|" + StateName(r.state) + "|" +
        std::to_string(shown) + "/" + std::to_string(total) + "|" + (leg.empty() ? "-" : leg) + "|" +
        (s ? ai::PlanStepKindName(s->kind) : "-") + "|" + PhaseText(r) + "|" + (note.empty() ? "-" : note);
}

// Which steps the run leader does today; the rest pause the run.
inline bool Supported(PlanStepKind kind)
{
    return kind != PlanStepKind::None;
}

} // namespace DungeonRun
} // namespace TortoiseBots

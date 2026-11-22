// BG1 (BATTLEGROUNDS-SPEC.md, rounds BG1 and decisions D4/D5/D6/D11): the queue, the join, the leave and the death of a bot in a
// battleground. World thread only, right after the bot manager's and the pool scheduler's tick (TbScripts.cpp).
//
// What it does, all through the core's own paths (the module never touches a queue structure):
//  - fills a human's queue: every 5 s while a non-bot session waits in a WSG/AB queue, free pool bots of that bracket and faction
//    are queued with WorldSession::JoinBattlegroundQueue up to the module's minimum per team plus one (HumanBgDemandPolicy.h);
//  - accepts a free bot's invitation after 2-8 s through HandleBattleFieldPortOpcode (declines it when the bot became ineligible),
//    once a human is inside or still invited to the instance (a short bound covers the accept-to-world-port-ack second);
//  - a bound bot accepts only after its human has entered the instance, and follows its human out of the queue when the human
//    cancels, logs out or unbinds it (the core leaves bound bots only on the human's leave inside a match);
//  - takes a module-queued bot out of the queue when it got a master or a run took it (RemovePlayer + RemoveBattlegroundQueueId,
//    as the core does);
//  - the core's own creation hook (WorldScript::CanCreateBattleground) holds a new instance back until both sides reach the fill
//    target, and refuses a bot-only one unless Bg.Autonomous; every pass re-schedules the core's queue update, because an accept
//    does not;
//  - a module-queued bot whose bucket lost its human leaves after a short grace, and no queue entry waits forever (BgQueuedDrop);
//  - D9's caps count every bot the module holds (queued, invited, inside), each match once, and never hold a human's own bucket
//    against him;
//  - a free bot leaves a finished match (STATUS_WAIT_LEAVE) after 10-30 s, and an unfinished one only at Bg.MaxMinutes, without the
//    Deserter debuff; a bound bot leaves with its human (the core), or after kBoundMasterGraceMs when that human never entered;
//  - a dead bot in a battleground releases at once, queues at the nearest area spirit healer and waits for the wave.
//
// The decisions are pure: ai/playerbot/policy/bg/BgQueuePolicy.h, BgJoinLeavePolicy.h, BgIds.h. This package owns the clock, the
// core calls, the events, the config and `.tb bg status|events`. The pool scheduler's side of D5 (it holds a bot's session while the
// bot is inside a match) reads the Player itself (TbPoolScheduler.cpp, marked `// BG`).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace TortoiseBots
{
namespace bg
{

// The `# BG` keys of forever/conf/tortoisebots.conf.dist. Called at boot and on `.reload config`.
void LoadConfig();
bool Enabled();

// One world tick (TbScripts.cpp, after BotMgr::Update and PoolScheduler::Update): a 1 s pass over the tracked bots and a 5 s fill
// pass while a human waits. True when the tick did anything (the tick cost goes into `.tb perf` with the rest).
bool Update(std::uint32_t diff);

// The PlayerScript hooks (TbScripts.cpp AddSC): OnBattlegroundQueueJoin / OnBattlegroundInvite / OnBattlegroundPortFailed, and
// nothing when Bg.Enable is 0.
void RegisterScripts();

// `.tb bg status`: the config line, the loaded maps, one line per instance holding bots (list id, status, world-state score, per
// side humans/bots), one per bot inside (team, alive, last move age) and one per bot waiting in a queue. `role` and `objective`
// are '-' until the WSG/AB rounds compute them.
std::vector<std::string> Status();

// `.tb bg events [since=<seq>]` (D11): the ring of the last 1024 events above `since`, oldest first, and the ring's bounds so a
// watchdog can ask for what is new and a reader can see that events were dropped.
struct EventPage
{
    std::vector<std::string> lines;
    std::uint32_t oldest = 0;   // the oldest seq still in the ring, 0 when empty
    std::uint32_t last = 0;     // the newest seq, 0 when empty
};
EventPage Events(std::uint32_t since);

} // namespace bg
} // namespace TortoiseBots

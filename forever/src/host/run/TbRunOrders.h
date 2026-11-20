// Slice 6, package O6 (orders): the executor that turns the run leader's order strings into the module's party orders and answers each one
// synchronously with a judged verdict (forever/docs/RUN-S6-O-NOTES.md; spec FOREVER-RUN-S5S6-SPEC.md 5.2).
//
// It is the ForeverCore counterpart of `IssueRunOrder` + `HandleCommanderOrder` (commands/Commander.cpp:7043, 6498): the Turtle run leader sends one
// string per tick (`move x y z [direct]`, `walk <bot> x y z`, `pull|pullback <handle>`, `attack <handle> <bot>`, `focus <handle>`, `rest`, `revive party`,
// `summon <bot>` ...) and reads the answer to see whether the module took it. `Issue` is that call: ONE path for every order the policy emits.
//
// The answer is TRUE: `accepted` only when the judge found the order carriable AND every order it queued passed the host's pre-flight (engine present,
// legacy action registered, queue not full, target still valid). An order the module cannot carry out is refused with a code and a reason, never "ok".
// The host runs a queued order on the bot's next engine tick; if it fails THERE (rare: the pre-flight tried to leave nothing but a race), the failure is
// handed back by `TakeLateFailures` on a later run tick and the caller feeds it to the policy's OrderRefused like any other refusal.
//
// Light on purpose (no policy header, no core header beyond Define/ObjectGuid): C6's tick includes it. Compiled with TORTOISEBOTS_SLICE1 only (the OFF
// build has no definitions: the callers are slice-only code).
//
// Threading: world thread only (the run tick in OnBeforeMapsUpdate and the `.tb run` commands), plain data, no lock. No Player, Unit or Map pointer is
// kept between calls: a run's order state is a list of (bot guid, order seq) pairs.
#pragma once

#include "Define.h"
#include "ObjectGuid.h"

#include <functional>
#include <string>
#include <vector>

namespace TortoiseBots
{
namespace run
{
struct RunRec;

// A party context for the lifted order judge: a run or a bound commander.
struct Party
{
    ObjectGuid leader;                                                                      // lead bot guid
    std::vector<ObjectGuid> members;                                                        // optional explicit members (if empty, read from leader->GetGroup())
    ObjectGuid owner;                                                                       // order/icon owner (leader for a run, commander for `.tb cmd`)
    bool fromRun = false;                                                                   // true for the run's own orders (gather, pull anchor on tank, late-failure tracking)
    bool persistentIcons = false;                                                           // true for `.tb cmd`: keep the skull raid icon on attack/pull targets
    std::function<bool(uint64 handle, std::string const& token, ObjectGuid& out)> resolveTarget;  // run handle or 32-hex guid -> ObjectGuid
};

// The synchronous answer of one order.
struct OrderAnswer
{
    bool accepted = false;      // true = the T "ACK": the order was judged carriable and queued on every bot it needs
    std::string code;           // refusal code when !accepted: invalid unsupported lead-unavailable not-bound no-engine no-bot no-tank too-far unreachable
                                // off-mesh no-ground no-los invalid-target dead-target friendly no-target not-dead failed not-allowed ... (empty when accepted)
    std::string text;           // the string the policy reads, in the Turtle shape: refused "<code>: <reason>" (so UnreachableAnswer, ReactTooFarAnswer,
                                // LeadNoProgressRefusal, DirectStragglerName and the "invalid-target" prefix of OrderRefused match), accepted "party <n>" / "bot:<name> 1"
    uint32 bots = 0;            // bots the order was queued on (0 when refused)
    bool pending = false;       // accepted, and its result on the bot is known only when the bot's engine ticks: TakeLateFailures reports a failure there
    // A plain `move` whose path was read: for DungeonRun::NoteMovePathType(run, order, movePathType) (T RunOrderCapture::movePathRead/Type).
    bool movePathRead = false;
    uint32 movePathType = 0;
};

// Lifted judge: carries out ONE order for `party` (either a dungeon run or a bound commander's `.tb cmd`), and logs it via `cmd::NoteRunOrder`.
OrderAnswer Issue(Party const& party, std::string const& order, uint32 nowMs, bool gather = false);

// Judges and carries out ONE order of the run `rec` (the leader is the tank bot; the party is the leader's group; every member is a host bot).
// `order` is exactly the string the policy returned (`DungeonRun::MoveOrder`, `WalkOrder`, "pull 12 body", "pullback 12", "attack 12 Mdps", "focus 12",
// "rest", "revive party", "summon Mdps", ...). `nowMs` is the run tick's clock (GameTime::GetGameTimeMS). `gather`: DungeonRun::IsGatherOrder(run, order):
// a move onto the lead bot's own spot is "hold here" within 3 yd (T s_runGather). Unit guids in the order are handles from ReadFacts: they are resolved
// with run::ResolveHandle and a handle that no longer resolves is refused invalid-target, never guessed. Unsupported verbs (escape rescue fetch return
// rejoin cc loot lootmethod open use ritual gossip, and `summon` by decision D3) are refused `unsupported`.
OrderAnswer Issue(RunRec& rec, std::string const& order, uint32 nowMs, bool gather = false);

// Orders accepted earlier whose queued work FAILED when the bot's engine ran it (or never ran within 20 s). Each is reported once, with the order text the
// policy sent and an answer text in the refusal shape ("failed: ..."): the caller passes both to DungeonRun::OrderRefused(run, order, text). Call once
// per run tick, before Issue.
struct LateFailure
{
    std::string order;
    std::string text;
};
void TakeLateFailures(RunRec& rec, uint32 nowMs, std::vector<LateFailure>& out);

// Drops a run's order state (the pending list). C6 calls it when the run is removed, next to run::ForgetRun.
void ForgetRunOrders(ObjectGuid leader);

// Whether `leader` may lead a run now, as far as ORDERS are concerned: a host bot with a running engine, in a group it leads, with the tank role set
// (`.tb role <leader> tank`, PlayerbotAI::IsTank): the facts' leadIsTank reads it and the pull orders need it. Every other member must be a host bot with
// a running engine. False with a code and a reason for `.tb run start` to refuse truthfully: not-bot no-engine not-leader no-tank. It does NOT look at the
// commander binding (C6 binds the members after this check): `Issue` refuses `not-bound` itself for a follower that is not bound to the leader.
bool StartCheck(ObjectGuid leader, std::string& code, std::string& message);
}
}

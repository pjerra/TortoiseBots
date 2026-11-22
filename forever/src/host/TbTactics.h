// Tactical orders, M1: stop, flee, guard, stance, engage and rebuff. ONE executor for both surfaces (`.tb order` queues the order
// through h3, `.tb cmd` does the same): the command validates and queues, and the bot's next engine tick runs the verb through the existing
// legacy action or strategy. See forever/docs/TB-COMMANDS.md section 23 and forever/docs/TACT-M1-NOTES.md.
//
// Compiled with and without TORTOISEBOTS_SLICE1 (the default build answers `engine-not-built`, as TbCc.cpp does).
// World thread only: the order queue and the toggle table are plain data (TbH3.h).
#pragma once

#include "Define.h"
#include "ObjectGuid.h"
#include "host/TbOrdersApi.h"

#include <string>
#include <vector>

class Player;
class PlayerbotAI;
class Unit;

namespace TortoiseBots
{
namespace h3
{
class Rec;
class Reply;
}
namespace tactics
{
// TortoiseBots.Orders.Tactical (default 1), read on each use: at 0 no tactical verb runs.
bool Enabled();

// The per-bot pre-flight of one queued order: engine present, the legacy action/strategy/value the verb needs, and the verb's own gate (a live
// bot for `stop`, out of combat for `rebuff`, the argument parsers). False with a refusal code and a reason in `code`/`detail`.
bool Preflight(Player* bot, h3::OrderSpec const& spec, std::string& code, std::string& detail);

// Runs one queued order on the bot's tick. Returns ok, fail:<code> or (interrupt only) pending:interrupt. `owner` is the commander when in
// the world, else the bot (the caller resolves it, as h3::Run does). `seq` is the order's number: a pending interrupt reports its end under it.
std::string Run(Player* bot, PlayerbotAI* ai, std::string const& verb, std::string const& arg, ObjectGuid unit, Player* owner, uint32 seq = 0);

// A pending interrupt order of `bot` tries again (the engine's next tick, before its own AI). True when one was pending: `seq` is its order
// and `result` the order's result now (pending:interrupt while it still tries, else ok or fail:<code>, see kInterruptWindowMs).
bool PollPendingInterrupt(Player* bot, PlayerbotAI* ai, uint32& seq, std::string& result);

// Any other order of the bot ends its pending interrupt (the commander changed its mind).
void CancelPendingInterrupt(Player* bot);

// The interrupt executor among `bots` (already the party order, or just the named bot): the first with a castable interrupt action.
// `action` is the action name the queued order runs; when none is found `reasons` is one `name:reason` per probed bot (dead, other-map,
// far, no-spell, no-engine). The probe is TB FindInterruptAction (BotCommandContext.cpp:556-586): registered action, known spell, the
// target's spell is interruptible by it, and CanCastSpell with range ignored (the queued action walks). A bot in range of its action
// wins over an earlier bot that would have to walk (PickInterrupt's first pass).
Player* PickInterruptBot(std::vector<Player*> const& bots, Unit* target, std::string& action, std::string& reasons);

// M3: reads one toggle's current state from the live strategy (aoe/autocc/sap/loot). False when the bot has no engine. The
// table's wanted state is not consulted: this is what the strategy reads now, so `status` and a flip's current value start here.
bool ReadToggle(Player* bot, std::string const& verb, bool& on);
// True (with code/detail) when a `status` read of this toggle is meaningless for the bot: `no-rogue` (sap on a non-rogue) or
// `no-engine`. The `.tb order` and `.tb cmd` status paths refuse such a bot and keep it out of the aggregate.
bool ToggleStatusFault(Player* bot, std::string const& verb, std::string& code, std::string& detail);

// M3: the toggle enforcement the engine calls before its own tick (h3::BeforeEngineTick): the host table's wanted state wins over
// a `co`/`nc` change of the same strategy. Idempotent; runs every tick so every ResetStrategies is covered. `sap` only on rogues;
// `loot` only once the table has an entry (the kit default is left alone otherwise).
void BeforeTick(Player* bot, PlayerbotAI* ai);

// Appends the tactical keys to a `.tb state` record: stance= and rebuff= (seconds left of the force-rebuff window).
void AppendState(h3::Rec& rec, Player* bot);

// `.tb grammar selftest`: one example of the records and keys this package adds.
void SelfTestRecords(h3::Reply& reply);
}
}

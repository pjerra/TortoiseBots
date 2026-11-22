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

// Runs one queued order on the bot's tick. Returns ok or fail:<code>. `owner` is the commander when in the world, else the bot (the caller
// resolves it, as h3::Run does).
std::string Run(Player* bot, PlayerbotAI* ai, std::string const& verb, std::string const& arg, ObjectGuid unit, Player* owner);

// The interrupt executor among `bots` (already the party order, or just the named bot): the first with a castable interrupt action.
// `action` is the action name the queued order runs; when none is found `reasons` is one `name:reason` per probed bot (dead, other-map,
// far, no-spell, no-engine). The probe is TB FindInterruptAction (BotCommandContext.cpp:556-586): registered action, known spell, the
// target's spell is interruptible by it, and CanCastSpell with range ignored (the queued action walks).
Player* PickInterruptBot(std::vector<Player*> const& bots, Unit* target, std::string& action, std::string& reasons);

// Appends the tactical keys to a `.tb state` record: stance= and rebuff= (seconds left of the force-rebuff window).
void AppendState(h3::Rec& rec, Player* bot);

// `.tb grammar selftest`: one example of the records and keys this package adds.
void SelfTestRecords(h3::Reply& reply);
}
}

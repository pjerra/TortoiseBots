// S3 (throws and falls): the host side of a knockback or another core-started flight. The watcher sees every bot's
// movement generator; while the core is flying a bot it keeps the engine's IsJumping out of the way (no StopMoving, no
// cast), and when the flight ends it applies the core's own fall damage from the highest point. `.tb knock` and
// `.tb speed` set the two things up by hand; `.tb state` reports thrown=, falldmg= and runrate=.
//
// Threading: world thread only (WorldScript::OnBeforeMapsUpdate, console commands).
#pragma once

#include "Define.h"
#include "ObjectGuid.h"

#include <string>

class ChatHandler;
class Player;

namespace TortoiseBots
{
class BotMgr;
namespace engine { struct Brain; }
namespace h3 { class Rec; }
namespace kb
{
// One world tick for one bot: observe its flight, tell the engine, land once with fall damage.
void Update(Player* bot, engine::Brain* brain);

// The bot left the world: drop its record (BotMgr::OnPlayerLogout).
void Forget(ObjectGuid guid);

// `.tb state`: thrown=<completed and started flights>, falldmg=<last fall's damage>, runrate=<current run speed rate>.
void AppendThrownState(h3::Rec& rec, Player* bot);

// `.tb knock <bot> <speedXY> <speedZ>`: the core's KnockbackFrom with an origin one yard in front of the bot (it flies
// backwards, away from it). True when the flight was started.
bool HandleKnock(ChatHandler* handler, std::string name, float speedXY, float speedZ);

// `.tb speed <bot> run|walk|swim <rate>`: Unit::SetSpeedRate. True when it was applied.
bool HandleSpeed(ChatHandler* handler, std::string name, std::string kind, float rate);
}
}

#pragma once

// Pool bot spawn (owner's design, Oct 2026): a fresh pool bot rolls its start level on its first login
// (AiPlayerbot.RandomBotStartLevelMin / .Max) and, at level 5 or above, receives a one-time gear kit. Both happen
// once: the spawn row in `tortoise_bots_player_flag` (`flag = 'pool-spawn'`) and an in-process mark keep a relog from
// repeating it, and a bot that spawns below level 5 keeps the starter outfit for good.
//
// The pure rule is runtime/PoolSpawnPolicy.h and runtime/GearSeedMixPolicy.h; this is the live half. World thread only.

#include "ObjectGuid.h"

class Player;

namespace TortoiseBots
{
namespace poolspawn
{
// TortoiseBots.Pool.GearOnCreate, read on a config load (the start-level keys come from PlayerbotAIConfig).
void LoadConfig();

// Creates or probes `tortoise_bots_player_flag` once (guarded), so a spawn attempt can persist its row.
void LoadAtStartup();

// Called on a bot's login before the level-bound spells, talents and professions are synced: a fresh pool bot gets
// its random start level once. Gives a level only, never takes one away.
void ApplyStartLevel(Player* bot);

// Called on the same login after those syncs: the one-time kit for a fresh pool bot of level 5+, and the row that
// says the spawn happened. A bot below 5 keeps the starter outfit and is still marked spawned.
void SeedSpawnKit(Player* bot);

// The character left the world: drop this login's answer (a guid can be reused by a later character).
void Forget(ObjectGuid guid);
} // namespace poolspawn
} // namespace TortoiseBots

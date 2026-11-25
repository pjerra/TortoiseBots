# Shared five-man boss tactics (#138)

Scope: classic five-man encounters. Raid tactics, including Onyxia, Molten
Core, Naxxramas and custom Turtle raids, belong to #162 and remain excluded.
The old `Dungeon*` raid registrations must not be enabled wholesale.

The first step compiles the existing `BossStandMgr` loader with
`WorldQueryCompat`, which refuses a missing table/column before ForeverCore
can abort on a schema error. The table is read once and sealed. The new
Forever migration creates the stand table and adds RFC rules supported by
`forever/data/waterys/rfc/rfc-bosses.csv` and `rfc-mobs.csv`: Oggleflint's
cleave/adds, Jergosh's Immolate/adds and Bazzalan's adds. Molten-pack trash
handling is #23, not this job. Taragaman needs a surveyed rock/knockback stand;
no unverified position or interrupt rule is invented for him.

An explicit `tank` stand role keeps the boss victim's point separate from melee
DPS seats. Existing `melee` role parsing and `StandRoleOf` semantics stay intact.
Oggleflint's initial tank point is the shipped navmesh-exported RFC boss-step
position. Native path/floor checks and a coordinated live check are still
required; matching an exported position is not evidence of successful combat.

The activation gate remains `AiPlayerbot.DungeonTactics` (on by default:
`PlayerbotAIConfig.cpp` reads it with default true, and `aiplayerbot.conf.dist` sets 1), or
membership in a commanded group, and a supported map with rows. Role state is
in memory: reapply/check tank/healer roles after a server restart.

Provenance: inherited TortoiseBots behavior in `BossTacticsActions.cpp`,
`BossTacticsTriggers.cpp`, `BossTacticsStrategy.cpp`, and `BossStandMgr.cpp`,
as preserved in base a7abd1d99; Forever adaptation targets core
b441bf74e048934d4289e64cef5e49cd02b0b8c5. Creature/spell facts come from the
committed Forever exports, not unverified Turtle migration rows.

## Wailing Caverns (map 43)

Goal (hypeer, 22 Nov): a player with four bots clears Ragefire Chasm and Wailing Caverns. Their WC run 8
died to: the tank dying first, respawned mobs on the walk back after a wipe, and Deviate Shambler (5761) packs.

`forever/sql/world/2026_11_22_30_tortoisebots_boss_tactics_wc.sql` adds only what the data backs:

- Interrupts for every WC cast that has a cast time, from `forever/data/waterys/wc/wc-casts.csv` (SmartAI
  casts in the world DB at core b441bf74; cast times from DB2 70959): Druid of the Fang (3840, 70 spawns)
  Druid's Slumber 8040, Healing Touch 5187, Lightning Bolt 9532; Deviate Shambler Wild Regeneration 7948;
  Cobrahn 8040/9532; Pythas and Anacondra Sleep 700, Healing Touch 5187, Lightning Bolt 9532; Serpentis
  Sleep 700, Healing Touch 6778, Lightning Bolt 9532; Skum Chained Bolt 6254; Verdan Grasping Vines 8142.
  The sleeps and Druid's Slumber are what put the tank out of the fight; the heals are what make packs last.
  At runtime a bot interrupts only when the core says the cast can be interrupted and it knows an interrupt
  (shield bash, pummel, kick, counterspell, earth shock, silence, spell lock).
- Kill first: an engaged Druid of the Fang before the rest of its pack (damage dealers only; tanks and
  healers keep their jobs; an explicit attack order wins).
- No stand point and no frontal-cone row: none is verified for WC yet. Instant casts (Thrash, Thunderclap,
  Terrify) cannot be interrupted and are not listed.

Not boss tactics, so not here: mobs respawning on the walk back after a wipe (respawn timers, a run/plan
matter) and the size of Deviate Shambler packs (pulling, #23-style pack handling).

The activation gate is unchanged: `AiPlayerbot.DungeonTactics` (default 1), or a commanded group, on a supported
five-man map with rows. So a player's own bots in WC use these rows without any setting. Each rule that fires logs `FiveMan tactic bot=<name> map=43 rule=<interrupt|kill-first> target=<entry>`.

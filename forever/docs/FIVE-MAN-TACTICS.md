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

The activation gate remains `AiPlayerbot.DungeonTactics` (off by default), or
membership in a commanded group, and a supported map with rows. Role state is
in memory: reapply/check tank/healer roles after a server restart.

Provenance: inherited TortoiseBots behavior in `BossTacticsActions.cpp`,
`BossTacticsTriggers.cpp`, `BossTacticsStrategy.cpp`, and `BossStandMgr.cpp`,
as preserved in base a7abd1d99; Forever adaptation targets core
b441bf74e048934d4289e64cef5e49cd02b0b8c5. Creature/spell facts come from the
committed Forever exports, not unverified Turtle migration rows.

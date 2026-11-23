# Riding: mount state, mount list, break-even (#152)

Scope: make a bot that owns a mount ride it on a long walk, and dismount near
its target, in combat or indoors. Buying the mount and training riding is the
separate buy package; this note covers the ride decision.

## ForeverCore mount model (verified)

- Riding is skill 762 (`src/server/game/Miscellaneous/SharedDefines.h:5966`).
  The core uses it to choose a retail mount capability
  (`src/server/game/Entities/Unit/Unit.cpp:8892`) and refreshes that capability
  when the skill rises (`src/server/game/Entities/Player/Player.cpp:6400`).
- A classic mount's speed is the amount of its
  `SPELL_AURA_MOD_INCREASE_MOUNTED_SPEED` aura, read in
  `Unit::UpdateSpeed` (`src/server/game/Entities/Unit/Unit.cpp:9312`). The core
  does not scale a mounted aura with riding skill; Turtle does, and that is why
  the legacy `MountValues` rider ladder (0 -> level/2, 75 -> 60, 150 -> 100) is
  wrong here. `TbCompat_RIDE.h` makes every Forever mount keep its own aura
  amount.
- Indoor dismount is a core rule: `Player::UpdateIndoorsOutdoorsAuras`
  (`src/server/game/Entities/Player/Player.cpp:7090`) removes mount auras when
  `Player::IsMountRestrictedHere` (`:7095-7107`) says so, and the rule is on by
  default (`CONFIG_CLASSIC_MOUNT_INDOORS_RULES`, `World.cpp:728`).
- Riding trainer rows exist but are retail tiers: Apprentice Riding 33388 at
  level 40 (1,000,000 copper) and Journeyman Riding 33391 at level 60
  (10,000,000 copper, requires 33388), all under TrainerId 1003xxx/1004xxx/
  1007xxx (`world.trainer_spell`). `Trainer.cpp:300-311` deliberately skips
  learning riding through the trainer: the API call sets 75 at 40 and 150 at 60.
  The buy package owns that path.
- Racial mount items are present in the client item data
  (`hotfixes.item_sparse`): normal mounts require level 40 and cost
  2,000,000 copper, epic mounts require level 60 and cost 20,000,000 copper.
  Their `RequiredSkill`/`RequiredSkillRank` are 0/1, so the item data itself
  carries no riding gate. Turtle-only items (Swift Riding Turtle 23720,
  Goblin 80460-80462/81190-81191) are absent.
- `collection_mount` and `sMountMgr` are Turtle-only; ForeverCore's
  `sTaxiPathNodesByPath`-style core stores have no collection mount table. The
  compat pair returns "no collection mount" on Forever.

## What was ported

- `ai/playerbot/strategy/actions/CheckMountStateAction.cpp` and
  `ai/playerbot/strategy/values/MountValues.cpp` are on the slice list
  (`forever/SLICE-W4.txt`). The pure policies
  `playerbot/MountDismountPolicy.h`, `policy/travel/MountShopPolicy.h` and the
  new `policy/travel/MountBreakEvenPolicy.h` need no port.
- Registered keys (slice context): strategy `mount`, action and trigger
  `check mount state`, values `mount list`, `current mount speed`,
  `max mount speed`, `travel target traveling`, `travel target working`.
- Every non-combat kit gets the `mount` strategy
  (`forever/src/slice/SliceAiFactory.cpp`), which
  `CheckMountStateAction::isUseful` requires before an unmounted bot may mount.
- The action still mounts for: a mounted group master, a master further than
  `sightDistance`, a guard/stay spot further than 40 yd, a target it can reach
  (40/45/50 yd by class), and a travel trip longer than the break-even
  distance. It dismounts: near the master, at guard/stay positions, near a
  gathering/travel/RPG target, in combat with an engaged target, indoors and on
  boats/taxis/teleports.
- ForeverCore has no `BattleGroundWS` class, so the compat gate refuses the
  battleground mounting branch (LESS, not more). The classic WS flag-stand rule
  stays in the Turtle header.

## Break-even

`MountBreakEvenDistance(runSpeed, castSec, mountSpeedPercent, factor)` =
`runSpeed * castSec / (mountSpeedPercent / 100) * factor`. Run speed is 7 yd/s,
the factor is `AiPlayerbot.MountBreakEvenFactor` (default 1.5), and the mount's
cast time comes from the spell mirror. Factor 0 disables the threshold; an
instant form has cast time 0 and mounts for any trip. The action picks the
fastest usable mount first, so a 40 % travel form and a 100 % epic mount each
use their own cast time.

## Left for later packages

- Buying the item and training riding: the purchase values
  (`mount skilltype`, `can train mount`, `can buy mount`,
  `available mount vendors`, `full mount list`) are compiled but not registered.
- `travel target traveling` is registered, but the travel chooser
  (`ChooseTravelTargetAction.cpp`) is not in the slice yet, so on a masterless
  bot the travel branch stays dormant. Mounting on a long walk works through
  the mounted-master and far-from-master rules; the travel target path comes
  with the travel package.
- World PvP: `EnemyPlayerValue.cpp` is not in the slice, so `enemy player
  target`/`has enemy player targets` read the inert empty values. The combat
  dismount still sees the PvE `dps target`, explicit attack and pull orders.

## Taxi path holes (TB PR #1682)

TBF needs the behaviour. ForeverCore stores `sTaxiPathNodesByPath` as a vector
of pointers resized to the table's path length and filled by node index
(`src/server/game/DataStores/DB2Stores.cpp:1636-1642`); a route that skips a
point number leaves a null slot, and the old accessors dereferenced it. The
compat accessors now expose `TaxiPathNodePresentCompat`, and `TravelNode.cpp`
skips absent slots in `generateTaxiPaths` and in both transport-path loops,
which is the PR's fix applied to this core's pointer list. The Turtle side's
node list holds values, so the helper only bounds-checks there.

See `forever/docs/W4-A2-NOTES.md` for the travel node store itself.

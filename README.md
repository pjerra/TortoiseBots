<img width="2752" height="1536" alt="TortoiseBots" src="https://github.com/user-attachments/assets/08e9a429-5679-4579-ac64-57e7e3c7d2c4" />

# TortoiseBotsForever

The port of [TortoiseBots](https://github.com/Sagiroth/TortoiseBots) to **ForeverCore**
([`Karrz1/TrinityCore`](https://github.com/Karrz1/TrinityCore), branch `forever`). TortoiseBots is a native bot module
for the Tortoise WoW 1.18.1 core; this repository makes the same bot brain run on ForeverCore. It is made for
educational purposes.

> [!WARNING]
> **Work in progress, private repository.** Much of TortoiseBots does not work on ForeverCore yet (see
> [What works today](#what-works-today)). Do not mention this repository in public places (TortoiseBots PRs, docs,
> Discord).

The core is not changed: the module uses only the core's public game API and script hooks. Bots are ordinary characters
that the module logs in as headless sessions, with no game client.

## What works today

Bots are driven by GM commands, `.tb ...` in chat (GM account) or `tb ...` on the worldserver console. The full list
with the reply format is in [`forever/docs/TB-COMMANDS.md`](forever/docs/TB-COMMANDS.md).

- Creating bot characters, logging them in and out, putting them in a party with a player.
- The real TortoiseBots engine on each bot: following and fighting. Warrior and priest are ported; the other classes
  are not yet.
- Party orders: attack, pull, follow, stay, come, free, focus, roles (tank, healer, DPS), a commander for a party.
- Dungeons: entering an instance, death and revive, and a full Ragefire Chasm run led by a tank bot from a dungeon plan.
- Doors, chests, looting and bags.
- The observability dashboard (`tools/observability`, started with `run-dashboard-forever.sh`): live map, bot
  roster, character sheets.

Not there yet:

- The `.bot` player commands and the addon protocol. The in-game panel,
  [TortoiseBotsForeverManager](https://github.com/TortoiseBots/TortoiseBotsForeverManager), works through `.tb` until
  then.
- Pool bots and the living world (roaming, questing, levelling on their own), hired bots, the auction house, guild
  orders, battlegrounds and most of the rest of the TortoiseBots feature list.
- The open list of engine parts that still answer "not ported yet":
  [`forever/docs/UNPORTED.md`](forever/docs/UNPORTED.md).

The guides under [`docs/`](docs/README.md) describe TortoiseBots on Tortoise WoW 1.18.1. They show what the port is
heading for, not what works on ForeverCore today.

## Build

The module builds inside a ForeverCore checkout at the commit pinned in [`forever/PINNED_CORE`](forever/PINNED_CORE).

```bash
# inside your Karrz1/TrinityCore checkout (branch forever, at the pinned commit)
git clone git@github.com:TortoiseBots/TortoiseBotsForever.git src/modules/TortoiseBots

cmake -S . -B build -DBUILD_TORTOISEBOTS=1 -DTORTOISEBOTS_SLICE1=ON
cmake --build build -j"$(nproc)"
cmake --install build
```

- `BUILD_TORTOISEBOTS=1` turns the module on. `TORTOISEBOTS_SLICE1=ON` builds the ported bot engine; without it the
  bots log in but do not think.
- Install copies `tortoisebots.conf.dist` into `worldserver.conf.d/`. The engine's own settings file,
  `aiplayerbot.conf`, must sit next to `worldserver.conf` (copy it from `forever/conf/aiplayerbot.conf.dist`); the
  module says so at startup when it is missing.
- World database rows (dungeon plans and bosses) are in [`forever/sql/world`](forever/sql/world). The core's updater
  applies them once their folder is listed in `updates_include`; [`forever/sql/README.md`](forever/sql/README.md) has the
  steps.

## For developers

- [`forever/docs/PORT-PLAN.md`](forever/docs/PORT-PLAN.md): how the port works (one repository, two cores, vertical
  slices) and its rules.
- [`forever/docs/CENSUS.md`](forever/docs/CENSUS.md): how much of the bot brain compiles on ForeverCore.
- The Tortoise WoW 1.18.1 build (`TortoiseBots.cmake`) must stay green: a change that breaks it is not finished.

## Credit and licence

TortoiseBots is made by **Sagiroth**. The in-game attribution line (`by Sagiroth`, source URL, licence) stays visible in
every build (see [`NOTICE.md`](NOTICE.md#attribution-author-notice)).

Licensed under **AGPL-3.0-or-later** ([`LICENSE`](LICENSE)). Donor code keeps its own notices; see
[`NOTICE.md`](NOTICE.md).

This is an unofficial, non-commercial community project made for educational purposes. It is not affiliated with
Blizzard Entertainment, Turtle WoW or the WoW Forever project.

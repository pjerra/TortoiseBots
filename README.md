<img width="2752" height="1536" alt="TortoiseBots" src="https://github.com/user-attachments/assets/08e9a429-5679-4579-ac64-57e7e3c7d2c4" />

[![Support on Ko-fi](https://img.shields.io/badge/Ko--fi-support-FF5E5B?logo=ko-fi&logoColor=white)](https://ko-fi.com/sagiroth)
[![Discord](https://img.shields.io/badge/Discord-join-5865F2?logo=discord&logoColor=white)](https://discord.gg/FT2rZcXm3)

# TortoiseBots for WoW Forever

The **ForeverCore port** of [TortoiseBots](https://github.com/Sagiroth/TortoiseBots), the native PlayerBots module for
Tortoise WoW 1.18.1. This repository runs the same bot brain on **ForeverCore**
([`Karrz1/TrinityCore`](https://github.com/Karrz1/TrinityCore), branch `forever`), the server for the WoW Forever
(Classic 1.60.1) client. This is an unofficial, community-driven project made for educational purposes, not affiliated
with Blizzard Entertainment, Turtle WoW or the WoW Forever project.

The module plugs into the core's module system (`src/modules/`) and does not change the core: it uses only the core's
public game API and script hooks. Bots are ordinary characters that the module logs in as headless sessions, with no
game client. The core builds and runs the same without the module.

> [!WARNING]
> **Work in progress, private repository.** Not everything from the Tortoise WoW build is on WoW Forever yet; see
> [Known limitations](docs/play/known-limitations.md). Do not mention this repository in public places (TortoiseBots
> PRs, docs, Discord).

> [!TIP]
> **New here? Start with [Getting Started](docs/play/getting-started.md).**
> It covers client setup, the [TortoiseBotsForeverManager](https://github.com/TortoiseBots/TortoiseBotsForeverManager)
> (`/tbfm`) addon, and bringing your own characters into your party. No GameMaster account is needed: players use
> `.bot` or the addon.
> Then go through the [feature table](docs/play/features.md) and turn on what you want. Each row says whether it's on by
> default and which flag enables it.

### 🐢 Vision

The same as TortoiseBots: the server should feel like a living, populated server for a player who plays alone. The
player levels and gears up at their own pace, and bots fill the world and their group the way real players would.

- **Organic living world:** Pool bots quest, grind and travel by foot, flight path and boat.
- **Earned progression:** Equipment and gold come from playing the game, not from handouts.
- **Player control first:** Explicit player orders always override automation.
- **Modern AI standard:** Gameplay and class AI adapt modern mod-playerbots behavior.

### ✨ Highlights

- [Your Own Characters](docs/play/features.md#your-own-characters): Log your own characters in as bots, summon them and
  take them into your party.
- [Party Controls](docs/play/features.md#party-controls): Follow, stay, attack, pull and pullback, focus, roles, and the
  addon's action buttons.
- [Combat & Classes](docs/play/features.md#combat--classes): Combat rotations for all nine classes, spec-based roles
  (tank, healer, DPS) and talent builds.
- [Dungeons & Raids](docs/play/features.md#dungeons--raids): Instance runs, pulls, death and revive, and wipe recovery.
- [The Living World](docs/play/features.md#the-living-world): A pool of free bots that log in on a schedule, quest,
  grind, level and travel on their own.
- [Guild Orders](docs/play/features.md#guild-orders): Bots in your guild farm or craft items for the guild.
- [Economy](docs/play/features.md#economy-auction-house): Free bots sell their own loot on the Auction House.
- [Dungeon Fill & Battlegrounds](docs/play/features.md#dungeon-fill--battlegrounds): `.bot lfg join` fills missing roles
  with free bots; battleground fill for Warsong Gulch and Arathi Basin.
- [Server & Monitoring](docs/play/features.md#server--monitoring): Live browser map, bot roster and character sheets.

Not on WoW Forever yet: hired companions, alt-bots, boss and raid tactics, Alterac Valley and arenas, and the Turtle WoW
custom content. See [Known limitations](docs/play/known-limitations.md).

## 📚 Documentation

| Guide / Reference | Description |
| :--- | :--- |
| **[Getting Started](docs/play/getting-started.md)** | **Start here:** client setup, `/tbfm` addon, your own characters as bots, first party |
| [What Bots Can Do](docs/play/features.md) | Feature catalog, defaults, and configuration flags |
| [Known Limitations](docs/play/known-limitations.md) | What is not on WoW Forever yet, and timers you will notice |
| [Documentation Start Page](docs/README.md) | Central switchboard, quick task finder, and audience directory |
| [Player Commands](docs/play/commands/README.md) | `.bot` command reference and the addon |
| [Class AI & Rotations](docs/classes/overview.md) | Rotations, specs and roles for all classes |
| [Dungeons & Raids](docs/play/dungeons-and-raids.md) | Pulls, crowd control and wipe recovery |
| [Living World](docs/world/living-world.md) | Roaming, questing, vendor trips and gear |
| [Configuration & Tuning](docs/server/configuration.md) | Settings reference and configuration files |
| [Admin Commands](docs/server/admin-commands.md) | `.tb` commands for GameMasters and the console |
| [Observability Dashboard](docs/server/dashboard.md) | Web dashboard and live map |
| [Architecture](docs/developers/architecture.md) | Module rules, headless sessions and host seams |

## 🛠️ Quick Build

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
- Install copies `tortoisebots.conf.dist` into `worldserver.conf.d/`. The module ships with
  `TortoiseBots.Enable = 0`: set it to `1` in your `tortoisebots.conf` to start the bots.
- The engine's own settings file, `aiplayerbot.conf`, must sit next to `worldserver.conf` (copy it from
  `forever/conf/aiplayerbot.conf.dist`); the module says so at startup when it is missing.
- World database rows (dungeon plans and bosses) are in [`forever/sql/world`](forever/sql/world). The core's updater
  applies them once their folder is listed in `updates_include`; [`forever/sql/README.md`](forever/sql/README.md) has the
  steps.

See [Install](docs/server/install.md) and [Configuration & Tuning](docs/server/configuration.md) for the rest.

### For developers

- [`forever/docs/PORT-PLAN.md`](forever/docs/PORT-PLAN.md): how the port works (one repository, two cores, vertical
  slices) and its rules.
- [`forever/docs/CENSUS.md`](forever/docs/CENSUS.md): how much of the bot brain compiles on ForeverCore.
- [`forever/docs/UNPORTED.md`](forever/docs/UNPORTED.md): engine parts that still answer "not ported yet".
- The Tortoise WoW 1.18.1 build (`TortoiseBots.cmake`) must stay green: a change that breaks it is not finished.

## 🙏 Credit

TortoiseBots is made by **Sagiroth**. The in-game attribution line (`by Sagiroth`, source URL, licence) stays visible in
every build (see [`NOTICE.md`](NOTICE.md#attribution-author-notice)).

### ☕ Support

If you enjoy TortoiseBots and would like to support its development, you can buy me a coffee on
[**Ko-fi**](https://ko-fi.com/sagiroth). It's completely optional: every feature stays free and open for everyone.

## 📜 License

Licensed under **AGPL-3.0-or-later** ([`LICENSE`](LICENSE)). Donor code keeps its own notices; see
[`NOTICE.md`](NOTICE.md).

**Disclaimer:**

- TortoiseBots is a non-commercial hobby project. Donations (Ko-fi) only support development. They buy no access,
  features or service, and are not tied to any server.
- The author is not affiliated with any server that uses this code, and is not responsible for how third parties use it.
- Not affiliated with Blizzard Entertainment, Turtle WoW or the WoW Forever project. World of Warcraft is a trademark of
  Blizzard Entertainment.

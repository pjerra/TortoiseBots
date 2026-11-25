---
id: developers-host-api
title: Host Seams & Module Contract
category: developers
summary: "How the Forever module attaches to ForeverCore: bot sessions, lifecycle, script hooks, command entry points, host services, and data mirrors."
tags: [host-api, seams, c++, sessions, packets, contract]
relates_to:
  - developers-architecture
  - developers-strategy-engine
---

# Host seams and module contract

**Target:** ForeverCore (TrinityCore-based, branch `forever-bots`) at the commit pinned in `forever/PINNED_CORE`.

**Purpose:** describe the generic core/module boundary the Forever build actually uses.

The core exposes generic capabilities. TortoiseBots assigns bot meaning to those capabilities.

```text
ForeverCore
    -> sessions, script hooks, public Player/Unit/Group APIs, DB2 data
TortoiseBots
    -> bot records, sessions, AI, commands, host services, gameplay behaviour
```

Normal gameplay systems do not need bot state. Do not reintroduce `WorldSession::GetBot()`, `WorldSession::SetBot()`, `Player::m_bot`, `sPlayerBotMgr`, `PlayerBotEntry`, or scattered module singletons into the core.

## 1. Build contract

| Option | Meaning |
| :--- | :--- |
| `BUILD_TORTOISEBOTS` | Builds the module library. Off by default; the core then builds and runs with no bot code. |
| `TORTOISEBOTS_SLICE1` | Also builds the legacy bot engine: the `ai/playerbot` files listed in `forever/SLICE-W4.txt`, the replaced factory and context sources, the shared run/why-trace runtime units, and the engine-facing host services (`forever/src/host/TbHost*.cpp`). |

The Turtle build path (`TortoiseBots.cmake`, `src/`, `host/`, `runtime/`) is not compiled into the Forever module. The module also refuses to configure beside the core's own PlayerbotV2 module: two bot systems would register two `.bot` command sets and two session drivers.

Sources are compiled from `forever/src` (recursively) plus the slice list. `forever/src/compat` holds the translations between the legacy bot code and ForeverCore; it is not an alternative API for new code.

## 2. Bot sessions

A bot is a `TortoiseBots::BotSession`, a `WorldSession` subclass created with the core's `isBot` flag. It uses only public `WorldSession` members.

| Aspect | Behaviour |
| :--- | :--- |
| Login | `BeginLogin(characterGuid)` builds the core's `LoginQueryHolder` and enters the world through the normal `HandlePlayerLogin` path, on the world thread. |
| Transport | No socket. The core skips socket teardown and the account-level `online` write, and clears only the bot character's online flag. |
| Inbound packets | Dropped, except `SMSG_SPELL_FAILURE`, `SMSG_CAST_FAILED` and `SMSG_SPELL_DELAYED` for the bot's own cast, which are copied into a bounded cast-result queue for the engine. |
| Cast-result queue | Thread-safe; any sending thread may push. The engine drains it on the world thread at the start of the bot's turn. A refusal the engine caused synchronously is counted instead of queued. |
| Teleports | The session acknowledges pending teleports and calls the host's teleport hook so the engine sees the landing. |
| Logout | `RequestLogout(reason)`; the next tick logs the character out. The engine brain is destroyed first. |

## 3. Lifecycle

`BotMgr` is the only owner of bot records and sessions.

```text
BotMgr::Login(characterGuid, master, free, pool, owned)
BotMgr::Logout(characterGuid, reason)
BotMgr::LogoutAll(reason)
BotMgr::CreateCharacter(accountId, name, race, class, gender)
BotMgr::Update(diff)            <- WorldScript::OnBeforeMapsUpdate
```

Rules the manager enforces:

- **One session per game account**, except owned bots of the same Battle.net account under the stated conditions. No caller can put two characters of an account into the world.
- **A bot is always a free bot when it has no master** and the engine is wanted: it grinds, loots and corpse-runs on its own. A masterless bot without the engine stands still.
- **The owner reclaims the character.** If the account's real session starts loading while a bot is in the world, the bot is asked to log out after a short guard window; its `Player` leaves and the human keeps the character.
- **Master grace.** A bot whose master logs out waits for `TortoiseBots.Slice.MasterGraceSeconds`, then logs out.
- **Shutdown.** The world script saves and logs out every bot while the world is still up.

## 4. Script hooks

The module registers no core patch. `AddSC_tortoisebots` creates the scripts below.

| Script | Hook | Use |
| :--- | :--- | :--- |
| `tortoisebots_world` | `OnConfigLoad`, `OnWorldInitialized`, `OnBeforeMapsUpdate`, `OnAfterReload`, `OnShutdown` | Load module and engine config, run the bot pass and host services, save/exit on shutdown. |
| `tortoisebots_player` | `OnCreatureKill`, `OnPlayerKilledByCreature`, `OnQuestStatusChange`, `OnLootItem`, `OnMoneyChanged`, `OnSpellCast`, `OnChat`, `OnChatSent`, `OnLogin`, `OnLevelChanged`, `OnFreeTalentPointsChanged`, `OnTalentsReset`, `OnLogout` | Dashboard events, bot life counters, commander chat orders, per-login/level syncs, engine teardown. |
| `tortoisebots_unit` | `OnDamageSpell`, `OnHealOutcome`, `OnUnitDeath` | Death and fight numbers. |
| `tortoisebots_group` | group hooks | Group bookkeeping used by the host services. |
| `tortoisebots_server` | `OnPacketReceive` | Reads the minimap-ping packet for a pending commander placement; the core's handler still runs. |
| `tortoisebots_commands` | `CommandScript` table | The `.tb` GM/console commands. |
| Player command script | `PlayerScript::OnCommand` | Claims `.bot` before the core's lookup; without the engine or with the module off it declines and the core answers. |
| Addon script | `PlayerScript::OnAddonMessageSent` | TBM addon channel; queues requester key + text for a separate world `OnUpdate` drain, and records that the character's client has the add-on (which stops the one-time add-on hint). |
| Chat-in script | `PlayerScript::OnChat` | Copies whispers/party lines into bot inboxes. |
| Other host scripts | `PlayerScript::OnCommand` for `.autogear`, `ServerScript`/`PlayerScript` fight-monitor and battleground/LFG hooks | Registered by their host services in the slice build. |

## 5. World tick

`WorldScript::OnBeforeMapsUpdate` calls `BotMgr::Update` first; that pass drains the chat arrivals queued since the last tick, confirms pending character creations and then updates the bots. After it the world script runs the pool reset and (slice build) passing buffs, the pool scheduler and character creation, the battleground service, the dashboard, and the slice-build run/feed/trace services.

Bot scheduling and the per-tick budget are described in [Strategy Engine & Action Scheduling](strategy-engine.md).

The module is inert when `TortoiseBots.Enable = 0`: no bot is created or logged in, `.tb login`/`.tb create` refuse, and `.bot` hands the word back to the core. Building a bot's brain needs the engine config; if `aiplayerbot.conf` is missing or `AiPlayerbot.Enabled = 0`, the brain cannot be built and the bot keeps the spike behaviour (a bot with a master follows and assists; a masterless bot stands).

## 6. Command entry points

| Entry | Path | Rules |
| :--- | :--- | :--- |
| `.tb …` | `forever/src/TbScripts.cpp` command table, handlers in `forever/src/host` | GM permission by default; the console may use most rows. `.tb create` needs the account-creation permission; `.tb pool prune` needs the GM permission and `confirm` for the destructive half. |
| `.bot …` | `host/player/TbPlayerCmd.cpp` | Player words (help/version/features, who/roster/list, add, logout/remove, follow/stay/attack/flee, summon, stats, maintenance, choice/service/totem/blessing/behavior, autogear, lfg). Inline on the world thread. |
| TBM addon messages | `host/TbAddonChannel.cpp` plus the `TbAddon*` verb services | Prefix `TBM`; bounded queue; a separate world `OnUpdate` drain; Send/Ack/Err answers. |
| Bot chat | `host/chat/TbChatIn.cpp` | `PlayerScript::OnChat` copies the line; the engine's `chat` strategy turns registered words into triggers/actions. |

`.bot` and `.tb` are separate surfaces: `.tb` is the server operator's/test surface, `.bot` is the player's. Do not document one as the other.

## 7. Host services

`forever/src/host` holds the server-side features the engine and the commands ask about. The main groups:

| Group | Files (representative) | What they answer |
| :--- | :--- | :--- |
| Pool | `pool/TbPoolCreate`, `TbPoolRegistry`, `TbPoolReset`, `TbPoolScheduler` | Pool accounts/characters, the registry, reset, and the login/logout scheduler. |
| Commander/orders | `cmd/TbCmdBinding`, `TbCmdOrders`, `TbCmdFeeds`, `TbPlace`, `TbOrders` | Commander bindings, party orders, position feeds, placement, stored orders. |
| Guild | `TbHostGuild`, `TbHostGuildJoin`, `TbHostGuildMail`, `TbHostGuildOrders*`, `TbHostGuildOrder*` | Guild participation, the player-asked invite into a bot-run guild, mail, order board, farm/craft/enchant/disenchant/fish services. |
| Auction house | `TbHostAuction`, `TbHostAhMarket*` | Market reading/supply and the seller/buyer service the engine consumes. |
| Errands/professions | `TbHostCapitalErrands`, `TbHostProfession`, `TbHostFishing`, `TbHostThievery` | Capital errands, profession travel/craft, fishing, pickpocketing. |
| LFG fill | `TbHostLfgFill` | Module-owned dungeon-finder fill (the client has no dungeon list; nothing writes `sLFGMgr`). |
| Open-world PvP | `TbHostWorldPvp` | Gives the seek role to a share of free bots and honours the core's PvP state. |
| Raids | `TbTactics`, `TbH3`, `TbH4`, `TbOnyxia`, `TbOnyxiaGround` | Raid tactics, group-fight commands, boss observation. |
| Automations | `TbAutoTrain`, `TbAutoSkillSpells`, `TbClassQuestSpells`, `TbTalents`, `TbAutoProfs`, `TbAutoTools`, `TbAutoHunterPet`, `TbAutoFood`, `TbAutogear`, `TbMountShop` | Login/level syncs that keep bots trained, geared, fed and mounted. |
| Rolls/party state | `TbRollVote`, `TbRollWon`, `TbPartyState` | Loot-method votes, won-roll reporting, party state queries. |
| Runs/plans | `run/TbPlanCommands`, `TbRunCommands`, `TbRunHooks`, `TbPlanUnits`, `TbRunTasks` | Dungeon plans and the run controller. |
| Dashboard/observability | `TbDashboard`, `dash/TbDashLink`, `dash/TbDashRuns` | Dashboard state, UDP feed, run summaries. |
| Lua API | `TbLuaBots` | Provider for the ForeverLua bot API; compiled only with the engine and `BUILD_FOREVER_LUA`. |

A host service is engine-facing code. It may include legacy headers and read `Player`/`Unit` state on the world thread; it must not keep a `Player` pointer across ticks.

## 8. Data and compat mirrors

The legacy engine reads item and spell data through public fields (`proto->Class`, `spellInfo->Effect[i]`). ForeverCore's `ItemTemplate` and `SpellInfo` expose them differently, so the module keeps two mirrors:

- `compat/TbItemPrototype.h` — the legacy `ItemPrototype` field names, filled from the core's `ItemTemplate` once per item.
- `compat/TbSpellEntry.h` — the legacy `SpellEntry` field names and units, filled from the core's `SpellInfo`; vanilla effect dice come from the core's `classic_spell_value` row when it is matched.

A field without a true ForeverCore counterpart is left out rather than guessed; its readers become census errors.

Features that do not work yet are listed in `forever/docs/UNPORTED.md`, counted through `compat/TbCompat_Unported.h`, logged once (`TortoiseBots: not ported yet: <name>`) and printed by `.tb unported`. A helper returns "the action did not happen", never an invented value.

## 9. Ownership model

| Responsibility | Owner |
| :--- | :--- |
| Network session lifetime | Core `World` |
| Bot session lifetime | Module `BotMgr` |
| Character load/save, map, spells, combat | Core |
| Bot AI, strategy graph, decisions | Module (engine brain, slice build) |
| Bot commands and chat | Module |
| Pool registry rows | Module (`forever/sql` tables) |
| Durable module tables | Module migrations in `forever/sql/{world,characters}` |
| Module settings | `forever/conf/tortoisebots.conf.dist` |
| Engine settings | `forever/conf/aiplayerbot.conf.dist` (generated from the code's reads) |

## 10. Checks

Before claiming a module/core pair works:

- `forever/tools/slice_registry_check.py` — the generated registry matches the full build's keys and creators.
- `python3 forever/tools/gen_aiplayerbot_conf.py --check` — the shipped engine config is current.
- `python3 tools/verify_okf.py` — the docs graph and links resolve.
- The module build with `TORTOISEBOTS_SLICE1` on, and the option-off build, both configure and compile.

[`UNPORTED.md`](../../forever/docs/UNPORTED.md) and the notes in `forever/docs/` record why a feature is out and what porting it needs.

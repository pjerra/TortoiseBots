# TortoiseBots vs ForeverBots (PlayerbotV2) — feature comparison

Read-only, local sources only. Nothing posted anywhere.
Sources: `TortoiseBots checkout` = TortoiseBots `main` @ `284395a` (2026-11-17; no `dev` branch exists upstream); `forever-bots src/modules/PlayerbotV2`, `BOT_COMMANDS.md`, `WORLD_BOTS.md`; addon at `the ForeverBots addon (repack)`; the WoW Forever repack; headstart note Sagi's headstart note.

Core framing (from the headstart note): TB runs on tortoise-wow (1.12 MaNGOS), headless sessions, vendored cmangos AI. FB runs on TrinityCore master with a modern client (build 70205+/16001). Line counts below are non-blank, non-comment lines via a local counter (cloc not installed).

## 1. Comparison table

| Area | TortoiseBots | ForeverBots (PlayerbotV2) | Gap / notes |
|---|---|---|---|
| Companions / hire | `.bot add` own alts; `.bot hire` NPC recruiters or anywhere; hire tiers 0-7; own alts can play on (alt-bots) | `.bot add <class> [role]`, `.bot call/summon/goto/save/dismiss/delete`, auto-level/gear/talent; no hire NPCs, no alt-bots | FB lacks recruiters, hire tiers, alt-bots; TB lacks FB's per-bot saved roster UI wording but equal in function |
| Party roles | Spec picked from talents; tank/healer/seat roles; owner `.bot role` | Role from declared/talent/spec file; `set main tank`; tank markers assign | Roughly equal; TB has per-character gear-tier unlocks, FB has saved-roster + auto Group Finder fill |
| Class rotations (9) | Per-class docs + Turtle custom spells; `docs/classes/{9}.md` | `Combat/Apl/Apl_Classic_{9}.cpp` (446–1050 lines each) + `ClassicSpec/TalentBuilds/Skills`, ExpectedSpells tests | Both full 9-class; FB's APL files are explicit per-spec lists, TB's are strategy/AI-context code. Similar scope |
| Dungeons | Run leader, 5-man tactics table (`ai_boss_tactic`), pullback, CC marks; ~13 vanilla row sets | 100+ dungeon scripts (`Bot/Dungeon/Scripts`: Deadmines..ZulGurub incl.), `DungeonTactics` tables, tank-lead/route engine | FB far broader (retail+classic breadth); TB deeper on its own Turtle-tuned rows |
| Raids (boss tactics) | Per-boss `ai_boss_tactic` + stand tables, MC→Naxx + Turtle customs (ES/Karazhan); boss-tactics doc ~215-450 lines of named encounters | `Bot/Raid`: MC/BWL/AQ20+40/ZG/Ony/Naxx encounter data + `RaidMechanics_Naxx.cpp` (1264 lines), `RaidBrain`, callouts, banter | Both strong on classic raids; FB also has TBC/WotLK/retail scripts (breadth), TB has deeper Turtle-specific tuning |
| BG / arenas | LFT queues + WSG/AB/AV/SV + Blood Ring bot arena; no rated arenas | Full BG scripts (WSG/AB/AV + retail maps), `ArenaScript.cpp` (2v2/3v3 positioning), battlemaster Join support | FB broader; TB's Blood Ring is bot-only skirmish vs FB's real arena support |
| World / random bots | Pool accounts, level seeding, grind/quest/wander, camps | Shaper pool (`TotalTarget`) + ambient spawner around player (`World.Enable`), town life, gatherings, world PvP mix | Same idea; FB adds anchor-ring spawner + faction mix + Duels/gathering behaviors |
| Travel | Foot/flight/boat, no teleports except stuck | Foot/flight/portal/ship, `goto`, hearth/mount binds; quest portal index | TB deliberately anti-teleport; FB more pragmatic |
| Questing | Full autonomous questing + upkeep + 21-command-style `.bot` control | `QuestOrders` (list/accept/drop/turn-in/share) + world quest hubs; older fallback verbs | TB's autonomous questing is deeper; FB's orders are owner-directed |
| Loot / vendor / repair | Need/greed rules, hire-loot toggle, vendor trash, repairs | `loot`/`loot all`, `sell grey/repair`, town errands | Equivalent; wording differs |
| Gear / upgrades | Tiered hire gear, autogear, enchant rules; no explicit gem system in docs | Role-score auto-equip (+10% rule), `equip best`, raid loadouts, BiS pools, raid enchants | TB has hire tiers + templates; FB has explicit raid loadouts/enchants |
| Guilds + guild orders | Guild bots + farm/buy/craft **guild orders** to guild bank | Guild charters, guild events, craft-order board, guild invite whispers; **no player-ordered farm/buy/craft economy loop** | TB's guild orders are the standout unique system |
| Auction house | Live player-market AH bot (buys player listings, fair-price caps) + guild-order buying | Core `AuctionHouseBot` sellers/buyers (fills houses, buys player auctions) | Same shape; TB's ties into guild orders |
| LFG | Turtle LFT autofill; dungeon finder leader flows | `.bot lfg`, Group Finder auto-fill, `.bot dungeon` queue with summons | Equivalent |
| Chat / personality | Greetings, no LLM (explicitly disabled both sides per capabilities.tsv) | Greetings, banter (mild/harsh), say-replies, **optional local bot-bridge chat model** (own program, file queue, offline) | FB's bridge is the only AI-chat path; both ship no model |
| Addon UI | TortoiseBotsManager `/tbm`: roster/party/gear/guild-orders/recipe search | ForeverBots `/fb`: buttons send plain chat; raid ready, gear page, finder, place/formation, hire, quests; **no addon protocol** (chat only) | TB addon is deeper (bot panel, guild orders); FB is deliberate chat-only (simpler client) |
| Dashboard / telemetry | Go daemon: live map + Prometheus `/metrics`, UDP emitter, per-bot logs | BotInspector, PerfCounters, ScenarioRunner, expected-spell/baseline audits; no web dashboard found | TB has the live dashboard; FB has heavier offline test/audit harness |
| Config knobs | `aiplayerbot.conf.dist` 4283 `AiPlayerbot.` lines | `playerbot.conf.dist` 1790 keys + override chain (`zz-*.conf`) | TB exposes far more knobs; FB leans on launcher presets |
| Test tooling | `tools/` 484 files incl. per-policy unit tests + fixture harness | `Diagnostics/` (ScenarioRunner, bot inspector, perf counters, expected-spell/dps baselines) + `tools/*.py` audits | Both strong; FB's ScenarioRunner is a notable full-sim rig |

## 2. TortoiseBots-only (what a port would ADD to Forever)

- Guild orders: player-ordered farm/buy/craft economy to the guild bank (`docs/play/guild-orders.md`).
- Hire system with NPC recruiters + gear tiers 0-7 + world-buff vendors (`roster-and-hiring.md`).
- Alt-bots (owned alts keep playing) + claimed guild bots.
- Turtle custom content: custom raids (Emerald Sanctum, Karazhan), custom Turtle spells/classes/races hooks, Turtle mount rules.
- Live observability dashboard + Prometheus + per-bot action logs + 4283-knob config surface.
- Remote commander, run leader, AI decision capture/run-trace tooling.

## 3. ForeverBots-only

- Ambient world-bot spawner around the player + shaper pools + faction mix + Duels/gather/town-life behaviors (`WORLD_BOTS.md`, `Fleet/WorldBotSpawner`, `GateDuels`, `Gathering`, `TownLife`).
- Raid banter + say-replies + optional local bot-bridge chat model.
- Auto Group Finder/LFG fill + `.bot dungeon` queue flow.
- Arena support (`ArenaScript.cpp`) + battlemaster Join on modern client.
- ScenarioRunner full-sim test rig + ExpectedSpells/dps baselines.
- `SecureHandler`-style stock-UI-style bindings? No — FB addon is chat-only by design (simpler, less capable than `/tbm`).
- Repack/launcher: one-click Windows installer, updater, backup/restore, control panel (WoW Forever repack).

## 4. Top 5 worth porting (to Forever)

1. **Guild orders** — the only player-directed farm/buy/craft economy loop; nothing like it in FB.
2. **Raid encounter brain pieces (targeted)** — FB already covers Naxx breadth; port TB's tank-swap/kill-priority idioms only where FB's Naxx notes show gaps.
3. **Observability daemon shape** — FB has counters but no live map/metrics endpoint; TB's Go daemon design ports cleanly.
4. **Alt-bot + claimed-guild-bot lifecycle** — persistent owned bots beyond the companion roster.
5. **Hire-anywhere + gear tiers** — FB's `.bot add` covers creation; TB's tier/contract economy is the missing half.

## 5. Size note

- TortoiseBots (main @284395a): `ai/` 1435 files / ~232k lines, `runtime/` 289 / ~79k, `commands/` 28 / ~26k, `tools/` 484 / ~110k, `host/` 52 / ~3.5k, `behavior/` 7 / ~0.5k. (Non-blank, non-comment, C++-ish files; docs/SQL excluded.)
- ForeverBots PlayerbotV2 module: 696 files / ~194k lines total. Combat APLs are compact per class (Druid 966 … Warlock 461 lines); `RaidMechanics_Naxx.cpp` alone is 1264 lines.
- TB's knob surface is ~2.4× FB's (4283 `AiPlayerbot.*` lines vs 1790 `playerbot.conf.dist` keys). Releases: TB ships its own daemon binary per OS; FB ships a full Windows repack.

DONE

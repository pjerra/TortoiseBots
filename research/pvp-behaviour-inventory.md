# PvP-specific bot behaviour inventory (Sagiroth/TortoiseBots#1447)

Facts only. TB = TortoiseBots at upstream/dev `cf507f077`. MP = mod-playerbots (liyunfan1223, master `05a1f6f`, AzerothCore WotLK).
CM = cmangos/playerbots (master `6823423`, 2026-10-28, same code lineage as TB). Paths are relative to each repo; TB paths start `ai/playerbot/`.
Read from shallow clones in `~/TortoiseBots-wt/ext-*` (read only, nothing built). Line numbers are from those commits.

Note on the issue text: `docs/classes/priest.md:91` and `warlock.md:81` do not hold the PvP-CC text at upstream/dev. The priest note is at `docs/classes/priest.md:75` (Psychic Scream "restricted to PvP or solo open-world emergencies"). The warlock docs (`warlock.md:77`) do not mention PvP Fear or Howl of Terror; the code does (below).

## 0. How the PvP layer is wired in TB

- Combat engine always gets the `pvp` strategy (class `AttackEnemyPlayersStrategy`, `strategy/generic/AttackEnemyPlayersStrategy.cpp:12`): `AiFactory.cpp:666` in a battleground, `:670` outside one (with `duel`, `cast time`), and `:1046` in the BG branch. So "pvp" is a default strategy, not a PvP-zone switch. Gate for actually taking a player: `AttackersValue.cpp:420` drops enemy players unless the bot has `pvp` (combat) or the open-world seek role (`WorldPvpSeekChecks::IsSeeking`).
- Per-class `<Class>PvpStrategy` / `Cc/Aoe/Buff/Boost/Cure PvpStrategy` classes exist for every spec (e.g. `priest/ShadowPriestStrategy.cpp:126`, `hunter/HunterStrategy.cpp:206`) and are switched by `Update<Class>PvpStrategiesAction` (`priest/PriestActions.h:293`). Their bodies are empty pass-throughs to `ClassPvpStrategy`/`CcPvpStrategy`/... (`strategy/generic/ClassStrategy.cpp:62-80,140-190,220-230`), except the rows listed below.
- CM has the same class skeleton; MP has no per-class PvP strategy classes.

## 1. Target priority / target acquisition

| Behaviour | Where | Gate | In TB? |
|---|---|---|---|
| "enemy player near" -> "attack enemy player" at relevance 55 | TB `strategy/generic/AttackEnemyPlayersStrategy.cpp:12-24`; MP `src/Ai/Base/Strategy/AttackEnemyPlayersStrategy.cpp:12` (55.0); CM `strategy/generic/AttackEnemyPlayersStrategy.cpp:10-11` (ACTION_EMERGENCY) | `pvp` strategy (always on in TB, see 0) | Yes (TB copied MP's 55 value, comment cites MP 79bd428) |
| Pick enemy player: duel opponent first, then flag carrier (auras 23333/23335), then nearest for melee / lowest HP for ranged | TB `values/EnemyPlayerValue.cpp:208-262`; MP `src/Ai/Base/Value/EnemyPlayerValue.cpp:58-127` (PvP combat refs first, flag carrier, nearest, in LOS, Z diff < 30); CM `values/EnemyPlayerValue.cpp:166` (flag aura) | PvP/BG only (needs an enemy player) | Yes (TB: duel, flag, melee-nearest/ranged-lowest-HP; MP sorts by distance only) |
| Aggro range and LOS gate for player targets: full range if bot HP > target HP, else 20 yd; LOS; Z < 30 | TB `values/EnemyPlayerValue.cpp:~140-160` (`CanEngageFromHere`, cites MP); MP `EnemyPlayerValue.cpp:115-126` | BG proximity fallback in TB; MP always in this value | Yes |
| Max attack distance: 60 outside BG, 40 in BG, AV 40 first 20 min then 10 | TB `values/EnemyPlayerValue.cpp:276-296`; CM `EnemyPlayerValue.cpp:201` | BG only (outside BG a flat 60) | Yes |
| Proximity scan of "possible targets" for hostile players when no threat entry exists | TB `EnemyPlayerValue.cpp:~38-60` (BG only; open world only for bots with the `world pvp` seek role) | `InBattleGround()` or seek role | TB-only (MP/CM use the attacker/nearest-enemy lists) |
| Open-world PvP seek role (`world pvp` strategy), PvP-prohibited zone list | TB `strategy/generic/WorldPvpStrategy.cpp:10-20`, `values/WorldPvpSeek*.{h,cpp}`, `PlayerbotAIConfig.cpp:386,1328`; MP `NearestEnemyPlayersValue::AcceptUnit` uses `IsPvpProhibited` (`EnemyPlayerValue.cpp:23`) and requires `enemy->IsPvP()`; MP `src/Ai/World/Rpg/Action/NewRpgOutdoorPvP.cpp` | Seek role only / flagged enemies | Yes (TB has its own seek role) |
| Players added to "attackers" (no threat list): MP adds valid enemy players in arenas and within 40 yd in BG | MP `src/Ai/Base/Value/AttackersValue.cpp:52-61`; TB `values/AttackersValue.cpp:~410-430` (gate above) | BG/arena (MP); `pvp` strategy (TB) | Partly (TB gates by strategy, not by BG) |
| Don't re-engage the player that just killed a masterless bot; give up on unreachable players for 15 s | TB `EnemyPlayerValue.cpp:~96-125` | Masterless random bots / PvP | TB-only |
| Yield to master PvP flag: with a master, only attack if master is PvP flagged | MP `EnemyPlayerValue.cpp:29-32` | Master present | No (not found in TB) |
| Tab-target / "dps target" skip players (players have no threat list) | TB `values/DpsTargetValue.cpp:144,182,212,367` (skips `IsPlayer()` as DPS candidates); `triggers/GenericTriggers.cpp:306` (LoseAggro returns false vs a player target); MP `Druid/Action/DruidCatActions.cpp:14`, `Warlock/WarlockActions.cpp:115`, `Priest/PriestActions.cpp:20` | Always (not PvP-only, it is a PvE-vs-player guard) | Yes |

## 2. Crowd control and diminishing returns

| Behaviour | Where | Gate | In TB? |
|---|---|---|---|
| Warlock "fear pvp": Fear a player target when bot HP <= lowHealth, target is hitting the bot, nothing already feared | TB `warlock/WarlockTriggers.cpp:289-310`, row at `warlock/WarlockStrategy.cpp:442` (`WarlockCcPvpStrategy`, ACTION_INTERRUPT+1) | PvP only (target must be `IsPlayer()`) | TB-only (not in MP; CM: not found) |
| Warlock Howl of Terror on "enemy ten yards" | TB `warlock/WarlockStrategy.cpp:446-449` | PvP only (`WarlockCcPvpStrategy`; comment: scatters PvE packs) | TB-only |
| Priest Psychic Scream on "enemy five yards" | TB `priest/PriestStrategy.cpp:385-389` (`PriestCcPvpStrategy`, comment "Issue #383"); CM `priest/PriestStrategy.cpp:316,885` (in the base CC strategy, no PvP gate); MP: only an action node, no trigger row found | PvP only in TB; always in CM | Yes (TB made it PvP-only) |
| Mage "ten yards" melee-range crowd rows | CM `mage/MageStrategy.cpp:389,804,1231` (no PvP gate found) | Always | See TB mage files (not PvP-specific) |
| Paladin Hammer of Justice on a moving player target | TB `paladin/PaladinTriggers.cpp:441-460` (`HammerOfJusticeOnEnemyTrigger`: `isMoving && target->IsPlayer()` -> true; else HP rules); rows at `RetributionPaladinStrategy.cpp:356`, `ProtectionPaladinStrategy.cpp:378` | Player branch is PvP-only; the trigger itself runs vs mobs too | TB-only |
| Rogue Riposte / Surprise Attack check target melee vs ranged for players | TB `rogue/RogueTriggers.cpp:10-45` | Player branch PvP-only | TB-only |
| Rogue Sap/Garrote-style range extended in BG and arena (+15 yd) | MP `Class/Rogue/RogueTriggers.cpp:66-70`, `Class/Druid/DruidTriggers.cpp:92-95` | BG/arena only | No |
| Diminishing returns tracking | none found: TB (`grep -i diminish` over `ai/`, `src/` is empty), MP (`src/`), CM (`playerbot/`) | n/a | No (absent in all three) |
| CC target value skips players | TB `values/CcTargetValue.cpp:158,184` (`!creature->IsPlayer()` conditions) | Always | Partly (CC marks are mob CC) |

## 3. Interrupts

| Behaviour | Where | Gate | In TB? |
|---|---|---|---|
| "Enemy healer" interrupt: finds an attacker casting a heal and kicks it (Kick, Counterspell, Silence, Spell Lock, Death Coil, Hammer of Justice) | TB `values/EnemyHealerTargetValue.cpp:11-53` (real heal only via `IsHealSpell`, held-CC-mark guard), rows: `rogue/RogueStrategy.cpp:378`, `mage/MageStrategy.cpp:439`, `mage/GenericMageStrategy.cpp:107`, `priest/ShadowPriestStrategy.cpp:57,340`, `warlock/WarlockStrategy.cpp:391,403,483`, `paladin/GenericPaladinStrategy.cpp:23`; MP `src/Ai/Base/Value/EnemyHealerTargetValue.cpp:12-39` (any positive spell); CM `values/EnemyHealerTargetValue.cpp` (IsPositiveSpell) | Always (applies to mob healers and players; not PvP-gated) | Yes |
| Generic interrupt of any cast (`IsInterruptableSpellCasting`) | MP `src/Bot/PlayerbotAI.cpp:4423-4450`; TB `PlayerbotAI.cpp` (same name, with an extra argument) | Always | Yes |
| Own cast interrupted when healing is needed | TB `generic/CombatStrategy.cpp:207` (`HealInterruptStrategy`) | Always | Yes |

## 4. Defensive and escape cooldowns, trinket, racials

| Behaviour | Where | Gate | In TB? |
|---|---|---|---|
| Free Action Potion on "random" in every class PvP strategy | TB `generic/ClassStrategy.cpp:62-67`; CM `generic/ClassStrategy.cpp:93-98` | `ClassPvpStrategy` (PvP, but in TB `pvp` is default-on) | Yes (same as CM) |
| Mage Ice Block / Blink at critical health, Blink back as kite escape | TB `mage/MageStrategy.cpp:74-88` | Always (not PvP-specific) | Yes |
| Shaman Ghost Wolf and Hunter Aspect of the Cheetah when carrying a flag | TB `shaman/ShamanStrategy.cpp:85-101` (`ShamanPvpStrategy`, ACTION_HIGH combat / ACTION_EMERGENCY non-combat), trigger `triggers/PvpTriggers.cpp` "player has flag"; CM `hunter/HunterStrategy.cpp:482`, `rogue/RogueStrategy.cpp:110`; MP `Class/Rogue/Strategy/GenericRogueNonCombatStrategy.cpp:39`, `Class/Druid/Strategy/FeralDruidStrategy.cpp:88` | BG flag carrier only | Partly (TB has shaman in PvP strategy; other classes not found) |
| Trinket use (equipped trinket, classifies spell as mana/defensive/aura) | TB `actions/UseTrinketAction.{h,cpp}:58-140`, `policy/gear/TrinketUsePolicy.h`; CM `actions/UseTrinketAction.h`; generic boost trigger turns on for player targets `triggers/GenericTriggers.cpp:1038-1042` | Boost trigger fires for player targets in any context | Partly: general trinket use yes; no dedicated PvP trinket (break CC) logic found in TB/MP/CM |
| Racials: boost trigger active when current target is a player | TB `triggers/GenericTriggers.cpp:1038-1042`, `generic/RacialsStrategy.cpp`; MP `src/Ai/Base/Strategy/RacialsStrategy.cpp` | Always on; player target short-circuits the balance check | Yes |
| Escape-class abilities that break roots/stuns (Every Man for Himself, Escape Artist, Will of the Forsaken) | none found as PvP logic in TB/MP/CM | n/a | No |

## 5. Kiting and line of sight

| Behaviour | Where | Gate | In TB? |
|---|---|---|---|
| `kite` strategy: "has aggro" -> "runaway" at 51 | TB `generic/KiteStrategy.cpp:7-11`; MP `src/Ai/Base/Strategy/KiteStrategy.cpp` (same row); `flee` via `src/Mgr/Move/FleeManager.cpp` | Strategy toggle, not PvP-only (and players have no threat so "has aggro" is false vs players) | Yes |
| Mage kite escape ("blink back") | TB `mage/MageStrategy.cpp:82-88` | Always | TB-only |
| Snare: Death Coil / snare on a player that is idle-moving and hitting the bot or a player | TB `values/SnareTargetValue.cpp:68-80`, `warlock/WarlockStrategy.cpp:407`; MP/CM `SnareTargetValue` has the same player branch | Player branch only fires for `IsPlayer()` | Yes |
| LOS gate when choosing a player target | TB/MP `EnemyPlayerValue` (`IsWithinLOSInMap`, see 1); no pillar/LOS-break movement for casters found in any of the three | PvP | Target pick yes; LOS-breaking movement no |
| Rogue/Druid stealth-opener distance +15 in BG/arena | MP `Rogue/RogueTriggers.cpp:66-70`, `Druid/DruidTriggers.cpp:92-95` | BG/arena | No |

## 6. Healer focus

| Behaviour | Where | Gate | In TB? |
|---|---|---|---|
| Healer bots keep a healing spec in BGs (not switched to DPS) | TB `AiFactory.cpp:1055-1062` (comment cites MP); `docs/world/queues.md:85` | BG only | Yes |
| Skip Berserk buff pickup if bot is healer or carrying flag | TB `actions/BattleGroundTactics.cpp:3213-3214` | BG only | Yes (BG tactics ported from MP/CM) |
| Prioritising the enemy healer as an attack target | none found: `EnemyHealerTarget` is "enemy casting a heal" for interrupts only; `EnemyPlayerValue` picks nearest/lowest HP | n/a | No (absent in all three) |
| WSG escort / support target logic (follow own flag carrier, guard) | MP `src/Ai/Base/Strategy/BattlegroundStrategy.cpp:32-45`, `BattleGroundTactics.cpp` | WSG only | Not found in TB beyond flag-carrier targeting |

## 7. Other PvP-only branches found in TB

- Duel: `strategy/generic/DuelStrategy.cpp` ("duel requested" -> accept, "no attackers" -> attack duel opponent at 70); added outside BGs only (`AiFactory.cpp:670`). MP `Strategy/DuelStrategy.cpp:14-16` identical rows. `docs/play/features.md:203`: no dedicated 1v1 mode.
- Hired companions leave in BGs unless `AiPlayerbot.HireAllowPvP=1` (`docs/play/commands/roster-and-hiring.md:92`); `AiPlayerbot.RandomGearAllowPvP` (`docs/play/features.md:127`).
- BG: join/queue (`actions/BattleGroundJoinAction.cpp`), per-map tactics (`BattleGroundTactics*.cpp`, WSG/AB/AV), no arenas (`docs/play/known-limitations.md:40`).
- Not found anywhere (TB, MP, CM): DR tracking, PvP trinket/CC-break logic, enemy-healer kill priority, LOS-pillar movement, explicit burst/defensive-cooldown PvP trigger sets. MP has arena-specific code (`ArenaTeamActions`, `InArena()` checks) that TB cannot use (no arenas on Turtle).

## Caveats

- "In TB?" is by file reading, not a live test. Some CM rows were matched by grep only (rows marked "not found" mean the greps `diminish|trinket|insignia|IsPlayer|pvp|InBattleGround` over strategy dirs).
- MP is a WotLK/AzerothCore codebase; its class files hold few PvP branches (only the Rogue/Druid BG/arena distance and the `IsPlayer()` threat guards).

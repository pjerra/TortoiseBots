# Onyxia first: Turtle behavior and Forever port plan

Board ticket: **[port] Raids, Onyxia first**. Read-only research, 2026-11-19/20.
This is an implementation plan and data inventory, not a tested Forever raid implementation.

## Target and evidence

Use **Classic map 249**, boss **10184**, and the repack's binding
`classic_boss_onyxia`. Do not substitute the separate remake `boss_onyxia.cpp`.

| Source | Pinned evidence |
| --- | --- |
| Forever module baseline | `origin/port/forever` at `ec859bb0d`; this data branch starts there |
| Turtle donor module | local `helper3/faerlina-188`, commit `35eec8aad727496cce55b1255262c2a4a32677e7` |
| Forever core source | local `forever/spike/TrinityCore`, commit `6517af05aaa78ec4a20292629c2a47f3b987f461` |
| Client data | build 70959, `forever/db2-70959/dbc/enUS`; SpellName/Misc/Effect/CastTimes/Duration/Radius |
| Repack data | read-only `world` queries on Nov 19: creature templates, difficulty, Classic levels/stats/base stats, SmartAI, spell overrides and target positions |
| Audit evidence | workspace `out/onyxia-port-evidence/`: TSV snapshots, `db2.json`, extraction/generation scripts and validation |

Core source paths below are relative to that Forever core checkout; Turtle paths are relative to the donor module. The local core revision explains timers; this work did **not** prove that the running repack executable was built from that revision. Verify that match before treating timers as live guarantees. DB binding and client facts were inspected directly. Nothing was deployed or pulled in game for this job.

## Creature data and health

| Entry | Creature / role | Classic level | Calculated repack max health | Evidence |
| --- | --- | --- | --- | --- |
| 10184 | Onyxia | 63 | 1,099,230 | class 1 base 3,331 × Classic multiplier 330 |
| 11262 | Onyxian Whelp; summoned adds | 56–57 | 2,699–2,784 | class 1 base × multiplier 1 |
| 12129 | Onyxian Warder; entrance trash | 60–63 | 76,300 / 78,600 / 80,925 / 83,275 | class 1 base 3,052 / 3,144 / 3,237 / 3,331 × 25 |
| 36561 | Onyxian Lair Guard; **remake, excluded** | Not established for Classic | Not established for Classic | no Classic level/stat override; not summoned by the Classic boss script; no queried static spawn |

These health values are **derived**, not measured from a live spawned unit.
`Creature.cpp`, `InitStatsForLevel`, uses `creature_classic_classlevelstats` ×
`creature_classic_stats` × classification health rate. Inspected configuration:
`Classic.ForeverCreatureHealth=0`, all relevant `Rate.Creature.HP.*=1`, and
`Classic.CreatureHealth.Raids=0`; the configured high-level increase therefore
excludes this raid. Recompute if these settings change. With the alternate
Forever health curve enabled, base values differ; do not hardcode the numbers.

The world DB has one Onyxia spawn and five Warders on map 249. Whelps are
script/egg summons; their absence from static spawn rows is expected.
`classic_npc_onyxian_whelp` joins zone combat and uses ordinary melee; no extra
whelp spell rotation appears in that script. Warder uses SmartAI.

70959 `Creature.db2` supplied no matching entries. `CreatureDifficulty.db2`
also supplied no relevant rows **and its layout hash differs from the local
schema**; it is not used for level/health conclusions. Spell table layout
hashes match. Some encrypted spell sections are skipped by the reader: an
absent ID is reported as not found in decoded data, not universally nonexistent.

Onyxia's repack immunity record `5010184` has `SchoolMask=4` (fire immunity).
Do not assign fire-only damage. Turtle's taunt immunity is established in its
own source/data; do **not** assume an identical Forever bit layout. The repack
`flags_extra=268959745` does not set the core's `NO_TAUNT=0x100`, and the immunity
row alone does not establish taunt behavior. Make runtime SpellInfo/immunity
validation an explicit port gate rather than promise taunt recovery.

## Forever encounter and abilities

[onyxia-abilities.csv](onyxia-abilities.csv) inventories direct spells and the
recursively reachable 70959 trigger chains, with actor, phase, initial/repeat
schedule, client cast/duration/radius, parent/child IDs, response and source.
`classic`, `classic-trash`, and their trigger rows belong to this plan.
`remake-excluded`, `declared-unused`, and `turtle-only-missing-70959` are explicitly
excluded. Empty radius means no radius index, not proof of no area effect.
Client cast times are not AI repeat timers. Triggered children have no separate
AI schedule; their client cast field is not a promised effective delay.

Authoritative timer implementation:
`src/server/scripts/Custom/Classic/instances/onyxias_lair/classic_boss_onyxia.cpp`,
`Reset`, `PhaseOne/Two/Three`, `DoMovement`, `PhaseTransition`, `MovementInform`.
SmartAI timings come from `world.smart_scripts` event parameters 1–4, not prose
comments saying “No Repeat”. Cast success, movement and other casts can delay
these timers; they are not a wall-clock metronome.

| Phase / event | Verified source behavior | Bot response |
| --- | --- | --- |
| Ground P1 | Flight transition below **65%**, not a copied 70% threshold | Observe actual flight/aura as well as health; assigned tank opens, raid waits for contact |
| Ground P1/P3 | Flame Breath 18435: 10–20 s; Cleave 19983: 2–5 s; Tail Sweep 15847: first 5 s then 3.5 s | Tank holds facing; melee/healers/ranged on flanks, never tail or front |
| Tank displacement | Wing Buffet 18500: first 10–20 s then 15–30 s; Knock Away 19633: first 15–25 s then 15–30 s, removes **25% victim threat** | Maintain instant threat while moving; recover position; DPS respects actual tank threat |
| Air P2 | Hover 17131; Fireball 18392 initial 3 s, recast timer 3 s after success, movement resets 5/10 s; removes **100% target threat** | Spread for 8-yard splash, continue ranged single-target damage; retarget after threat/target changes |
| Deep Breath | First movement decision at 20 s; subsequent checks 15–25 s, or 20–25 s after a breath. 35% clockwise / 35% counterclockwise / **30% breath** | Detect warning and actual root; do not predict a fixed breath period |
| Breath warning/flight | Directional root casts for 5 s; script waits **5.5 s** before flight, then casts heated ground 22191 and 22197 | Every role escapes checked damage-chain geometry before cast completion; avoid both egg pits |
| P2 whelps | First wave begins after 5 s; summons a pair about each second until count reaches 16. Later target count 5–7, pairwise (6 or 8 actual), with 30 s between waves | Add tanks/melee handle whelps; ranged rescue endangered healers, then return to boss |
| Landing P3 | Below **40%**, deferred until no breath cast/flight; threat reset on transition; **triggered fear on landing** | Assigned tank reacquires boss; reserve takes over only on real owner failure; do not rely on pre-cast warning for first fear |
| P3 fear | **22686 Bellowing Roar**, client 1.5 s cast / 4 s duration; first ongoing timer 10 s, repeats 15–30 s; triggers **1297001 Lava Eruption** | Fear Ward/Tremor/fear-break capabilities; recover facing after control returns; validate eruption geometry separately |
| P3 adds | Whelp timer carries through transition, then one whelp every 1–10 s | Keep add duty active after landing |
| Egg spawner | GO 176510 casts **17646** on next instance update after creation | Avoid egg caves; pick up accidental adds without dragging the boss into them |
| Warder trash | 20203 Fire Nova 1/12 s; 18958 Flame Lash 5/20 s; 15754 Cleave 7/(9–11) s; 12097 Pierce Armor 3/35 s | Face away, protect tank from armor reduction, avoid unnecessary fire AoE |

Eight breath roots: **17086 N→S, 18351 S→N, 18576 E→W, 18609 W→E,
18564 SE→NW, 18584 NW→SE, 18596 SW→NE, 18617 NE→SW**. Resolve
`spell_target_position` and all child spell effects rather than treating the
boss's current facing as the full lane. Build a fresh chain/position snapshot
for this core/client; Turtle's geometry is donor logic, not Forever proof.

Important substitutions: Turtle fear **18431** was not found in decoded 70959
spell data; Classic explicitly uses **22686**, including its **1297001** trigger.
Do not port Turtle's old 17731 fissure propagation as if verified here.
`SPELL_ONY_DEEPBREATH=23461` is declared but not cast by this Classic script;
the eight directional roots are the actual warning casts. Lair Guard 36561's
68958/68960/15284 SmartAI rows are remake data; 68958 and 68960 were not found
in decoded client data, another reason not to import that rotation.

DB `classic_spell_value` overrides client damage (for example Fireball,
Flame Breath, Tail Sweep and breath-chain damage). The CSV deliberately does
not present DB2 damage as effective combat damage. No `classic_spell_info`
rows were returned for the initially queried direct boss/Warder spells;
client cast/radius facts still need normal SpellInfo corrections considered.

## Turtle behavior to carry over

The donor has a substantially later Onyxia stack than the legacy files already
present in the Forever module. File presence is not proof of Forever runtime
support. Read these files at the pinned donor revision:

| Piece | Source paths under `ai/playerbot/` | Behavior / boundary |
| --- | --- | --- |
| Encounter registration | `strategy/generic/OnyxiasLairDungeonStrategies.{h,cpp}`, `strategy/triggers/OnyxiasLairDungeonTriggers.{h,cpp}` | Start/end cleanup, air, whelp, egg and emergency breath triggers; fire resistance/protection support |
| Ground owner and formation | `strategy/actions/OnyxiaTankPositionActions.{h,cpp}`, `OnyxiaTankPositionPolicy.h`, `OnyxiaEngagementPolicy.h` | One explicit landing tank, controlled opening, reserve election, threat recovery and flanks; manual cancellation wins |
| Air targets and reach | `strategy/actions/OnyxiasLairDungeonActions.cpp`, `OnyxiaAirCombat.h`, `OnyxiaAirPolicy.h`, `strategy/values/OnyxiaAirCombatValue.h`, `strategy/actions/OnyxiaAirReachAction.cpp` | Boss vs healer-rescue whelp choice; cache/auto-repeat retargeting; usable ranged reach instead of assist loops |
| Breath and Fireball | `strategy/actions/OnyxiaBreathActions.cpp`, `policy/raid/OnyxiaBreathPolicy.h`, `policy/raid/OnyxiaFireballPolicy.h` | All-role warning escape, air pre-positioning, safe routes and splash spacing with actual-arrival diagnostics |
| Whelps/eggs | `policy/raid/OnyxiaWhelpEggPolicy.h`, dungeon actions/triggers | Add duty, healer rescue, egg clearance; avoid unsafe AoE/chasing |

Turtle setup uses `onyxia deep breath` for all roles, `onyxia air spread` for
ranged (later spacing tests use all), `onyxia whelps` for melee, and exactly one
`onyxia landing tank`. `labby/ony-t1-pull.sh` preserves that assignment recipe.
These are donor controls to implement/verify, **not commands claimed to work
on the current Forever slice**.

Donor map-249 tank anchor: **(22.8763, -217.152, -85.054054)**. Ground corridor:
x -110..35, y -232..-198, floor z -94..-80. Initial flank radii: melee 20 yd,
casters/healers 26 yd, hunters 36 yd; rear approaches use a 34-yard outer arc.
Later code keeps any already-safe usable position and tries nearby candidates
when range/LOS fails. Native paths must remain complete, on the floor, clear of
eggs/hazards, and in healer range/LOS. These coordinates are a donor starting
point: revalidate against Forever navmesh, creature reach, eggs and spell data.
No unchecked teleport/direct-path fallback belongs in normal bot behavior.

## What failed, and what changed

The workspace reports are evidence pointers, not claims that every patch
achieved a clean live kill:

| Observed / reproduced failure | Donor correction | Evidence in workspace `out/` |
| --- | --- | --- |
| Landing tank could not find idle boss during an authorized pull; projected floor Z rejected valid XY endpoint | Use cancellable pull target before contact; validate projected floor; hold companions/pets until contact; put non-tanks on flanks | `554-onyxia-t1-opening.md` |
| Knock Away preceded threat handoffs; death/fear/displacement confused with spare-tank stealing | Keep one owner; threat/health-aware recovery; preserve genuine replacement after death; do not manufacture threat or loop immune taunts | `541-onyxia-tanks-and-air-gaps.md`, `542-onyxia-threat-and-pace.md`, `544-onyxia-taunt-immunity.md` |
| Repeated high-priority assist consumed cycles; stale target/auto-repeat stayed on whelps; hunter dead zone blocked shots | Shared air target choice, correct cache/clock and auto-repeat repair, minimum-range-aware movement | `539-onyxia-air-loop.md`, `540-onyxia-air-dps.md` |
| Two fixed flank endpoints failed duty LOS and blocked generic reach; safe positions abandoned | Bounded nearby candidates; retain safe useful position; record first actual damage and hold coverage | `559-onyxia-dps-engagement.md` |
| 83 captured breath hit rows named 15 warriors, not the 19 ranged/healers with safe-goal rows | All-role emergency warning escape; log actual arrival and hits, not just safe destinations | `559-onyxia-dps-engagement.md`, `560-onyxia-deep-breath-round2.md` |
| 8-yard Fireball splash, crowded safe pockets, route budget spent near one failed point | Wider already-approved floor search, diverse pockets, peer/destination reservations, neighbour-count scoring | `562-onyxia-fireball-spread.md`, `564-onyxia-fireball-spread-wider.md` |
| Landing fear and eruption handling overstated | Retarget boss after control returns; do not claim predictive fissure avoidance without timing/path evidence | `onyxia-phase3-recovery-analysis.md` |

Historical shared PULL-LOG includes a 587 s kill with 5/38 dead (Oct 25), a
77.3% / 176 s wipe, and a never-engaged run (Oct 29). Those entries carry
uncertain batch labels; they establish mixed outcomes, not a measured effect
size for one patch. Earlier Oct 16 notes report 176 Tail Sweep hits and roughly
240 breath hits. Keep gear, roster, buffs, instance and exact code constant
when comparing a Forever port. No new live results are claimed here.

## Smallest-first implementation checklist

1. **Read-only encounter identity and telemetry.** Recognize map 249/10184,
   11262 and 12129; expose phase, victim/threat, real flight and spell start/hit,
   target validity, casts and deaths. Verify 70959 IDs, actual SpellInfo,
   taunt/fire immunity, Classic binding and binary/source match. Exit/reset
   must clear state. Gate: controlled fixture detects P1→P2→P3 and cleanup.
2. **Ground tank and useful single-target combat.** Adapt one landing owner,
   manual-order cancellation, contact hold and instant threat while moving.
   Port flank geometry with fresh native path/floor/egg/LOS validation; retain
   useful casts. Gate: no spare steals the boss by automation, no persistent
   tail/front cluster, actual positive damage after hold release. Test owner
   death, fear and Knock Away without inventing a taunt guarantee.
3. **Whelps and egg boundaries.** Separate add crew from boss tank, prefer
   endangered-healer rescue, return ranged to boss, and maintain P3 add duty.
   Gate: picked-up whelps stop killing healers; no path crosses egg caves;
   no boss pull initiated merely by scanning an idle creature.
4. **Air damage before elaborate spacing.** Port shared target selection,
   combat repair, cache invalidation and auto-repeat retargeting; preserve
   class damage and hunter minimum range. Gate: measured damage and sustained
   casts on airborne boss; no repeated assist-only loop or phantom engagement.
5. **Emergency Deep Breath for every role.** Rebuild all eight root chains and
   their DB target positions; consume the real 5 s warning; give checked escape
   precedence over conflicting movement. Retain fear/root/physical constraints
   and manual control. Gate: warning→route→arrival→hit correlation for every
   role and all directions, including a late warning and no-route case.
6. **Fireball spacing and proactive air positions.** Add bounded search,
   reservations and duty/LOS constraints after emergency movement works.
   Gate: fewer splash victims per cast, arrival coverage, continued DPS and
   healing; no claim that all 40 players always fit at the requested spacing.
7. **Landing, fear and eruption validation.** Reacquire after threat reset;
   integrate existing class fear tools using 22686. Inspect 1297001 geometry
   and timing independently before adding predictive hazard movement. Gate:
   tank facing recovers, add duty remains, no indefinite damage hold.
8. **One controlled full encounter, then regression comparison.** Same gear,
   buffs and composition; record phase duration, first/last damage, target
   owner, boss HP, deaths/cause, breath warning/arrival/hits and Fireball splash.
   Verify reset/cancellation and another encounter to catch shared-engine leaks.
   Stop and diagnose unsafe movement or repeated control failure before scaling.

Adapt policies inside the module through existing host interfaces. Shared
changes (movement, spell prechecks, target cache, damage callbacks) require
focused negative controls for non-Onyxia encounters. Keep the module optional;
do not copy Turtle core fields, GUID assumptions, spell packet layouts or
private phase timers directly into Forever. Reuse pure-policy tests where
possible, then test the actual host boundaries; a compiled fixture is not a
live raid pass.

# Slice 2, package R5: the review fix round

Branch `slice2/integ` (worktree `~/wt-slice2-integ`), build slot 1. T = Turtle core, F = pinned ForeverCore (6517af05). Server: the build VM's `~/forever-run` (container
`forever-world`, started through `world-gdb.sh`, stopped with `world.sh stop` after every session; several boots, a server of the other worker was never touched: I waited for it to stop each time). Times are VM UTC on
2026-11-19. Characters of this package: `Rfivepri`, `Rfivepritwo` (priests), `Rfivewar` (warrior), `Rfivepria`, `Rfivewara` (fresh priest and warrior trained with the final
rule), master `Tbbeta`. Raw tables: `SLICE2-R5-RANKS-priest.txt`, `SLICE2-R5-RANKS-warrior.txt`, `SLICE2-R5-RANKS-compare.txt` (this directory).

## Item 1 and 9: spell ranks from chain data, one table for every rank question

### What the data says (the brief's rule met the real data)
* (a) the core's own chain: `SpellMgr::LoadSpellRanks` (SpellMgr.cpp:837-905) builds `SpellInfo::ChainEntry` (SpellMgr.h:518-525) from the SkillLineAbility rows' `SupercedesSpell`.
* (b) the client's skill table: `SkillLineAbilityEntry::SupercedesSpell` (DB2Structure.h:3715-3738, field at :3724). The core reads it only in LoadSpellRanks, so (a) and (b) see the
  same rows. **Live data: the table has supersedes links for the warrior (20 chains, source `core`, e.g. Heroic Strike 78/284/285/1608/11564-11567/25286) and for NO priest class spell**
  (`.tb ranks priest` on the first boot: Smite 585/591/598/..., Lesser Heal 2050/2052/2053, Renew 139/6074/... all `single`). With rule (c) of the brief ("no chain: no rank") every priest
  spell would have no rank and the engine would cast rank 1 of everything (the R4 bug P1 again).
* **Decision (the brief did not foresee it): a third authoritative source (c)**, the trainers' required-ability links. `trainer_spell.ReqAbility1..3` (`Trainer::Spell::ReqAbility`,
  Trainer.h:53-64) names, for each spell a trainer teaches, the spell the player must know first; the core enforces it in `Trainer::GetSpellState` (Trainer.cpp:431-433). In the
  world database the priest's ranks are linked that way: 2052 requires 2050, 591 requires 585, 598 requires 591, 6074 requires 139 (trainer 1000376). It is game data, not a name
  or level guess. A prerequisite is not always a rank (Whirlwind requires Berserker Stance, Revenge requires Defensive Stance, Prayer of Spirit requires Divine Spirit), so a trainer
  link becomes a rank link only when both spells are the same ability by the spell data: same `SpellFamilyName`, same `SpellFamilyFlags` (128 bits), same effect shape (per effect:
  effect type, aura, both implicit targets) and the required one has the lower spell level. The icon is NOT compared (Heal changes icon between rank 2 and 3: 2055 -> 6063; the first
  version compared it and split the Heal chain). The name of a spell is never read. Priority: core chain, then SkillLineAbility links, then trainer links; a spell that is in a chain
  of an earlier source is not touched; a spell with two different successors or predecessors is ambiguous and none of its links is used (3 spells: hunter Counterattack 19306,
  20909, 1242634; none is a priest or warrior spell).
* (c') a spell with no chain from any source has no rank: `Rank` text empty, `SpellRankOf` 0, the engine treats it as a single spell. R4's by-name derivation (name + family + spell
  level, TbSpellEntry.cpp) is deleted; Renew 425268 is a chainless retail-era spell and no longer a rank above 10929.
* Boot line: `spell chains: 349 chains, 1631 spells (616 from the core's own chain, 0 from skill-line links, 1015 from trainer required-ability links; N trainer links are
  prerequisites, not ranks), 3 ambiguous, 0 broken, 0 rank differences`. (0 skill-line links: every SkillLineAbility link is already in the core's own chain; the core's rank numbers
  equal the positions: 0 differences.)

### Where it lives
`forever/src/compat/TbSpellEntry.cpp` `ChainTable` / `BuildChainTable` (built once at `InitSpellTable`, immutable, published before the index), API in `TbSpellEntry.h`
(`tbcompat::SpellRankOf`, `SpellFirstRank`, `SpellNextRank`, `SpellPrevRank`, `SpellLastRank`, `SpellsShareChain`, `SpellIsHigherRank`, `ForEachSpellChain`).

### Item 9: every rank question of the slice, routed through that one table
| Call site | Question | Answer from |
|---|---|---|
| `SpellIdValue.cpp:134` (`pSpellInfo->Rank[0]`, the highest "Rank N" of the known spells) | rank number | the mirror's `Rank` text, filled in `BuildSpellEntry` from `SpellRankOf` (TbSpellEntry.cpp) |
| `SpellIdValue.cpp:170` -> `IsHighRankOfSpellCompat` (TbCompat_V.h:183), the save-mana branch | is higher rank of | `tbcompat::SpellIsHigherRank`; NOT `SpellInfo::IsHighRankOf` (SpellInfo.cpp:4835-4843: core ChainEntry only, false for every spell without one) |
| `GenericSpellActions.cpp:962` (downrank candidates: name + family + icon) -> new `SameRankLineCompat` (TbCompat_V.h:190; T: always true, `ai/cmangos-compat-r4v.h:115`) | same chain | `tbcompat::SpellsShareChain`: a same-named retail variant is never offered as a lower rank |
| `.tb spell`, `.tb ranks`, `.tb train` pass 2 (TbScripts.cpp) | rank, chain, chain membership | the table |
No other call of the slice asks a rank question (grep of `GetFirstSpellInChain`, `GetNextSpellInChain`, `GetSpellRank`, `ChainEntry`, `IsRankOf`, `Rank[` over SLICE1.txt + SLICE2.txt and the compat headers). Not a
rank question and left alone: the mage "conjure food/water" pick by highest spell id (GenericSpellActions.cpp:217-236).

**`active` flag (item 9, second paragraph).** Turtle has the same state: `PlayerSpell::active` (Player.h:129), set false for a lower rank when a higher one is learned (Player.cpp:4287-4294,
:4440-4456) exactly like F (Player.cpp:3035-3045, :3196-3207). Turtle's `SpellIdValue::Calculate` does not read it either, and F's `Spell::CheckCast` does not test it for a bot (the
"known" test of `HasActiveSpell` is in the client-packet path, Player.cpp:33672). Both cores keep a superseded rank castable, and the save-mana branch chooses lower ranks on purpose, so filtering
inactive spells in the shared walk would change Turtle and break downranking: **no change**, answer written here. What F adds is only that its chain makes the core mark lower ranks inactive where
it has a chain (`.tb spells Rfivewar`: `78:Heroic Strike(inactive)`, `772:Rend(inactive)`); the engine's rank pick (highest "Rank N") never chooses them.

### Proof against the two data files
`.tb ranks priest|warrior` prints, for each class skill line (category 7 with a SkillRaceClassInfo row of the class), every chain as `id:rank:spell level[:t<trainer level>]` and every ability
in no chain (`single`, rank 0), and the trainer links kept out as prerequisites. Compared with `~/wt-forever-port/forever/data/waterys/rfc/priest-spells-1-18.csv` (14 rows) and
`warrior-spells-1-18.csv` (18 rows), `SLICE2-R5-RANKS-compare.txt`. Every row of the files is rank 1 (the lists end at level 18); the rule: the table agrees when the spell is rank 1 of a
chain or in no chain, and the level equals the level the class trainer teaches it (`t<n>`), else the spell level.
* priest: 14 rows, **12 agree**. Disagreements: `5019 Shoot` (not in the class skill lines: a general ability, the file lists it as starting spell), `21562 Prayer of Fortitude` (file: level 30,
  data: spell level 48, no trainer row; rank agrees: a single spell).
* warrior: 18 rows, **16 agree**. Disagreements, both in the level only: `20252 Intercept` (file 10, data: spell level 30, first rank of the core chain 20252/20616/20617), `7386 Sunder Armor`
  (file 12, data: spell level 10, first rank of the core chain 7386/7405/8380/...).
* Rank columns: 32 of 32 agree. Heal is one chain `2054, 2055, 6063, 6064` (4 ranks), Divine Spirit `14752, 14818, 14819, 27841`, Prayer of Spirit (27681) is in no chain (its two required
  abilities are prerequisites), Slam is `1240193, 1464, 8820, 11604, 11605` (the core's own chain: a level-20 rank 1 exists in this data).

### What `.tb train` teaches now
Pass 1 (the class trainers, `TeachAvailableSpellsFree`) is unchanged. Pass 2 (`TbScripts.cpp` `HandleTrain`, `ClassLineSpells`): from the class's own skill-line abilities, a spell of the class
family that fits class and race, is valid, has spell level <= the bot's level, AND is in a verified chain (every rank up to the level) or is chainless with an id below `kClassicSpellIdEnd`
= 100000. Justification from the data: in the class lines of priest and warrior the 1.12-era ids end at 28382 and the retail-era ones begin at 401859 (nothing between). A retail-era spell
that the TRAINER pass taught (the trainers' tables carry them) and that is in no chain is taken away again at the end (the core cannot be told to skip one trainer row); the command prints
both counts.
* Priest level 10 (14 spells of the class lines, from trainers 11 + lines 1 in the first pass): Smite 585/591, Lesser Heal 2050/2052/2053, Renew 139, Power Word: Shield 17, Power Word:
  Fortitude 1243, Shadow Word: Pain 589/594, Mind Blast 8092, Fade 586, Resurrection 2006, Divine Grace 1277370.
* Priest level 18 (27): the above plus Smite 598, Heal 2054, Renew 6074, Power Word: Shield 592/600, Power Word: Fortitude 1244, Shadow Word: Pain 970, Mind Blast 8102, Divine Grace 1277371,
  Inner Fire 588, Psychic Scream 8122, Cure Disease 528, Dispel Magic 527. Renew 425268 not learned; 30 (level 10) / 32 (level 18) retail-era chainless class-line spells were skipped, 0 taken away.
* Warrior level 10 (16 + 39 passive talent ranks): Battle Shout 6673, Battle Stance 2457, Bloodrage 2687, Charge 100, Death Wish 12328, Defensive Stance 71, Execute 20647, Hamstring 1715, Heroic Strike
  78/284, Last Stand 12975, Rend 772/6546, Sunder Armor 7386, Taunt 355, Thunder Clap 6343.
* Warrior level 18 (26): plus Battle Shout 5242, Demoralizing Shout 1160, Disarm 676, Heroic Strike 285, Mocking Blow 694, Overpower 7384, Revenge 6572, Shield Bash 72, Shield Block 2565, Thunder
  Clap 8198; the trainer pass taught two retail-era spells (1310185 Tactical Mastery, 1282735 Offensive State (DND)) that were taken away again.
* **Known leftover (as before R5, also what the core's own `.learn my spells` does):** the chainless rule admits TALENT spells (Death Wish, Last Stand, 39 passive "Improved ..." ranks) and the
  triggered effect spell `Execute 20647` (the engine casts that id: 42 casts in the live run, the real Execute chain 5308/20658/... starts at level 24). Excluding talents and triggered spells
  from `.tb train` needs a data rule that was not in the brief: item for the next slice.

What a trained priest casts (Rfivepria, level 18, final binary, the action log `[CAST_START]`): Smite(598) (rank 3), Lesser Heal(2053) (rank 3), Heal(2054), Renew(6074) (rank 2), Power Word:
Shield(600) (rank 3), Shadow Word: Pain(970) (rank 3), Mind Blast(8102) (rank 2), Power Word: Fortitude(1244). Rfivepri (level 10, trained before the effect-shape change): Lesser Heal(2053), Renew(139), Power Word: Shield(17).

## Item 2: area loot windows released
`LootReleaseAllCompat` (`TbCompat_I2.h:81`; T: the named window only, `ai/cmangos-compat-s2i.h:31`) used at `LootAction.cpp:788` after the money take: the named corpse first, then every other owner
in `GetAELootView()` through the same handler (`HandleLootReleaseOpcode`, owner guids copied first). `.tb where` now prints `loot: UNIT_FLAG_LOOTING set|clear, N window(s)` and `corpses within 60 yd`.
Live (binary of 00:15, which carries every R5 fix; the later builds changed only the chain rule and `.tb where`; three kills 2 s apart): the priest's windows `2 -> 3 -> 0`, flag set -> clear within 1 s of each take (`00:22:51 set, 2 window(s)`, `00:22:52 clear, 0`; `00:22:58 set, 3 windows`, `00:22:59 clear, 0`);
second run on the final binary `00:39:59 set, 2 window(s)` -> `00:40:09 clear, 0`; six more kills: one moment `set, 5 window(s)`, 8 s later clear. Money: Rfivewara 19 -> 37, Rfivepria 15 -> 33 copper for the same six kills (equal shares), the
master 0. Corpses: 3 corpses after the three kills at 00:39:58, one gone at 00:44:07 (about 249 s, the corpses of three kills lose 5-minute timers), back to the baseline of 1 by 00:45:04 (about 5 min): the
normal timer, not held by an open window.

## Item 3: incoming heals and the overheal cancel
Registered: value `incoming heals`, trigger `heal target full health`, strategy `heal interrupt` (it holds the trigger; CombatStrategy.cpp:246-251), added to the priest's combat engine in `SliceAiFactory.cpp`
(as the full factory does, AiFactory.cpp:1021). The classes are in files of the slice and compile (census 0); the core calls they make are reads (`GetCurrentSpell`, `m_targets.GetUnitTargetGUID`,
`Spell::CalculateDamage`, `ObjectAccessor::GetUnit`); generator and registry check current ((b) 233 -> 231). Boot: `S:+heal interrupt` in the priest's log.
Live (the 00:15 binary), TWO priests (Rfivepri, Rfivepritwo) on one hurt warrior (Rfivewar):
* patient at 60 % (162/271), twice: **one** Lesser Heal(2053) (00:26:05.457 by Rfivepritwo, 00:26:28.221 by Rfivepri, 0.19 s after the hurt), the other priest cast nothing (the incoming heal covers the wound);
* patient at 50 % (135/271) and 30 % (81/271): both priests cast, the second 1.0 to 1.5 s into the first one's bar (00:26:51.679 and 00:26:53.032): the pick credits an incoming heal only above the
  danger line and never vetoes urgent help (`HealingTriagePolicy.h:64`, "a heal in flight is not a guarantee of survival"); this is the legacy rule on both cores.
No overheal cancel fired in these runs (the second heal landed on a patient that still needed it).

## Items 4 to 8 and 10, 11
| # | Change | Evidence |
|---|---|---|
| 4 | `TbCompat_I2.h:313`: the in-tick refusal counts only when `refusal.spellId == spellData.SpellId` | drink `00:34:51.727` (j below) still works; no refusal mismatch case was forced (code reading only) |
| 5 | `ItemCountAfterUseCompat` (`TbCompat_I2.h:332`, T: `itemUsed->GetCount()`, `cmangos-compat-s2i.h:25`), used at `UseItemAction.cpp:658`: F reads `bot->GetItemCount(itemId)` (all stacks), no freed Item | code reading; the verbose `use` reply path was not driven live |
| 6 | `RequireBot` (`TbScripts.cpp:421`): every `.tb` command that changes a character (hurt, power, level, train, additem, use, rage, cast, castfail, castlog sink...) accepts only a module bot or a bot-session master; a real player's character gets a refusal text. `.tb castlog` copies (`BotSession::PeekCastResults`), never drains | a name that is not a bot answers the refusal; no real player was connected, so the "real player" text was not triggered live; the `castlog` peek was not driven live (code reading) |
| 7 | row `heal estimate` removed from `ai/playerbot/UnportedFeatures.h` and `UNPORTED.md` (73 features); whitespace fixed in `SLICE2-KEYS.md`, `SLICE2-R4-NOTES.md`; `git diff --check` clean | boot line count |
| 8 | see below | - |
| 10 | `EstimateHealOfEffectCompat` (TbCompat_Unported.h:354): expected value, the dice roll taken out (`CalcValue` gives the roll back in `variance`; value minus `CalcBaseValue * roll` leaves the average, SpellInfo.cpp:608-616), then `SpellHealingBonusDone`, then `SpellHealingBonusTaken` (0 when <= 0; Spell::EffectHeal, SpellEffects.cpp:1288). Turtle's estimate (`CalculateSpellDamage`, Object.cpp:4416-4458) rolls the dice with `irand` and has no taken step: the taken modifier is an addition on F only | downrank picks seen: `Lesser Heal(2053)`, `Heal(2054)` by wound |
| 11 | `CalculateSimpleValue` returns 0 beyond the third slot (`TbSpellEntry.h:116`), `IsPositiveEffect` false beyond the spell's effects (the core tests a 32-bit set); the mirror fill is bounded | grep: no other array of the mirror is indexed by an effect index outside loops below `MAX_EFFECT_INDEX` |

### Item 8 (for the next slice, no change made)
`MovementAction::ChaseTo`, `ai/playerbot/strategy/actions/MovementActions.cpp:3632-3640`: when the motion master is already `CHASE_MOTION_TYPE` towards the same target and the bot is not stopped, it refreshes
the target and returns `true` without `WaitForReach` or any next-check delay. The engine therefore runs the next tick at the earliest possible time. On Turtle the world loop runs about every 50 ms, so the line costs
20 ticks a second; on ForeverCore the loop runs every millisecond (R4: ~870 updates a second idle), so it spun the engine once per millisecond (R4 P3: 1,385 `reach party member to heal` lines in 4.6 s). The 50 ms
host step (`TbEngineBrain.cpp`, `kMinEngineStepMs`) hides it. Both-core fix to make: in that branch book a delay of the chase's own pace (`WaitForReach(distance)` for the remaining path, or one engine
interval) so the engine does not tick again until the chase can have changed something; on Turtle the delay equals the world tick, so behaviour there does not change.

## Live observations repeated (final binary)
| Obs | Result | Evidence |
|---|---|---|
| c | PASS | `tb hurt Tbbeta 40` 00:33:07.436 -> Power Word: Shield(600) 07.469, Lesser Heal/`Heal(2054)` start 08.973 (1.5 s), master 227/227 at +8 s |
| d | PASS | `tb attack Tbbeta` 00:33:27.201, `tb hurt Rfivewara 30` 28.730 -> PW:S(600) 29.230, Heal(2054) 30.737 (2.0 s after the hurt), warrior 432/498 at 38.3 |
| f | PASS | injected `reason 13` heard 23 ms after sending (`[CAST_FAILED_ASYNC] spell=598 reason=13 injected=1` 00:33:55.996), injected interrupt `[SPELL_FAILURE] spell=598 reason=63` 27 ms; natural: `Mind Blast(8102)` started 00:34:37.234 (1.5 s bar), `[CAST_FAILED_ASYNC] reason=13` at 38.736 (target died), next action `A:smite` evaluated 1 ms later |
| j | PASS | `tb power Rfivepria mana 10` 00:34:51.484, `A:drink - OK src=high mana` 51.727, sitting (`stand state: 1`), mana +36 per 1.2 s |
| k | PASS | six kills: Rfivewara +18, Rfivepria +18 copper, master 0 |
| a | PASS | boot with the engine, `.tb engine on` for four bots; no `ASSERT`/`SIGSEGV` in the last boot's `world.out` (0 hits) |

## Gates (final commit)
* Option ON `worldserver` (slot 1, `-DTORTOISEBOTS_SLICE1=1`): `CMAKE_RC=0 MAKE_RC=0` (builds of 2026-11-19: 227 s, 409 s, 769 s, 33 s, 24 s, 29 s as the tree grew).
* Option OFF module build (`-DTORTOISEBOTS_SLICE1=OFF ... tortoisebots`): `CMAKE_RC=0 MAKE_RC=0`, 6 objects (`TbBotMgr`, `TbBotSession`, `TbScripts`, `compat/TbItemPrototype`, `compat/TbSpellEntry`, `host/TbEngineBrain`).
* Census: `SLICE2.txt` 160 files, 0 error places; `SLICE1.txt` 133 files, 0. Generator `--check` current; registry check `(a)=0 (b)=231`.
* Turtle: `vm-build ~/wt-slice2-integ --wait` on 6614ef341 (all shared-file changes): `MAKE_RC=0`; `vm-test tb ~/wt-slice2-integ`: `RESULT: PASS rc=0` (sha 457b8bf3d, the commit after it changed only forever/).
* Test server stopped (`docker ps`: no `forever-world`; `forever-test1` untouched); free disk on the VM never below 6.5 GB.

## Unsure / left
1. Source (c) is a deviation from the brief (the data has no supercedes links for the priest). It is data, but the "same ability" test (family, flags, effect shape) is a judgement about which trainer links are ranks; 8 warrior and 2
   priest trainer links were rejected as prerequisites (all stance or Divine Spirit prerequisites, listed in the RANKS files).
2. `.tb train` still teaches talents and the triggered `Execute 20647` (see above).
3. Items 4, 5 and the real-player refusal of item 6 were not triggered live (code reading, same helpers as the live paths).
4. The overheal cancel did not fire in the live runs; only its registration and the incoming-heal skip (60 %) were observed.

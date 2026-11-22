# Class-quest spells, round 1 — notes

Ticket: bots learn the class spells real players get from class quests (Summon Imp first).

## What shipped

| File | Lines | What |
|---|---|---|
| `forever/src/host/TbClassQuestSpellsPure.h` | 236 | the 39-row reviewed allow-list, `RowsForClass`, `FindRow`, `CollectTaught`, `Decide`, `VerdictName`; no core header |
| `forever/src/host/TbClassQuestSpells.h` | 30 | `LoadConfig`, `Enabled`, `BuildIndex`, `Sync` |
| `forever/src/host/TbClassQuestSpells.cpp` | 234 | the index over `sObjectMgr->GetQuestTemplates()`, the row checks, the per-bot sync |
| `forever/src/TbScripts.cpp` | +17 | the config load, the world-start build, the quest reload, the login sync and a new `OnLevelChanged` |
| `forever/conf/tortoisebots.conf.dist` | +11 | `TortoiseBots.ClassQuestSpells = 0` with its comment block |
| `forever/tools/test_class_quest_spells.cpp` | 315 | the ten policy cases |
| `forever/tools/test_class_quest_spells_wiring.py` | 189 | the nine wiring pins, `--mutate` |

Learning is `Player::LearnSpell(id, false)` only: no cast, no `LearnQuestRewardedSpells`, no quest status rows, and
`TbBotMgr.cpp`, `AutoLearnSpellAction.*`, `TrainerAction.*` and `forever/SLICE*.txt` are untouched. No core file changed.

## How it works

- The table is the reviewed list: the allow-listed learned spell, its quest hint, the fallback level and the race rule.
- `BuildIndex` walks the quest store once: an open-to-all-classes or repeatable quest is skipped; per class the quest's
  reward spell, its `SPELL_EFFECT_LEARN_SPELL` trigger spells, its reward display spells and its source spell (with triggers)
  are collected, and only allow-listed ids are kept.
- Every table row is then checked: `unknown spell` (no `SpellInfo`), `no quest` (no class-masked quest of that class teaches it)
  or `hint mismatch` (no counted quest carries the hint and the hint spell does not teach the row). A failing row is disabled
  with one WARN line; one INFO line reports `<n> of 39 rows active, <q> quests`.
- `Sync` runs on every bot login and on every level change. Candidates are the indexed quests that teach the row; a race-fitting
  quest decides the gate, a class-wide row falls back to all of its class's quests when none fits the bot's race. The gate is the
  quest's classic min level, else the row's fallback. `IsSpellFitByClassAndRace` has the last word (the old engine's troll-priest
  bug). `HasSpell` first, so a second login or level event teaches and logs nothing.
- The index is a `std::shared_ptr<Index const>` swapped under a mutex; map threads only copy the pointer.

## Red test first

Case 1 was written against a stubbed `Decide` (always `NoQuest`) and failed on the VM:

```
FAIL case 1 (level-1 orc warlock gets 688): d.verdict == Q::Verdict::Teach (line 44)
```

## Pure test

`g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_class_quest_spells.cpp` on the build VM:

```
case 1 (level-1 orc warlock gets 688): PASS
case 2 (688 already known stays known): PASS
case 3 (warrior stances: below and at level 10): PASS
case 4 (troll warlock: ClassWide falls back to all class quests): PASS
case 5 (priest racials: race, fit, no race quest): PASS
case 6 (hunter pet kit from both quest lines): PASS
case 7 (CollectTaught: masks, repeatable, displays, source spell): PASS
case 8 (shaman totem: the bot's own race decides the level): PASS
case 9 (no quest; one jump 1 -> 30 teaches everything): PASS
case 10 (the 39-row table is sane): PASS
350 checks
all checks passed
```

The Imp quests' classic min level is 1 in the pinned core's data (the four quests carry RewardSpell 7763, display 688, MinLevel
1), so the gate for 688 is 1 and the acceptance reads "has 688 at 1". The build VM's synced core tree excludes `sql/`, so the SQL
rows were not re-read there; a live world whose data differs supplies its own gate through the index.

## Mutations (each applied once, then reverted)

| Mutation | Failing case(s) |
|---|---|
| M1 drop the level test | case 3, case 8 |
| M2 drop the known test | case 2 |
| M3 no ClassWide fallback | case 4 |
| M4 ignore reward display spells | case 6 |
| M5 accept classMask 0 | case 7 |
| M6 ignore `IsSpellFitByClassAndRace` | case 5 |
| M7 ignore the source spell | case 7 |

## Wiring test

`FOREVER_CORE=~/forever-build/src-4 python3 forever/tools/test_class_quest_spells_wiring.py` on the build VM:

```
.........
Ran 9 tests in 0.045s

OK
```

`--mutate` (each pin's own mutation applied, the pin must fail):

```
mutation login_sync: caught
mutation level_sync: caught
mutation config_load: caught
mutation world_init_builds: caught
mutation reload_rebuilds: caught
mutation conf_key: caught
mutation learn_only: caught
mutation off_build_has_it: caught
mutation core_pins: caught
9/9 mutations caught
```

## Gates

On the build VM, slot 4 (core `d3358c21dc`), through the slot lock:

| Gate | Result |
|---|---|
| module, `-DTORTOISEBOTS_SLICE1=ON` | `CMAKE_RC=0` `MAKE_RC=0` |
| module, slice OFF | `CMAKE_RC=0` `MAKE_RC=0` |
| full `worldserver` link, slice ON | `CMAKE_RC=0` `MAKE_RC=0` (`[2/2] Linking CXX executable bin/Release/bin/worldserver`) |
| pure test | 10 cases PASS, 350 checks |
| wiring test | 9 tests, OK |
| wiring `--mutate` | 9/9 mutations caught |
| `tbf-premerge` | stopped on the lead's waiver of the Turtle compile gate; it did not reach its slice1-ON/OFF steps |

## Left for the live queue (spec section 6)

- L1: a level-1 orc warlock bot made by `.tb create` and logged in holds `688:Summon Imp` in `.tb spells`, and the log has one
  `learned 688` line; a `tbf/main` image bot does not.
- L2: `.tb level <bot> 10` teaches 697; a relog adds no `learned` line.
- L3: the warrior 1 -> 10, hunter 9 -> 10 and troll-priest checks.
- L4: the level-1 warlock summons the Imp in its first fight.
- Startup line `class quest spells: 39 of 39 rows active`: a disabled row is a data finding for round 2, and the offline SQL
  check tool (round 2) makes a wrong row fail before it ships.

## Review round (Claude cold review + Codex adversarial)

- Fixed: a config reload that turned `TortoiseBots.ClassQuestSpells` on left no index until a restart; `OnConfigLoad(reload)`
  now builds the index (switch on) or drops it (switch off) at once.
- Fixed: `OnAfterReload` rebuilt only while the switch was on, so a quest reload with the switch off kept an index whose `Quest`
  pointers point into the replaced store (`ObjectMgr::LoadQuests` clears `_questTemplates`), read again after a re-enable. The
  rebuild now runs on every `reload all` / quest reload and `BuildIndex` drops the index when the switch is off.
- Wiring test: both pinned (`config_reload_builds` mutation added, 10/10 caught); `--help` names `--mutate`.
- Changed (lead, after review): the switch is OFF by default, following AGENTS.md "big new feature ships off by default"; test servers set it to 1.
- Table: the warlock and hunter rows and the warrior 1498/1719, shaman 1518/96/92468/94505, priest 5658, druid 6001/31/98362
  rows were checked against the pinned core's quest_template / quest_template_addon / quest_classic_level rows: reward and display
  spells and min levels agree with the table. Forever hunter quests 94013/94050 sit at QuestLevel 10, so their min level comes
  from `quest level - 3` (7); they count only for a race that can take them, or as the class-wide fallback.
- Live risk recorded: `IsSpellFitByClassAndRace` returns false when the client DB2 has no SkillRaceClassInfo row for a race and
  class; a Forever-race bot could then miss a class-wide row (the live check uses classic races).

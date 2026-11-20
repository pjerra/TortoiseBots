# Review f93 notes: trainer order, nearest-trainer gold, ignore-list defence

Branch: `fix/pr93-followup`. Topic: PR #93 follow-up on main.

## 1. Trainer order and nearest-trainer gold (town visit = `.bot downtime` + `TrainerAction`)

### 1a. Step order: train before optional buys — already right, with evidence

- The town visit (`.bot downtime`, `runtime/DowntimePolicy.h:59-77`
  `NextDowntimeErrand`) runs Trainer first, then Vendor (junk sale only),
  then Repair. The visit arms only `rpg` + `rpg maintenance` (no vendor
  strategy, so no buy triggers) and sells junk via a direct one-pass sale
  (`DowntimeService.cpp:418-425, 739-745`). There are no vendor buys in the
  visit, so training cannot be starved by them. No change.
- Pool path priorities agree: sell 1.100 > repair 1.095 > train 1.080 > buy
  1.030 (`RpgStrategy.cpp:138-185`). Every vendor buy is gated per item on
  `free money for <bucket>`, which reserves the full train cost: the savings
  order (`BudgetValues.h:67`) puts spells before travel/consumables/gear, and
  `FreeMoneyFor` subtracts every earlier bucket (`BudgetValues.cpp:278-296`),
  so buys never eat training gold. Two deliberate exceptions, not changed:
  hunter ammo (`NeedMoneyFor::ammo` precedes spells — ammo is combat-critical
  and self-correcting) and repair (full purse; survival-first, and the
  trainer-first order protects training in the visit).
- After the vendor leg the next leg re-probes the trainer need
  (`Update`: `tried=[Vendor]` → `ArmNextErrand` → sale proceeds fund the
  trainer leg), so a broke bot sells first and trains after. No change.

### 1b. Several affordable ranks: learned in listing order — FIXED

- `TrainerAction::Iterate` iterated the trainer rows in listing (spell-map /
  DB) order with no sorting. With gold for only some ranks the visit could
  learn an expensive high rank and leave a cheap foundational rank unlearned
  (then log `train-refused` on the cheap one), or learn a rank ahead of its
  own prerequisite chain.
- Fix: pure `ai::TrainerVisitRank` + `TrainerVisitRankLess` +
  `SortTrainerVisitRanks` in `ai/playerbot/policy/town/TownErrandPolicy.h`
  (lowest required level, then cheapest, then spell id); `Iterate` sorts the
  collected rows with it before learning (`TrainerAction.cpp:88-98`).
- Test: `forever/tools/test_w2_town.cpp` section 8.
  FAIL before: `FAIL old_logic_demo.cpp:12: listing.front().second == 238`
  (3 checks, 3 failed). PASS after: `PASS: test_w2_town (8 checks, 0 failed)`.

### 1c. Gold check for the nearest reachable trainer — FIXED (downtime)

- `TrainerErrandNeeded` (`DowntimeService.cpp`) probed the cheapest due rank
  across the GLOBAL `trainable spells` list at list price, while
  `FindErrandDestination` walks to the NEAREST reachable class trainer.
  Trainer offerings differ per entry (rank subsets, level gates), so the
  probe could admit a walk to a trainer teaching nothing affordable — the
  arrival then idles to the 90 s visit timeout (`Update`: `done` stays false)
  having learned nothing.
- Fix: pure `TortoiseBots::DowntimeTrainerRank` +
  `DowntimeTrainerEntryTeachesAffordable` in `runtime/DowntimePolicy.h`; the
  Trainer branch of `FindErrandDestination` skips destinations whose entry
  teaches no due rank the purse covers, so the picked visit is the nearest
  reachable trainer that can actually teach something (list price — the
  visit can only be cheaper). The global probe stays as the cheap pre-filter.
- Test: new `forever/tools/test_downtime_policy.cpp` (order pin + per-trainer
  affordability + start gate + window cap).
  FAIL before: `FAIL old_logic_demo.cpp:21: !(oldAdmits && nearestCheapest > purse)`
  (3 checks, 3 failed). PASS after:
  `PASS: test_downtime_policy (5 checks, 0 failed)`.
- Not changed: the pool travel need (`ShouldTravelNamedValue`, same global
  cheapest pattern) — the nearby-service per-trainer check
  (`RpgTrainTrigger::TeachesAffordableSpell` for the exact NPC) is already
  exact, and reworking the hot travel need is out of scope for this visit fix.

## 2. Ignore list never ignoring a mob that attacks the party — FIXED

- Every ignore check dropped the target unconditionally: `IsValid`
  (`PossibleAttackTargetsValue.cpp:310`, the funnel for dps/tank/assist/pull/
  defence tournaments, explicit targets and only-marks-left), grind pulls
  (`GrindTargetValue.cpp:350`), runner chase (`DpsTargetValue.cpp:480`),
  focus/assist/area marks (`RtiTargetValue.h:90`) and the CC splash guard
  (`TargetValue.cpp:306`). A listed mob beating the party could never be
  targeted back. (The raw `attackers` list never filtered the list — the
  drop happened one step later in every pick.)
- Fix: pure `ai::IgnoreMobYieldsToDefence` in
  `ai/playerbot/policy/combat/IgnoreMobPolicy.h` (victim + bot + party guids
  + threat flag), read by one engine helper
  `PossibleAttackTargetsValue::IgnoredMobAttacksParty` (victim, group
  membership, threat on bot/members), applied at all five drop sites. A
  listed mob with no violence aimed at the party stays ignored.
- Left alone on purpose: `.tb cc` still refuses a listed mob (`TbCc.cpp:214`)
  — a refused command, not target selection or splash; the `rti cc` mark path
  was and stays exempt, and an explicit attack/pull/assist/focus order still
  takes the mob off the list first (`TbOrders.cpp:388-396,542`).
- Test: `forever/tools/test_ignore13.cpp` `TestDefenceException`.
  FAIL before: `FAIL old_logic_demo.cpp:16: !(oldIgnored && attackingParty)`
  (3 checks, 3 failed). PASS after: `62 checks, 0 failed`.

## 3. The lows

Skipped (no small, clear bugs found in the touched code). Two deliberate
non-changes recorded above: hunter-ammo bucket precedes the spell reserve,
and repair spends from the full purse.

## Gates run

- Pure: `test_w2_town` 8/8, `test_ignore13` 62/62, `test_downtime_policy` 5/5.
- `slice_registry_check.py` (0, 228 later), `slice_gen_context.py --check`
  (up to date), `test_ignore13_wiring.py` OK, `test_w2_town_wiring.py` OK,
  `test_w1_wiring.py` OK, `git diff --check` clean.
- `tbf-build.sh <wt> tortoisebots`: BUILD OK (final pass re-run after the
  include fix; first pass BUILD OK tortoisebots ON 29b37c626).
- `turtle-compile-check <wt> origin/main`: COMPILE OK at 060c7d1e (the first
  run caught a missing `SharedValueContext.h` include in DowntimeService.cpp;
  shared ai/ files touched: TownErrandPolicy.h, TrainerAction.cpp,
  IgnoreMobPolicy.h, PossibleAttackTargetsValue.h/.cpp, GrindTargetValue.cpp,
  DpsTargetValue.cpp, RtiTargetValue.h, TargetValue.cpp).

## Live checks (lead's combined run)
- Order/ranks: poor bot with two due ranks and gold for the cheaper only,
  `.tb order Testbot train #o4` → learns the lower-level rank
  (`[W2] train ... spell=<low>`), purse kept for the rest; `.tb state`
  shows the new spell.
- Nearest trainer: alt near a start-zone trainer that teaches nothing it can
  afford, `.bot downtime Alty` → no 90 s idle at that trainer; the window
  tries the next reachable trainer or reports nothing reachable.
- Defence: `.tb ignore Testbot add <guid32hex> #t1` → ok; pull that mob onto
  the party → bots target and defend (previously untargetable); mob left
  alone while it fights nobody. `.tb cc` on it stays `ignored`.

# Slice 8, package G8: crowd control inside the self-led run, plus the M8 follow-ups

Branch `s8/g` (worktree `~/wt-s8-g`), build slot 4, off `s8/m` with `tbf/main` (feab6c882, the self-led run) merged: no conflict. `SLICE5.txt` did not change on main (176 paths); `SLICE8.txt` is SLICE5 + the 22 class files + `CancelChannelAction.cpp` = 199 paths, unchanged. Core read only (pin 6517af05), zero core changes. The Turtle policy header `runtime/DungeonRunPolicy.h` is untouched (the R4 rule: `test_dungeon_run_policy` unchanged). Scope change from the owner during the work: no full clear; the live check is the shrunk one (section 8).

## 1. Run crowd control (items 1 and the spec's "G8 adds")
* **Facts** (`TbRunFacts.cpp`, `TbRunFactsPure.h`): `MemberFact::polymorph` for a mage that knows a rank of Polymorph (`kPolymorphSpells`), `cc` = 0 and `canSap` = false for every member (the run's crowd control on this host is the mage's Polymorph only: the other families have no class row in a kit, so reading them as available would order a cast nobody casts; no Sap is ever ordered, it needs a real player master); `UnitFact::polymorphed` (core `Unit::IsPolymorphed` or a Polymorph rank aura), `ccRemainMs` (soonest aura duration), `ccImmune` (Polymorph's mechanic against `Unit::GetMechanicImmunityMask`, which holds the creature template's immunities), on step units, engaged units and party attackers. `healer`, `tank`, `level`, `caster`, `creatureType`, `normalRank` and the raid icons were read before.
* **Verbs** (`TbRunOrdersPure.h`, `TbRunOrders.cpp`): `cc <mark> <handle> <bot> [family]` and `cc clear <bot> [<mark> <handle>]` are parsed (they were `unsupported`) and carried out synchronously by `h8::ApplyCcOrder` / `ClearCcOrder` (the same calls `.tb cc` makes: icon on the group, `rti cc`, `run cc family`, read back and undone when it does not hold). Nothing is queued, so the answer is final. Host refusal codes map to the order words (`CcAnswerCode`: invalid-target, invalid, not-bound, no-engine, failed). A dead bot is refused `unavailable`.
* **Tick** (`TbRun.cpp`): `DungeonRun::CcOrderTaken(run, order, accepted)` after every order (the Turtle glue does the same: a refused automatic order gives its unit, bot and mark back); a `cc` event in `.tb run events`; at the run's end (`Finish`) `ClearRunCc`: each icon of `Run::ccIcons` only where it still holds that unit, every bot that held a run mark gets `rti cc` / `run cc family` back to none (before `ForgetRun`, so the handles resolve).
* **Bare clear** (`TbCc.cpp`): `.tb cc <bot> clear` with no mark and no unit reads the bot's own `rti cc` before the reset and removes that icon too (reply `mark= icon_removed=`). With mark and unit the old rule stands (only while the icon still holds that unit).
* Tests: `test_run_orders` 162 (the two strings that were `unsupported` now parse; the policy's own `CcOrderText` / `CcClearOrder` parse back), `test_run_gather` (the policy's own `PickCcOrderPlan` over a plan with the RFC cc lists: no pick without the member fact, `cc moon 2 Mmage` with it, never the kill_first Shaman 11319 or the pull target, nothing for an immune or ill-fitting unit), `test_g8_wiring.py`.

## 2. `reach cc`
Added to `SLICE8_KEYS` (action), generator rerun: 19/54/86/173 (`--check` up to date), registry check `(a)=0 (b)=212` as before. The mage's approach to a CC mark out of spell range now has a creator (`ReachCcAction`, range minus `CcCastRangeMargin`). `use trinket` and `current mount speed::self target`, checked: `use trinket` is an item cast (on the "left unregistered, does less" list: apply oil, throw grenade, thistle tea, ...), and `current mount speed` is read only by `FindPartyMember` for an out-of-group buff search (`allowBufOutOfGroupPlayers`, never on in a party): both stay unregistered, each does less.

## 3. Mana filter
`PlayerWithoutAuraPredicate` already skipped a non-mana unit by `GetPowerType() != POWER_MANA`, yet 48 casts were refused: on ForeverCore the display power is a field, so the test alone let the rage and energy users through. The rule is now one pure function (`ai::BuffTargetFits(spell, displayIsMana, maxMana)` in `GroupBuffPolicy.h`: the four mana buffs need display power mana AND a mana pool), used by the party search and by the group-buff quorum count (`CountGroupMembersMissingBoth`, which counted warriors as missing Arcane Brilliance). `CAST_REFUSED` lines now name the target (`target=`), so a remaining refusal can be traced. Test: `test_group_buff_policy` 75 checks (warrior, rogue, display-mana-without-pool refused; priest and mage fit; an ordinary buff fits all). Legacy-neutral: on Turtle the rule is the same test with the pool added.

## 4. Rogue kit
`cc` stays in the rogue's combat kit (`SliceAiFactory.cpp`, comment header fixed), recorded in the spec ("Kit decision (G8)": Kick and Kick on enemy healer live in `RogueCcStrategy`; Sap rows stay silent without a real player master, D3/D4 stand). Pinned by `test_g8_wiring.py`.

## 5. The regroup that did not hold (the two M8 pauses)
Both pauses were `regroup: the party did not gather within 60 s (a bot is 72.5 / 86.4 yd from the lead bot)` after a tank death and revive, with the policy's `summon` refused `unsupported` (D3) four times. **Choice: a gather that tolerates the revive, no summon.** A summon would be a teleport of a bot (`h4::TeleportNextTo` exists, a GM power the spec's D3 withheld on purpose); the gather change moves nothing, only waits, and leaves the policy as it is. Meeting stone / GM summon command were looked at and not used: the ritual and `Player::SendSummonRequestFrom` need the target's client to answer a summon request (the module's incoming packet side is not ported), and the GM way is the same teleport.
* `TbRunGatherPure.h` + `TbRun.cpp`: right after `DungeonRun::Tick`, a pause whose note is the regroup gather's is answered by the policy's own checked resume (`DungeonRun::Resume`: a fresh regroup and a fresh gather window sized for the farthest bot), so `Observe` never sees a pause. Bounds, per plan step: at most 3 extensions; the first is free (a revived priest drinks first, a far bot walks a long path), the next ones need the farthest member at least 8 yd closer than at the previous pause; never when a member is 400+ yd away (another map, not in the world) or the lead is dead. Events `gather-wait` and `gather-stop` (with the numbers) in `.tb run events`. Every other pause is untouched.
* Test: `test_run_gather` (the note text is the policy's own, a change there fails the test; first free, not closing = stands, closing = extends, cap, new step resets, lost bot, dead lead, other pause silent), `test_g8_wiring.py` (the call order: after Tick, before the executor and Observe).

## 6. Talent CSV
There is no loader in the code (`.tb learn` is typed by the live setup; H8 notes: "nothing reads those files"). `forever/tools/talent_rows.py` is the loader of the setup: it reads a per-rank id when the data has one (a talent's rows carry distinct spell ids, or a `rank` above 1 with its own id) and otherwise reports. `talents-level18.csv` repeats ONE spell id (rank 1) on every point of a talent, so no per-rank id exists in it: **27 of the 45 points are UNLEARNABLE** with the data as it is (the rest are each talent's first point). Data owner: Talent.db2 `SpellRank[0..8]` (ForeverCore `DB2Structure.h` TalentEntry) holds the per-rank spell ids; the CSV needs a spell id per rank. Rogue 13733 (talent 202) is `unknown` to the core (M8): its row needs a fresh id too. Per role (`python3 forever/tools/talent_rows.py forever/data/waterys/rfc/talents-level18.csv`):

| role | learnable ids (first points) | points the data cannot teach |
|---|---|---|
| protection warrior | 12301 12298 12297 12299 | 5 (talent 142 x1, 1601 x2, 138 x1, 140 x1) |
| arms warrior | 12295 16462 12834 | 6 (talent 641 x4, 130 x1, 121 x1) |
| priest | 14908 14913 18530 27789 | 5 (406 x2, 410 x1, 1181 x1, 1635 x1) |
| mage | 11070 29438 11151 | 6 (37 x4, 1649 x2) |
| rogue | 13733 (unknown) 13705 13743 13715 | 5 (202 x2, 181 x1, 222 x1, 221 x1) |

`--selftest` covers: repeated id reported, per-rank ids all learnable, ranks follow points, a second talent is its own group.

## 7. Gates (final tree)
(filled at the end: census, builds, generator, registry, tests)

## 8. Live check (shrunk by the owner: no full clear)
(filled after the boot)

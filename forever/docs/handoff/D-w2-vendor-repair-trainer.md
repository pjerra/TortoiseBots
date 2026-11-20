# D. W2: vendor, repair, trainer (+ mail read)

**Waits for W1 "grind and live"** (our branch `world/w1`, in progress): W2's autonomous half runs on W1's masterless
`free` bots. Can run in parallel with E (W3).

## Done looks like

A bot sells its grey items, buys food/water/ammo/reagents for its level, repairs, learns its new trainer ranks (paying),
and takes money and items out of its mailbox, through the core's own vendor, repair, trainer and mail code.

**Live acceptance** (world-life plan, W2 row): a bot with full bags and red gear, first ordered, then autonomous, at
Goldshire: vendor, smith, class trainer; money, durability, bag slots and known spells before/after in the log. Add one mail
with money and an item sent to the bot: read and taken. Repeat once for a Horde bot at Razor Hill.

## Scope and size (world-life plan)

"sell greys, buy food/ammo/reagents, repair, train new ranks (paying), mailbox read", 3 worker-days (code), biggest risk
"`Trainer` and vendor conditions (`PlayerConditionId`) refuse silently". Mail **sending** is out (guild orders, later).

Legacy files (lines / census places): `MailAction.cpp` 689/30 (read part only), `BuyAction.cpp` 439/16,
`CheckMailAction.cpp` 304/33, `TrainerValues.cpp` 302/35, `TrainerAction.cpp` 301/32, `SellAction.cpp` 331/4,
`RepairAllAction.cpp` 80/7, `VendorValues.cpp` (all under `ai/playerbot/strategy/`). The whole area is 27 files, 4,392
lines, 221 places; take what the registry closure needs (`forever/tools/slice_registry_check.py`), leave the rest unregistered.

## ForeverCore differences and APIs

From the world-life plan:
- `VendorItem` has `ExtendedCost`, `Type`, `PlayerConditionId`, `BonusListIDs` (`CreatureData.h:692-700`).
- `Player::BuyItemFromVendorSlot(guid, slot, item, count, bag, slot)` (`Player.h:1699`); buy in chunks.
- `SellItemToVendor` (`Player.h:1701`, with `CanSellItemToVendor` `:1700`, both return `Optional<SellResult>`).
- `DurabilityRepairAll(takeCost, discount, guildBank)` (`Player.h:2455`).
- The `Trainer` class replaces `TrainerSpellData` (33 places); `MasterPlayer` (25 places) does not exist.

Trainers (`forever/data/waterys/core-howto/trainers.md`, read against the pin): `sObjectMgr->GetTrainer(id)`
(`ObjectMgr.h:1627`); `Trainer::TeachSpell(npc, player, spellId)` (`Trainer.h:72`) is the authoritative gate (state via
`GetSpellState`, reputation discount, refuses on short money, charges with `ModifyMoney`); `SendSpells` (`:71`) is exposed
"used by Playerbot to drive bulk-train". Call `TeachSpell` per spell; trainer contents come from `trainer` + `trainer_spell`.

Mail read (lines read at the pin for this pack): mails are in memory, `Player::GetMails()` (`Player.h:1992`), `GetMail(id)`
(`:1990`); taking goes through `WorldSession::HandleMailTakeMoney` / `HandleMailTakeItem` / `HandleMailMarkAsRead`
(`WorldSession.h:1612-1614`) with typed `WorldPackets::Mail::*` structs. No characters-DB query needed.

NPC checks: `Player::GetNPCIfCanInteractWith(guid, npcFlags)` (`Player.h:1300`) before every vendor/repair/trainer call.

## Data facts (DB sweep of the world DB at the pin)

- `npc_vendor` 54,141 rows over 3,139 NPCs; `trainer` 716, `trainer_spell` 36,660, `creature_trainer` 1,363 (Type 0 class,
  2 tradeskill, 1 pet, 3 other). **Trainer truth is `creature_trainer`, not npcflag**; spawn-level `npcflag` is 0 everywhere.
- **Spawns carry no zone/area id** (2 of 85,076 rows): any town/NPC index must be built from coordinates.
- Every start town has vendors and a repairer. Class trainers: Goldshire 7, Kharanos 8, Razor Hill 6, Brill 7; Sentinel
  Hill, Thelsamar, Crossroads and the Sepulcher train no class (the bot must go to the capital = W4 travel).
- Ready lists in the repo: `forever/data/waterys/service-points/` (`repair-vendors.csv`, `reagent-ammo-vendors.csv`,
  `class-trainers-*.csv`, `mailboxes.csv`), `supplies-by-level.csv`, `trainer-chains-check.csv`.

## UNPORTED rows

The W2 files are not in any slice yet, so most of the gap is census places, not rows. It shrinks
`incoming: trade and item errors` (buy errors) if you feed buy results; `skills` only if the trainer path needs the
`SetSkill` semantics. Every part you leave out gets a row (failure value = "did not happen").

## Decisions already made

- Blizzlike: the bot pays (trainer, repair, vendor); no free items, no `.tb train` shortcut in the real path (`.tb train` stays a test helper).
- No teleport to a town: the only bot teleport is W1's hard-stuck ghost rescue.
- Direct server calls over packets where a public call exists (`BuyItemFromVendorSlot`, `SellItemToVendor`,
  `DurabilityRepairAll`, `TeachSpell`).
- Ideas (read, write our own): the ForeverCore team's per-map `RepairVendorIndex` / `TownNpcIndex` built once, and
  `TownErrand` spot caps (no 48 bots in one inn).

## Rules that bite here

- A refused buy/teach (`PlayerConditionId`, `ExtendedCost`, reputation, money) is logged with the reason and the bot does
  less; it never retries forever and never reports a buy it did not see in the bag.
- Never sell quest items, soulbound gear it wears, or items W6a says are upgrades (until W6a: greys only).
- Any world query (vendor/trainer lists not in the core's caches) goes through `WorldQueryCompat`; prefer the core's
  loaded caches (`GetTrainer`, the creature's vendor list).
- Re-find the NPC by guid each tick; `FindMap()` on a moving bot.

## Tests (failing case first)

- **Failing case:** a vendor item gated by `PlayerConditionId` (or `ExtendedCost`) is offered by the legacy picker and
  "bought" without a bag change; must be skipped with a logged reason.
- Pure: sell-list choice (greys only, never quest/bound-worn), buy list by level and class from `supplies-by-level.csv`
  rules, repair threshold, which trainer ranks are due and affordable.
- Wiring: every vendor/trainer/mail call goes through the host seam (one `tbhost::` file), and no raw `WorldDatabase.Query`.
- `.tb state` keys: `money=`, `dur=`, `bagfree=` (exist), plus `mail=<unread>` and a log line per errand
  (`[W2] sell|buy|repair|train|mail bot= npc= copper_before= copper_after=`).

## Open for you to settle

How far an autonomous W2 bot walks to an NPC before W4 travel exists. Suggestion: only NPCs on a reachable path within the
bot's grind area, else skip the errand (less).

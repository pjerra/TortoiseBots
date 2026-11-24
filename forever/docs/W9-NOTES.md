# W9 Notes: Guilds and Guild Orders — Foundation (`E-w9-guilds-and-orders`)

Branch: `feat/w9-guilds`.

## What was done

1. **Pure header census (`forever/SLICE-W9.txt`, `forever/src/slice/SliceGuildOrderHeaders.cpp`)**:
   - `forever/SLICE-W9.txt` extends `forever/SLICE-W1.txt` with `forever/src/slice/SliceGuildOrderHeaders.cpp`, which includes all 41 reusable pure guild/order headers (`runtime/GuildOrder*Policy.h`, `GuildOrderContract.h`, `GuildOrderReagentTree.h`, `GuildOrderProtectedItems.h`, `GuildOrder*Queue.h`, `QueuedOrderPolicy.h`, `WorldThreadQueue.h`, `ai/playerbot/policy/guild/{GuildFoundingPolicy,EnchantGatherPolicy,EnchantTradePolicy}.h`).
   - All 41 headers compile cleanly on ForeverCore without modification; all 33 existing `tools/test_guild_order_*.cpp` unit tests pass.

2. **Host seam (`forever/src/host/TbHostGuild.h`, `forever/src/host/TbHostGuild.cpp`, `tbhost::guild`)**:
   - **Founding (blizzlike)**: `RequiredPetitionSigns`, `BuyPetition` (`HandlePetitionBuy`), `SignPetition` (`HandleSignPetition`), `TurnInPetition` (`HandleTurnInPetition`).
   - **Guild & rank rights**: `GetGuildId`, `GetLeaderGuid`, `GetMemberRankId`, `HasAnyRankRight` (`Guild::HasAnyRankRight` + `Member::GetRankId`), `CanEditOfficerNotes` (`GR_RIGHT_EOFFNOTE`), `IsMemberOnline`.
   - **Membership & ranks**: `InviteByName` (`HandleGuildInviteByName`), `AcceptInvite` (`HandleGuildAcceptInvite`), `SetMemberRank` (`HandleGuildAssignRank`).
   - **Guild bank (DEPOSIT only, decision 6)**: `DepositMoney` (`HandleGuildBankDepositMoney`, confirms money left bags), `DepositItem` (`HandleAutoGuildBankItem`, confirms item count left bags; never withdraws or reads private bank tabs).
   - **Mail SEND**: `SendMail` (`HandleSendMail` with `MAIL_STATIONERY_DEFAULT`, core postage/delay; never `MailDraft`).

3. **Guarded guild order store (`forever/sql/characters/2026_11_20_01_tb_guild_orders.sql`, `forever/src/host/TbHostGuildOrders.{h,cpp}`)**:
   - `forever/sql/characters/2026_11_20_01_tb_guild_orders.sql`: idempotent `CREATE TABLE IF NOT EXISTS` for `tortoise_bots_guild_order` matching all 44 columns of `StoreColumns::kSelect`.
   - `LoadOrders` (called at `OnWorldInitialized`) checks `CharacterSchemaHasCompat` + `WorldQueryNamesCompat` + `CharacterQueryGuardedCompat`; if the table or any column is missing, it logs one warning and disables persistence (does less, never aborts).
   - Writes (`GiveOrder`, `CancelOrder`) validate schema first and execute in `CharacterDatabase.BeginTransaction()` / `CommitTransaction(trans)` using `CharacterDatabase.EscapeString` (prepared statements via `CharacterDatabaseStatements` would require core enum edits, which are forbidden by zero-core-changes).

4. **`.tb guildorder` command & `.tb state` (`forever/src/TbScripts.cpp`, `forever/src/host/TbH3.h`, `forever/src/host/TbH3Off.cpp`, `forever/src/host/TbPartyState.cpp`)**:
   - `.tb guildorder create <bot> <item_id> <amount> [farm|craft|buy] [#token]` (checks `CanEditOfficerNotes` when issued by a player)
   - `.tb guildorder list [<bot|guild_id>] [#token]`
   - `.tb guildorder cancel <bot> [#token]` (checks `CanEditOfficerNotes` when issued by a player)
   - `.tb guildorder status <bot> [#token]`
   - Grammar v2 replies (`Reply` / `Rec` / `Fail`); option-OFF stub in `TbH3Off.cpp` (`error code=engine-not-built`).
   - `.tb state` appends `order=<id>:<have>/<want>` when an order exists for the bot.

5. **Fulfilment seams (Step 5 — "cannot" stubs for W7/W8/W4)**:
   - `IGatherCraftProvider` (`GatherCraft()` / `SetGatherCraftProvider`) — default `NullGatherCraftProvider` returns `false` (`w7-not-wired`).
   - `IAuctionBuyProvider` (`AuctionBuy()` / `SetAuctionBuyProvider`) — default `NullAuctionBuyProvider` returns `false` (`w8-not-wired`).
   - `IDeliveryProvider` (`Delivery()` / `SetDeliveryProvider`) — default `NullDeliveryProvider` returns `false` (`w4-travel-not-wired`).

## Gate results

- **Census (`forever/SLICE-W9.txt`)**:
  `files 287  compile clean 287  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0`
- **`worldserver` ON**:
  `BUILD OK worldserver ON`
- **`worldserver` OFF**:
  `BUILD OK worldserver OFF`
- **Pure C++ tests (`tools/test_guild_order_*.cpp`)**:
  `CPP tests: pass=33 fail=0`
- **No-direct-queries check (`tools/test_forever_no_direct_queries.py`)**:
  `Ran 5 tests in 0.038s -> OK`
- **Wiring test (`forever/tools/test_w9_guild_wiring.py`)**:
  `Ran 5 tests in 0.001s -> OK`
  `--mutate` -> `MUTATE FAILED AS EXPECTED`
- **Registry & context checks**:
  `summary: (a)=0  (b)=228 keys to register or stub later`
  `SliceAiObjectContext.cpp is up to date`

## Live test steps (for combined lead run)

Apply `forever/sql/characters/2026_11_20_01_tb_guild_orders.sql` to the `characters` DB first. With a player and a bot in the same guild (player has `GR_RIGHT_EOFFNOTE`):
```
tb guildorder create <bot> 4234 10 craft #g1
tb guildorder status <bot> #g2
tb guildorder list <bot> #g3
tb state <bot> #s1
tb guildorder cancel <bot> #g4
```
Expected:
- `create` returns `guildorder op=create bot=<bot> guild=<id> item=4234 want=10 type=craft state=working #g1` and `guildorder-end ... #g1`.
- `status` returns `guildorder op=status bot=<bot> item=4234 have=0 want=10 type=craft state=working reason=- #g2`.
- `tb state` includes `order=4234:0/10`.
- A player without `GR_RIGHT_EOFFNOTE` receives `error code=no-rank-right cmd=guildorder ...`.

## What is left for after W7 / W8 / W4 merge

- Wire `IGatherCraftProvider` to W7's gather/craft actions and profession checks.
- Wire `IAuctionBuyProvider` to W8's auction house buy seam.
- Wire `IDeliveryProvider` to W4's travel/pathing + `tbhost::guild::DepositItem` (with fallback to `tbhost::guild::SendMail` when bank tab right is denied).
- Port the autonomous guild order tick/actions (`Guild*Action.cpp`, `GuildValues.cpp`, `GuildStrategy`, `GuildTriggers`) and add `W9_KEYS` once those legacy actions are added to `SLICE-W9.txt`.

## Live result (board ticket #150, 2026-11-22, Waterys Island Test: core b441bf74, main d0d13b9f6)

Run by Audrey in game (Asdasdasd, Horde GM) with host bots; state read from the server log and the database before/after
(`out/w3-liveproof-150.md` in the worker notes). Setup note: a bot needs `.tb master <bot> <player>` AND `.tb party <player> <bot>`
(a bot outside the commander's party loses the binding on the next tick, so no engine starts).

- **Guild order to the guild bank: PASS.** Guild "Waterys Orders" (bot Guild Master Homeleec, Audrey Officer), bank tab 0;
  `.tb guildorder create Homeleec 4234 5 farm` + `work` next to the Orgrimmar Guild Vault -> `state=done`, DB bank tab 0 slot 0 = 5 Heavy Leather.
- **Guild order by mail: PASS.** Same order away from the vault, next to a mailbox -> `state=mailed`; DB mail "Guild Order" from
  Homeleec to Asdasdasd with 5 Heavy Leather; bot copper -30 (postage).
- Needs `forever/sql/characters/2026_11_20_01_tb_guild_orders.sql` applied (the test server lacked it). A new guild has no bank tab
  until one is bought (here inserted while the world was stopped).

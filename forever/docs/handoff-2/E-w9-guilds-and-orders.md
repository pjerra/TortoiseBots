# E. W9: guilds and guild orders

**Comes last of the five, after W7 and W8.** Needs W2's mail (read is in PR #93; sending mail is new here), W7 (gather,
craft) and W8 (buy on the AH), and W4 for walking to bankers, mailboxes and farm zones.

## Done looks like

Bots found and join guilds the blizzlike way, respect rank rights, deposit to the guild bank, and take guild orders from
players: they gather, buy or craft the ordered items and deliver them to the guild bank or by mail, all through the
core's own guild, petition, bank and mail handlers.

**Live acceptance** (world-life plan, W9 row): a player in a guild with bots orders 10 Linen Bags; the bots gather
(or buy) linen, craft the bags and deliver them by mail; the order shows its progress (`3/10` ... `done`). Log
`[W9] order|step|deliver|refuse id= bot= item= have= want=`.
**From a client**: the forever-agent client's mail round (R7: mail list, take item, take money) takes the delivered
bags on the player's side.

## Scope and legacy files

Plan scope: "bot guilds, ranks, bank, the order board and delivery", 8 worker-days, biggest risk "biggest area with
the most F gaps (22 error files)". Area: 82 files, 21,792 lines, 336 places.

**What already exists from the Turtle guild-orders work, and what to do with it:**

| Legacy | Size | Reuse |
|---|---|---|
| `runtime/GuildOrder*Policy.h` (Access, Auction, Bring, Buy, CancelDeposit, Category, Craft, Delivery, Disenchant, EnchantLifecycle, Enchant, Fishing, GiveUp, Keep, Mail, Meet, Need, PlayerBuy, Pool, Preview, Refund, Relist, Search, Sources, StepHistory, SubOrder, Supply, Telemetry, WhoSearch), `GuildOrderContract.h`, `GuildOrderReagentTree.h`, `GuildOrderProtectedItems.h`, the queue headers | about 30 headers, 50-460 lines each | **reuse as-is**: core-free, each with a test in `tools/test_guild_order_*` (about 40 tests). Add them to the census list; they should compile clean. |
| `ai/playerbot/policy/guild/GuildFoundingPolicy.h`, `EnchantGatherPolicy.h`, `EnchantTradePolicy.h` | 62-257 | reuse (pure) |
| `runtime/GuildOrderStore.cpp` 4,461 / 34 places, `GuildOrderStoreColumns.h` | big | **port behind guards**: it reads and writes the characters tables `tortoise_bots_guild_order`, `_relist`, `_disenchant` and reads `guild_member`. Ship the tables as `forever/sql/characters/` files; every read through its own guarded entry like `CharacterQueryCompat`; writes through prepared statements in transactions. |
| `runtime/GuildOrderDelivery.cpp` 1,101 / 41, `GuildOrderSources.cpp` 980 / 49, `GuildOrderAccess.cpp` 487 | | port onto the host seams of W2/W7/W8 (no own copy of buy/craft/mail code) |
| `commands/Order.cpp` 1,226 / 15 | | the `.bot order` chat command; port as a `.tb guildorder` row (grammar v2) |
| `strategy/actions/Guild*Action.cpp` (Accept 125, Bank 197, CraftOrder 326, Create 392, EnchantOrder 373, Management 201, OrderBuy 570, OrderVendor 166), `GuildValues.cpp` 1,603, `GuildStrategy`, `GuildTriggers`, `GuildMeetingTrigger` | | the bot side: port what the order loop needs; enchant orders last or out |
| `host/ClaimGuildAdapter.*`, hire/claim lifecycles | | out (non-blizzlike hire module) |
| Player docs `docs/play/guild-orders.md`, `docs/server/settings/guild-orders.md`, `docs/developers/guild-supply.md` | | the behaviour contract; read first |

## ForeverCore APIs (25fc7ca)

- `Guild` (`Guilds/Guild.h`): **public** `Create(leader, name)` (`:750`), `GetLeaderGUID` (`:758`), `GetBankMoney`
  (`:763`), `HasAnyRankRight(rankId, rights)` (`:919`), `GetMember(guid) const` (`:929`), `Member::GetRankId` (`:358`),
  `Member::IsOnline` (`:378`), `HandleInviteMember`/`HandleAcceptMember` (`:780-781`), `HandleSetMemberRank` (`:785`),
  `HandleMemberDepositMoney` (`:789`), `BroadcastToGuild` (`:832`), `SwapItemsWithInventory` (`:865`).
  **Private**: `GetRankInfo`, `_HasRankRight` (`:913-917`), `GetBankTab` (`:925`), the mutable `GetMember` (`:936`). So the
  UNPORTED row's "port with `GetRankInfo`, `_HasRankRight`" is not possible: use `HasAnyRankRight` + the member's rank.
- `sGuildMgr->GetGuildById` (`GuildMgr.h:47`), `GetGuildByLeader` (`:46`).
- Founding: petitions `HandlePetitionBuy` / `HandleSignPetition` / `HandleTurnInPetition` (`WorldSession.h:1498`,
  `:1503`, `:1506`); signatures needed: `CONFIG_MIN_PETITION_SIGNS` (`World.h:303`).
- Joining: `HandleGuildInviteByName`, `HandleGuildAcceptInvite` (`WorldSession.h:1509-1511`).
- Bank: `HandleAutoGuildBankItem` (deposit, `:1843`), `HandleGuildBankDepositMoney` (`:1841`). Tab contents are private
  and the query answer is a packet the headless session drops.
- Mail sending: `HandleSendMail` (`:1612`) at a mailbox (postage, delay are the core's); `MailDraft` (`Mails/Mail.h:132`)
  skips postage, so not in the real path.

## Files to touch (suggested)

Host `forever/src/host/guild/TbHostGuild.{h,cpp}` (`tbhost::guild`: Rank, Right, Invite, Accept, Deposit, SendMail),
`TbGuildOrders.{h,cpp}` (the order loop on the reused policies), compat pair `TbCompat_WL9.h` +
`ai/cmangos-compat-wl9.h`, `forever/sql/characters/<date>_tb_guild_orders.sql`, census `forever/SLICE-W9.txt`, command
row `.tb guildorder`, `.tb state` block `order=<id>:<have>/<want>`, `W9_KEYS`, config `TortoiseBots.World.Guilds = 0`
and `TortoiseBots.World.GuildOrders = 0`.

## UNPORTED rows removed

`guild` (rank right and member slots, via `HasAnyRankRight`), `incoming: guild and petition` (invite and signature
answered through the handlers), the guild-order part of `pool and hire services` (`TortoiseBots::GuildOrders`). The
hire and claim parts stay.

## Rules that bite here

- Every characters-DB read through a guarded entry per table; a missing table = no orders (less). No printf-style queries.
- Keep guild id, order id and member guids; re-find the guild with `GetGuildById` each tick.
- An order is `done` only when the items left the bot's bags into the bank (bag count before/after) or a sent mail was
  accepted by the handler; never on intent.
- Blizzlike: charters and signatures, no `Guild::Create` without a turned-in petition; bots pay postage and fees.
- Bot guild founding stays off by default (as on Turtle).

## Tests (failing case first)

- **Failing case:** a deposit that the bank refuses (no tab right) is still reported `done`; it must fall back to mail
  to the guild master, and the order shows `mailed`.
- Reused policy tests all pass on the ForeverCore side unchanged; the store's guarded reader returns "no orders" when a
  table is missing; a player without the rank right cannot order.
- Wiring: no SQL outside the guarded entries; no `MailDraft` in the real path; `--mutate` drops the rank check and must
  FAIL.

## Open for you to settle

1. **Guild bank contents.** Recommendation: deposit only, never withdraw by content; the "what the bank already has"
   preview reads `guild_bank_item` through a guarded SELECT and is marked possibly stale.
2. **Rank gate.** Turtle required "may edit officer notes". Recommendation: the same right through `HasAnyRankRight`.
3. **Enchant and disenchant orders.** Recommendation: out of the first round.

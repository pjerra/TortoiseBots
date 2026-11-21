# Fixmail Notes: Honest Mail Orders & Core-Accepted Mailbox Finder

Branch: `fix/wl2-mail` (based on `feat/world-life-2`).

## Root cause

1. **Mailbox lookup (`ai/playerbot/strategy/actions/MailAction.cpp:651-666`)**:
   - `MailProcessor::FindMailbox` read `"nearest game objects no los"` (range `sPlayerbotAIConfig.sightDistance` = 60 yd, unsorted) and returned the first `GAMEOBJECT_TYPE_MAILBOX` without checking `Player::GetGameObjectIfCanInteractWith(guid, GAMEOBJECT_TYPE_MAILBOX)` (`forever-local/src/src/server/game/Handlers/MailHandler.cpp:51`, `Player.cpp:1993-2029`).
   - A mailbox outside interact range (or in a different phase / `IconName == "Point"`) was rejected by `WorldSession::CanOpenMailBox`.
2. **Dishonest take/send results**:
   - `tbhost::town::TakeMailItem` (`forever/src/host/TbHostTown.cpp:421`) returned `true` unconditionally without checking whether the item arrived in the bot's bags.
   - `MailTakeMoneyCompat` / `MailTakeItemCompat` (`forever/src/compat/TbCompat_W2Town.h:202-212`) discarded the `bool` result (`void`).
   - `TakeMailProcessor::Process` / `After` (`MailAction.cpp:165-277`) reported `true` (`ord=...:ok`) even when 0 takes succeeded.
   - `tbhost::town::LogErrand("mail", bot, nullptr, ...)` always logged `npc=0` (mailbox is a `GameObject`), masking whether a mailbox guid was passed.
   - `tbhost::guild::SendMail` (`forever/src/host/TbHostGuild.cpp:263`) returned `true` without verifying the attachment left the bag or postage/copper was deducted.

## What was changed

1. **Pure policy (`ai/playerbot/policy/town/TownErrandPolicy.h:193-229`)**:
   - Added `MailTakeMoneyConfirmed`, `MailTakeItemConfirmed`, `MailSendConfirmed`, `MailOrderResult`.
2. **Host mailbox finder & proof (`forever/src/host/TbHostTown.h`, `TbHostTown.cpp`)**:
   - `tbhost::town::FindNearestMailbox(bot, maxDistYd = 0.0f)`: when `maxDistYd <= 0`, scans 15 yd and filters by `GetGameObjectIfCanInteractWith(guid, GAMEOBJECT_TYPE_MAILBOX)` (the exact check `CanOpenMailBox` enforces), picking the nearest; when `maxDistYd > 0`, scans `maxDistYd` (300 yd for W9 delivery) in the same zone/phase.
   - `TakeMailMoney` uses `ai::MailTakeMoneyConfirmed` and logs `mailbox=<counter> ok=<0|1>`.
   - `TakeMailItem` records item entry + bag count before `HandleMailTakeItem`, verifies `ai::MailTakeItemConfirmed`, and logs `mailbox=<counter> ok=<0|1>`.
3. **Compat & callers (`TbCompat_W2Town.h`, `cmangos-compat-w2town.h`, `MailAction.cpp`)**:
   - `FindNearestMailboxCompat` delegates to `tbhost::town::FindNearestMailbox(bot)` on ForeverCore.
   - `MailTakeMoneyCompat` / `MailTakeItemCompat` return `bool`.
   - `TakeMailProcessor`, `TakeOrderMail`, and `TakeRelistValue` count/report only confirmed takes; `TakeMailProcessor::After` returns `confirmedTakes > 0`.
4. **Honest mail order (`forever/src/host/TbOrders.cpp`)**:
   - Queue-time: refuses `mail` with `code=no-mailbox` if `FindNearestMailbox(bot)` is null.
   - Execution-time: returns `fail:no-mailbox` if no mailbox, `ok` only if `act("mail", ...)` confirmed at least one take, else `fail:mail-none-taken`.
5. **W9 delivery (`forever/src/host/TbHostGuild.cpp`, `TbHostGuildOrders.cpp`)**:
   - `HostDeliveryProvider::CanDeliverByMail` and `Deliver` use `tbhost::town::FindNearestMailbox(bot, kW2MaxVisitDistYd)`.
   - `tbhost::guild::SendMail` verifies `ai::MailSendConfirmed` before returning `true`.

## Gate results

- **Pure test (`forever/tools/test_w2_town.cpp`)**: `PASS: test_w2_town (8 checks, 0 failed)`
- **W2 wiring (`forever/tools/test_w2_town_wiring.py`)**: `Ran 6 tests in 0.001s` (`OK`); `--mutate` -> `FAILED` (`w2_mutate_exit=1`)
- **W9 wiring (`forever/tools/test_w9_guild_wiring.py`)**: `Ran 6 tests in 0.001s` (`OK`); `--mutate` -> `FAILED` (`w9_mutate_exit=1`)
- **Context & registry**: `SliceAiObjectContext.cpp is up to date`; `summary: (a)=0  (b)=228 keys to register or stub later`
- **Census `SLICE-W1.txt`**: `files 287  compile clean 287  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0`
- **Census `SLICE-W9.txt`**: `files 287  compile clean 287  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0`
- **ON link (`worldserver`)**: `[343/343] Linking CXX executable bin/RelWithDebInfo/bin/worldserver` -> `BUILD OK worldserver ON a76428ac3`

## Live check (for combined lead run)

1. Away from any mailbox: `tb order Dashmag mail #m0` -> `order bot=Dashmag verb=mail status=refused code=no-mailbox #m0`.
2. At Stormwind mailbox (`-8815.17, 652.923, 94.8966`) with no ready mail: `tb order Dashmag mail #m1` -> `tb state Dashmag #m2` shows `ord=<n>:fail:mail-none-taken`.
3. With ready money/item mail (`deliver_time <= now`) at Stormwind mailbox: `tb order Dashmag mail #m3` -> `tb state Dashmag #m4` shows `ord=<n>:ok`, `money=`/`bagfree=` updated, and `Server.log` shows `[W2] mail bot=Dashmag ... mailbox=<id> ok=1`.

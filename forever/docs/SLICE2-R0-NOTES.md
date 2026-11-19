# Slice 2, package H, R0 (no boots): list, baseline, cast-result path, keys, spike recipes

Branch `slice2/h` off c88084785 (slice 1 after R8), worktree `~/wt-slice2-h`, build slot 1. No server was started. Core read only at the pin 6517af05
(`~/ForeverCore`, paths below are under `src/server/game/` unless they start with `src/`). Turtle-side paths are under the repository root.

## 1. R0: list and census baseline

* `forever/SLICE2.txt` = the 132 files of `SLICE1.txt` plus the 27 files of FOREVER-SLICE2-SPEC section 3 = **159** (the spec text says 158; the 27 files
  add up to the spec's own 8,387 lines, so 159 is the sum).
* Baseline (slot 1, `FOREVER_CENSUS_LIST=forever/SLICE2.txt`): `files 159  compile clean 148  with errors 11  error places 260 (in 14 files)  includes of a header ForeverCore does not have 1`.
  Top of `where.tsv`: LootAction.cpp 76, UseItemAction.cpp 63, LootValues.cpp 48, LootObjectStack.cpp 22, QuestValues.h 16, CorpseLootPolicy.h 13, AddLootAction.cpp 12,
  PriestActions.cpp 3, LootTriggers.cpp 2, then six files with 1 each (GenericPriestStrategy.h and .cpp, PriestMultipliers.cpp, NearestCorpsesValue.cpp, PartyMemberToDispel.cpp: 14 files in all).
  The 27 `.cpp` files hold the spec's 231 places; the other 29 are in two headers they pull in (`QuestValues.h` 16, `CorpseLootPolicy.h` 13): the A2/V2/I2 workers must own those two headers.
  The one missing include is `GenericPriestStrategy.h:12 #include "RangedCombatStrategy.h"` (a bare name, resolves only on Turtle); `StrategyContext.h:41` writes `generic/RangedCombatStrategy.h`.
* `forever/docs/CENSUS.md` has the "Slice 2" section and a row for the whole tree at c88084785 (649 files, 322 clean, 7,650 places in 350 files; before slice 1: 11,063 places, 11 clean).

## 2. The cast-result path: what the core does at the pin

### 2.1 Every route a bot's session is reached

`Spell::SendCastResult` (Spell.cpp:4688-4711) sends `SMSG_CAST_FAILED` with `receiver->SendDirectMessage(castFailed.Write())` (:4710), the receiver being the caster
(`m_caster->ToPlayer()`, :4692; with `SPELL_ATTR7_REPORT_SPELL_FAILURE_TO_UNIT_TARGET` it is the unit target instead, :4693-4695: a bot can then receive a failure of somebody else's
spell; the packet names no caster, so the queue cannot tell: see 3.5). It returns without sending when the receiver is loading (:4701) or has no player (:4698).
`Spell::SendInterrupted` (Spell.cpp:5382-5400) builds `SpellFailure` (CasterUnit = the caster, :5385) and `SpellFailedOther`, both with
`m_caster->SendMessageToSet(..., true)` (:5390, :5399): `Player::SendMessageToSet(data, self)` is `SendMessageToSetInRange` (Player.h:2415; Player.cpp:6859-6867), which sends to self
first by `SendDirectMessage(data)` (:6862) and then to the other players in range, skipping the source (`GridNotifiers.h:200` `player == i_source`). So the caster gets exactly one
copy of each, and every other player that sees the caster (a bot too) gets a copy as well: the queue keeps only `SMSG_SPELL_FAILURE` whose `CasterUnit` is the bot.
`Player::SendDirectMessage` (Player.cpp:6891-6908) is `m_session->SendPacket(data)` (:6907; null session: dropped, :6905) and `WorldSession::SendPacket` is virtual (WorldSession.h:1036),
`BotSession::SendPacket` is the override. A bot session has a null socket and is not in the world's session map: nothing else reads these packets.

| Failure after the cast started | Where the core decides, and the call that tells the caster | Reaches the bot session? | Thread |
|---|---|---|---|
| Interrupted by movement | `Spell::update` Spell.cpp:4336 (`m_timer != 0 && isMoving() && CheckMovement() != SPELL_CAST_OK`, `CheckMovement` :7510, `SPELL_FAILED_MOVING` :7522) -> `cancel()` :3668 -> state PREPARING: `SendCastResult` :3681 + `SendInterrupted` :3682; channelled: `SendChannelUpdate(0, INTERRUPTED)` :3697 + `SendInterrupted` :3698. Whether a bot moved by MotionMaster carries a moving flag: U3 (`MoveSplineInit.cpp:172-176` sets FORWARD in the spline's flags; not run) | yes (both packets) | map thread (`Spell::update` runs from `Unit::Update`: events at Unit.cpp:458, `_UpdateSpells` :465 / :3082, from `Player::Update` Player.cpp:968) |
| Interrupted by damage | `Unit::DealDamage` Unit.cpp:1170-1207: `isCastInterrupted` -> `victim->InterruptSpell(CURRENT_GENERIC_SPELL, false, false)` :1205 -> `Unit::InterruptSpell` Unit.cpp:3348 -> `spell->cancel(...)` (:3380-3381 region); absorb case :937-939 `InterruptNonMeleeSpells`; pushback (`Delayed`, Spell.cpp:8371, `SMSG_SPELL_DELAYED`) is NOT a failure | yes | the attacker's map thread (a creature or player attacking the bot: same map, same thread) |
| Silence / stun / pacify applied | `InterruptSpellsWithPreventionTypeOnAuraApply` SpellAuraEffects.cpp:2556-2564 (silence, pacify) -> `InterruptSpell`; stun/root-type: `Unit::SetControlled` Unit.cpp:12180-12191 `CastStop()` (:1245) -> `InterruptSpell`; both end in `cancel()` | yes | the thread of the map that applies the aura (spell hit on the map thread; a world-thread GM command or script would be the world thread) |
| Silenced / stunned at the END of the cast bar | `Spell::cast(false)` -> `CheckCast` -> `CheckCasterAuras` (Spell.cpp:7337 `SPELL_FAILED_STUNNED`, :7366, :7369 `SPELL_FAILED_SILENCED`); `cleanupSpell` Spell.cpp:3803-3808: `SendCastResult` + `SendInterrupted` | yes | map thread (`Spell::update` :4353 `cast(!m_casttime)`) |
| Target died / despawned during the bar | `Spell::update`: `UpdatePointers` fails :4321-4326, or the target pointer is gone :4328-4333 -> `cancel()`; a target that is dead but still there fails at the end of the bar in `CheckCast` (`SPELL_FAILED_TARGETS_DEAD` :6833) -> `cleanupSpell` | yes | map thread |
| Target out of range at the end of the bar | `CheckCast` -> `CheckRange` (`SPELL_FAILED_OUT_OF_RANGE` Spell.cpp:7636-7658) -> `cleanupSpell` :3806-3807 | yes | map thread |
| Target out of line of sight at the end | `CheckCast` `SPELL_FAILED_LINE_OF_SIGHT` Spell.cpp:6276, :6284, :6600 -> `cleanupSpell` | yes | map thread |
| Caster died | `Unit::setDeathState` Unit.cpp:9556-9562 `InterruptNonMeleeSpells(false)` (:9561) -> `cancel()`; also `CheckCast` `SPELL_FAILED_CASTER_DEAD` :6016 | yes (the session outlives the death) | map thread (damage) or world thread (GM kill) |
| Caster logging out / removed | `Unit::CleanupBeforeRemoveFromMap` Unit.cpp:10793 `InterruptNonMeleeSpells(true)`; `SpellEvent::~SpellEvent` Spell.cpp:8747-8750 `cancel()` | yes (session may already be detached: `SendDirectMessage` drops it) | world thread |
| A new cast replaces the old one | `Unit::SetCurrentCastSpell` Unit.cpp:3250-3300 `InterruptSpell(...)` on the old spell (its `SMSG_SPELL_FAILURE` carries the OLD spell id) | yes | the thread of the new cast: the engine's world thread |
| Synchronous refusal of the cast just made | `Spell::prepare` Spell.cpp:3500-3563 (`SPELL_FAILED_SPELL_IN_PROGRESS` :3531, `CheckCast` :3556-3563, movement :3599): `SendCastResult` only, the same code is the return value | yes (CAST_FAILED only) | the caller's thread: the world thread inside the engine tick; the engine already has it |
| **NOT reported:** auto-repeat spell already in flight | `Unit::InterruptSpell` Unit.cpp:3360-3366 (Classic: the shot lands; stopping drops the pointer silently) | no | - |
| **NOT reported:** Feign Death own cast | SpellAuraEffects.cpp:2427-2430 `FinishSpell(CURRENT_GENERIC_SPELL, INTERRUPTED)` "prevent interrupt message" | no | - |
| **NOT reported:** failure results marked "do not report" | the packet IS sent but with `SPELL_FAILED_DONT_REPORT` (Spell.cpp:4703-4704; `SelectSpellTargets` finish :3882): the engine sees `reason` = the DONT_REPORT value | yes, with that reason | any |
| **NOT reported:** item use that fails before a spell exists | `Player::ProcessItemCast` Player.cpp:33751-33835 answers with `SendEquipError` (`SMSG_INVENTORY_CHANGE_FAILURE`), a pending request replaced or dropped gets `CastFailed` with `SPELL_FAILED_DONT_REPORT` (:33603-33616) | equip errors: no (not captured); the replaced-request packet: yes | world thread |
| **NOT reported:** `Spell::finish` of a spell that is in state IDLE/FINISHED | `Spell::cancel` returns at once for FINISHED (:3670-3671) | no | - |

What the engine sees for the unreported ones: nothing arrives, the cast bar time (`WaitForSpellCast`, PlayerbotAISpells.cpp:1767-1770) runs out and the next tick re-plans; `last spell cast`
stays set to the dead cast until the next cast (the state the missing `SpellInterrupted` would have cleared).

### 2.2 Threads

* Maps update on `MapUpdate.Threads` worker threads: `MapManager::Update` MapManager.cpp:437-445 (`schedule_update` / `wait`; activated whenever the setting is above 0, MapManager.cpp:56-59,
  default 1, worldserver.conf.dist:641, World.cpp:893), `Map::Update` -> `player->Update(t_diff)` Map.cpp:711, other objects :774. So everything in the table marked "map thread"
  runs on a map worker, concurrently with other maps; the bot's cast results arrive there.
* The world thread: `sScriptMgr->OnWorldBeforeMapsUpdate(diff)` World.cpp:2465 (the engine tick: `BotMgr::Update`) and then `sMapMgr->Update(diff)` World.cpp:2475, which waits for the workers
  (MapManager.cpp:445). The engine therefore never runs during a map update, and map threads never run during the engine tick; they still run concurrently with console commands, `Map::DelayedUpdate`
  (MapManager.cpp:447-448) and other world-thread work.
* Hence the design: the only state shared between a map thread (producer: `BotSession::SendPacket`) and the world thread (consumer) is one mutex-protected deque per session; the producer calls nothing of
  the engine; the consumer drains at the start of the bot's engine tick.

## 3. The cast-result queue

### 3.1 What is queued, how it is read (`forever/src/TbBotSession.{h,cpp}`)

`BotSession::SendPacket` (override) filters on the opcode first; only `SMSG_SPELL_FAILURE` and `SMSG_CAST_FAILED`, and only while the session's sink flag is on, reach `CaptureCastResult`, which copies the packet
(reading moves the read position and the same packet object goes to other receivers) and reads the fields with the core's own readers in the order of the core's own writers, nothing hand-parsed:

* `SMSG_SPELL_FAILURE`: `WorldPackets::Spells::SpellFailure::Write` SpellPackets.cpp:532-542: `ObjectGuid CasterUnit, ObjectGuid CastID, int32 SpellID, SpellCastVisual Visual, uint16 Reason, ObjectGuid FailedBy`.
  Read with `ByteBuffer >> ObjectGuid` (ObjectGuid.h:438), `>> int32`, `WorldPackets::Spells::SpellCastVisual` `operator>>` (CombatLogPacketsCommon.h:111, .cpp:212), `>> uint16`. A packet whose `CasterUnit` is not the bot is dropped.
* `SMSG_CAST_FAILED`: `CastFailed::Write` SpellPackets.cpp:555-565: `ObjectGuid CastID, int32 SpellID, SpellCastVisual Visual, int32 Reason, int32 FailedArg1, int32 FailedArg2, ObjectGuid FailedBy`. The caster is the session's own character (no caster field).
* Kept per entry (`CastResult`): kind, caster, cast id, spell id, reason, an `injected` flag. A read that throws `ByteBufferException` is counted as `unreadable` and dropped.

`SMSG_LOOT_RESPONSE` (the spec's third opcode, section 2) is NOT queued here: section 5 of the spec reads loot from the core's `Loot` object right after the open (`GetAELootView`), so nothing consumes the packet; the kind enum is where it would go.

### 3.2 Bound

`CastResultCapacity = 32` entries per session. A full queue removes the OLDEST entry and counts it (`dropped`). A bot's casts are a few per second at most and the queue is emptied every world tick
(about 50 ms), so a drop means a drain that stopped. Counters: queued, delivered, dropped, refusedInTick, unreadable, pending (`BotSession::GetCastResultCounts`).

### 3.3 Which results are NOT queued (decision, say so if you disagree)

An `SMSG_CAST_FAILED` that the core sends while THIS bot's engine tick is running on this thread (`BotSession::EngineTickScope`, set by `engine::Tick`) is the synchronous refusal of the engine's own
`CastSpell` call: the engine reads the same code as the call's return value (`CAST_REFUSED result=87` in R8). Queueing it would turn every refusal into a second, "asynchronous" event (and
5,000 refusals per 90 s of `battle shout` in R8 note 11.2 would flood the queue). They are counted (`refusedInTick`) and the latest one is kept (`BotSession::TakeInTickRefusal`) for a core call that
returns nothing, `Player::CastItemUseSpell` (see U1). `SMSG_SPELL_FAILURE` is queued even inside the tick (a cast the engine started and the core cancelled).

### 3.4 The drain (`forever/src/host/TbEngineBrain.cpp`)

`engine::Tick` (world thread): inside its `try`, `DeliverCastResults(brain)` then `UpdateAI`. Each result is handed to the engine the way the Turtle handlers do:

* SPELL_FAILURE: action-log line `SPELL_FAILURE spell=<id> reason=<n>` then `PlayerbotAI::SpellInterrupted(spellId)` (public; the call of PlayerbotAIPackets.cpp:187-199): clears `last spell cast` when its id matches and shortens the bot's wait to the remaining global cooldown (PlayerbotAI.cpp:3172-3194).
* CAST_FAILED: action-log line `CAST_FAILED_ASYNC spell=<id> reason=<n>` and, when the spell is crowd control and the log is open, the `CC_CAST_FAILED spell= result= target= dist= moving=` line of PlayerbotAIPackets.cpp:554-591 (target printed as the core's guid string).
  The Turtle handler's last step, `botOutgoingPacketHandlers.AddPacket(packet)` (the "cast failed" trigger, WorldPacketTriggerContext.h:45), is not done: no strategy maps that trigger.
* The sink is on from the end of `engine::Create` and off in `Dispose`; with the engine not built (`TORTOISEBOTS_SLICE1` off) `SendPacket` compiles to the old empty body and nothing is queued.
* `.tb where` (engine bots) ends with `cast results: queued N, delivered N, dropped N (queue holds 32), pending N; refused during the engine's own tick (not queued) N, unreadable N`.
* `.tb castfail <bot> <spell id> <reason> [interrupt]`: builds the core's own typed packet (`CastFailed`, or `SpellFailure` with `interrupt`) and sends it with `Player::SendDirectMessage`, so the reader, queue and drain are all exercised; refused with a reason when the bot has no running engine. Reasons (SharedDefines.h): 63 interrupted, 75 moving, 87 not known, 118 out of range, 130 targets dead.
  Expected live: `CAST_FAILED_ASYNC spell=<id> reason=<n> injected=1` in the bot's action log (needs `AiPlayerbot.EnableActionLog`), `.tb where` counts queued 1 delivered 1.

### 3.5 Known limits

* A CAST_FAILED sent to the bot because of `SPELL_ATTR7_REPORT_SPELL_FAILURE_TO_UNIT_TARGET` (somebody else's spell, the bot being the target) cannot be told from the bot's own (no caster field). The engine only logs it and the CC line; it does not reset anything on CAST_FAILED, so the effect is a stray log line.
* Item-use failures that go through `SendEquipError` are not heard (3.1 table, last rows).
* Not run: no boot was allowed. Built (option ON and OFF), the reader compiled and linked against the core's `operator>>`; the first live check is `.tb castfail`.

## 4. NEEDS FROM the merge (the files this package must not edit)

1. `ai/playerbot/UnportedFeatures.h:32` and `forever/docs/UNPORTED.md:33` (`incoming: cast failed`): the packet row `{ SMSG_CAST_RESULT, "cast failed", false }` (PlayerbotAIPackets.cpp:135) stays in the table, so the group in `forever/src/compat/TbCompat_Unported.cpp:91`
   (`{ Feature::IncomingCastFailed, { "cast failed" } }`) still counts one registration per engine built. What is heard now is the result itself, not the "cast failed" world-packet trigger. Recommended change: RENAME the row to
   `incoming: cast failed trigger` with the text "the 'cast failed' world-packet trigger is not fed (no strategy maps it); the result itself is heard through the host's cast-result queue (CAST_FAILED_ASYNC, SpellInterrupted)"; keep the group entry at TbCompat_Unported.cpp:91 unchanged. If the lead
   prefers the row gone: delete UnportedFeatures.h:32, UNPORTED.md:33 and the TbCompat_Unported.cpp:91 line, and drop `"cast failed"` from `kBotOutgoingPacketHandlers` only if nothing else lists it (do not: the trigger key `cast failed` exists in WorldPacketTriggerContext.h:45).
2. `incoming: spell failure and delay` (UnportedFeatures.h:36, UNPORTED.md:37): rename to `incoming: spell delay`, text "SMSG_SPELL_DELAYED (1.12 layout)". The failure half is now the host queue. In `TbCompat_Unported.h:228` (`RegisterDirectPacketCasesCompat`) the `Feature::IncomingSpellState` count stays (the delay case is still dead on F) under the new name; `PackedGuidReadCompat(IncomingSpellState ...)` at PlayerbotAIPackets.cpp:192 (the failure case) is now unreachable on F by construction and may stay.
3. Spec section 4 also removes `incoming: forced speed change` and `incoming: knock back`: not touched here.
4. Nothing else outside `forever/src/{TbBotSession,TbScripts}.*`, `forever/src/host/TbEngineBrain.*` and docs changed. `TbBotMgr.*`, `TbCompat_Unported.h` and `SliceLinkStubs.cpp` were not touched.

## 5. Spike recipes (for when the test server is free; patches NOT applied)

Both run on one boot with a priest or any caster that has a cast-time spell at level 1 (priest Smite 585 / Lesser Heal 2050 are the spec's spells; the worker looks the ids up with `.tb spells <c>`, not from memory), logged in WITHOUT a master
(`.tb login <priest>`) and with the engine off for U3 (the spike walk, `.tb go`, is for a bot without a master), engine on for U1 if the drain line is wanted.

### U1: drinking through the core's own item-cast call

Console steps: `.tb login Tbpriest` -> `.tb power`-less variant: the patch's `.tb additem Tbpriest <water entry> 5` (look the entry up with `.tb item <entry>` for a candidate and read the mirror's name/class; no id from memory) ->
`.tb use Tbpriest <entry>` (starts the item cast) -> `.tb use Tbpriest 0` three times, 3 s apart (reports).
Lines to look for:
* the command's own line `item <entry>: legacy yes|no, N effect(s), on-use spell <id>`: `legacy yes` or `0 effect(s)` means the core ignores the item's effects (`Player::CastItemUseSpell` Player.cpp:9718-9744 only accepts effects of non-`ITEM_FLAG_LEGACY` items, ItemTemplate.h:212): U1 FAILS for that item, say so and stop;
* the server log `Player::CastItemUseSpell: Tried to cast Spell ... not found on Item` (Player.cpp:9742) = FAIL;
* PASS: the report lines show the drink aura id applied within 1 s (`auras:` list), `casting: no` after the cast and mana rising ~ every 3 s, and the item count down by one when the aura ends (or at once: note which).
* with the sink on (engine bot): `.tb where` counts: a refusal inside the call is `refusedInTick`, outside the tick (console) it is `queued`.
Minimal host code (TbScripts.cpp, console only, not applied):
```cpp
{ "additem", HandleAddItem, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
{ "use",     HandleUse,     rbac::RBAC_PERM_COMMAND_GM, Console::Yes },

static bool HandleAddItem(ChatHandler* handler, std::string name, uint32 entry, uint32 count)
{
    Player* bot = BotMgr::Instance().FindBot(GuidByName(name));
    if (!bot) return Fail(handler, name + " is not a logged-in bot.");
    if (!sObjectMgr->GetItemTemplate(entry)) return Fail(handler, "no such item");
    if (!bot->StoreNewItemInBestSlots(entry, count, ItemContext::NONE)) return Fail(handler, "not stored");   // Player.h:1626
    handler->PSendSysMessage("TortoiseBots: %s now has %u of %u.", name.c_str(), bot->GetItemCount(entry), entry);
    return true;
}

// .tb use <c> <entry>; entry 0 = report only
static bool HandleUse(ChatHandler* handler, std::string name, uint32 entry)
{
    Player* bot = BotMgr::Instance().FindBot(GuidByName(name));
    if (!bot) return Fail(handler, name + " is not a logged-in bot.");
    if (entry)
    {
        Item* item = bot->GetItemByEntry(entry);                                        // Player.h:1507
        if (!item) return Fail(handler, "not in the bags");
        uint32 spellId = 0;
        for (ItemEffectEntry const* effect : item->GetEffects())                        // Item.h:372
            if (effect->TriggerType == ITEM_SPELLTRIGGER_ON_USE) { spellId = effect->SpellID; break; }
        handler->PSendSysMessage("TortoiseBots: item %u: legacy %s, %u effect(s), on-use spell %u", entry,
            item->GetTemplate()->HasFlag(ITEM_FLAG_LEGACY) ? "yes" : "no", uint32(item->GetEffects().size()), spellId);
        if (!spellId) return Fail(handler, "no on-use effect");
        SpellCastTargets targets;
        targets.SetUnitTarget(bot);
        bot->CastItemUseSpell(item, spellId, targets, ObjectGuid::Empty, { 0, 0, 0 });     // Player.h:2683, the call ProcessItemCast ends in (Player.cpp:33833)
    }
    std::string auras;
    for (auto const& [id, app] : bot->GetAppliedAuras()) auras += std::to_string(id) + " ";
    handler->PSendSysMessage("TortoiseBots: mana %u/%u, casting: %s, auras: %s", bot->GetPower(POWER_MANA), bot->GetMaxPower(POWER_MANA),
        bot->GetCurrentSpell(CURRENT_GENERIC_SPELL) ? "yes" : "no", auras.c_str());
    return true;
}
```
(`ItemContext::NONE` is used the same way at AchievementMgr.cpp:556; the rest is compile-checked only when the patch is applied.) If it passes, the I2 helper calls `CastItemUseSpell` from `engine::Tick`'s scope and reads
the result with `BotSession::TakeInTickRefusal` (the call returns nothing); if the item is `legacy`, D3 applies (drinking dropped, listed as unported).

### U3: does the core cancel a cast when a bot is moved by its movement system

Console steps: `.tb login Tbpriest` (no master, engine off) -> `.tb castlog Tbpriest on` (patch below: turns the sink on) -> a hurt target nearby or the bot itself: `.tb cast Tbpriest <Lesser Heal id>` (self cast, `HandleCast` TbScripts.cpp:407; result 0 = started) ->
within 0.5 s `.tb go Tbpriest 15 0` (the spike walk, `MovePoint`, TbBotMgr.cpp:333-344) -> `.tb castlog Tbpriest` (prints and drains the queue) -> `.tb where Tbpriest` (motion type 8 point, position moved).
Lines to look for: one line `kind=CastFailed spell=<id> reason=75` and one `kind=SpellFailure spell=<id> reason=75` (75 = `SPELL_FAILED_MOVING`) within about one world tick of the walk's start = the core cancels (Spell.cpp:4336); no line and a heal that lands
(health rises, mana drops) while the bot walks = it does NOT cancel (the `isMoving()` flags of a spline-moved player are clear): then observation g of the live test is "no cancel", and the engine's `CanMove`/movement gate is what stops a bot from walking and casting. Repeat once with a 1.5 s spell and once with an instant spell
(instant: no line expected, `m_timer` is 0).
Minimal host code (TbScripts.cpp, not applied; uses only members that exist in this branch):
```cpp
{ "castlog", HandleCastLog, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },

// .tb castlog <c> [on|off]: turn the cast-result sink on/off for a bot WITHOUT an engine; without the word, print and drain the queue.
static bool HandleCastLog(ChatHandler* handler, std::string name, Optional<std::string> mode)
{
    Player* bot = BotMgr::Instance().FindBot(GuidByName(name));
    if (!bot) return Fail(handler, name + " is not a logged-in bot.");
    auto* session = dynamic_cast<TortoiseBots::BotSession*>(bot->GetSession());     // include "TbBotSession.h"
    if (!session) return Fail(handler, "not a bot session");
    if (mode) { session->SetCastResultSink(*mode == "on"); return true; }
    std::vector<TortoiseBots::CastResult> results;
    session->DrainCastResults(results);
    for (auto const& r : results)
        handler->PSendSysMessage("TortoiseBots: kind=%s spell=%u reason=%u cast=%s%s", r.kind == TortoiseBots::CastResult::Kind::SpellFailure ? "SpellFailure" : "CastFailed",
            r.spellId, r.reason, r.castId.ToString().c_str(), r.injected ? " injected" : "");
    handler->PSendSysMessage("TortoiseBots: %u result(s); isMoving now %s.", uint32(results.size()), bot->isMoving() ? "yes" : "no");
    return true;
}
```
Also worth one extra line in the same run: `isMoving` is printed by the command, so a reading taken while the spline runs answers U3's real question (moving flags yes/no) even if the cast is too short to show a cancel.

## 6. Shared host files touched (for the merge with the slice-1 fix round)

| File | Lines (at this branch) | What |
|---|---|---|
| `forever/src/TbScripts.cpp` | 159 (one command-table row `castfail`), 300-319 (the `HandleCastFail` handler, before `HandlePerf`) | additive |
| `forever/src/host/TbEngineBrain.h` | 36-38 (doc of `Tick` extended), 44-49 (new `InjectCastResult` declaration, `Describe` comment) | additive |
| `forever/src/host/TbEngineBrain.cpp` | includes 11, 32, 36-37; `Brain::session` 49; helpers 62-111 (`BotSessionOf`, `DeliverCastResult`, `DeliverCastResults`); `Dispose` 114-116; `Create` 236 and 249-252; `Tick` 266-269; `InjectCastResult` 301-308; `Describe` 377-386; OFF stub 407 | additive |
| `forever/src/TbBotSession.{h,cpp}` | the slice-2 queue (new members and functions; `SendPacket` body replaced) | owned by this package |
| `forever/src/TbBotMgr.*`, `compat/TbCompat_Unported.h`, `slice/SliceLinkStubs.cpp` | none | not touched |

No file outside `forever/` changed (`git diff --stat c88084785 HEAD`: 8 files, all under `forever/`). `CMakeLists.txt` (repository root) still reads `forever/SLICE1.txt`
(line 59): R1's generator package must point it at `forever/SLICE2.txt` (or a switch) when the priest files compile.

## 7. Gates

| Gate | Result |
|---|---|
| Option OFF module build: `FOREVER_SLOT=1 FOREVER_CMAKE_EXTRA=-DTORTOISEBOTS_SLICE1=OFF ~/forever-build/build.sh 12 tortoisebots` | `CMAKE_RC=0 MAKE_RC=0` (twice: after the queue code and on the final tree) |
| Option ON `worldserver` in slot 1 (`FOREVER_CMAKE_EXTRA=-DTORTOISEBOTS_SLICE1=1`, `build.sh 12 worldserver`) | `CMAKE_RC=0 MAKE_RC=0`, `worldserver` linked (first try failed on a missing `#include "Spell.h"` for `SPELL_CAST_SOURCE_NORMAL`, fixed) |
| Slot 1 restored to option OFF | `CMakeCache.txt`: `TORTOISEBOTS_SLICE1:BOOL=OFF` |
| Slice-1 census (`FOREVER_CENSUS_LIST=forever/SLICE1.txt`) | `files 132  compile clean 132  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0` |
| Slice-2 census (baseline) | `files 159  compile clean 148  with errors 11  error places 260 (in 14 files)  includes of a header ForeverCore does not have 1` |
| Files outside `forever/` | none |
| Not done (by the brief) | no boot, no live check of the queue, no spike run |

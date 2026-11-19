# Slice 3: the 45 registry keys (FOREVER-SLICE3-SPEC section 3)

Applied through `forever/tools/slice_gen_context.py` (`SLICE3_KEYS`); the generated file is `forever/src/slice/SliceAiObjectContext.cpp`, `--check` and
`forever/tools/slice_registry_check.py` are current. Creator = the text copied byte for byte from the full build's context header at `file:line`
(the line of the `creators[...]` assignment). Counts: 7 strategies, 21 actions, 16 triggers, 1 value = 45. Registered totals after this slice:
`slice: registered 15/44/72/168` (slice 2: 8/23/56/167).


## Strategies (7)

| Key | Context class | Creator | Creator (file:line) |
|---|---|---|---|
| `tank assist` | AssistStrategyContext | `new TankAssistStrategy(ai)` | `ai/playerbot/strategy/StrategyContext.h:263` |
| `threat` | StrategyContext | `new ThreatStrategy(ai)` | `ai/playerbot/strategy/StrategyContext.h:112` |
| `stay` | MovementStrategyContext | `new StayStrategy(ai)` | `ai/playerbot/strategy/StrategyContext.h:247` |
| `free` | MovementStrategyContext | `new FreeStrategy(ai)` | `ai/playerbot/strategy/StrategyContext.h:251` |
| `passive` | StrategyContext | `new PassiveStrategy(ai)` | `ai/playerbot/strategy/StrategyContext.h:94` |
| `tank face` | StrategyContext | `new TankFaceStrategy(ai)` | `ai/playerbot/strategy/StrategyContext.h:144` |
| `wait for attack` | StrategyContext | `new WaitForAttackStrategy(ai)` | `ai/playerbot/strategy/StrategyContext.h:174` |

## Actions (21)

| Key | Context class | Creator | Creator (file:line) |
|---|---|---|---|
| `tank assist` | ActionContext | `new TankAssistAction(ai)` | `ai/playerbot/strategy/actions/ActionContext.h:168` |
| `stay` | ActionContext | `new StayAction(ai)` | `ai/playerbot/strategy/actions/ActionContext.h:183` |
| `return to stay position` | ActionContext | `new ReturnToStayPositionAction(ai)` | `ai/playerbot/strategy/actions/ActionContext.h:208` |
| `pull my target` | ActionContext | `new PullMyTargetAction(ai)` | `ai/playerbot/strategy/actions/ActionContext.h:188` |
| `end pull` | ActionContext | `new EndPullAction(ai)` | `ai/playerbot/strategy/actions/ActionContext.h:199` |
| `pull start` | ActionContext | `new PullStartAction(ai)` | `ai/playerbot/strategy/actions/ActionContext.h:192` |
| `pull action` | ActionContext | `new PullAction(ai)` | `ai/playerbot/strategy/actions/ActionContext.h:193` |
| `pull end` | ActionContext | `new PullEndAction(ai)` | `ai/playerbot/strategy/actions/ActionContext.h:195` |
| `reach pull` | ActionContext | `new ReachPullAction(ai)` | `ai/playerbot/strategy/actions/ActionContext.h:150` |
| `release pull hold` | ActionContext | `new ReleasePullHoldAction(ai)` | `ai/playerbot/strategy/actions/ActionContext.h:200` |
| `return to pull position` | ActionContext | `new ReturnToPullPositionAction(ai)` | `ai/playerbot/strategy/actions/ActionContext.h:194` |
| `wait for attack keep safe distance` | ActionContext | `new WaitForAttackKeepSafeDistanceAction(ai)` | `ai/playerbot/strategy/actions/ActionContext.h:159` |
| `tank face away` | ActionContext | `new TankFaceAwayAction(ai)` | `ai/playerbot/strategy/actions/ActionContext.h:211` |
| `set facing` | ActionContext | `new SetFacingTargetAction(ai)` | `ai/playerbot/strategy/actions/ActionContext.h:210` |
| `successor white cap noop` | ActionContext | `new SuccessorWhiteCapNoopAction(ai)` | `ai/playerbot/strategy/actions/ActionContext.h:143` |
| `threat reset white stop noop` | ActionContext | `new ThreatResetWhiteStopNoopAction(ai)` | `ai/playerbot/strategy/actions/ActionContext.h:144` |
| `attack my target` | ChatActionContext | `new AttackMyTargetAction(ai)` | `ai/playerbot/strategy/actions/ChatActionContext.h:141` |
| `attack rti target` | ActionContext | `new AttackRTITargetAction(ai)` | `ai/playerbot/strategy/actions/ActionContext.h:171` |
| `follow chat shortcut` | ChatActionContext | `new FollowChatShortcutAction(ai)` | `ai/playerbot/strategy/actions/ChatActionContext.h:157` |
| `stay chat shortcut` | ChatActionContext | `new StayChatShortcutAction(ai)` | `ai/playerbot/strategy/actions/ChatActionContext.h:158` |
| `follow target` | ChatActionContext | `new SetFollowTargetAction(ai)` | `ai/playerbot/strategy/actions/ChatActionContext.h:213` |

## Triggers (16)

| Key | Context class | Creator | Creator (file:line) |
|---|---|---|---|
| `tank assist` | TriggerContext | `new TankAssistTrigger(ai)` | `ai/playerbot/strategy/triggers/TriggerContext.h:130` |
| `lose aggro` | TriggerContext | `new LoseAggroTrigger(ai)` | `ai/playerbot/strategy/triggers/TriggerContext.h:131` |
| `has aggro` | TriggerContext | `new HasAggroTrigger(ai)` | `ai/playerbot/strategy/triggers/TriggerContext.h:134` |
| `party member attacked` | TriggerContext | `new PartyMemberAttackedTrigger(ai)` | `ai/playerbot/strategy/triggers/TriggerContext.h:133` |
| `protect party member` | TriggerContext | `new ProtectPartyMemberTrigger(ai)` | `ai/playerbot/strategy/triggers/TriggerContext.h:98` |
| `tank face needed` | TriggerContext | `new TankFaceNeededTrigger(ai)` | `ai/playerbot/strategy/triggers/TriggerContext.h:190` |
| `high threat` | TriggerContext | `new HighThreatTrigger(ai)` | `ai/playerbot/strategy/triggers/TriggerContext.h:166` |
| `return to stay position` | TriggerContext | `new ReturnToStayPositionTrigger(ai)` | `ai/playerbot/strategy/triggers/TriggerContext.h:61` |
| `pull start` | TriggerContext | `new PullStartTrigger(ai)` | `ai/playerbot/strategy/triggers/TriggerContext.h:125` |
| `pull end` | TriggerContext | `new PullEndTrigger(ai)` | `ai/playerbot/strategy/triggers/TriggerContext.h:126` |
| `pull hold expired` | TriggerContext | `new PullHoldExpiredTrigger(ai)` | `ai/playerbot/strategy/triggers/TriggerContext.h:127` |
| `pull anchor done` | TriggerContext | `new PullAnchorDoneTrigger(ai)` | `ai/playerbot/strategy/triggers/TriggerContext.h:128` |
| `return to pull position` | TriggerContext | `new ReturnToPullPositionTrigger(ai)` | `ai/playerbot/strategy/triggers/TriggerContext.h:62` |
| `wait for attack safe distance` | TriggerContext | `new WaitForAttackSafeDistanceTrigger(ai)` | `ai/playerbot/strategy/triggers/TriggerContext.h:162` |
| `successor white cap` | TriggerContext | `new SuccessorWhiteCapTrigger(ai)` | `ai/playerbot/strategy/triggers/TriggerContext.h:168` |
| `threat reset white stop` | TriggerContext | `new ThreatResetWhiteStopTrigger(ai)` | `ai/playerbot/strategy/triggers/TriggerContext.h:169` |

## Values (1)

| Key | Context class | Creator | Creator (file:line) |
|---|---|---|---|
| `master position` | ValueContext | `new MasterPositionValue(ai)` | `ai/playerbot/strategy/values/ValueContext.h:273` |

Total 45.


## Not registered, on purpose (spec section 3, "Deliberately unregistered")

`should pull`, `pull nearest target` (autonomous pulls: batch4-party-dungeon.md row 7), `chat` and every ChatTriggerContext key (the order path is the host queue and
`DoSpecificAction`, lead decision D1), `pull back`, `loose adds near`, `possible ads`; also `sit` (strategy, trigger and action of StayStrategy.cpp: nothing asks for a bot to sit in
this slice) and `debug threat` (a debug aid; its two visuals are the unported row `debug spell visuals`). `slice_registry_check.py` lists them under (b) with the file that names them: the engine skips a missing key and the guard logs `slice: missing ...` once per name.

Already registered by `WarriorAiObjectContext.cpp` (compiled as it is): `protection` / `tank`, `protection pve`, `pull`, `taunt`, `taunt on attacker`, `mocking blow taunt` and the warrior triggers.

Two differences from the spec's key table: `attack rti target` is a key of `ActionContext` (ActionContext.h), not of `ChatActionContext` (the spec says :142 of ChatActionContext.h); the registry
check proves the creator text and the context class for all 45, so the table above is the authority. The spec says "(1+)" values: the closure of the new sources adds none
(`master position` is the one value the sources write through `MEM_AI_VALUE` that no registered key answered; every other `AI_VALUE` the ten files read is registered by slices 1 and 2 or
answered by the missing-key guard).

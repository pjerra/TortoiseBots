# Onyxia host integration

Board: [port] Raids, Onyxia first. Donor behavior: Turtle TortoiseBots
35eec8aad727496cce55b1255262c2a4a32677e7; implementation uses the pinned
ForeverCore host APIs. No core changes.

## Observation (step 1)

Engine build only (`TORTOISEBOTS_SLICE1=ON`), off until explicitly requested:

```
.tb onyxia Bot observe <32-hex-boss-guid> [#token]
.tb onyxia Bot status [#token]
.tb onyxia Bot off [#token]
```

The GUID must resolve to entry 10184 in the bot's current map-249 instance.
Only host bots can be addressed. Start the bot's engine to tick observations.
This command never pulls, casts, moves, changes targets or writes to the DB.
Replies use the existing counted/percent-encoded host grammar. The engine-off
build refuses with `engine-not-built`.

`status` supplies phase, elapsed observation time, boss HP, current victim,
this bot's threat, sampled current boss cast, hover/flight and bot position/life.
A one-second log samples the same encounter identity and combat fields.
Flight is observed from Hover 17131 or core flight flags, not inferred from
crossing 65% HP. Landing follows observed air; an observer attaching below 40%
on the ground recognizes phase three. Idle/reset clears phase history and
elapsed time. Leaving the instance/losing the boss removes the watch; destroying
the engine/logout removes it too. No state is shared across instances.

Samples can miss short casts and are not spell-hit/death-cause telemetry.
Elapsed time starts when this observer sees combat, not necessarily at pull.
A future run controller can call this host interface without owning legacy
engine internals. Hypeer's unpushed `TbRun` run slices are not assumed here.

Validation: `tools/test_forever_onyxia.cpp` exercises actual production policy
through P1 -> P2 -> P3 -> death/reset, late attachment, wrong boss/map and
instance replacement. Native module builds cover the host calls. A live
observation test remains required; the repack was kept off during development.

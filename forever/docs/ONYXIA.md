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

## Ground combat (step 2)

Opt in each bot separately, with its engine running and host role configured.
The owner must be a living host tank in the same group and map/instance:

```
.tb onyxia Tank ground <32-hex-boss-guid> Tank [#token]
.tb onyxia DamageBot ground <32-hex-boss-guid> Tank [#token]
.tb onyxia Healer ground <32-hex-boss-guid> Tank [#token]
```

Configure the owner first. Conflicting owner selections are rejected. Ground
configuration does not pull an idle boss. Use the existing host `pull` order
on the designated tank for that same boss when ready. Other accepted manual
orders cancel that bot's ground control; canceling the owner also cancels its
followers. `observe` and `off` likewise release ground control. Owner death
selects the first living configured host tank by GUID, consistently across
participants; revival does not reclaim ownership. Leaving the group/map,
losing the boss, removing the strategy or destroying the brain releases state.

The tank uses the donor's ground anchor (22.8763, -217.152, -85.054054), with
bounded recovery toward the boss when outside melee and not its victim.
Other bots choose flank positions with melee/ranged/healing range and LOS
checks. Every launched path must be a complete native navmesh path; sampled
segments must remain inside the room, outside egg margins and (for non-tanks)
front/rear cones. Tank paths stay inside a narrower corridor. Active paths are
rechecked against moving cones and engine hazards. There is no unchecked
straight-line fallback. Failed routes log `reason=no-checked-route`; a first
live trial must stage non-tanks on safe flanks, since this increment does not
construct an outer arc from unsafe starting positions.

Normal class actions supply single-target combat. Non-owner boss damage waits
for a living owner who is the boss victim, has at least 60% health and a 3000
threat lead. It holds again at 40% health, lost victim ownership or negative
lead; spare tanks stay held until selected as owner. Held pets stop attacking
and temporarily become passive; their prior reaction is restored on release
or cleanup unless it was manually changed. Existing healing actions continue;
this increment does not redesign healer behavior. Already launched projectiles
are not recalled. Healing actions are exempt from the damage gate. Enemy area spells are held
using the existing action-name policy and spell shapes; single-target threat
metadata still needs live verification for each class.

Ground routing/attack control yields during observed flight. It is **not** the
air, breath, fear/eruption or whelp implementation, nor a complete encounter
runner. `ConfigureGround`, `GroundOwner` and `ForgetGround` expose host seams
for a future run controller. Policy fixtures cover damage hysteresis and route
geometry; ON/OFF module compilation checks native API compatibility. Runtime
movement, class rotations and threat recovery still require an authorized
live test with the pinned core and map/navmesh assets.

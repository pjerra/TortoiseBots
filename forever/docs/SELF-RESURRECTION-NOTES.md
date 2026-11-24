# Self-resurrection on ForeverCore (soulstone and Reincarnation)

`SelfResurrectAction` (key `self resurrect`, `ReleaseSpiritAction.h`) is now registered in the slice context, and the two
both-core helpers in `forever/src/compat/TbCompat_D4.h` do the work instead of counting the unported `SelfResurrection`
feature. Turtle is unchanged: its twin `ai/cmangos-compat-d4.h` still reads `PLAYER_SELF_RES_SPELL` and sends
`CMSG_SELF_RES`.

## ForeverCore model (verified)

- A dead player's self-resurrection spells are the player's own list, `m_activePlayerData->SelfResSpells`
  (`src/server/game/Entities/Object/Updates/UpdateFields.h:1349`: `DynamicUpdateField<int32, 0, 20>`), a public member
  (`src/server/game/Entities/Player/Player.h:3202`) with add/remove/clear accessors (`Player.h:3124-3131`).
- The list is filled by `Player::InitializeSelfResurrectionSpells` (`src/server/game/Entities/Player/Player.cpp:28085`),
  called from `setDeathState(JUST_DIED)` (`Player.cpp:1237`) and cleared on the way back (`Player.cpp:1257`). It holds, in
  priority order, soulstone 3026, Twisting Nether 23700 and Reincarnation 21169 (the Reincarnation row needs passive 20608
  and no cooldown on 21169, `Player.cpp:28106-28108`).
- The client request is typed: `WorldPackets::Spells::SelfRes` with `int32 SpellID`
  (`src/server/game/Server/Packets/SpellPackets.h:981`) into `WorldSession::HandleSelfResOpcode`
  (`src/server/game/Server/WorldSession.h:1835`). The handler checks list membership, asks the spell store, refuses under
  `SPELL_AURA_PREVENT_RESURRECTION`, casts the spell on the player and removes it from the list
  (`src/server/game/Handlers/SpellHandler.cpp:445-459`). A missing `SpellInfo` returns silently.
- Spell lookup is `sSpellMgr->GetSpellInfo(id, difficulty)` (`src/server/game/Spells/SpellMgr.h:775`), exact by
  `(spell, difficulty)` with a difficulty fallback chain (`src/server/game/Spells/SpellMgr.cpp:706`); `DIFFICULTY_NONE` is 0
  (`src/server/game/DataStores/DBCEnums.h:937`).

## What the helpers do

- `SelfResurrectPossibleCompat`: false without a session, with an empty list, or under a resurrection-preventing aura.
  Otherwise true when at least one listed id has a `SpellInfo` (this is the guard that keeps a data gap from turning into a
  per-tick retry: the handler would return silently and the dead engine would offer the action again).
- `SelfResurrectCompat`: sends the first listed id that has a `SpellInfo`; the list is already the core's priority order. The
  answer is "the core did something": the player is alive, or the spell left the list (the handler removes it after casting).

No new config key and no SQL. A spell id without data is a silent do-less, not a crash.

## What the lead must check live

1. `.tb spell 21169` must answer the spell name (`21169 - Reincarnation, rank 1` on the live realm), otherwise the
   Reincarnation half cannot fire and the gap is server data.
2. `.tb spell 3026` (`Use Soulstone`) for the other half.
3. Shaman: `.tb login <shaman> free`, `.tb learn <shaman> 20608`, `.tb engine <shaman> on`, `.tb kill <shaman>`, then
   `.tb where <shaman>` within ~5 s: health > 0 at the death spot is the pass. `.tb revive <shaman>` resets.

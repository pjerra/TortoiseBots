# 12 Notes: `.tb cc <bot|party [leader]> [class] allow|deny <kind>... | list [#token]`

Branch: `feat/cc-permissions-12`.

## What was done

- **Pure decision logic (`ai/playerbot/policy/combat/CcPermissionPolicy.h`, new, core-free):**
  - Kind bits in the issue's order (fear, sheep, banish, shackle, sap, sleep, trap, charm; `all`/`cc` = every kind). Stuns, roots, snares and interrupts have no kind and no deny can touch them.
  - Spell-to-kind table over the AI's spell names: fear (Fear, Howl of Terror, Death Coil, Psychic Scream, Intimidating Shout, Scare Beast, Turn Undead), sheep (Polymorph + the `polymorph: ...` variants), banish, shackle (Shackle Undead), sap, sleep (Hibernate, Wyvern Sting), trap (Freezing Trap), charm (Seduction, Enslave Demon, Mind Control). Case-insensitive, no allocation. The Razuvious mind-control tactic is not the charm spell (its action is named `razuvious mind control`) and is untouched.
  - Class words incl. plurals (`@mages` = mage); the store is memory-only per (owner, class), class 0 = the all-classes entry, a missing entry reads as fear denied, an explicit allow (mask 0) wins over the all-classes entry and the default. Version bumps on change for the tick guard.
  - Turtle behaviour is unchanged by construction: nothing in the brain consults the store, and a mask of 0 (which Turtle bots always carry) returns before any kind is read.
- **Brain gate (`ai/playerbot/PlayerbotAI.h`, `ai/playerbot/PlayerbotAISpells.cpp`):**
  - New `m_ccDeniedMask` (default 0) with setter/getter.
  - `CanCastSpell(std::string ...)` refuses a denied kind up front; `CheckCanCastSpell` (the single funnel every name and id check runs through) does the same first, before the pet bypass (a succubus casts Seduction from its own spellbook). Refusal is `SPELL_FAILED_NOT_READY`, so the rotation moves on to its next spell. Marked CC and automatic CC flow through the same check (the chooser calls `CanCastSpell(spell, ...)`), so a denied mark's target is skipped there too.
- **Host command (`forever/src/host/TbCc.cpp`, `forever/src/host/TbCc.h`):**
  - Extends the existing `.tb cc` (no new `TbScripts.cpp` row, no new engine keys/slice block/compat pair): after the address, `[class] allow|deny <kind>... | list`. `allow` clears bits, `deny` sets them, on the all-classes entry or the named class. Replies are `cc` records (`verb=perms`, `op=`, `class=`, `owner=` = the commander account, `denied=` as a `;` list, `status=ok|stored|skipped`) between `cc-begin`/`cc-end`.
  - The owner is the bot's commander (`.tb master` binding) or the sender, by account id; no database anywhere. A bot whose class does not match the class word is `skipped`. A bot with no engine is `stored` and picked up at creation.
  - The OFF half answers `error code=engine-not-built` for the permission path as for the mark path, and stubs the store.
- **Push (`forever/src/host/TbEngineBrain.cpp`):** creation applies the owner's setting before the first tick; every engine tick re-syncs while the store is non-empty (one version compare when unused), so rebinds and late joiners land without touching the order files.
- **Tests:** `forever/tools/test_cc_permissions.cpp` (84 checks, standalone `g++`), `forever/tools/test_cc_wiring.py` (incl. a `--mutate` mode that fails).

Deliberate limits: a party leader literally named Mage/Priest/... reads as a class word (order that bot by name); a second GM character starts from the default (keyed by commander character's account, but a fresh account has no entries); `.tb state` does not show denied kinds; the Razuvious mind-control boss tactic ignores charm denies.

## Gates

- ON module build (`tbf-build.sh <wt> tortoisebots`, fixed script re-run): `[309/309] Linking CXX static library src/server/modules/TortoiseBots/libtortoisebots.a` (exit 0; the only warning is the pre-existing `sTerrainMgr` redefinition, also fired by untouched files).
- OFF module build (`TB_SLICE=OFF tbf-build.sh <wt> tortoisebots`, fixed script re-run): `[27/27] Linking CXX static library src/server/modules/TortoiseBots/libtortoisebots.a` (exit 0).
- Census (`tbf-build.sh <wt> census:forever/SLICE9.txt`, fixed script re-run): `files 268 compile clean 268 with errors 0 error places 0` (exit 0).
## Expected live output

```
cc-begin #c1
cc bot=Mwarlock verb=perms op=deny class=warlock owner=3 status=ok denied=fear #c1
cc-end count=1 records=1 verb=perms op=deny target=Mwarlock bots=1 ok=1 stored=0 skipped=0 #c1
```

```
cc-begin #c2
cc bot=Mwarlock verb=perms op=list class=warlock owner=3 status=ok denied=fear;sheep #c2
cc bot=Mmage verb=perms op=list class=mage owner=3 status=ok denied=fear #c2
cc-end count=2 records=2 verb=perms op=list target=party:Mtank bots=2 ok=2 stored=0 skipped=0 #c2
```

A warlock ordered `deny fear` then fights: its rotation casts never include Fear/Howl of Terror/Death Coil, and its `cc target` value never resolves through a fear spell (a fear mark stays on the icon with no cast). In the OFF build: `error code=engine-not-built cmd=cc ...`.

## Untested

- `worldserver` link and live console run (no live server in this environment): the exact steps and outputs above are expected, not observed.
- Dungeon-run CC interplay (marked/auto CC under a deny) beyond the shared cast check: covered by code path, not by a live fight.

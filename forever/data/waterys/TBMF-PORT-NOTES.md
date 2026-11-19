# Port notes: TortoiseBotsManager 2.4.1 → TortoiseBotsManagerForever

Target client: **WoW Forever 1.60.1** (`## Interface: 16001`, modern Classic API).
Base addon: **TortoiseBotsManager v2.4.1** by Sagiroth (MIT), the newest release on
2026-11-17. The protocol is unchanged (PROTOCOL.md); this file lists every API change,
what is not portable, and what to ask Sagi before the server module is written.

## Identity and packaging

| Item | 1.12 addon | Forever port |
|---|---|---|
| Folder / addon name | `TortoiseBotsManager` | `TortoiseBotsManagerForever` |
| `.toc` | `## Interface: 11200` | `## Interface: 16001`, title suffix “Forever” |
| Version | `2.4.1` | `2.4.1-forever.1` (`TB.C.VERSION` stays a 3-number semver for the update check) |
| SavedVariables | `TortoiseBotsDB` | unchanged, so port and upstream share a saved-variables name and prefs format |
| Media path | `Interface\AddOns\TortoiseBotsManager\Media\` | `...\TortoiseBotsManagerForever\Media\` |
| Credit | Sagiroth | kept in the header, /tbm help and NOTICE; port noted in the `.toc` `X-Port` field |

New files: `Compat.lua` (all modern API replacements), `FakeServer.lua` (`/tbmf`),
`PROTOCOL.md`, `PORT-NOTES.md`, `NOTICE`.

## Client API changes (all files)

1. **Event and script signatures.** Every `SetScript("OnEvent", function() …)` became
   `function(self, event, ...)` with `local arg1 … = ...` where the body reads the event
   arguments. Every `SetScript("OnClick"/"OnMouseUp"/…, …)` became `function(self, button)`,
   `OnUpdate` `function(self, elapsed)`, and the other handlers `function(self, …)`.
   The 1.12 globals `this`, `event`, `arg1…` are gone from the addon. The same conversion
   was applied to the table callbacks `onClick = function()`, `onEnter`, `onLeave` in
   `TB.UI.CreateButton/CreateIconButton`, and to named handlers passed to `SetScript`.
2. **Backdrops.** Every `CreateFrame` call without a template now passes
   `BackdropTemplateMixin and "BackdropTemplate" or nil`, so `SetBackdrop` keeps working on
   modern clients (BackdropTemplate is required since 9.0). Original calls that already
   passed a template are untouched.
3. **Lua 5.0 → 5.1 library.** `table.getn(x)` → `#x`; `string.gfind` → `string.gmatch`;
   `math.mod` → `math.fmod`; `getglobal(x)` → `_G[x]`; `table.setn` (vanilla array-size
   hint) removed from `Roster.lua` (`clearTable` clears the slots; 5.1 derives the size).
4. **Addon messages.** `RegisterAddonMessagePrefix`/`SendAddonMessage` globals replaced by
   `C_ChatInfo` behind `TB.RegisterAddonPrefix` and `TB.SendAddonMessageCompat`
   (`Compat.lua`): prefix `TBM` is registered on `ADDON_LOADED`; a send refused with result
   code 3 or 8 (throttle) is retried once a second, five times; a final refusal is traced
   and the command falls back to the existing chat queue. `CHAT_MSG_ADDON` is read with the
   modern argument order `(prefix, text, channel, senderName, …)`.
5. **Group size.** `GetNumPartyMembers`/`GetNumRaidMembers` replaced by
   `TB.PartyCount()`/`TB.RaidCount()`: `GetNumSubgroupMembers()` for the party,
   `IsInRaid()` + `GetNumGroupMembers()` for the raid, with the legacy functions as a
   fallback (the offline harness mocks them).
6. **Bags.** `GetContainerNumSlots/GetContainerItemLink/GetContainerItemInfo/
   PickupContainerItem/SplitContainerItem/CursorHasItem` go through `TB.Bag*`
   (`C_Container` first). `C_Container.GetContainerItemInfo` returns a table, so
   `TB.BagInfo` unpacks it into the old `(icon, count, locked, quality, itemID)` shape.
   `TB.BagReady()/HasBagPickup()/HasBagSplit()/HasBagCursor()` keep the features disabled
   when the client lacks them (the mail-attach path checks them).
7. **Unit power.** `UnitMana/UnitManaMax` → `TB.UnitMana/UnitManaMax` (`UnitPower(unit, 0)`).
8. **Auras.** The two blessing/drink icon checks use `TB.BuffIcon`: `C_UnitAuras` when
   present, else `UnitBuff` with the modern return order (name first, icon second).
9. **Sound.** `PlaySound("ReadyCheck")` → `TB.PlaySound("ReadyCheck", "READY_CHECK")`
   (`SOUNDKIT.READY_CHECK`; the old string form is attempted under `pcall` for safety).
10. **Mail attachment.** `GetSendMailItem()` (1.12, no arguments, `name, texture, count`)
    → `TB.SendMailItem()`: passes slot `1` and accepts both return shapes (modern is
    `name, itemID, texture, count`), returning `name, count`.
11. **Chat filtering.** The 1.12 `ChatFrame_OnEvent` replacement hook was removed;
    `ChatFrame_AddMessageEventFilter` (present on this client, also used by Karrz's
    ForeverBots) now hides `.bot…`, `TBM:…` and stock bot replies. The offline tests were
    updated to assert the filter path.
12. **Static popups / misc.** `StaticPopupDialogs` callbacks take the popup frame (`self`)
    and read `self.editBox` (one dialog, `BotTotem.lua`). The target-frame button anchors
    to `_G["TargetFrame"]` instead of `getglobal("TargetFrame")`. The search-box placeholder
    wrapper in `Utils.lua` now forwards the handler arguments it used to drop.
13. **New accessor.** `TB.GetClaimedBots()` (read-only list of guild bots) — the fake-server
    test needs it and the guild tab already had the data internally.

## Offline testing and debug modes (new)

- `/tbmf on|off` — message trace: every addon message sent (`>`) and received (`<`) shows
  in chat (the same trace as `/tbm trace`, with the transport label).
- `/tbmf fake` — fake server: commands are answered locally through the real
  `TBM:*` parsers (a canned party of two bots, one claimed guild bot, an inventory, a
  professions sheet, a bot state and one guild order), so every tab can be clicked without
  a server. Nothing leaves the client. Limitation: party/raid membership comes from the
  client's group state, so the party tab stays empty while offline; the roster, guild,
  order, bot panel and log tabs work.
- `/tbmf status` — trace/fake/transport state.

## Verified

| Check | Command | Result |
|---|---|---|
| Regression suites (upstream tests, harness adapted) | `lua5.1 tests/regression.lua .` | pass, 6 suites (wire 214 controls, alt summary, geometry, community, phase 0, regression) |
| Fake server suite | included in the regression run (`tests/fake_mode.lua`) | pass |
| UI smoke: every window/tab built, every registered event fired | `sh tests/ui_smoke.sh .` | pass |
| Static lint (WoW globals + modern API use) | `luacheck *.lua` (`.luacheckrc`) | 0 warnings, 0 errors (26 files) |
| Port lint (Lua 5.1 compile of every .toc file, no 1.12 API left) | `lua5.1 tests/lint.lua .` | pass |

The upstream `tests/lua50_syntax.lua` (a Lua 5.0 guard) was removed; `tests/lint.lua` was
rewritten for this port. The regression `mock` translates the tests' 1.12
`event`/`arg1`/`this` style into the modern handler arguments at `SetScript`.

## Not portable / needs a check on the real client

- **Dropdown and faux-scroll FrameXML.** `UIDropDownMenu_*` and `FauxScrollFrame_*` are
  still present on modern clients but deprecated; the port keeps them. If 1.60.1 has
  removed them, the menus move to the `Menu` API and the lists to a plain ScrollFrame.
- **Mail internals.** `MailFrame`, `MailFrameTab_OnClick`, `ClickSendMailItemButton`,
  `SendMailNameEditBox`/`Subject`/`Body` and `GetSendMailItem` are unverified on 1.60.1.
  The attach path fails closed: if the globals are missing the button refuses and says
  which API is missing.
- **`TargetByName`** (target button for a party row) exists in modern Classic but is not
  exercised offline; the button hides when it returns false.
- **Chat fallback in combat.** Karrz's notes say this client refuses `SendChatMessage`
  from addon code in combat/battlegrounds; until the module advertises
  `TBM:TRANSPORT|party`, the chat path may be refused in combat. The addon shows the
  failure in the status line; the eventual module should always advertise the addon channel.
- **Addon-message delivery.** ForeverBots works around a client build that did not raise
  `CHAT_MSG_ADDON` for hidden whispers (70658) by also accepting marked system lines. This
  addon already parses both transports, but only if the module mirrors replies onto the
  transport it was asked on.
- **Bindings.xml.** Unchanged; modern clients read `Bindings.xml` the same way. The
  override bindings themselves were not exercised offline.

## Open questions for Sagi

1. Should the Forever module keep the addon-message transport on `PARTY` (prefix `TBM`,
   bare-command payload) and mirror replies as addon messages? Any plan to change the
   transport line or add capabilities (e.g. `forever`)?
2. Does 1.60.1 still expose `GetSendMailItem(slot)` with `(name, itemID, texture, count)`
   and the stock Send Mail frame globals, or should the mail feature be feature-gated?
3. Are `UIDropDownMenu_*` and `FauxScrollFrame_*` still shipped in the FrameXML of
   Forever (they are deprecated on retail)? If not, menus/lists need the `Menu`/
   `ScrollFrame` rewrite.
4. `TargetByName` semantics on Forever (server-side target) — keep the target-frame
   button, or drive it from the roster only?
5. Keep the `TortoiseBotsDB` SavedVariables name, or rename to
   `TortoiseBotsManagerForeverDB` to avoid clashing with a 1.12 install on the same
   machine? (We kept it for preference continuity.)
6. Any module-side version/protocol bump planned? The port reports `TBM:PROTO|1|0` today
   and understands the whole 2.4.1 protocol.

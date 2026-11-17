# TortoiseBots Manager Forever: addon ↔ server protocol

This is the wire contract between **TortoiseBotsManagerForever** (this addon) and the
TortoiseBots server module. It is **unchanged from TortoiseBotsManager 2.4.1** on purpose:
a server module written for the 1.12 manager must talk to the Forever port without edits.
Only the client API underneath changed (see PORT-NOTES.md).

Direction follows the player: **C→S** is the addon asking, **S→C** is the module answering.

## 1. Transport

| | |
|---|---|
| Addon-message prefix | `TBM` (`C.ADDON_PREFIX`) |
| Addon channel | `PARTY` (`C.ADDON_TRANSPORT`), after the server advertises it |
| Chat fallback | `.bot <verb> [args]` via `SendChatMessage`, until the server advertises the addon channel |
| Addon-message payload | the command **without** the `.bot ` prefix: `<verb> [args]` |

- The client asks `roster`; the module answers with `TBM:TRANSPORT|party`. From then on
  commands travel as addon messages on `PARTY` and the module mirrors its chat replies
  onto the same addon channel. A `TBM:TRANSPORT|<other>` switches the addon back to chat
  (`.bot …`).
- Replies that arrive as addon messages are parsed exactly like the system-chat lines:
  `<prefix>`, `<text>`, `<channel>`, `<sender>` → `OnAddonMessage`. On modern clients the
  event is `CHAT_MSG_ADDON` with `(prefix, text, channel, senderName, senderGUID, isPing, isZone)`.
- Chat-sent commands are hidden locally (`.bot …`, `TBM:…`, the module's stock replies);
  failures and non-protocol lines stay visible.
- Addon messages are throttled by the client (about ten at once, then one per second).
  The port retries a line refused with result code 3 (`AddonMessageThrottle`) or 8
  (`ChannelThrottle`) once per second, five times, and traces the give-up in `/tbmf on`.
- The addon throttles itself: one request per `C.SEND_DELAY` = 0.35 s, rest queued in order.

## 2. Field conventions

- Fields are separated by `|`; the first field is `TBM:<TYPE>`.
- `-` (or an empty field) means "none/unknown" unless a type says otherwise (`inf` for the
  order time limit, `0/0` for an empty run).
- Lists inside one field use `,` (state keys, capabilities, marks), `+` (known spell
  names), `;` (item/reagent rows) and `:` (numbers inside those rows). Values may contain
  spaces but never `|`.
- Numbers are decimal; ids are the game's entry ids.
- A stream (`*_BEGIN … *_END`) is committed only when its promised count matches; a
  mismatch is reported and the stream discarded. Extra trailing fields are ignored, so a
  newer module can append fields without breaking older addons.

## 3. Handshake and capability lines

| S→C line | Fields | Meaning |
|---|---|---|
| `TBM:TRANSPORT|<channel>` | `party`, or another channel name | Use addon messages from now on / go back to chat |
| `TBM:PROTO|<protocol>|<minAddonProtocol>` | int, int | Module protocol level and the oldest addon protocol it accepts |
| `TBM:VERSION|<version>` | e.g. `v2.4.1`, or a dated build id | Module version (semantic = shown in the header) |
| `TBM:CAPS|<cap>[,<cap>]…` | capability tokens | Features the module offers; each line replaces the whole set |
| `TBM:LATEST|<moduleVersion>\|<addonVersion>` | versions or `-` | Newest releases known to the module; drives the update hint |

Capability tokens the addon gates on: `order`, `order-steps`, `guild-order-steps`,
`professions`, `blessing`, `choice`, `service`, `totem`, `loot-log`, `guild-bots`,
`altsummary`, `dungeon-run`. An unknown token is ignored.

## 4. Commands (C→S)

Everything after `.bot ` on chat is sent verbatim as the addon-message payload. Replies
are listed with each command; `ACK`/`ERR` mean `TBM:ACTION_ACK|…` / `TBM:ACTION_ERR|…`.

### Lifecycle / roster
| Command | Reply |
|---|---|
| `roster`, `list` | `TBM:TRANSPORT`, `PROTO`, `VERSION`, `CAPS`, roster stream, `TBM:BOTSTATE_*` trailer |
| `alts` | `TBM:ALTSUM|…` lines + `TBM:ALTSUM_END|<count>` |
| `invite <Bot>` | `ACK invite …` and/or the stock "Invitation sent" chat line |
| `summon <Bot>` | `ACK summon …` / the stock "Summoning …" line |
| `help` | `Bot commands: …` (hidden; proves the module is present) |

### Party actions and switches
| Command | Reply |
|---|---|
| `action <intent> [<Bot>]` | `ACK <intent>|<party\|bot:Name>|<n>|<detail>`, `ERR <intent>|<code>|<message>` |
| `behavior <Bot>` | `TBM:BOTSTATE_BEGIN|1`, `TBM:BOTSTATE|<bot>|<k=v,…>`, `TBM:BOTSTATE_END`, `ACK behavior` |
| `behavior party <key> <on\|off>` | `ACK behavior|party|<n>|<key>=<on\|off>` |
| `strategy <token>` / `strategy ±feature` | `ACK strategy …`, the "Strategy change … applied" line |
| `formation <scope> <name>` | `ACK formation …` ("formation set to …") |
| `role <scope> <role>` | `ACK role …` |
| `totem <Bot> status` | `TBM:TOTEM|<bot>|earth=…,fire=…,water=…,air=…,mode=<auto\|prepull\|manual>[,known=a+b]` |
| `totem <Bot> set <four names,>` | `ACK totem set …`, `ERR totem set …` |
| `totem party drop` | `ACK totem drop …` |
| `blessing <scope> status` | `TBM:BLESSING|<bot>|warrior=…,paladin=…,druid=…,known=…` (one line per paladin) |
| `choice <Bot> <kind> status` | `TBM:CHOICE|<bot>|<kind>=<value\|auto>,known=v+v` |
| `service <Bot>` | `TBM:SERVICE|<bot>|<food>|<water>|<healthstone>|<portals>` |
| `repair`, `sell`, `learn`, `rest`, `ready`, `rebuff`, `release`, `downtime`, `autogear <Bot>`, `maintenance <Bot>` | `ACK`/`ERR` (buttons; same words as the server commands) |

### Bot panel
| Command | Reply |
|---|---|
| `inv <Bot>` | `TBM:INV_BEGIN|<bot>|<copper>`, `TBM:INV_EQ|…`, `TBM:INV_ITEM|…`, `TBM:INV_END|<bot>|<free>|<total>` |
| `professions <Bot>` | `TBM:PROF_BEGIN|<bot>`, `TBM:PROF|…`, `TBM:RECIPE|…`, `TBM:RECIPE_MISSING|…`, `TBM:PROF_END|<bot>|<recipes>[|<missing>]` |
| `item <Bot> equip <bag> <slot>`, `item <Bot> unequip <slot>`, `item <Bot> use <bag> <slot>`, `item <Bot> trade` | `ACK item …`, refreshed `INV_*` snapshot |
| `command <Bot> <verb> [args]` | stock chat replies (whisper mirror via `ACK`), e.g. bank/mail/spells/pet |

### Guild orders
| Command | Reply |
|---|---|
| `order list` | `TBM:ORDER_ACCESS|…`, `TBM:ORDER_BEGIN|<n>`, `TBM:ORDER|…` rows, `TBM:ORDER_END` |
| `order search <Bot> <farm\|buy\|craft> [cat=<c>] <text>` / `order search <Bot> enchant <text>` | `TBM:ORDER_ITEMS_*` or `TBM:ORDER_ENCHANTS_*` stream |
| `order <Bot> <farm\|buy\|craft> <itemId> <n> [hours\|inf] [keep] [gold=<copper>] [bring=<ids>]` | `ACK order|bot:<Bot>|1|<verb>`, `ERR order|<code>|<message>` |
| `order <Bot> enchant <spellId>` / `order <Bot> preview …` | same `ACK order …` |
| `order <Bot> cancel` / `remove` | `ACK order …` |
| `order <Bot> steps` | `TBM:ORDER_STEPS_BEGIN|<bot>|<n>|<dropped>`, `TBM:ORDER_STEP|…`, `TBM:ORDER_STEPS_END|<bot>` |
| `order access` / `order rank <i>` / `order reassign <sub> <new>` | `ACK order …` + `TBM:ORDER_ACCESS|…` |
| `order <Bot> meet` | `ACK order|bot:<Bot>|1|meet coming\|meet later\|meet gathering` |

### Loot history, run leader, server info
| Command | Reply |
|---|---|
| `lootlog` / `lootlog <cursor>` | `TBM:LOOT_BEGIN|<n>|<nextCursor>`, `TBM:LOOT|…`, `TBM:LOOT_END|<nextCursor>` |
| `run list` | `Dungeon plans: …` chat line + `ACK run list|plans|<n>|-` |
| `run start …`, `run pause`, `run resume`, `run skip`, `run stop [reason]`, `run here`, `run status` | `TBM:RUN|…`, `ACK`/`ERR run …` |
| `commander bind <lead> [<bot> …] inparty`, `commander unbind`, `commander status`, `cmd enter near` | `ACK`/`ERR commander …`, `TBM:RAID_LAYOUT|…` |
| `features` | `Server features: …` chat line + one `TBM:FEATURE|<key>|<value>|<label>` per switch |
| `features all <text>` | `Config: N keys match …` + `TBM:CONFIG_BEGIN|<matches>`, `TBM:CONFIG|<key>|<value>`, `TBM:CONFIG_END|<shown>|<matches>` |

## 5. Replies (S→C)

### Roster and guild bots
```
TBM:ROSTER_BEGIN|<n>
TBM:ROSTER|<guid>|<name>|<classId>|<serverState>|<group 0|1>|<location>|<ccMark|->|<className>
TBM:ALTBOTS|<Name1,Name2>        (optional; owned alts playing on their own)
TBM:ROSTER_END
TBM:CLAIMED_BEGIN|<n>
TBM:CLAIMED|<guid>|<name>|<classId>|<level>|<serverState>|<group 0|1>|<location>|<gearLocked 0|1>|<profs|->|<alt 0|1>|<className>
TBM:CLAIMED_END
TBM:CC_ASSIGN_BEGIN|<n>
TBM:CC_ASSIGN|<Name>|<mark>
TBM:CC_ASSIGN_END
```
`<serverState>` is a lifecycle word (`online`, `starting`, `offline`, …). `<profs>` is
`skillId:value;…` or `-`. Rows are unique by name; a rejected row drops the whole stream.

### Alt summary
```
TBM:ALTSUM|<name>|<text>          (text is a whole sentence, may contain item links)
TBM:ALTSUM_END|<count>
```

### Bot panel
```
TBM:BOTSTATE_BEGIN|<n> |  TBM:BOTSTATE|<bot>|<key>=<value>[,<key>=<value>] |  TBM:BOTSTATE_END
TBM:INV_BEGIN|<bot>|<copper>
TBM:INV_EQ|<slot>|<itemId>|<durability>|<maxDurability>[|<slots>]
TBM:INV_ITEM|<bag>|<slot>|<itemId>|<count>|<flags>
TBM:INV_END|<bot>|<free>|<total>
TBM:PROF_BEGIN|<bot>
TBM:PROF|<skillId>|<value>|<max>
TBM:RECIPE|<skillId>|<spellId>|<itemId|0>|<orange>|<yellow>|<green>|<grey>[|<order>]
TBM:RECIPE_MISSING|<skillId>|<spellId>|<itemId|0>|<reqSkill>|<recipeItemId|0>
TBM:PROF_END|<bot>|<recipeCount>[|<missingCount>]
TBM:SERVICE|<bot>|<food 0|1>|<water 0|1>|<healthstone 0|1>|<portal spellId:city,…|->
TBM:TOTEM|<bot>|earth=<v>,fire=<v>,water=<v>,air=<v>,mode=<auto|prepull|manual>[,known=a+b]
TBM:CHOICE|<bot>|<kind>=<value|auto>,known=<v>+<v>
TBM:BLESSING|<bot>|warrior=<b>,paladin=<b>,…,druid=<b>,known=<b>+<b>[,players=<Name>:<b>+…]
```
`INV_*` flags: `e` equippable, `u` upgrade over the equipped item, `b` no-trade.
`BOTSTATE` keys are the per-bot switches (e.g. `move=follow`, `loot=on`, `aoe=off`).

### Orders
```
TBM:ORDER_BEGIN|<n>
TBM:ORDER|<bot>|<type>|<itemId>|<item>|<have>|<want>|<state>|<reason>|<zone>|<difficulty>|
          <hours>|<keep>|<parent>|<goldCap>|<step>|<need>|<carry>|<bring>
TBM:ORDER_END
TBM:ORDER_ACCESS|<rankIndex>|<rankName>|<tabsOk>|<tabsDenied>|<gold 0|1>|<canSetRank 0|1>
TBM:ORDER_ITEMS_BEGIN|<n> / TBM:ORDER_ITEM|<itemId>|<item>|<quality>|<sources>|<minLevel>|<skill>|<verdict>|<zone>|<chance>|<difficulty>|<category> / TBM:ORDER_ITEMS_END
TBM:ORDER_ENCHANTS_BEGIN|<n> / TBM:ORDER_ENCHANT|<spellId>|<name>|<slot>|<reqSkill>|<verdict>|<reagents> / TBM:ORDER_ENCHANTS_END
TBM:ORDER_WHO_BEGIN|<n> / TBM:ORDER_WHO|<type>|<itemId>|<item>|<count>|<bot|->|<skill>|<have:…> / TBM:ORDER_WHO_END
TBM:ORDER_STEPS_BEGIN|<bot>|<n>|<dropped> / TBM:ORDER_STEP|<bot>|<ageSec>|<state>|<reason>|<step>|<itemId> / TBM:ORDER_STEPS_END|<bot>
TBM:ORDER_PREVIEW|…                        (same row shape as ORDER)
```
`ORDER` inside `BEGIN/END` replaces the whole list; outside a stream it updates one bot's
row. `need`/`reagents` are `itemId:need:have;…` (or `-`); `bring` is a comma list of
ticked item ids (or `-`). Step lists are memory-only and capped by the server.

### Loot, run, server info
```
TBM:LOOT_BEGIN|<n>|<nextCursor>
TBM:LOOT|<ts>|<bot>|<event StoreLootAction|LootMoney|LootRoll>|<itemId>|<item>|<sourceType>|<source>|<quality>
TBM:LOOT_END|<nextCursor>
TBM:RUN|<plan>|<state>|<step>/<steps>|<leg>|<kind>|<phase>|<note>[|left <K>][|speed <x>]
TBM:RAID_LAYOUT|<bots>|<tanks>|<tankHealers>|<raidHealers>|<groups>
TBM:RAID_LAYOUT|warning|<text>
TBM:FEATURE|<key>|<value on|off|N>|<label>
TBM:CONFIG_BEGIN|<matches> / TBM:CONFIG|<key>|<value> / TBM:CONFIG_END|<shown>|<matches>
TBM:LATEST|<moduleVersion|->|<addonVersion|->
```

### Acknowledgements and errors
```
TBM:ACTION_ACK|<intent>|<scope>|<count>|<detail>
TBM:ACTION_ERR|<intent>|<code>|<message>
```
`<scope>` is `party`, `raid` or `bot:<Name>`; `<count>` is the number of bots the order
reached; `<detail>` is verb-specific (`on`/`off`/`mixed`, a stance, a CC mark, a meet
state, `-` for none). Unknown intents are shown in the status line, never crash.

## 6. Compatibility rules for the Forever module

1. Keep the prefix `TBM`, the verbs and the field order above.
2. Append new fields at the end only; older addons ignore extras.
3. A stream's `BEGIN` count is authoritative; send `END` only after the promised rows.
4. Keep replies on the transport that carried the request (addon message ↔ chat).
5. Do not reuse a state/reason token with a new meaning; unknown tokens are displayed raw.
6. `TBM:TRANSPORT|party` must be sent before the addon-message channel is expected.

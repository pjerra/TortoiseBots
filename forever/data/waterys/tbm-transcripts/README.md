# Live Turtle TBM transcript fixtures

Captured November 18, 2026, approximately 02:16–02:25 CST, from Waterys' live Turtle server. These are **observed replies**, including real errors and missing features, not examples generated from the specification.

## Reading a fixture

Each `<verb>.txt` groups real requests in capture-time order. Between `REQUEST` and `REPLIES BEGIN` is the exact addon payload or `.bot` chat command. Between `REPLIES BEGIN` and `REPLIES END` are the complete received reply texts, unchanged and in arrival order. Multiple sections cover different transports or state transitions. A repeated command can legitimately fail after the first already changed the state (for example unbind/remove).

Addon requests used prefix **TBM**, channel **PARTY**, through turtle-agent's `tbm` RPC with `via=addon`; chat requests used `via=chat`, which sends `.bot <command>` through its normal chat path. Addon replies were actually recorded as addon events with prefix TBM. The client's decoded channel label is `guild` on these responses; that label is retained as observed, not silently changed to PARTY. It does not change the explicitly requested outbound channel.

Capture paged the event log without overflow, waited for a quiet response window (at least three seconds total and two seconds without a chat/addon event), and preserved bot whispers as well as system/addon messages. All observed inventory/profession/config streams have their END markers. Counted roster, assignment, state, loot and recipe streams were checked against their declared counts. A single unrelated “GM Labbytres just logged in.” notification was excluded from a professions reply; its exclusion is recorded in the manifest. There are no omitted reply rows or ellipses inserted into the payloads.

## Coverage and limits

- All **35 top-level verbs** enumerated in section 4 of the supplied `TortoiseBotsManagerForever/PROTOCOL.md` have both addon and chat captures. This is not an assertion that every possible subcommand or parameter combination was exercised.
- **38 verb files / 111 requests**, including supplemental `status`, setup `hire`, and cleanup `remove`. `manifest.json` lists each exact request, requester, transport, reply count and source-capture hash.
- Labby supplied read-only examples using his existing hires. Labby's 39-member raid was retained throughout.
- Claude authorized Labbytres as a separate requester for mutations. He stayed outside instances and hired one level-20 mage, **Yonder**, for 50 silver. Mutation examples concern only that temporary bot/one-bot party. Toggles, formation, role, equipment and commander binding were restored; the hire was then dismissed and its character deletion verified. Labbytres logged out. No raid pulls or server changes occurred; the pull lock stayed byte-for-byte unchanged.
- Both controllers lack guild membership. Guild-order files contain authentic permission errors, **not successful order or recipe-search streams**. No guild or shared access settings were changed.
- `choice`, `blessing`, and `rebuff` are unknown commands on this deployed build. `behavior <Bot>` is rejected with usage; `behavior party loot off` is accepted. The documented slot-only `item ... unequip 15` is rejected; `item ... unequip 255 15` succeeds and is followed by the full inventory stream. These differences are deliberately preserved.
- `roster` gives zero persistent roster rows but includes all temporary hires in the assignment/state trailers. No PROTO/VERSION lines were added where the server did not emit them. Use the recorded capability set rather than assuming every feature in the document is live.
- `run` examples are list/status only. No run was started, paused, skipped or resumed. `cmd enter near` records the unbound refusal; additional bound stay/follow examples demonstrate successful commander ACKs without entering an instance.

Start with [first-verbs.md](first-verbs.md) for companion-UI implementation order.

## Provenance and validation

- Source specification: `PROTOCOL.md` in TortoiseBots/TortoiseBotsManagerForever (its digest is in `provenance.json`).
- Captured server image: `sha256:de89e2cbe3bf9538a66ba0b96e5ba8b1de836facfbc17deecc03a5fcbf93e94f`; container started `2026-11-18T04:15:36.023346081Z`.
- Branch base: latest fetched `origin/port/forever`, `fe4c6dce2b7c702e25b1ae5809e48bbd30daef39`. Local branch `waterys/tbm-transcripts`; no push.
- Operator evidence and capture scripts are kept locally by Waterys; reply texts are self-contained here. No credentials or account configuration files are included.
- Validation: both transports for every required verb; no event-buffer overrun or capture deadline; matching counted stream lengths and complete END markers; exact ownership/cleanup and unchanged Labby raid/STOP checks. No module/core code or host hook changed, so no native build or gameplay test was run. Agent-assisted capture and documentation.

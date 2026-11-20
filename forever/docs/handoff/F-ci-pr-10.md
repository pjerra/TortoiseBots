# F. CI PR #10 (retarget to `main`)

PR #10 (`ci/port-forever`, already based on `main`, one line in `.github/workflows/build.yml`) adds `port/forever` to the
`pull_request` branch filter. `port/forever` is retired and `main` is already in that filter (`branches: [main, dev]`),
so the line is moot. Retargeting means: drop `port/forever`, then either close the PR or make it useful. As written, the
Linux and Windows build jobs check out the **Turtle** core with the module, so PRs into `main` never build the `forever/`
side. The useful replacement is a job that checks out ForeverCore at `forever/PINNED_CORE`, links the module as
`src/modules/TortoiseBots`, and builds `worldserver` twice, `-DTORTOISEBOTS_SLICE1=ON` and OFF, on paths under `forever/`
and the shared `ai/`, `runtime/`, `commands/` folders. Mind the org's limit of 20 concurrent jobs. Whether to add that job
or just close #10 is your call.

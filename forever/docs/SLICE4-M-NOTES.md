# Slice 4, package M4: merge, link, live test

Branch `slice4/integ` (worktree `~/wt-slice4-integ`), build slot 1, off `port/forever` (3b9138052, slices 1-3 and the wide pass). Times are the build VM's UTC. Core read only (pin 6517af05), zero core changes. Test server: the build VM's own, `~/forever-run`, driver `~/forever-build/m4-sess.sh` (m3's, binary `~/forever-build/m4-on/worldserver`); `forever-test1` never touched.

## 1. The merge
`git merge --no-ff tbf/slice4/p` then `tbf/slice4/h` (both local branches equal the remote ones: 11d24e55a and 3b1e59c84). **No conflicts** (H4 predates the wide pass; the two packages touch disjoint files). Needs done in one commit (f78702bf8):
* `CMakeLists.txt` reads `forever/SLICE4.txt` (176 lines, 174 files); comment updated.
* Entrance check: the stricter one in both places. P4's `InstanceEntranceKnownCompat` already required a real `world_safe_locs` row; H4's `BotMgr::ReleaseHasEntrance` (TbBotMgr.cpp) now requires `GetWorldSafeLoc(id) != nullptr` as well as `id != 0`, so the host's `relhold=no-entrance`, the order refusal, the lease answer and the engine's hold agree.
* H4's `relhold` is NOT switched to P4's `AutoReleaseSpiritAction::ReleaseHoldReason()`: the host key stays computed from core facts (no engine header in host code); the live test compares it with the engine's behaviour.
* Generated: `slice_gen_context.py --check` current (`registered 17/48/75/170`), registry check `(a)=0 (b)=215`.

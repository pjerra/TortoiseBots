# W0-A: pool registry and creation (world slice W0, round A)

Engine side of the bot pool on ForeverCore, core pin `25fc7ca` (decision D1/D2 of `FOREVER-W0-W4-SPEC.md` part 1): the
characters-database registry, the balanced composition and the paced creation of level-1 characters. No scheduler, no tiers, no
live pool yet (rounds W0-B, W0-C, W0-L). Everything is off by default. Zero core changes.

Spec: `~/TortoiseBots-audit/FOREVER-W0-W4-SPEC.md` section 1.3 row W0-A, config in 1.3, U2/U4 in 1.6. Rules:
`SLICE-WORKER-RULES.md`, `SLICE4-COMMON.md`. Worktree `~/wt-w0a`, branch `tbf/world/w0a` from `tbf/main` (W1 merged).

## 1. What runs

| Piece | Where |
|---|---|
| The composition: faction alternating, race in turn, class from the race's vanilla list | `ai/playerbot/policy/world/PoolCompositionPolicy.h` (pure), test `forever/tools/test_pool_composition_policy.cpp` |
| The two characters-database tables, their load and their writes | `forever/src/host/pool/TbPoolRegistry.{h,cpp}`, `forever/sql/characters/2026_11_20_02_tb_pool_rows.sql` (upgrades `2026_11_20_01_tb_pool.sql`) |
| The row's trust (name, id, creation stamp) and the unfinished-creation decisions | `ai/playerbot/policy/world/PoolRegistryPolicy.h` (pure), test `forever/tools/test_pool_registry_policy.cpp` |
| Creation: account, name, the core's own `CreateCharacter`, one per tick at the configured rate | `forever/src/host/pool/TbPoolCreate.{h,cpp}` |
| The name service of a created bot, now in the slice build | `runtime/BotNameService.cpp` (added to `forever/SLICE-W0.txt`) |
| `.tb pool create <n>`, `.tb pool prune [confirm]` | `forever/src/TbScripts.cpp` (row appended at the table end; the handler sits next to `.tb free`) |
| The world tick | `TortoiseBots::pool::CreateUpdate(diff)` right after `BotMgr::Update(diff)` in `OnBeforeMapsUpdate` |
| Config | `TortoiseBots.Pool.Enable = 0`, `.CreatePerMinute = 30`, `.MaxCharacters = 50` at the end of `forever/conf/tortoisebots.conf.dist` |
| The both-core pair (name check; the characters-database guard of the two pool tables) | `forever/src/compat/TbCompat_WL0.h` + `ai/cmangos-compat-wl0.h`, wrapper `ai/playerbot/TbCompatWL0.h` |

## 2. The decisions the code makes

- **Composition is a balanced sequence, not a random pick.** `CompositionAt(serial, classRoll)`: the serial (the pool's character
  count, so a restart continues it) alternates the faction and walks the four races of that faction; the class roll only chooses
  within that race's vanilla list. 100 picks are 50/50 factions and every race 12 or 13 times (the spec's test), and a pair the
  client cannot make cannot come out. `BotMgr::CreateCharacter` still checks `sObjectMgr->GetPlayerInfo`, so a database whose
  `playercreateinfo` disagrees with the policy refuses that one and creation stops (does less, never a character that cannot exist).
- **`info.UseNPE = false;` is now written out** in `BotMgr::CreateCharacter` before `Player::Create` (the core's default is already
  false, `CharacterPackets.h:67`; the line is the gate the spec asks for and it is above the create call).
- **Accounts.** Only accounts the module created are ever touched: a name `TBPOOL<nnnn>`, `AccountMgr::CreateAccount` with a random
  16-character password that is zeroed after the call and never logged or stored, and a random creation stamp written as the account's
  e-mail (the one account field `AccountMgr::GetEmail` reads back). A row is trusted only when its id still answers the row's
  username, that name still resolves to the id, the name is a pool name and the e-mail is the row's stamp; anything else is marked
  `stale` and never used (fix round, section 7). A creation the reconciliation cannot finish is marked `failed`: never used, and
  removed only by `.tb pool prune` (section 4d). On `AOR_NAME_ALREADY_EXIST` the name is skipped and the next number tried (an
  account outside the registry is never reused); any other result stops the job with the result code (the spec's U4). At most 10
  characters per account; one online character per account is the scheduler's rule (W0-B).
- **Registry guard.** Every statement goes through `PoolCharacterQueryCompat` / `PoolCharacterExecuteCompat` (compat WL0), built on
  the commander store's characters guard: a single-table form, a whitelist of the two pool tables, an `information_schema` check of
  the columns before every write, a null result for "no rows". The module runs its own `CREATE TABLE IF NOT EXISTS` once when the key
  is on; a missing table is one error line naming the SQL file and the pool is off, never the core's abort.
- **Load.** `LoadConfig` at boot and on `.reload config`; the rows are read on first use. An account is validated with
  `AccountMgr::GetName`, `GetId` and `GetEmail` (no auth SQL of the module's own); a row that is not the pool's any more is marked
  `stale` with one error line and never used (its account and characters are not touched).
- **Creation pacing and the cap.** One character per world tick at most, `60000 / CreatePerMinute` ms apart (30/min: 2 s), so
  `.tb pool create 200` is a job of about seven minutes. A second `.tb pool create` while one runs is refused with the number left.
  `MaxCharacters` (default 50) bounds the pool's total: a create at the cap is refused with the size and the key, a larger request
  creates only what fits, and the running job stops at the cap.
  Every failure (no name after five ticks, a refused account, the core's refusal, a GUID not in the character cache, a write that
  does not read back) stops the job and is one log line: no retry loop.
- **A created character is the core's**: `Player::Create` + `SaveToDB` through `BotMgr::CreateCharacter`, in its race's start
  position (map 0/1, never the NPE position), with the core's creation gear from `playercreateinfo_item` and `CharacterLoadout` (U2,
  measured live below).

## 3. Shared lines touched (the W2/W3/W6a and Commander P3/P6 developer)

| File | Lines |
|---|---|
| `CMakeLists.txt` | `TB_SLICE_LIST` now `forever/SLICE-W0.txt` (+ the three comment lines around it and the FATAL_ERROR text) |
| `forever/src/TbScripts.cpp` | one include (`host/pool/TbPoolCreate.h`), `pool::LoadConfig()` in `OnConfigLoad` and `OnWorldInitialized`, `pool::CreateUpdate(diff)` after `BotMgr::Update`, the `{ "pool", HandlePool, ... }` row appended at the table end, and `HandlePool` before `HandleFree` |
| `forever/src/TbBotMgr.cpp` | the `// W0` line `info.UseNPE = false;` in `CreateCharacter` (one line) |
| `forever/conf/tortoisebots.conf.dist` | new `# W0 pool` block at the end (Enable, CreatePerMinute); W0-B appends its keys there |
| `forever/tools/slice_gen_context.py` | `WL0_KEYS` (empty) and one name in the `_join(...)` call |
| `forever/tools/test_w1_wiring.py` | two assertions point at `SLICE-W0.txt`/the new `_join` list (the W1 files and keys are unchanged) |
| `forever/tools/test_slice9_l9_wiring.py` | two lines: the list it reads and the CMake pointer |
| `forever/SLICE-W0.txt` | new: `SLICE-W1.txt`'s 270 paths + `runtime/BotNameService.cpp` |

`FreeKitPolicy.h`, `SliceLinkStubs.cpp`, `UnportedFeatures.h`, `TbH3.cpp` and `RpgSubActions.cpp` are untouched: W4's.

## 4. For W0-B (scheduler)

- `BotMgr::Login(guid, master, free)` already takes the free flag; the pool tag and `IsPoolBot` are the round's.
- The registry exposes `Accounts()`, `Characters()`, `AccountRegistered`, `CharactersOnAccount`, and reads `last_login` /
  `last_logout`; the write of those two columns is not added yet (it belongs with the scheduler). A `failed` row is not in
  `Accounts()` or `Characters()`: the scheduler sees only what the database confirmed. `.tb pool prune` is the only way a
  failed row or its account goes away.
- The world tick line to extend is `TortoiseBots::pool::CreateUpdate(diff);` in `OnBeforeMapsUpdate`. The `.tb pool` handler is
  `HandlePool` in `TbScripts.cpp` (`create`, and `prune [confirm]` since the third fix round).
- Budget note of the spec (1.4): "never in the same tick as a login" is not enforced yet; it needs the scheduler's login step.

## 4b. Open point for W0-B / W0-L

`UnportedFeatures.h` and `forever/docs/UNPORTED.md` still say `pool and hire services` are not ported. That text belongs to the
W0 wave's shared-seam owner (FOREVER-W0-W4-SPEC, "Shared seams"), not to round A: the new pool replaces the legacy facade and the
line must say so once the scheduler and the live pool are in.

## 4c. Fix round (review findings)

The adversarial review of W0-A (`codex-w0a.txt`) found three defects; this round fixes them. The decisions are the lead's
(registry trust by name/id/stamp; a write-ahead creation row with a reconciliation; a total cap).

| Finding | Fix | Test that failed before |
|---|---|---|
| An account id reused by a real account was taken for the pool's | `PoolRegistryPolicy.h`: `CheckPoolAccount` (account still answers the row's username, the name still resolves to the id, the name carries `TBPOOL`, the e-mail carries the row's stamp) and a row that fails it is marked `stale`, once, and never used | `pool_registry_check` `TestIdReuseIsStale` / `TestRenamedRealAccountIsStale` / `TestReusedNameIsStale`; pre-fix probe: `FAIL probe.cpp:43: pool::Accounts().empty()` |
| A failure between the auth and character databases left an account or character outside the registry forever | Claims first: `tb_pool_account` is written `creating` (id NULL) before `CreateAccount`, `tb_pool_character` `creating` (guid NULL) before `CreateCharacter`; each step adopts what already exists by name + stamp (account) or by name on the claim's account (character); `Reconcile` at module start and before a create finishes a claim whose character the characters database has, and marks the rest `failed` (it deleted a verified pool account in this round; section 4d replaced that with `.tb pool prune`) | `pool_registry_check` `TestCreationWritesTheClaimFirst` / `TestCrashAfterAccountCreationKeepsTheAccount` / `TestCrashAfterCharacterCreationIsFinished` / `TestAForeignAccountUnderAClaimIsNeverDeleted` / `TestAClaimWithNoIdIsResolvedByItsStamp`; pre-fix probe: `FAIL probe.cpp:70: fakeAccounts.size() == CharacterDatabase.accounts.size()` |
| The per-command count did not bound the pool | `TortoiseBots.Pool.MaxCharacters` (default 50): `AllowedPoolCharacters` at the start, the running job stops at the cap | `pool_registry_check` `TestTheTotalCapRefuses` / `TestTheTotalCapClamps`; pre-fix probe: `FAIL probe.cpp:60: !start.started` |
| The source-text pins were no behavioural test | `forever/tools/pool_registry_check.cpp` compiles the two pool translation units against a fake core (`forever/tools/pool_registry_fakes/`: characters DB with the core's row contract and failure injection, auth DB, character cache, `BotMgr::CreateCharacter`) and drives real scenarios; `test_wl0_wiring.py` runs it and (with `POOL_REGISTRY_MUTATE=1`) proves each fix's mutation is caught | `python3 -m unittest forever/tools/test_wl0_wiring.py` (21 tests, 2 skipped; `Behaviour.test_the_pool_check_passes`; `POOL_REGISTRY_MUTATE=1 ...Mutations` 5 mutations caught) |

### 4d. Fix round 3 (the review's critical finding)

The adversarial review of the fix round (`codex-w0a-r2.txt`) found two defects, both in the same place: a claim's word was
worth more than a database read.

| Finding | Fix | Test that failed before |
|---|---|---|
| [critical] a transient character-cache miss deleted an account and every character on it: reconciliation read a `creating` character row as "not in the cache, so never created", dropped the row, saw no ready character and called `AccountMgr::DeleteAccount` | Reconciliation deletes nothing, ever: a claim is either finished - proven by a read of the core's own `characters` table (`PoolCharacterOwnerCompat`), never the cache - or marked `failed` with a log line. A database that does not answer leaves the claim as it is (the next start tries again). Account cleanup is the explicit `.tb pool prune [confirm]` (below). | `pool_registry_check` `TestACacheMissDoesNotDeleteTheAccount` / `TestAnUnansweredDatabaseKeepsTheClaim`; mutation `reconciliation-decides-from-the-cache` |
| [high] a queued cross-database commit counted as a successful creation: `BotMgr::CreateCharacter` queued the saves, added the cache entry and returned, and the pool marked both rows `ready` | A creation is queued, not created: `BotMgr::CreateCharacter` adds no cache entry and the pool's row stays `creating`; the pool reads the `characters` table back on later ticks (`ConfirmPending`, every 500 ms, 30 s at most) and only then writes `ready`, the account row and the cache entry (`PublishCharacter`). `BotMgr::ConfirmCreations` publishes the cache entry of a `.tb create` character the same way, from the database. A commit that never lands marks the row `failed`. | `pool_registry_check` `TestAQueuedCommitIsNotReady` / `TestACommitThatNeverLandsIsMarkedFailed`; mutation `creation-ready-when-queued` |

`.tb pool prune` (dry run) and `.tb pool prune confirm`: only `failed` rows, re-read from the database every run. It removes an
account only when its username carries `TBPOOL<nnnn>`, the account still answers that username, its id and the row's creation stamp
(`CheckPoolAccount`), nothing of it is `ready`, the `characters` table answered its character list with no one online, and every
character on it is named by a `tb_pool_character` row of that account. A failed character row of a live pool account loses only
the row, and only when the database has no character of that name. Everything else is printed with the reason and kept; nothing
is removed without `confirm`.

Gates of fix round 3:

| Gate | Result |
|---|---|
| Census, `forever/SLICE-W0.txt` (271 files) | `files 271  compile clean 271  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0`, run folder `wt-w0a-s4-SLICE-W0-20261120T185854Z-1081118` (slot 4) |
| Generator / registry | `SliceAiObjectContext.cpp is up to date`; `summary: (a)=0  (b)=218` |
| Policy | `test_pool_registry_policy`: `RESULT: PASS (73 checks)`; `test_pool_composition_policy`: `RESULT: PASS (2079 checks)` |
| Behaviour check | `pool_registry_check: RESULT: PASS (174 checks)`; the mutation run catches all 10 restored defects (`reconciliation-decides-from-the-cache`, `creation-ready-when-queued`, `reconciliation-deletes-again`, `prune-does-not-verify-the-account`, `prune-ignores-foreign-characters`, and the five of the earlier rounds), `POOL_REGISTRY_MUTATE=1 python3 -m unittest forever.tools.test_wl0_wiring.Mutations`: 2 tests OK |
| Wiring | `python3 -m unittest forever/tools/test_wl0_wiring.py`: 23 tests OK, 2 skipped; the whole `forever/tools` discovery: 192 tests OK, 8 skipped; `python3 tools/test_forever_no_direct_queries.py`: 5 OK |
| Module ON (`TORTOISEBOTS_SLICE1=ON`, `worldserver` link) | `CMAKE_RC=0 MAKE_RC=0`, 0 `error:`, binary 72,097,856 B, `strings` has the reconciliation's failed lines, `did not reach the characters database` and `prune dry run` (slot 4, core `31fbd97af8e9`) |
| Module OFF (`TORTOISEBOTS_SLICE1=OFF`, `tortoisebots`) | `CMAKE_RC=0 MAKE_RC=0`, 0 `error:` (slot 4) |
| Turtle build (`vm-build`) | `CMAKE_RC=0 MAKE_RC=0` on `bf881aca8`, `module files changed/added/removed: 70`, `module-tree-hash=3ff7669c5847437a` |
| Turtle unit tests (`vm-test tb`) | `RESULT: PASS  kind=tb wt=wt-w0a sha=bf881aca8 dirty=0 files=4038`, `vm_duration=1765s` (the commits after that sha are the notes only; nothing compiled changed) |
| `tools/verify_all.sh` | steps 1-6 pass (OKF, surface, wiring, engine unit tests, 482 policy tests, decision trail); step 7 (host contract) fails on the same missing `src/game/SessionTransport.h` as any branch in this checkout, no host seam touched |

No schema change is needed for the round: `state` is `varchar(8)`, which holds `creating`, `ready`, `stale` and `failed`, so the
applied migration is untouched and the registry's own `CREATE TABLE IF NOT EXISTS` already writes the same shape.

Gates of the fix round:

| Gate | Result |
|---|---|
| Census, `forever/SLICE-W0.txt` (271 files) | `files 271  compile clean 271  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0`, run folder `wt-w0a-s4-SLICE-W0-20261120T174051Z-560394` |
| Generator / registry | `SliceAiObjectContext.cpp is up to date`; `summary: (a)=0  (b)=218` |
| Policy | `test_pool_registry_policy`: `RESULT: PASS (57 checks)`; `test_pool_composition_policy`: `RESULT: PASS (2079 checks)` |
| Behaviour check | `pool_registry_check: RESULT: PASS (86 checks)`; pre-fix probe on the round's own sources: `FAIL probe.cpp:43 pool::Accounts().empty()`, `FAIL probe.cpp:60 !start.started`, `FAIL probe.cpp:70 fakeAccounts.size() == CharacterDatabase.accounts.size()` |
| Migration | applied twice on a scratch MySQL 8 database: first run converts the old rows (names filled, old rows `stale`, a character row without its game character removed), second run changes nothing (idempotent) |
| Wiring | `python3 -m unittest forever/tools/test_wl0_wiring.py`: 21 tests OK, 2 skipped; `python3 tools/test_forever_no_direct_queries.py`: 5 OK |
| Module ON (`TORTOISEBOTS_SLICE1=ON`, `worldserver` link) | `CMAKE_RC=0 MAKE_RC=0`, 0 `error:`, binary 72,059,064 B, `strings` has `TortoiseBots.Pool.MaxCharacters`, the reconciliation and the cap lines (slot 4, core `31fbd97af8e9`) |
| Module OFF (`TORTOISEBOTS_SLICE1=OFF`, `tortoisebots`) | `CMAKE_RC=0 MAKE_RC=0`, 0 `error:` (slot 4) |
| Turtle build (`vm-build`) | `CMAKE_RC=0 MAKE_RC=0` on `23351acf2`, `dirty-entries=0`, `files=4038` |
| Turtle unit tests (`vm-test tb`) | `RESULT: PASS  kind=tb wt=wt-w0a sha=f172824d2 dirty=0 files=4038`, `vm_duration=918s` (the two commits after that sha are a wiring pin and a comment; neither is compiled) |
| `tools/verify_all.sh` | steps 1-6 pass (OKF, surface, wiring, 2673 engine tests, 482 policy tests, decision trail); step 7 (host contract) fails on the same missing `src/game/SessionTransport.h` as any branch in this checkout, no host seam touched |

Schema: `forever/sql/characters/2026_11_20_02_tb_pool_rows.sql` converts the first round's tables idempotently: `tb_pool_account`
keyed by `username` with `stamp` and `state` (old rows: `stale`, since they carry no stamp), `tb_pool_character` keyed by `name`
with a nullable `guid` and `state` (old rows: names filled from `characters`, then `stale`). A server with the old tables and the
migration not applied logs one line and the pool is off; the module's own CREATE statements write the current shape for a fresh
database. The 8 bots and the `TBPOOL0001` account of the W0-A live run stay in the database, unused (no stamp to prove them).

### 4e. Fix round 4 (the cold review `claude-w0a.md` and Codex round 3)

No review found a path that deletes a real account or character. These are the findings, with the behaviour that now holds and the
check that failed before the fix (`pool_registry_check` on the round's previous sources, with `StopReason()` stubbed so it compiles:
`275 checks, 63 FAILED`; after: `RESULT: PASS (275 checks)`).

| Finding | Fix | Check that failed before |
|---|---|---|
| [high, Codex] the pool schema upgrade used `DELIMITER //` and a stored procedure, which the character updater cannot run: an upgraded server kept the old tables and the pool stayed off | `2026_11_20_02_tb_pool_rows.sql` is plain statements: `CREATE TABLE IF NOT EXISTS` for the current shape, then every old-shape step as `information_schema` + `PREPARE` (the repository's pattern). What a step does only because a column was missing is decided once into a variable before the ALTER that adds it, so a second run changes nothing. Applied on a scratch MySQL 8 database from empty, from the first round's shape with rows (rows turned `stale`, the name filled, the orphan row removed), and twice | `test_wl0_wiring` `test_the_migration_has_no_delimiter_and_no_procedure`; the scratch runs above |
| [medium] a character could land on a pool account the cached list still trusted (renamed, deleted, or its id reused by a real player after the load) | `VerifyAccount` re-reads the registry row and asks the account manager (name, id back-lookup, pool prefix, stamp) before EVERY character, also on a freshly made or reused account. A failure drops the account from the cache, logs, and stops the job ("no longer the pool's") with no claim written and no character made | `TestNoCharacterGoesOnAnAccountThatChangedAfterTheLoad` (rename, `.account delete`, id reused, name re-registered); mutation `account-not-reverified-before-a-character` |
| [medium] the cap counted ready rows only | `PoolCharacters()` is the database's: every character on any account a registry row names (row state ignored: failed, stale and migration-02 rows count), plus every non-ready row whose character is not there (a claim, a failed row). An account the auth database says is not a pool account is not counted; one it did not answer for is. No answer from the characters database is "unknown" (`kPoolCountUnknown`): `StartCreate` and the job refuse, never read it as zero. A job counts again before every character | `TestTheCapCountsFailedAndStaleRowsAndPresentCharacters` (failed+stale rows, characters on a pool account no row names, migration-02 stale rows, a commit that lands after the 30 s timeout); mutations `cap-ignores-the-database-count`, `cap-ignores-unfinished-rows` |
| [medium] one failed auth lookup marked every ready account stale for good | `AccountMgr` cannot tell "no such account" from "did not answer", so `NoAccount` never marks a row stale in the load: the row is unusable for now and read again at the next use. Only a positive fact stales it (the account answers a different name, or the row's username resolves to another id). The reconciliation likewise leaves a `creating` character claim whose account lookup did not answer for the next start | `TestAFailedAuthLookupMarksNothingStale` (the load, and a claim in the reconciliation); mutations `failed-auth-lookup-marks-rows-stale`, `reconciliation-stales-a-claim-on-an-unanswered-lookup` |
| [low] the account was made before the name was checked, and a failed creation left a new empty account each time | In order: the core's name rules (`CheckPlayerNameCompat`), the characters table, then the account. A name that already exists is adopted only onto a registered pool account, else the job stops with no account made. The account is, in order: the job's own while it has room (by the database count; so a cache reload does not open a second one), a registry account with room, a failed pool account with nothing on it (proven the pool's, `FindReusableAccount`), and only then a new one, never beyond `MaxPoolAccounts(cap, 10)` account rows | `TestTheNameIsCheckedBeforeAnAccountIsMade` (a real character has the name, the core refuses the name, four refused creations make one account), `TestThePoolAccountCountIsBounded`; mutations `account-made-before-the-name-is-settled`, `core-name-rules-skipped`, `empty-account-not-reused`, `account-count-not-bounded` |
| [low] `BotMgr::CreateCharacter` skipped `LOGIN_REP_REALM_CHARACTERS` and `OnPlayerCreate` and committed the login side even when the character commit failed | The core's order (CharacterHandler.cpp): the realm count (the account's non-deleted characters in the database, plus creations in flight, plus one) rides in the login transaction; the character commit goes first through `AsyncCommitTransaction`; `ConfirmCreations` commits the login side, fires `sScriptMgr->OnPlayerCreate` and tells the cache only when that commit answered true. A failed commit leaves nothing behind. The Player and its session live until then (player destroyed first) | source pins `test_a_created_character_is_the_cores_creation_with_its_bookkeeping` (the real core is not in the standalone check; the ON `worldserver` link below compiles it) |
| [low] `.tb pool create` and `.tb pool prune confirm` ran at the lowest GM permission | `pool` is a sub-table: `create` asks `RBAC_PERM_COMMAND_ACCOUNT_CREATE`, `prune` stays at the GM level for the dry run and `confirm` checks `RBAC_PERM_COMMAND_ACCOUNT_DELETE` in the handler (the console has every permission) | `test_creating_and_confirmed_pruning_need_an_administrator_permission` |
| [low] a job read the live config and ignored the key being turned off | The job snapshots the cap and the rate at its start. Every tick asks `KeyOn()` first: a `.reload config` that turns the pool off stops the job with "the pool was turned off ..." (`StopReason()`, one log line); a queued character's row stays `creating` for the reconciliation at the next start | `TestAJobSnapshotsItsConfigAndStopsWhenTheKeyIsTurnedOff`; mutations `job-reads-the-live-cap`, `job-ignores-the-key-turned-off`. `TestACacheReloadDuringAJobDoesNotOpenASecondAccount` guards the reload case and also passed before the fix |
| [low] source-text pins stood in for behaviour, and the fake's `EscapeString` escaped nothing | The fake escapes like `mysql_real_escape_string` and every fake read parses a string literal strictly (an unescaped quote is a SQL error: no answer). `TestSqlTextIsEscaped` reads names with a quote, a backslash and an injection attempt through the core reads and writes one through the registry; the escape check is also mutated away in `TbCompat_WL0.h` and in the registry's `SqlText` | mutations `core-read-name-not-escaped`, `registry-sql-text-not-escaped` |

Unknown by design: `AccountMgr::GetName` returns false for both a deleted account and a failed lookup, so a ready row of a
deleted pool account stays `ready` and unusable (not stale, not counted as characters: its characters went with the account). It is
re-read at each use and costs nothing; `.tb pool prune` does not touch it (not `failed`).

Gates of fix round 4 (slot 4, core pin `25fc7ca`; commits `7faadf39a` .. `875788814`):

| Gate | Result |
|---|---|
| Census, `forever/SLICE-W0.txt` (271 files) | `files 271  compile clean 271  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0`, run folder `wt-w0a-s4-SLICE-W0-20261120T195801Z-1520047` (taken before the last small commit, which touches one `.cpp` the ON link below compiles) |
| Generator / registry | `SliceAiObjectContext.cpp is up to date`; `summary: (a)=0  (b)=218` |
| Policy | `test_pool_registry_policy`: `RESULT: PASS (78 checks)`; `test_pool_composition_policy`: `RESULT: PASS (2079 checks)` |
| Behaviour check | `pool_registry_check: RESULT: PASS (279 checks)`; on the previous round's sources the same check (275 checks then) printed `63 FAILED` (`StopReason()` stubbed so it compiles), and the reconciliation check added last fails 3 checks without its fix |
| Mutations | `POOL_REGISTRY_MUTATE=1 python3 -m unittest forever.tools.test_wl0_wiring.Mutations`: 2 tests OK, all 23 restored defects caught (10 of the earlier rounds, 13 of this one) |
| Wiring | `python3 -m unittest forever/tools/test_wl0_wiring.py`: 28 tests OK, 2 skipped; the whole `forever/tools` discovery OK (8 skipped); `python3 tools/test_forever_no_direct_queries.py`: 5 OK (the one allowed direct call is the core's realm-count statement in `BotMgr::CreateCharacter`) |
| Migration | `2026_11_20_02_tb_pool_rows.sql` applied on a scratch MySQL 8 database: from empty (current shape), from the first round's tables with rows (rows `stale`, name filled), and a second run changes nothing |
| Module ON (`TORTOISEBOTS_SLICE1=ON`, `worldserver` link) | `CMAKE_RC=0 MAKE_RC=0`, 0 `error:`, binary 72,262,592 B, `strings` has the new "no longer the pool's", "is empty and proven the pool's", "the character commit of" and "cannot be checked now; the claim stays" lines (this includes `TbBotMgr.cpp`: the core's `AsyncCommitTransaction`, `OnPlayerCreate`, `sRealmList`) |
| Module OFF (`TORTOISEBOTS_SLICE1=OFF`, `tortoisebots`) | `CMAKE_RC=0 MAKE_RC=0`, 0 `error:` (slot 4, final tree) |
| Turtle build (`vm-build`) | `CMAKE_RC=0 MAKE_RC=0` on `7faadf39a`, `dirty-entries=0`, `files=4038`, `module-tree-hash=83ce819f12b29eaf` (the two commits after it touch no file the Turtle build compiles: a test script and a `forever/` source) |
| Turtle unit tests (`vm-test tb`) | `RESULT: PASS  kind=tb wt=wt-w0a sha=63d2070b4 dirty=0 files=4038`, `vm_duration=1331s` |

### 4f. Last fix round (Codex round 4 `codex-w0a-r4.txt`, cold review `claude-w0a-r2.md`)

Merged `tbf/main` (core pin `d02645dd21`) first. Conflicts, all mechanical, both sides kept: `TbScripts.cpp` (the pool includes beside the
orders include; the pool's boot lines plus main's corrected `World.cpp:1622` comment; the `pool` command row beside main's
`gear`/`cmd`/`quest`/`ignore`/`free` rows), `slice_gen_context.py` (main's W6/W2/W3 key tables, `WL0_KEYS` last in the `_join`),
`test_w1_wiring.py` (main's looser `SLICE9_KEYS, W1_KEYS` pin; the exact `WL0_KEYS)` pin in `test_wl0_wiring.py` is now `WL0_KEYS)`).
Not a text conflict but a consequence: main's `SLICE-W1.txt` grew 16 files (W6a/W2/W3), so `SLICE-W0.txt` (W1 plus the name service)
got the same 16 lines, as `test_w1_wiring` requires.

| Finding | Fix | Test that failed before | Mutation |
|---|---|---|---|
| [Codex high] an existing character was adopted without a prior claim | `MakeOne` adopts a name the database already holds only when this pool's own `creating` row for that exact name AND account exists; otherwise it logs, skips the name (five skips stop the job) and writes no claim | `TestAnExistingCharacterIsNeverAdoptedWithoutItsOwnClaim` | `adopts-an-existing-character-without-its-claim` |
| [medium] `.tb create` on any account | row needs `RBAC_PERM_COMMAND_ACCOUNT_CREATE`; `CreateAccountAllowed` (policy) accepts only `TBPOOL<nnnn>` or an account in `TortoiseBots.Create.TestAccounts` (conf.dist, TB-COMMANDS.md) | `test_pool_registry_policy` (7 checks), `test_the_last_round_pins` | policy test |
| [low] prune vs claims | `Prune` runs `Reconcile` first; an account with a ready or `creating` character row is kept | `TestPruneReconcilesFirstAndKeepsAnAccountWithAClaim` (a, b) | `prune-ignores-creating-claims` |
| [low] account cap and unknown reads | `PoolAccountRows` counts non-stale rows; a null row read is confirmed by `COUNT(*)` of the table (`PoolRowCountCompat`), `PoolCharacters` answers unknown when either table did not answer | `TestStaleAccountRowsDoNotBlockANewAccountAndAFailedReadIsUnknown` | `stale-rows-count-toward-the-account-cap`, `a-failed-row-read-is-an-empty-table` |
| [low] `FactsOf` half failure | an unanswered e-mail, or an id of the row's own name that is missing for a found same-named account, makes `Check` answer `NoAccount` (unknown, never stale) | `TestAHalfFailedAccountLookupIsNeverAStaleMark` | `a-half-failed-lookup-marks-the-row-stale` |
| [low] migration | character half keyed on the table still being `guid`-keyed (`@tb_char_pk`), so a rerun repairs a run that stopped after the ALTER; rows whose name is still empty (gone or soft-deleted characters) are deleted before the `name` key; account stale mark keyed on `@tb_acct_old OR @tb_acct_pk` | scratch MySQL 8.0 run (below), `test_the_last_round_pins` | n/a |
| [low] commit window | `BotMgr::CreationInFlight(name)`; `ConfirmPending` waits while the login commit and `OnPlayerCreate` have not run, so the row turns ready only after them | `TestARowIsNotReadyUntilTheLoginCommitRan` | `ready-before-the-login-commit` |

Scratch MySQL 8.0 (`mysql:8.0` container, removed after): from empty the current shape is created (`PRIMARY KEY (name)`); from the old
shape with two soft-deleted empty names and one orphan row the result is `Alpha`/`Delta` stale and no duplicate-key error, the account
stale; a second run leaves identical `CHECKSUM TABLE` values; a state interrupted after both ALTERs (names empty) is repaired by one run.

Not changed (stated limits): `Reconcile` first in prune cannot be caught by a mutation on its own, because an account with a
`creating` row is kept either way (both halves of the review's "or" are in); the pending `Player` of a creation in flight at shutdown
is still destroyed in `BotMgr`'s static destructor (the row then stays `creating` and is finished by the next start's `Reconcile`).

Gates of the last fix round (slot 4, core `d02645dd21` from `~/ForeverCore-d02645dd`, module at `b184516c6`):

| Gate | Result |
|---|---|
| Census, `forever/SLICE-W0.txt` (287 files) | `files 287  compile clean 287  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0` |
| Module ON (`TORTOISEBOTS_SLICE1=ON`, `build.sh 12 tortoisebots`) | `CMAKE_RC=0 MAKE_RC=0 TOTAL_SECONDS=1271`, 0 `error:`, 0 `warning:` |
| Module OFF (`build.sh 12 tortoisebots`) | `CMAKE_RC=0 MAKE_RC=0 TOTAL_SECONDS=41`, 0 `error:` |
| Generator / registry | `SliceAiObjectContext.cpp is up to date`; `summary: (a)=0  (b)=228` |
| Policy | `test_pool_registry_policy`: `RESULT: PASS (85 checks)`; `test_pool_composition_policy`: `RESULT: PASS (2079 checks)` |
| Behaviour check | `pool_registry_check: RESULT: PASS (328 checks)`; the new tests fail on the previous sources (31 FAILs, all five groups); `POOL_REGISTRY_MUTATE=1 python3 -m unittest forever.tools.test_wl0_wiring.Mutations`: 29 mutations, all caught |
| Wiring | `python3 -m unittest discover -s forever/tools -p "test_*.py"`: OK (8 skipped); `tools/test_forever_no_direct_queries.py`: OK |
| Turtle build (`vm-build ~/wt-w0a --wait`) | FAILS in `ai/cmangos-compat-unported.h:126` (`ChatHandler::ExtractKeyFromLink` is protected), W3's `ExtractQuestIdCompat` from `tbf/main`; the file is identical to `tbf/main` (`git diff tbf/main -- ai/cmangos-compat-unported.h` is empty) and none of this round's files is in the error list |

## 5. Unknowns of the spec

| # | Result |
|---|---|
| U2 | Answered live: every race has creation gear in `character_inventory` right after creation (below), so the data sweep's "no starting gear" was indeed only the SQL table. |
| U3 | not needed for A: a created bot is only logged in by the scheduler; W0-B can use the spec's 60 s rule |
| U4 | Answered live: `sAccountMgr->CreateAccount("TBPOOL0001", <random 16>, "", 0, 0)` returned `AOR_OK` on 25fc7ca (login row with NULL bnet columns), account id 3. The core's `AccountMgr::CreateAccount` is a member, not a static (`AccountMgr.h:68`, `sAccountMgr` at `:105`): the first compile caught it. |

## 6. Gates and live

| Gate | Result |
|---|---|
| Census, `forever/SLICE-W0.txt` (271 files) | `files 271  compile clean 271  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0`, run folder `wt-w0a-s4-SLICE-W0-20261120T163116Z-3920867` (core 25fc7ca, slot 4) |
| Generator / registry | `slice_gen_context.py --check` up to date; `slice_registry_check.py` (a) = 0 (218 keys are the later slices') |
| Turtle build (`vm-build`) | `CMAKE_RC=0 MAKE_RC=0` on 73634d754 (no file outside `forever/` changed after it) |
| Turtle unit tests (`vm-test tb`) | `RESULT: PASS  kind=tb wt=wt-w0a sha=fbb544aec dirty=0 files=4022`, `vm_duration=1150s` |
| Module ON (`TORTOISEBOTS_SLICE1=ON`, `worldserver` link) | `CMAKE_RC=0 MAKE_RC=0` (slot 4, core 25fc7ca), binary 72,149,416 B, `strings` has `TortoiseBots.Pool.Enable` and the create line |
| Module OFF (`TORTOISEBOTS_SLICE1=OFF`, `tortoisebots`) | `CMAKE_RC=0 MAKE_RC=0` |
| Wiring | `python3 -m unittest forever/tools/test_wl0_wiring.py` 15 tests OK; the whole `forever/tools` discovery 183 tests OK (6 skipped); `tools/test_forever_no_direct_queries.py` 5 OK |
| Policy | `test_pool_composition_policy`: `RESULT: PASS (2079 checks)` |
| `tools/verify_all.sh` | OKF, command table, migrations, module surface, action wiring: pass; engine unit tests `Ran 2673 tests ... OK`; 482 standalone policy tests; decision trail: pass. **Step 7 (host contract, `../tortoise-wow`) fails in this checkout**: `required core file is missing: src/game/SessionTransport.h` — the local Tortoise core is at `5d79486` without that file, the same for any branch (no new failure from this round; no host seam touched) |

### Live run (server `forever-run2`, console only, core 25fc7ca, binary from slot 4, module SQL none needed: the registry creates its tables)

Setup: `TortoiseBots.Enable = 1` (already), `TortoiseBots.Pool.Enable = 1`, `.CreatePerMinute = 30` appended to
`etc/worldserver.conf.d/tortoisebots.conf`; world2 started on the slot-4 ON binary (backup `bin/worldserver.pre-w0a`).

| Observation | Result |
|---|---|
| U4, account | `TC> tb pool create 8` -> `pool create started: 8 character(s) at 30 per minute`, then `pool account TBPOOL0001 (#3) created` |
| Composition | 8 rows, race 1..8 exactly once each, 4 Alliance / 4 Horde, all valid pairs: human rogue, orc shaman, dwarf hunter, undead rogue, night-elf druid, tauren hunter, gnome rogue, troll shaman; genders 0/1 mixed. `SELECT * FROM tb_pool_account` = 1 row; `tb_pool_character` = 8, `characters.level` = 1 |
| Policy vs the server's table | all 40 policy pairs (6+5+5+5+5+4+4+6 for races 1-8) are rows of `world.playercreateinfo`; the server's table is broader (death knight and later classes), the pool stays on the spec's vanilla pairs |
| Positions (gate a) | every saved `characters.position_x/y/z/map` equals the `world.playercreateinfo` row of its race and class (e.g. human `-8949.95 -132.49 83.53` map 0; orc `-618.52 -4251.67 38.72` map 1); only maps 0 and 1, never 2175 |
| Creation gear (U2) | `character_inventory` per bot: human rogue 47,48,49,2092,2947; orc shaman 36,153,154; dwarf hunter 37,129,147,148,2508; undead rogue 120,121,2092,2105,2947; NE druid 3661,6123,6124; tauren hunter 37,127,2508,6126; gnome rogue 47,48,49,2092,2947; troll shaman 36,6134,6135 |
| Pacing | the 8 log lines are 2 s apart (30/min), `pool create stopped after 8 character(s) (0 already-existing name(s) skipped): the request is complete` |
| Free play | `.tb login Alarick free` and `.tb login Zugrukkar free`: both entered the world at the positions above (`map 0` / `map 1`), engines built in 1 ms, `free=yes`; Alarick was fighting a Young Wolf within 12 s (the W1 kit); `bagfree=18` / `17` |
| Stability | `.tb logout` both: `left the world`; no `ASSERTION FAILED`, no segfault, no `terminate called` in the log; server stopped |

The pool rows and the 8 characters stay in the run2 characters database for the later rounds (no rollback of the data was
asked for); the binary and `tortoisebots.conf` were restored to their pre-run versions and the restored server was booted once
(`worldserver-daemon) ready`) and stopped.

"""Wiring pins for the startup pool reset.

The behaviour is proved by forever/tools/pool_reset_check.cpp, the three real translation units against the fake core
in forever/tools/pool_registry_fakes: a pool-led guild with a real player refuses the reset before any deletion,
`once:t1` deletes every ready pool row once and a second plan deletes nothing. This file pins the wiring that the
behaviour cannot show from source text alone, plus the pure policy test.

Run: python3 -m unittest forever.tools.test_pool_reset_wiring
"""
import pathlib
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOLS = ROOT / "forever" / "tools"
SCRIPTS = (ROOT / "forever/src/TbScripts.cpp").read_text()
RESET = (ROOT / "forever/src/host/pool/TbPoolReset.cpp").read_text()
RESET_H = (ROOT / "forever/src/host/pool/TbPoolReset.h").read_text()
SCHED = (ROOT / "forever/src/host/pool/TbPoolScheduler.cpp").read_text()
CREATE = (ROOT / "forever/src/host/pool/TbPoolCreate.cpp").read_text()
CONF = (ROOT / "forever/conf/tortoisebots.conf.dist").read_text()
SQL = (ROOT / "forever/sql/characters/2026_11_23_03_tb_pool_state.sql").read_text()
DOCS_CONF = (ROOT / "docs/server/configuration.md").read_text()
POLICY = (ROOT / "ai/playerbot/policy/world/PoolResetSafetyPolicy.h").read_text()
POLICY_TEST = (ROOT / "tools/test_pool_reset_safety_policy.cpp").read_text()
CHECK = (ROOT / "forever/tools/pool_reset_check.cpp").read_text()


def func(text, signature):
    """The text of the function whose definition line contains `signature` (up to its closing brace)."""
    i = text.index(signature)
    j = text.index("{", i)
    depth = 0
    for k in range(j, len(text)):
        if text[k] == "{":
            depth += 1
        elif text[k] == "}":
            depth -= 1
            if depth == 0:
                return text[i:k + 1]
    raise AssertionError(signature)


class Wiring(unittest.TestCase):
    def test_the_key_is_host_scoped_and_off_by_default(self):
        self.assertIn('TortoiseBots.Pool.Reset = "off"', CONF)
        self.assertIn('GetStringDefault("TortoiseBots.Pool.Reset", "off")', SCRIPTS)
        self.assertIn("| `TortoiseBots.Pool.Reset` | `off` |", DOCS_CONF)

    def test_the_reset_is_planned_only_at_world_startup(self):
        start = func(SCRIPTS, "void OnWorldInitialized(")
        self.assertIn("pool::Reset::PlanAtStartup(", start)
        config = func(SCRIPTS, "void OnConfigLoad(")
        self.assertNotIn("PlanAtStartup", config)
        tick = func(SCRIPTS, "void OnBeforeMapsUpdate(")
        self.assertLess(tick.index("pool::Reset::Update(diff)"), tick.index("PoolScheduler::Update(diff)"))
        shutdown = func(SCRIPTS, "void OnShutdown(")
        self.assertIn("pool::Reset::Shutdown()", shutdown)

    def test_no_live_reset_verb_exists(self):
        # The pool command table has create, prune, start, stop, status and population only.
        self.assertNotIn('"reset"', SCRIPTS)
        status = func(SCRIPTS, "bool HandlePoolStatus(")
        self.assertIn("pool::Reset::StatusLine()", status)

    def test_the_deletion_is_the_core_call_once_per_tick(self):
        self.assertEqual(RESET.count("Player::DeleteFromDB("), 1)
        self.assertIn("Player::DeleteFromDB(guid, target.accountId, true, true)", RESET)
        # One target per call: its index advances in the same branch and the phase does not loop.
        self.assertIn("++s.next;", RESET)
        deleting = func(RESET, "case Phase::Deleting:\n        {")
        self.assertNotIn("for (", deleting)
        self.assertIn("DeleteOne(s, ScopeOfPool(), why)", deleting)

    def test_the_row_is_rechecked_before_each_delete(self):
        body = func(RESET, "Step DeleteOne(")
        # Row still ready, account still the pool's, character still on that account, no human, no auction, guild clear.
        self.assertIn("FindCharacterRow(target.name, rowNow)", body)
        self.assertIn("rowNow.state == world::PoolRowState::Ready", body)
        self.assertIn("VerifyAccount(target.accountId, target.username, accountWhy)", body)
        self.assertIn("CharacterOwner(target.name, found, nowGuid, nowAccount)", body)
        self.assertIn("!session->IsBot()", body)
        self.assertIn("OwnsAuctions(target.guid, owned, auctionsAnswered)", body)
        self.assertIn("RecheckGuildsOfLeader(target.guid, scope, why, guildBlocked)", body)
        self.assertLess(body.index("RecheckGuildsOfLeader("), body.index("Player::DeleteFromDB("))

    def test_the_guild_predicate_holds_only_ready_pool_characters(self):
        self.assertIn("LEFT JOIN `tb_pool_character` p ON p.`guid` = gm.`guid` AND p.`state` = 'ready'", RESET)
        self.assertIn("c.`account` IS NULL OR p.`guid` IS NULL", RESET)
        self.assertIn("c.`account` NOT IN (", RESET)
        # The guild re-check right before a deletion is one query: the COUNT of the same statement.
        recheck = func(RESET, "bool RecheckGuildsOfLeader(")
        self.assertEqual(recheck.count("CharacterQueryGuardedCompat"), 0)  # it goes through the two helpers
        self.assertIn("CountGuildOutsiders(filter, scope, count)", recheck)
        self.assertIn("g.`leaderguid` = {}", recheck)

    def test_the_state_table_and_the_generation_are_recorded_last(self):
        self.assertIn("CREATE TABLE IF NOT EXISTS `tb_pool_state`", SQL)
        self.assertIn("`applied_generation` varchar(128) NOT NULL DEFAULT ''", SQL)
        self.assertIn('kStateTable = "tb_pool_state"', RESET)
        self.assertIn("ON DUPLICATE KEY UPDATE `applied_generation` = VALUES(`applied_generation`)", RESET)
        # The generation is written in Rebuilding, after Verifying cleared.
        update = func(RESET, "void Update(")
        self.assertLess(update.index("case Phase::Verifying:"), update.index("case Phase::Rebuilding:"))
        rebuilding = update[update.index("case Phase::Rebuilding:"):]
        self.assertIn("ApplyGeneration(s.setting)", rebuilding)
        self.assertIn("s.phase = Phase::Complete;", rebuilding)
        self.assertIn("s.maintenance = false;", rebuilding)
        self.assertLess(rebuilding.index("ApplyGeneration("), rebuilding.index("StartCreate("))

    def test_the_preflight_refuses_before_maintenance_and_deletes_nothing(self):
        start = func(RESET, "void PlanAtStartup(")
        # Human session, auction and guild checks all precede the maintenance switch.
        self.assertLess(start.index("FindSession("), start.index("`auctionhouse`"))
        self.assertLess(start.index("`auctionhouse`"), start.index("CheckGuilds("))
        self.assertLess(start.index("CheckGuilds("), start.index("s.maintenance = true"))
        self.assertLess(start.index("DecidePoolResetPlan("), start.index("s.maintenance = true"))
        # The generation is read through a COUNT probe, never a null result read as "not applied".
        self.assertIn("SELECT COUNT(*) FROM `{}` WHERE `pool_name` = {}", RESET)

    def test_the_exact_log_lines_are_pinned(self):
        for line in (
            "TortoiseBots: random pool reset aborted before any deletion: {}",
            "TortoiseBots: random pool generation {}; reset scheduled for {} characters on {} managed accounts",
            "TortoiseBots: random pool reset: {} character(s) to delete, one per world tick",
            "TortoiseBots: random pool reset progress: {}/{} characters deleted",
            "TortoiseBots: random pool reset verified: {} of {} pool characters remain; {} character(s) on pool accounts kept (not pool rows)",
            "TortoiseBots: random pool generation '{}' applied; pool rebuild starts now",
            "TortoiseBots: random pool generation '{}' already applied; reset skipped",
            "TortoiseBots: random pool reset failed: {}",
        ):
            self.assertIn(line, RESET)
        self.assertIn("is led by pool character", RESET)
        self.assertIn("but holds a member outside the managed pool (character", RESET)
        self.assertIn("); reset aborted", RESET)

    def test_the_scheduler_and_creation_pause_during_a_reset(self):
        start = func(SCHED, "SwitchResult Start(")
        self.assertIn("Reset::PoolAvailable()", start)
        update = func(SCHED, "bool Update(")
        self.assertIn("Reset::PoolAvailable()", update)
        self.assertIn("s.stopping = Draining(s) != 0;", update)
        create = func(CREATE, "CreateStart StartCreate(")
        self.assertIn("Reset::PoolAvailable()", create)
        create_tick = func(CREATE, "void CreateUpdate(")
        self.assertIn("Reset::PoolAvailable()", create_tick)

    def test_the_status_line_is_the_documented_shape(self):
        status = func(RESET, "std::string StatusLine(")
        self.assertIn('"reset={} phase={} targets={} applied={}"', status)
        self.assertIn("reset=", RESET_H)
        self.assertIn("phase=", RESET_H)

    def test_the_policy_states_the_safety_rules(self):
        self.assertIn("enum class PoolResetTargetAction", POLICY)
        self.assertIn("PoolResetMemberOutsidePool", POLICY)
        self.assertIn("FirstPoolResetOutsider", POLICY)
        self.assertIn("CountPoolResetKeptCharacters", POLICY)
        self.assertIn("DecidePoolResetTarget", POLICY)
        self.assertIn("DecidePoolResetPlan", POLICY)
        # The test's two headline cases.
        self.assertIn("A human session is final", POLICY_TEST)
        self.assertIn("A real player's character on a normal account blocks it", POLICY_TEST)


class Behaviour(unittest.TestCase):
    """The reset's own translation units against the fake core: g++ and a run, no build tree."""

    def test_the_pure_policy_test_passes(self):
        gxx = shutil.which("g++")
        if not gxx:
            self.skipTest("no g++")
        with tempfile.TemporaryDirectory(prefix="pool-reset-policy-") as tmp:
            exe = pathlib.Path(tmp) / "policy"
            subprocess.run([gxx, "-std=c++17", "-O1", "-Wall", "-Wextra", "-I", str(ROOT),
                            str(ROOT / "tools/test_pool_reset_safety_policy.cpp"), "-o", str(exe)],
                           check=True, capture_output=True, text=True)
            out = subprocess.run([str(exe)], capture_output=True, text=True)
        self.assertEqual(out.returncode, 0, out.stdout + out.stderr)
        self.assertIn("OK:", out.stdout)

    def test_the_reset_check_passes(self):
        gxx = shutil.which("g++")
        if not gxx:
            self.skipTest("no g++")
        with tempfile.TemporaryDirectory(prefix="pool-reset-") as tmp:
            tmp_path = pathlib.Path(tmp)
            for rel in ("forever/tools/pool_reset_check.cpp",
                        "forever/src/host/pool/TbPoolReset.h", "forever/src/host/pool/TbPoolReset.cpp",
                        "forever/src/host/pool/TbPoolRegistry.h", "forever/src/host/pool/TbPoolRegistry.cpp",
                        "forever/src/host/pool/TbPoolCreate.h", "forever/src/host/pool/TbPoolCreate.cpp",
                        "forever/src/compat/TbCompat_WL0.h", "forever/src/compat/TbCompat_CharStore.h",
                        "forever/src/compat/TbSqlNames.h",
                        "ai/playerbot/policy/world/PoolRegistryPolicy.h",
                        "ai/playerbot/policy/world/PoolCompositionPolicy.h",
                        "ai/playerbot/policy/world/PoolResetSafetyPolicy.h",
                        "runtime/policy/infra/PoolResetPolicy.h"):
                (tmp_path / rel).parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(ROOT / rel, tmp_path / rel)
            shutil.copytree(TOOLS / "pool_registry_fakes", tmp_path / "forever/tools/pool_registry_fakes")
            exe = tmp_path / "pool_reset_check"
            subprocess.run([gxx, "-std=c++17", "-O0", "-DTORTOISEBOTS_SLICE1",
                            "-I", str(tmp_path / "forever/tools/pool_registry_fakes"),
                            "-I", str(tmp_path / "forever/src"), "-I", str(tmp_path),
                            str(tmp_path / "forever/tools/pool_reset_check.cpp"),
                            str(tmp_path / "forever/src/host/pool/TbPoolRegistry.cpp"),
                            str(tmp_path / "forever/src/host/pool/TbPoolCreate.cpp"),
                            str(tmp_path / "forever/src/host/pool/TbPoolReset.cpp"), "-o", str(exe)],
                           check=True, capture_output=True, text=True)
            out = subprocess.run([str(exe)], capture_output=True, text=True)
        self.assertEqual(out.returncode, 0, out.stdout + out.stderr)
        self.assertIn("RESULT: PASS", out.stdout)


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
"""Wiring pins and mutations for the commander map layers (package P4, forever/docs/CMD-P4-NOTES.md).

The core calls cannot run without a server, so the wiring tests pin where the two clocks stand, what the feed files may never do (a database call,
an engine-free path that still answers), that a layer is only reported after its scan ran, that the caps stop the grid visit and that the cooldown
conversion is the pure half's. The mutation tests (CMD_P4_MUTATE=1, they compile forever/tools/cmd_feeds_check.cpp, so run them on the build VM)
break the pure half four ways and expect the standalone check to FAIL each time:
  cap      a full sweep keeps taking candidates (the card's caps no longer stop the visit)
  seconds  the cooldown is passed through as milliseconds (a 90 s cooldown reads 90000)
  boss     the instance's DONE encounter no longer marks an unseen boss dead
  window   the position report always starts at the first binding (bindings past the cap never get a turn)
"""
import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SRC = ROOT / "forever" / "src"
HOST = SRC / "host"
CMD = HOST / "cmd"


def text(path):
    return Path(path).read_text()


def code(source):
    """The source without // comments (the notes in them name the very calls the tests forbid)."""
    return re.sub(r"//[^\n]*", "", source)


def function_body(source, signature_regex):
    m = re.search(signature_regex, source)
    assert m, f"function not found: {signature_regex}"
    start = source.index("{", m.end() - 1 if source[m.end() - 1] == "{" else m.end())
    depth = 0
    for i in range(start, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[start:i + 1]
    raise AssertionError("unbalanced braces")


class Wiring(unittest.TestCase):
    def setUp(self):
        self.cpp = text(CMD / "TbCmdFeeds.cpp")
        self.hdr = text(CMD / "TbCmdFeeds.h")
        self.pure = text(CMD / "TbCmdFeedsPure.h")
        self.scripts = text(SRC / "TbScripts.cpp")
        self.dash = text(SRC / "dash" / "TbDashRuns.cpp")

    def test_a_scan_that_did_not_run_is_never_reported(self):
        # The failing case of spec section 7 (mobs:null, never []): a binding without a lead in the world never reaches the scan, so its layers
        # stay unreported; the two reported flags are set only after their own sweep in the same function.
        fill = function_body(self.cpp, r"void FillFeeds\(std::vector<CT::BindingInfo>& out\)")
        self.assertIn("if (!lead)", fill)
        self.assertGreater(fill.index("continue;", fill.index("if (!lead)")), fill.index("if (!lead)"))
        self.assertLess(fill.index("if (!lead)"), fill.index("FillBinding(lead, binding, nowMs, watch);"))
        body = function_body(self.cpp, r"void FillBinding\(Player\* lead, CT::BindingInfo& binding")
        self.assertGreater(body.index("binding.mobsReported = true;"), body.index("Cell::VisitAllObjects"))
        objs = function_body(self.cpp, r"void FillObjects\(Player\* lead, CT::BindingInfo& binding")
        self.assertGreater(objs.index("binding.objectsReported = true;"), objs.index("Cell::VisitGridObjects"))

    def test_the_caps_bound_the_grid_visit(self):
        # The caps are the sweep's, and both visitors stop the visit at them: a full sweep refuses the next candidate and every later grid
        # manager call returns without touching a ref, so the caps bound the work (not only the rows). The old shape is gone: no list of every
        # match, no per-match map, no sort of everything in range.
        body = function_body(self.cpp, r"void FillBinding\(Player\* lead, CT::BindingInfo& binding")
        self.assertIn("FD::Sweep<Unit*> sweep(FD::kMobCap);", body)
        self.assertIn("Cell::VisitAllObjects(lead, visit, Get().mobRange);", body)
        self.assertLess(body.index("CappedUnitVisit visit("), body.index("Cell::VisitAllObjects"))
        objs = function_body(self.cpp, r"void FillObjects\(Player\* lead, CT::BindingInfo& binding")
        self.assertIn("FD::Sweep<GameObject*> sweep(FD::kObjectCap);", objs)
        self.assertIn("Cell::VisitGridObjects(lead, visit, Get().objectRange);", objs)
        for name in ("CappedUnitVisit", "CappedObjectVisit"):
            visit = function_body(self.cpp, "struct " + name + r"\s*\{")
            self.assertIn("if (stopped)", visit)                      # the rest of the visit is skipped once the cap is reached
            self.assertGreater(visit.index("stopped = true;"), visit.index("sweep.Offer("))
            self.assertIn("sweep.Full()", visit)
        self.assertNotIn("FD::KeepNearest", self.cpp)
        self.assertNotIn("FD::KeepNearest", self.pure)
        self.assertNotIn("UnitListSearcher", self.cpp)
        self.assertNotIn("GameObjectListSearcher", self.cpp)

    def test_the_cooldowns_go_out_in_seconds(self):
        # SpellHistory::Duration counts milliseconds; the wire's cl is whole seconds, so the collector converts through the pure half's
        # RemainingSeconds instead of passing .count() on.
        body = function_body(self.cpp, r"void ReadReadyCooldowns\(Player\* bot, State::Ready& entry\)")
        self.assertIn("FD::RemainingSeconds(leftMs)", body)
        self.assertIn("GetRemainingCooldown(proto).count()", body)
        self.assertEqual(body.count("count()"), 1)
        self.assertIn("longest = std::max(longest, FD::RemainingSeconds(leftMs));", body)
        self.assertIn("remainingMs / 1000", self.pure)

    def test_the_feed_files_make_no_database_call(self):
        # All six sources, comments stripped: no pool, no query, no executes. The boss roster is read from the core's in-memory spawn data.
        for path in (CMD / "TbCmdFeeds.cpp", CMD / "TbCmdFeeds.h", CMD / "TbCmdFeedsPure.h"):
            body = code(text(path))
            for forbidden in ("WorldDatabase", "CharacterDatabase", "LoginDatabase", "Query(", "PQuery", "PExecute",
                              "Execute(", "DirectExecute", "Transaction", "WorldQueryCompat", "CharacterQueryCompat"):
                self.assertNotIn(forbidden, body, f"{path.name} names {forbidden}")

    def test_no_player_pointer_is_kept_between_ticks(self):
        watched = function_body(self.cpp, r"struct WatchedBinding\s*\{")
        self.assertNotIn("Player*", watched)
        self.assertNotIn("Unit*", watched)
        self.assertIn("std::vector<ObjectGuid> mobs;", watched)
        # The position pass starts from guids only and resolves them again.
        self.assertIn("InWorld(PlayerGuid(watch.leadGuid))", self.cpp)

    def test_the_two_clocks_are_wired(self):
        self.assertIn('#include "host/cmd/TbCmdFeeds.h"', self.scripts)
        init = function_body(self.scripts, r"void OnWorldInitialized\(\) override")
        self.assertIn("TortoiseBots::run::LoadPlans()", init)
        self.assertIn("TortoiseBots::cmd::LoadFeeds()", init)              # after the plans: the boss roster needs them sealed
        self.assertGreater(init.index("TortoiseBots::cmd::LoadFeeds()"), init.index("TortoiseBots::run::LoadPlans()"))
        config = function_body(self.scripts, r"void OnConfigLoad\(bool reload\) override")
        self.assertIn("TortoiseBots::cmd::LoadFeeds()", config)
        tick = function_body(self.scripts, r"void OnBeforeMapsUpdate\(uint32 diff\) override")
        self.assertIn("TortoiseBots::cmd::UpdateFeeds(diff)", tick)
        self.assertGreater(tick.index("TortoiseBots::cmd::UpdateFeeds(diff)"), tick.index("TortoiseBots::DashRuns::Update(diff)"))
        # The collector fills the layers before it serializes them.
        cycle = function_body(self.dash, r"void Cycle\(State& s\)")
        self.assertIn("cmd::FillFeeds(bindings);", cycle)
        self.assertGreater(cycle.index("cmd::FillFeeds(bindings);"), cycle.index("cmd::FillRunOrders(bindings);"))
        self.assertLess(cycle.index("cmd::FillFeeds(bindings);"), cycle.index("CT::BuildPayloadParts"))

    def test_the_keys_and_the_defaults_agree(self):
        load = function_body(self.cpp, r"void LoadFeeds\(\)")
        for key in ("TortoiseBots.Commander.Feeds", "TortoiseBots.Commander.MobRange", "TortoiseBots.Commander.ObjectRange",
                    "TortoiseBots.Commander.PosMs"):
            self.assertIn(key, load)
            self.assertIn(key, text(ROOT / "forever" / "conf" / "tortoisebots.conf.dist"))
        # Defaults equal the template's values.
        self.assertIn("kDefaultMobRange = 60.0f", self.pure)
        self.assertIn("kDefaultObjectRange = 40.0f", self.pure)
        self.assertIn("kDefaultPosMs = 100", self.pure)
        conf = text(ROOT / "forever" / "conf" / "tortoisebots.conf.dist")
        self.assertIn("TortoiseBots.Commander.MobRange = 60", conf)
        self.assertIn("TortoiseBots.Commander.ObjectRange = 40", conf)
        self.assertIn("TortoiseBots.Commander.PosMs = 100", conf)
        self.assertIn("TortoiseBots.Commander.Feeds = 1", conf)

    def test_the_engine_free_build_answers_nothing(self):
        self.assertIn("#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1", self.cpp)
        tail = self.cpp[self.cpp.rindex("#else"):]
        self.assertIn("void FillFeeds(std::vector<::TortoiseBots::CommanderTelemetry::BindingInfo>&) {}", tail)
        self.assertIn("bool FeedsActive() { return false; }", tail)

    def test_the_boss_cache_is_one_pass_over_the_spawn_data(self):
        body = function_body(self.cpp, r"void BuildBossCache\(\)")
        self.assertEqual(body.count("GetAllCreatureData()"), 1)
        self.assertNotIn("for (uint32 entry", body)          # never item x template loops: one pass, then plan steps fill the gaps
        self.assertIn("ai::DungeonPlanTableGeneration()", self.cpp)  # a plan reload rebuilds it once, in the feed (not the reload command)
        self.assertIn("kMaxBossesPerBinding", body)          # an over-cap roster is never sent short


MUTATIONS = {
    "cap": ("    bool Full() const { return rows_.size() >= cap_; }", "    bool Full() const { return false; }"),
    "seconds": ("    return remainingMs > 0 ? uint32_t(remainingMs / 1000) : 0;", "    return remainingMs > 0 ? uint32_t(remainingMs) : 0;"),
    "boss": ("    return encounterDone ? 'd' : 'u';", "    return 'u';"),
    "window": ("    w.first = start % bindings;", "    w.first = 0;"),
}


@unittest.skipUnless(os.environ.get("CMD_P4_MUTATE") == "1", "mutations compile the standalone check: run on the build VM with CMD_P4_MUTATE=1")
class Mutations(unittest.TestCase):
    def build(self, header_text):
        tmp = Path(tempfile.mkdtemp(prefix="cmdp4-"))
        (tmp / "forever" / "src" / "host" / "cmd").mkdir(parents=True)
        (tmp / "forever" / "tools").mkdir(parents=True)
        (tmp / "forever" / "src" / "host" / "cmd" / "TbCmdFeedsPure.h").write_text(header_text)
        (tmp / "forever" / "tools" / "cmd_feeds_check.cpp").write_text(text(ROOT / "forever" / "tools" / "cmd_feeds_check.cpp"))
        exe = tmp / "check"
        subprocess.run(["g++", "-std=c++17", "-O0", "-I", str(tmp), "-I", str(ROOT), str(tmp / "forever" / "tools" / "cmd_feeds_check.cpp"),
                        "-o", str(exe)], check=True, capture_output=True, text=True)
        return subprocess.run([str(exe)], capture_output=True, text=True)

    def test_unmutated_passes(self):
        out = self.build((CMD / "TbCmdFeedsPure.h").read_text())
        self.assertEqual(out.returncode, 0, out.stdout)

    def test_each_mutation_fails_the_check(self):
        source = (CMD / "TbCmdFeedsPure.h").read_text()
        for name, (old, new) in MUTATIONS.items():
            with self.subTest(mutation=name):
                self.assertIn(old, source, f"mutation {name} no longer applies")
                out = self.build(source.replace(old, new, 1))
                self.assertNotEqual(out.returncode, 0, f"mutation {name} was NOT caught:\n{out.stdout}")
                self.assertIn("FAIL", out.stdout)


if __name__ == "__main__":
    unittest.main()

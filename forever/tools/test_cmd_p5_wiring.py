#!/usr/bin/env python3
"""Wiring pins and mutations for the fight debugger (package P5, forever/docs/CMD-P5-NOTES.md).

The core calls cannot run without a server, so the wiring tests pin where the hooks stand, what the monitor may never do, and the two option
builds. The mutation tests (CMD_P5_MUTATE=1) compile forever/tools/cmd_combat_check.cpp with the pure half broken seven ways and expect the
check to FAIL each time:
  keepbook   a card whose lead is in a loading screen (group 0) loses its book at once (the lead-away grace is unreachable)
  feed0      a card with no live group is fed (every party-less binding lands on one book)
  bookkey    a card's fight is keyed by its WoW group (two commanders in one group overwrite each other's members and fight)
  rankorder  the engaged units are ordered by arrival instead of rank (the newest mob is kept ahead of the boss met before it)
  focus      the raced boss entry is ignored (the debuff sample reads the biggest mob instead of the boss being fought)
  applyonly  the aura diff never reports a removal (the timeline keeps auras that fell off, forever)
  alwaysdue  every pass runs on every world tick (the 1 Hz sample, the 250 ms poll and the 100 ms drain stop being clocks)
"""
import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
CMD = ROOT / "forever" / "src" / "host" / "cmd"
CMAKELISTS = (ROOT / "CMakeLists.txt").read_text()


def text(rel):
    return (CMD / rel).read_text()


def code(source):
    """The source without // comments (the notes in them name the very calls the tests forbid)."""
    return re.sub(r"//[^\n]*", "", source)


class Wiring(unittest.TestCase):
    def setUp(self):
        self.combat = text("TbCmdCombat.cpp")
        self.pure = text("TbCmdCombatPure.h")
        self.binding = text("TbCmdBinding.cpp")
        self.tracing = (ROOT / "forever" / "src" / "host" / "TbHostTracing.cpp").read_text()
        self.scripts = (ROOT / "forever" / "src" / "TbScripts.cpp").read_text()

    def test_the_stub_is_gone_and_the_shared_service_is_compiled(self):
        # The two stub definitions would collide with runtime/CombatMonitorService.cpp, the Turtle TU this build now compiles.
        self.assertNotIn("CombatMonitorService& CombatMonitorService::Instance", self.tracing)
        self.assertNotIn("void CombatMonitorService::LoadTimelineWatch", self.tracing)
        self.assertNotIn('#include "runtime/CombatMonitorService.h"', self.tracing)
        self.assertIn("target_sources(tortoisebots PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/runtime/CombatMonitorService.cpp)", CMAKELISTS)
        # Only the engine build feeds it: the line sits inside the TORTOISEBOTS_SLICE1 block, after the slice sources that need it.
        slice_block = CMAKELISTS[CMAKELISTS.index("if(TORTOISEBOTS_SLICE1)"):]
        self.assertIn("runtime/CombatMonitorService.cpp", slice_block)
        off_block = CMAKELISTS[:CMAKELISTS.index("if(TORTOISEBOTS_SLICE1)")]
        self.assertNotIn("runtime/CombatMonitorService.cpp", off_block)

    def test_a_held_book_survives_a_lead_in_a_loading_screen(self):
        # Review finding: a lead that is not in the world gives group 0, and the book was forgotten at once, so the lead-away grace was
        # unreachable. The sampler passes whether the card already holds a book, and the book is still keyed by the binding.
        self.assertIn("P::Feedable(book.key, book.group, Feeds().count(book.key) != 0)", self.combat)
        self.assertIn("inline bool Feedable(uint32_t bookKey, uint32_t groupId, bool hasBook = false)", self.pure)
        self.assertIn("feed.away.Overdue(usable, nowMs)", self.combat)

    def test_an_expired_book_is_dropped(self):
        # The lead-away grace expired: SampleCard says so, the sample leaves the card out of the standing set, and the forget/erase loop frees the
        # feed and the service book.
        combat = code(self.combat)
        self.assertIn("bool SampleCard(C::CardView const& card, uint32 nowMs)", combat)
        self.assertIn("return P::KeepBook(true);", combat)
        self.assertIn("if (!SampleCard(card, nowMs))", combat)
        self.assertLess(combat.index("if (!SampleCard(card, nowMs))"), combat.index("standing.insert(book.key);"))

    def test_the_tick_and_the_scripts_are_registered(self):
        update = code(self.scripts)
        self.assertIn("TortoiseBots::cmd::combat::Update(diff)", update)
        self.assertIn("TortoiseBots::cmd::combat::RegisterScripts()", update)
        # The tick call is in the engine block (it feeds the engine's cards and the engine's member table).
        engine_block = update[update.index("#ifdef TORTOISEBOTS_SLICE1"):]
        self.assertIn("combat::Update(diff)", engine_block)
        self.assertLess(engine_block.index("run::Update(diff)"), engine_block.index("combat::Update(diff)"))

    def test_every_card_gets_its_window(self):
        body = code(self.binding)
        self.assertIn("combat::FillWindow(out.back());", body)
        append = body[body.index("void AppendBindings(std::vector<pure::CT::BindingInfo>& out"):]
        append = append[:append.index("\n}")]
        # After the card was appended, and before the datagram cap can drop it.
        self.assertLess(append.index("out.push_back(P::BindingOf(h.view));"), append.index("combat::FillWindow(out.back());"))

    def test_the_monitor_writes_no_database_query_of_its_own(self):
        # The service's one read (the aura watch table) goes through the compat helper; the feed file names no database at all.
        combat = code(self.combat)
        for forbidden in ("WorldDatabase", "CharacterDatabase", "PQuery", "Query(", "Execute("):
            self.assertNotIn(forbidden, combat, f"the feed must not touch the database ({forbidden})")
        service = (ROOT / "runtime" / "CombatMonitorService.cpp").read_text()
        self.assertIn("AuraWatchRowsCompat()", service)
        self.assertNotIn("WorldDatabase.Query", service)
        # The query text lives in the two compat files only.
        candidates = list((ROOT / "forever" / "src").rglob("*")) + list((ROOT / "runtime").rglob("*.cpp")) + \
            list((ROOT / "runtime").rglob("*.h")) + list((ROOT / "ai").glob("*.h")) + list((ROOT / "ai" / "playerbot").glob("*.h"))
        holders = [p for p in candidates
                   if p.is_file() and p.suffix in (".h", ".cpp") and "FROM `ai_combat_aura_watch`" in p.read_text(errors="ignore")]
        rel = {str(p.relative_to(ROOT)) for p in holders}
        self.assertEqual(rel, {"forever/src/compat/TbCompat_C5.h", "ai/cmangos-compat-c5.h"},
                         f"the watch table is read through the compat helper only: {sorted(rel)}")

    def test_the_gate_and_the_costs(self):
        combat = code(self.combat)
        self.assertIn("sPlayerbotAIConfig.combatMonitor && DashRuns::link::Active()", combat)
        self.assertIn("service.SyncConfig(on);", combat)
        # Off: every book is forgotten, nothing keeps sampling.
        gate = combat[combat.index("void Update(uint32"):]
        gate = gate[:gate.index("\n}")]
        self.assertIn("if (!on)", gate)
        self.assertIn("Feeds().clear();", gate)
        # The three clocks are the pure header's constants, never a per-tick pass.
        self.assertIn("P::Due(nowMs, g_lastCastMs, P::kCastPollMs)", combat)
        self.assertIn("P::Due(nowMs, g_lastSampleMs, P::kSampleMs)", combat)
        self.assertIn("P::Due(nowMs, g_lastEventMs, P::kEventsMs)", combat)
        # The cast poll is bounded and reads the two ranks only.
        poll = combat[combat.index("void PollEnemyCasts()"):]
        poll = poll[:poll.index("\n}")]
        self.assertIn("CollectEngaged(feed, lead, watch, true)", poll)
        self.assertNotIn("for (Player*", poll)
        # The sampler's cost is logged, as the Turtle sampler's was.
        self.assertIn("bound fights 60s", combat)

    def test_the_engaged_walk_is_bounded(self):
        # The cap bounds the work: the pure scan visits at most kScanRefs references, keeps at most kWatchCap, and the feed resolves only
        # what it kept. No set of every guid and no sort of the whole pull may come back.
        combat = code(self.combat)
        self.assertIn("P::EngagedScan<ObjectGuid> scan(watchedOnly);", combat)
        self.assertIn("scan.Keep(guid, RankOfEntry(mapId, guid.GetEntry()));", combat)
        for forbidden in ("std::set<ObjectGuid>", "stable_sort", "refs.push_back"):
            self.assertNotIn(forbidden, combat, f"the walk keeps at most kWatchCap units ({forbidden})")
        pure = code(self.pure)
        self.assertIn("constexpr size_t kScanRefs = 128;", pure)
        self.assertIn("bool Over() const { return m_visited >= kScanRefs || Saturated(); }", pure)

    def test_two_cards_in_one_group_are_two_books(self):
        # A card's fight is keyed by its commander binding: the group counter is metadata the payload reports, never the key.
        combat = code(self.combat)
        self.assertIn("P::BookRef const book = P::BookOf(card.commanderGuid, card.groupId);", combat)
        self.assertIn("Feed& feed = Feeds()[book.key];", combat)
        self.assertIn("service.Refresh(book.key,", combat)
        self.assertIn("service.NoteBinding(book.key, card.commanderName, card.commanderGuid, lead->GetMapId(), book.group);",
                      combat)
        self.assertIn("standing.insert(book.key);", combat)
        self.assertIn("WindowJson(binding.commanderGuid)", combat)
        for forbidden in ("Feeds()[card.groupId]", "service.Refresh(card.groupId", "WindowJson(binding.groupId)",
                          "service.NoteBinding(card.groupId"):
            self.assertNotIn(forbidden, combat, f"the group counter is never the book key ({forbidden})")

    def test_the_payloads_go_through_the_dashboard_link(self):
        combat = code(self.combat)
        self.assertIn("DashRuns::link::Send(payload)", combat)
        self.assertIn("service.TakePayloads(", combat)
        self.assertIn("service.TakeEventPayloads(", combat)
        for forbidden in ("asio", "udp::", "socket("):
            self.assertNotIn(forbidden, combat, f"no second socket: the dash-runs link is the sender ({forbidden})")

    def test_no_pointer_survives_a_tick_and_no_addon_output(self):
        combat = code(self.combat)
        feed = combat[combat.index("struct Feed"):]
        feed = feed[:feed.index("\n    void NewFight()")]
        for forbidden in ("Player*", "Unit*", "Creature*", "Group*"):
            self.assertNotIn(forbidden, feed, f"Feed keeps guids and plain data only ({forbidden})")
        for forbidden in ("TBM:", "AddonMessage", "SendAddonMessage"):
            self.assertNotIn(forbidden, combat)

    def test_the_sampler_does_not_mutate_the_card_state(self):
        body = code(self.binding)
        cards = body[body.index("std::vector<P::CardView> StandingCards()"):]
        cards = cards[:cards.index("\n}")]
        for forbidden in ("PruneToStanding", "touch(", "s.roles", "log.Note"):
            self.assertNotIn(forbidden, cards, f"the 1 Hz read must not write the card state ({forbidden})")
        self.assertIn("return {};", body)   # the option-OFF definition exists beside the real one

    def test_the_off_build_answers_nothing(self):
        combat = code(self.combat)
        self.assertIn("#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1", combat)
        off = combat[combat.index("\n#else\n"):]
        for no_op in ("void Update(uint32) {}", "void FillWindow(CommanderTelemetry::BindingInfo&) {}",
                      "void RegisterScripts() {}"):
            self.assertIn(no_op, off)

    def test_the_golden_is_captured(self):
        check = (ROOT / "forever" / "tools" / "cmd_combat_check.cpp").read_text()
        self.assertNotIn('kGoldenFight = R"JSON()JSON"', check, "the golden fight was never captured")


MUTATIONS = {
    "feed0": ("    return bookKey != 0 && (groupId != 0 || hasBook);", "    return groupId != 0 || hasBook;"),
    "keepbook": ("    return bookKey != 0 && (groupId != 0 || hasBook);", "    return bookKey != 0 && groupId != 0;"),
    "keepexpired": ("    return !graceExpired;", "    return true;"),
    "bookkey": ("    return BookRef{ commanderGuid, groupId };", "    return BookRef{ groupId, groupId };"),
    "rankorder": ("        while (at < m_kept && static_cast<uint8_t>(m_keys[at].rank) >= static_cast<uint8_t>(rank))",
                  "        while (at < m_kept && static_cast<uint8_t>(m_keys[at].rank) > static_cast<uint8_t>(rank))"),
    "focus": ("    if (bossEntry)\n        for (size_t i = 0; i < engaged.size(); ++i)", "    if (false)\n        for (size_t i = 0; i < engaged.size(); ++i)"),
    "applyonly": ("            edges.emplace_back(before[a], false);", "            (void)before[a];"),
    "alwaysdue": ("    return sinceMs == 0 || nowMs - sinceMs >= everyMs;", "    return true;"),
}


@unittest.skipUnless(os.environ.get("CMD_P5_MUTATE") == "1", "mutations compile the standalone check: run on the build VM with CMD_P5_MUTATE=1")
class Mutations(unittest.TestCase):
    def build(self, header_text):
        tmp = Path(tempfile.mkdtemp(prefix="cmdp5-"))
        (tmp / "forever" / "src" / "host" / "cmd").mkdir(parents=True)
        (tmp / "forever" / "tools").mkdir(parents=True)
        (tmp / "forever" / "src" / "host" / "cmd" / "TbCmdCombatPure.h").write_text(header_text)
        check = (ROOT / "forever" / "tools" / "cmd_combat_check.cpp").read_text()
        (tmp / "forever" / "tools" / "cmd_combat_check.cpp").write_text(check)
        exe = tmp / "check"
        out = subprocess.run(["g++", "-std=c++17", "-O0", "-DCOMBAT_MONITOR_TEST_CONFIG", "-I", str(ROOT / "tools" / "tsan_stubs"),
                              "-I", str(tmp), "-I", str(ROOT), str(tmp / "forever" / "tools" / "cmd_combat_check.cpp"),
                              str(ROOT / "runtime" / "CombatMonitorService.cpp"), "-o", str(exe), "-pthread"],
                             capture_output=True, text=True)
        if out.returncode != 0:
            raise AssertionError(f"the check did not compile:\n{out.stderr}")
        return subprocess.run([str(exe)], capture_output=True, text=True)

    def test_unmutated_passes(self):
        out = self.build((CMD / "TbCmdCombatPure.h").read_text())
        self.assertEqual(out.returncode, 0, out.stdout + out.stderr)

    def test_each_mutation_fails_the_check(self):
        source = (CMD / "TbCmdCombatPure.h").read_text()
        for name, (old, new) in MUTATIONS.items():
            with self.subTest(mutation=name):
                self.assertIn(old, source, f"mutation {name} no longer applies")
                out = self.build(source.replace(old, new, 1))
                self.assertNotEqual(out.returncode, 0, f"mutation {name} was NOT caught:\n{out.stdout}")
                self.assertIn("FAIL", out.stdout + out.stderr)


if __name__ == "__main__":
    unittest.main()

"""Wiring pins for TBF #141: the run trace, the why trace and the decision capture on ForeverCore.

The services are the shared Turtle runtime sources; the host glue is forever/src/host/cmd/TbCmdTrace.cpp and the run
leader's own tick (forever/src/host/run/TbRun.cpp). This proves, by reading the sources, that (1) the three services are
compiled into the slice build and their R6B stand-ins are gone, (2) `.tb trace` and `.tb why` are registered in both
builds, (3) the world tick drives them once, (4) the trace sources use the one 64-bit guid key of either core (a
ForeverCore guid is 128 bits and cannot be rebuilt from it), and (5) the run summary carries the kill times and spots.

Run: python3 -m unittest forever/tools/test_run_trace_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


def text(rel):
    return (ROOT / rel).read_text()


class RunTraceWiring(unittest.TestCase):
    def test_runtime_sources_are_in_the_slice_build(self):
        cmake = text("CMakeLists.txt")
        for name in ("RunTrace.cpp", "RunTraceWriter.cpp", "WhyTrace.cpp", "DecisionCapture.cpp", "DecisionCaptureFiles.cpp"):
            self.assertIn("${CMAKE_CURRENT_SOURCE_DIR}/runtime/" + name, cmake, name)
        generator = text("forever/tools/gen_aiplayerbot_conf.py")
        for name in ("RunTrace.cpp", "RunTraceWriter.cpp", "WhyTrace.cpp", "DecisionCapture.cpp", "DecisionCaptureFiles.cpp"):
            self.assertIn('"' + name + '"', generator, name)

    def test_the_stand_ins_are_gone(self):
        host = text("forever/src/host/TbHostTracing.cpp")
        self.assertNotIn("WhyTraceService&", host)
        self.assertNotIn("RunTraceService&", host)
        self.assertNotIn("RunTraceWriter::Close", host)
        self.assertIn("ObservabilityEmitter& ObservabilityEmitter::Instance()", host)

    def test_commands_are_registered_and_sliced(self):
        scripts = text("forever/src/TbScripts.cpp")
        self.assertEqual(scripts.count('{ "trace",  TortoiseBots::trace::HandleTrace'), 1)
        self.assertEqual(scripts.count('{ "why",    TortoiseBots::trace::HandleWhy'), 1)
        self.assertIn("#ifdef TORTOISEBOTS_SLICE1", scripts)
        cpp = text("forever/src/host/cmd/TbCmdTrace.cpp")
        self.assertIn("#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1", cpp)
        self.assertIn("h3::NotBuilt(handler, args, \"trace\")", cpp)
        self.assertIn("h3::NotBuilt(handler, args, \"why\")", cpp)
        self.assertIn("RunTrace::TraceOffReason(sPlayerbotAIConfig.enabled, sPlayerbotAIConfig.runTrace)", cpp)

    def test_the_world_tick_drives_them_once(self):
        scripts = text("forever/src/TbScripts.cpp")
        for call in ("RunTraceService::Instance().Initialize()",
                     "RunTraceService::Instance().Update(diff)",
                     "RunTraceService::Instance().Shutdown()",
                     "WhyTraceService::Instance().Update(diff)",
                     "WhyTraceService::Instance().Shutdown()",
                     "DecisionCaptureService::Instance().Update()",
                     "DecisionCaptureService::Instance().Shutdown()"):
            self.assertEqual(scripts.count(call), 1, call)

    def test_the_trace_sources_use_the_one_guid_key(self):
        for rel in ("runtime/RunTrace.cpp", "runtime/WhyTrace.cpp"):
            body = text(rel)
            self.assertNotIn("GetRawValue()", body, rel)
            self.assertNotIn("ObjectGuid(guid)", body, rel)
            self.assertIn("GuidKeyCompat(", body, rel)
        self.assertIn("PlayerOfGuidKeyCompat(", text("runtime/RunTrace.cpp"))
        self.assertIn("PlayerOfGuidKeyCompat(", text("runtime/WhyTrace.cpp"))

    def test_both_cores_have_the_trace_helpers(self):
        wrapper = text("ai/playerbot/TbCompatTrace.h")
        self.assertIn('#include "TbCompat_TRACE.h"', wrapper)
        self.assertIn('#include "cmangos-compat-trace.h"', wrapper)
        forever = text("forever/src/compat/TbCompat_TRACE.h")
        turtle = text("ai/cmangos-compat-trace.h")
        for name in ("SpellCooldownDelaySecondsCompat", "ForEachThreatListEntryCompat",
                     "TraceSendSysMessageCompat", "HarmfulGroundAuraCompat"):
            self.assertIn(name, forever, name)
            self.assertIn(name, turtle, name)

    def test_the_run_summary_carries_times_and_spots(self):
        run = text("forever/src/host/run/TbRun.cpp")
        self.assertIn("NoteBossKilled(rec, nowMs, plan->steps[i].entry)", run)
        self.assertIn("NoteBossKilled(rec, nowMs, entry)", run)
        self.assertIn("NoteSpot(rec.deathSpots", run)
        self.assertIn("NoteSpot(rec.wipeSpots", run)
        pure = text("forever/src/dash/TbDashRunsPure.h")
        self.assertIn('\\"killed_at_s\\":', pure)
        self.assertIn('\\"wipe_spots\\":', pure)
        self.assertIn('\\"death_spots\\":', pure)
        view = text("forever/src/dash/TbDashRuns.cpp")
        self.assertIn("rec.bossKilledMs.find(step.entry)", view)
        self.assertIn("v.wipeSpots = spots(rec.wipeSpots)", view)

    def test_the_decision_capture_wraps_the_run_tick(self):
        run = text("forever/src/host/run/TbRun.cpp")
        self.assertIn("DecisionCaptureService::Instance().Begin(", run)
        self.assertIn("DecisionCaptureService::Instance().BeforeTick(captureKey, run, facts, dt)", run)
        self.assertIn("DecisionCaptureService::Instance().AfterTick(captureKey, run, order)", run)
        # #307: the capture's key is one function (run::CaptureKey), shared by the tick, the command and the end.
        self.assertIn('DecisionCaptureService::Instance().End(CaptureKey(rec.leader), why)', run)
        self.assertIn("std::uint64_t CaptureKey(ObjectGuid leader)", run)

    def test_the_shipped_switches_are_off(self):
        conf = text("forever/conf/aiplayerbot.conf.dist")
        self.assertRegex(conf, r"(?m)^AiPlayerbot\.RunTrace\.Enabled = 0$")
        self.assertRegex(conf, r"(?m)^AiPlayerbot\.DecisionCapture\.Enabled = 0$")


if __name__ == "__main__":
    unittest.main()

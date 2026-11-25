"""Wiring pins for TBF #307: `.tb run decisions`, the read path for a run's decision rows.

The rows are the decision capture's (runtime/DecisionCapture*), which the run leader's own tick writes; the command
is forever/src/host/run/TbRunCommands.cpp. This proves, by reading the sources, that (1) the verb is registered in the
one `.tb run` handler and parsed, (2) the read goes through the shared access check the run's controlling verbs use,
(3) capture off is a refusal that names the capture's own toggle (no second key), (4) the rows come from the capture's
bounded window, which the recorder feeds at every tick it records, (5) a row is never split over continuation lines,
and (6) the field table is documented as the stable interface.

Run: python3 -m unittest forever/tools/test_run_decisions_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


def text(rel):
    return (ROOT / rel).read_text()


def decision_body():
    """The command's own section of TbRunCommands.cpp (after the parse branches)."""
    cpp = text("forever/src/host/run/TbRunCommands.cpp")
    marker = "// ---- decisions (#307)"
    assert marker in cpp, "the decisions section marker is gone"
    return cpp[cpp.index(marker):]


class RunDecisionsWiring(unittest.TestCase):
    def test_the_verb_is_registered_and_parsed(self):
        scripts = text("forever/src/TbScripts.cpp")
        self.assertEqual(scripts.count('{ "run",    TortoiseBots::run::HandleRun'), 1)
        cpp = text("forever/src/host/run/TbRunCommands.cpp")
        self.assertIn('verb != "decisions"', cpp)
        self.assertIn("decisions <leader|run> [since=<seq>] [limit=<n>]", cpp)
        # `since=` and `limit=`, once each, and a bad value is the shared refusals.
        body = cpp[cpp.index('else if (verb == "decisions")'):]
        self.assertIn('word.compare(0, 6, "since=")', body)
        self.assertIn('word.compare(0, 6, "limit=")', body)
        self.assertIn('"bad-argument"', body)
        self.assertIn("DecisionCapture::ClampRows(typed)", body)
        # A run id is accepted as well as a leader's name, and the run record
        # prints the id the command takes.
        self.assertIn("RunByArg(leaderName)", cpp)
        run_cpp = text("forever/src/host/run/TbRun.cpp")
        self.assertIn('N("run", int64(rec.runId))', run_cpp)

    def test_access_is_the_shared_run_check(self):
        body = decision_body()
        self.assertIn("h3::CheckRunObserverAccess(handler, rec->leader, code, detail)", body)
        self.assertIn('Fail(handler, args, "run", code, detail)', body)

    def test_capture_off_is_a_refusal_that_names_the_toggle(self):
        body = decision_body()
        self.assertIn("DecisionCaptureService::Instance().Enabled()", body)
        self.assertIn('"capture-off"', body)
        self.assertIn("AiPlayerbot.DecisionCapture.Enabled", body)
        self.assertIn('"no-capture"', body)

    def test_a_player_led_run_is_refused_and_starts_no_capture(self):
        body = decision_body()
        self.assertIn("if (IsPlayerLed(*rec))", body)
        # The refusal comes before the lookup, so an empty capture cannot pass
        # for a run with no decisions.
        self.assertLess(body.index("if (IsPlayerLed(*rec))"), body.index("Rows(captureKey)"))
        run_cpp = text("forever/src/host/run/TbRun.cpp")
        self.assertIn("if (captureTicks)", run_cpp)
        self.assertIn("Create(tank->GetGUID(), name, plan, nowMs, 0, false)", run_cpp)
        self.assertIn("bool captureTicks = true", text("forever/src/host/run/TbRun.h"))

    def test_rows_come_from_the_capture_window_the_tick_feeds(self):
        body = decision_body()
        self.assertIn("DecisionCaptureService::Instance().Rows(captureKey)", body)
        self.assertIn("CaptureKey(rec->leader)", body)
        self.assertIn("DecisionCapture::Select(", body)
        run = text("forever/src/host/run/TbRun.cpp")
        self.assertEqual(run.count("DecisionCaptureService::Instance().Begin(CaptureKey(leader)"), 1)
        self.assertIn("uint64_t CaptureKey(ObjectGuid leader)", run)
        recorder = text("runtime/DecisionCaptureRecorder.h")
        self.assertIn("rows_.Push(std::move(row))", recorder)
        self.assertIn("RowLog const& rows() const", recorder)
        # The row is the tick's decision: the same `out` values the record carries.
        self.assertIn("row.state = static_cast<std::uint8_t>(run.state)", recorder)
        self.assertIn("row.order = order", recorder)
        # A tick whose record the file never took still has its row: the push
        # comes before the header backpressure return, and the one early return
        # that skips it is the repeated `done` (not a new decision).
        self.assertLess(recorder.index("rows_.Push(std::move(row))"),
                        recorder.index("if (headerPending_ && !PushHeader())"))
        self.assertLess(recorder.index("if (done && doneWritten_)"),
                        recorder.index("rows_.Push(std::move(row))"))
        # A row says what became of its record: one uniform stream would hide a
        # decision whose evidence never reached the disk.
        self.assertIn("rows_.SetNewestRecord(kRecordTick)", recorder)
        self.assertIn("rows_.SetNewestRecord(kRecordDropped)", recorder)
        self.assertIn("rows_.SetNewestRecord(accepted ? kRecordStub : kRecordDropped)", recorder)
        codec = text("tools/test_decision_capture_codec.cpp")
        self.assertIn("static void TestRowWindow()", codec)
        self.assertIn("TestRowWindow();", codec)
        self.assertIn("kRecordDropped", codec)
        self.assertIn("kRecordStub", codec)

    def test_one_row_never_splits_over_continuation_lines(self):
        body = decision_body()
        self.assertIn("DecisionCapture::kRowTextBytes", body)
        self.assertIn('S("record", DecisionCapture::RecordName(row.record))', body)
        self.assertIn("out.T(field.key, *field.value", body)
        rows = text("runtime/DecisionCaptureRows.h")
        self.assertIn("static_assert(kWorstRowLineBytes <= kRowLineBytes", rows)
        self.assertIn("inline constexpr std::size_t kRowLineBytes = 3000", rows)
        # The fixed key order and the clamps the command relies on live there too.
        self.assertIn('{ "order", &row.order }, { "note", &row.note }, { "say", &row.say }', rows)
        self.assertIn("inline constexpr int kMaxRows = 200", rows)
        self.assertIn("inline constexpr int kDefaultRows = 50", rows)

    def test_the_field_table_is_documented_as_the_stable_interface(self):
        page = text("docs/play/dungeon-run-leader.md")
        self.assertIn(".tb run decisions <leader|run> [since=<seq>] [limit=<n>]", page)
        self.assertIn("stable", page)
        for key in ("seq", "t", "dt", "state", "phase", "step", "record", "order", "note", "say", "next", "more", "trunc"):
            self.assertRegex(page, r"`" + key + r"`", key)
        capture = text("docs/server/decision-capture.md")
        self.assertIn("`.tb run decisions`", capture)
        self.assertIn("trunc", capture)
        self.assertIn("512", capture)   # the window size the command reports over


if __name__ == "__main__":
    unittest.main()

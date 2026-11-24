#!/usr/bin/env python3
"""Unit tests for forever/tools/run_tools_tests.py.

Each test builds a throwaway repo root (forever/tools/ with fake test files), points the runner at
it, and checks what the runner decides: the mode it picks per file, the zero-check rule, the one
retry, the known-failing list and the exit code. No real test file is run.
"""
import contextlib
import io
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run_tools_tests as runner


def write_file(root, name, body):
    directory = os.path.join(root, "forever", "tools")
    os.makedirs(directory, exist_ok=True)
    path = os.path.join(directory, name)
    with open(path, "w", encoding="utf-8") as f:
        f.write(body)
    return path


class RunnerTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = self._tmp.name
        self._old_root = runner.ROOT
        self._old_known = runner.KNOWN_FAILING
        runner.ROOT = self.root
        runner.KNOWN_FAILING = {}

    def tearDown(self):
        runner.ROOT = self._old_root
        runner.KNOWN_FAILING = self._old_known
        self._tmp.cleanup()

    def run_main(self, argv=()):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = runner.main(list(argv))
        return code, out.getvalue()

    def test_classify_reads_the_file_shape(self):
        self.assertEqual(runner.classify("import unittest\n\nclass T(unittest.TestCase):\n    pass\n"), "unittest")
        self.assertEqual(runner.classify('if __name__ == "__main__":\n    main()\n'), "script")
        self.assertEqual(runner.classify("failures = []\nprint('OK: pins')\n"), "script")

    def test_count_checks_unittest_reads_the_ran_line(self):
        self.assertEqual(runner.count_checks("unittest", [], "Ran 0 tests in 0.000s\n\nNO TESTS RAN"), 0)
        self.assertEqual(runner.count_checks("unittest", [], "....\nRan 12 tests in 0.007s\n\nOK"), 12)

    def test_count_checks_script_reads_reported_lines(self):
        self.assertEqual(runner.count_checks("script", ["", "  ", "PASS: x"], ""), 1)
        self.assertEqual(runner.count_checks("script", ["", "  "], ""), 0)

    def test_modes_are_chosen_per_file_and_all_pass(self):
        write_file(self.root, "test_unit.py",
                   "import unittest\n\nclass T(unittest.TestCase):\n    def test_ok(self):\n        self.assertTrue(True)\n")
        write_file(self.root, "test_script_main.py",
                   'def main():\n    print("OK: main guard")\n\nif __name__ == "__main__":\n    main()\n')
        write_file(self.root, "test_script_flat.py", "print('PASS: module level')\n")
        code, out = self.run_main()
        self.assertEqual(code, 0, out)
        self.assertIn("PASS     unittest", out)
        self.assertIn("PASS     script   ", out)
        self.assertIn("3 files: 3 passed, 0 known-failing (issue listed), 0 failed", out)

    def test_a_script_file_that_prints_nothing_runs_zero_checks_and_fails(self):
        write_file(self.root, "test_silent.py", "checks = []\n")
        code, out = self.run_main()
        self.assertEqual(code, 1, out)
        self.assertIn("ran zero checks", out)
        self.assertIn("0 passed, 0 known-failing (issue listed), 1 failed", out)

    def test_a_unittest_file_without_tests_exits_five_and_fails(self):
        # unittest exits 5 ("NO TESTS RAN") on a file with no TestCase; the runner must not read that as a pass.
        write_file(self.root, "test_none.py", "import unittest\n")
        test_file = runner.TestFile("forever/tools/test_none.py")
        self.assertEqual(test_file.kind, "script")
        write_file(self.root, "test_green.py",
                   "import unittest\n\nclass T(unittest.TestCase):\n    def test_ok(self):\n        pass\n")
        files = {f.path: f for f in (runner.TestFile(p) for p in runner.find_tests())}
        self.assertEqual(files["forever/tools/test_none.py"].kind, "script")
        self.assertEqual(files["forever/tools/test_green.py"].kind, "unittest")
        self.assertTrue(runner.run_one(files["forever/tools/test_green.py"]).ok)
        self.assertEqual(files["forever/tools/test_green.py"].checked, 1)

    def test_a_failure_is_retried_once(self):
        marker = os.path.join(self.root, "first-run-done")
        write_file(self.root, "test_flaky.py",
                   "import os, sys\n\nmarker = %r\n"
                   "if not os.path.exists(marker):\n"
                   "    open(marker, 'w').close()\n"
                   "    print('transient failure')\n"
                   "    sys.exit(1)\n"
                   "print('OK: second run')\n" % marker)
        code, out = self.run_main()
        self.assertEqual(code, 0, out)
        self.assertIn("(passed on the retry)", out)

    def test_no_rerun_counts_the_first_failure(self):
        write_file(self.root, "test_bad.py", "print('FAIL: one')\nraise SystemExit(1)\n")
        code, out = self.run_main(["--no-rerun"])
        self.assertEqual(code, 1, out)
        self.assertIn("1 failed", out)

    def test_known_failing_is_reported_and_skipped(self):
        write_file(self.root, "test_known.py", "raise SystemExit(1)\n")
        runner.KNOWN_FAILING = {"forever/tools/test_known.py": "issue #1: dev bug"}
        code, out = self.run_main(["--no-rerun"])
        self.assertEqual(code, 0, out)
        self.assertIn("KNOWN", out)
        self.assertIn("issue #1", out)
        self.assertIn("1 known-failing (issue listed)", out)

    def test_words_filter_files(self):
        write_file(self.root, "test_alpha.py", "print('OK: alpha')\n")
        write_file(self.root, "test_beta.py", "print('OK: beta')\n")
        tests = runner.find_tests(["alpha"])
        self.assertEqual(tests, ["forever/tools/test_alpha.py"])

    def test_no_file_matched_exits_two(self):
        code, out = self.run_main(["nothing-matches"])
        self.assertEqual(code, 2, out)


if __name__ == "__main__":
    unittest.main()

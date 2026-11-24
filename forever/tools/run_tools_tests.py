#!/usr/bin/env python3
"""Run every forever/tools/test_*.py the way that file is meant to run, and fail on a file that ran nothing.

The files under forever/tools come in two shapes:

  * unittest files: they define ``class ...(unittest.TestCase)``. They run as
    ``python3 -m unittest forever/tools/<file>.py`` from the repo root and report ``Ran N tests``.
  * script files: the checks sit at module level (or under ``if __name__ == "__main__":``)
    and the file is meant to run as ``python3 forever/tools/<file>.py``. It reports its result
    as a printed line ("PASS: ...", "OK: ...", "N checks, M failed") and exits non-zero on failure.

``python3 -m unittest`` on a script file finds no tests and exits 5 ("NO TESTS RAN"), which is a
run of zero checks, not a pass. This runner reads each file, picks the mode from the file itself,
runs it from the repo root, and counts what the file actually ran:

  * unittest mode: the ``Ran N tests`` line; N must be at least 1.
  * script mode: the non-empty lines the script printed on stdout (each such file prints its
    result line; a script that prints nothing checked nothing).

A file that fails once is run once more (the fake-ssh census tests use flock and are timing
sensitive on a loaded box), and only the second result counts.

A file in KNOWN_FAILING is run, reported as KNOWN, and does not fail the run: those are the
real bugs in dev that a test catches; each entry names its issue. Never put a file there to
get green without an issue number and a file:line in the issue.

Usage (from anywhere; the repo root is found from this file):
    python3 forever/tools/run_tools_tests.py              # run everything
    python3 forever/tools/run_tools_tests.py --list       # only print file and mode
    python3 forever/tools/run_tools_tests.py test_w1 test_wl0   # only files whose name contains one of the words
    python3 forever/tools/run_tools_tests.py --no-rerun   # a failure counts on the first run

Exit codes: 0 all passed (known-failing skipped), 1 a file failed or ran zero checks,
2 no file matched the name filter.
"""
import argparse
import os
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TESTS_GLOB_DIR = os.path.join("forever", "tools")

# Real bugs in dev that a test catches. Each one is run, reported KNOWN (so the workflow is green) and
# stays visible here; the issue says what the test proves and where the faulty code is. A stale test is
# updated, never listed here, and a line comes out only when its issue is fixed.
KNOWN_FAILING = {
    # Empty: #260 (`available mount vendors` unregistered and read as a type the stand-in lacked) is fixed and
    # forever/tools/test_pc_r5_fix.py passes. A real bug a test catches goes here with its issue number and a
    # file:line in the issue; never put a file here to get green.
}

TESTCASE_RE = re.compile(r"^\s*class\s+\w+\s*\(\s*unittest\.TestCase\s*\)\s*:", re.MULTILINE)
RAN_RE = re.compile(r"^Ran (\d+) tests? in ", re.MULTILINE)


class TestFile:
    """One forever/tools/test_*.py: how it runs and what its last run reported."""

    def __init__(self, path):
        self.path = path                                  # repo-relative, e.g. forever/tools/test_w1_wiring.py
        self.text = read_text(os.path.join(ROOT, path))
        self.kind = classify(self.text)
        self.checked = 0                                  # tests run (unittest) or result lines printed (script)
        self.ok = False
        self.retried = False
        self.output = ""
        self.seconds = 0.0

    def __str__(self):
        return self.path


def read_text(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def classify(text):
    """'unittest' if the file defines a TestCase, else 'script'.

    Every file here is one of the two: script files keep their checks at module level or under a
    ``__main__`` guard, so ``python3 <file>`` runs them. A file that prints nothing is caught by
    the zero-check rule below, whether it has a guard or not.
    """
    return "unittest" if TESTCASE_RE.search(text) else "script"


def count_checks(kind, stdout_lines, output):
    """How many checks the file ran, as the file reports it."""
    if kind == "unittest":
        ran = RAN_RE.findall(output)
        return int(ran[-1]) if ran else 0
    if kind == "script":
        return sum(1 for line in stdout_lines if line.strip())
    return 0


def run_one(test_file, rerun=True, timeout=600):
    """Run one file from the repo root; a first failure is retried once."""
    argv = [sys.executable, "-m", "unittest", test_file.path] if test_file.kind == "unittest" \
        else [sys.executable, test_file.path]
    for attempt in (1, 2):
        start = time.monotonic()
        try:
            completed = subprocess.run(argv, cwd=ROOT, capture_output=True, text=True, timeout=timeout)
            code, stdout, stderr = completed.returncode, completed.stdout, completed.stderr
        except subprocess.TimeoutExpired as expired:
            code = 124
            stdout = (expired.stdout or b"").decode("utf-8", "replace") if isinstance(expired.stdout, bytes) else (expired.stdout or "")
            stderr = "timed out after %s s" % timeout
        test_file.seconds = time.monotonic() - start
        test_file.output = (stdout + stderr).strip()
        test_file.checked = count_checks(test_file.kind, stdout.splitlines(), test_file.output)
        test_file.ok = code == 0 and test_file.checked > 0
        if test_file.ok or not rerun or attempt == 2:
            return test_file
        test_file.retried = True
    return test_file


def find_tests(words=None):
    directory = os.path.join(ROOT, TESTS_GLOB_DIR)
    if not os.path.isdir(directory):
        return []
    files = sorted(
        os.path.join(TESTS_GLOB_DIR, name).replace(os.sep, "/")
        for name in os.listdir(directory)
        if name.startswith("test_") and name.endswith(".py")
    )
    if words:
        files = [f for f in files if any(word in os.path.basename(f) for word in words)]
    return files


def main(argv=None):
    parser = argparse.ArgumentParser(description="Run the forever/tools tests the way each file is meant to run.")
    parser.add_argument("words", nargs="*", help="only files whose name contains one of these words")
    parser.add_argument("--list", action="store_true", help="print file and mode, run nothing")
    parser.add_argument("--no-rerun", action="store_true", help="a first failure counts")
    parser.add_argument("--timeout", type=int, default=600, help="seconds per file (default 600)")
    args = parser.parse_args(argv)

    tests = [TestFile(path) for path in find_tests(args.words)]
    if not tests:
        print("no forever/tools/test_*.py files matched")
        return 2

    if args.list:
        for test_file in tests:
            print("%-8s %s" % (test_file.kind, test_file.path))
        return 0

    failed, known, passed = [], [], []
    print("run_tools_tests: %d files under %s/" % (len(tests), TESTS_GLOB_DIR))
    for test_file in tests:
        run_one(test_file, rerun=not args.no_rerun, timeout=args.timeout)
        if test_file.ok:
            passed.append(test_file)
            print("PASS     %-8s %-4d %6.1fs  %s%s" % (test_file.kind, test_file.checked, test_file.seconds,
                                                        test_file.path, "  (passed on the retry)" if test_file.retried else ""))
        elif test_file.path in KNOWN_FAILING:
            known.append(test_file)
            print("KNOWN    %-8s %-4d %6.1fs  %s  (%s)" % (test_file.kind, test_file.checked, test_file.seconds,
                                                          test_file.path, KNOWN_FAILING[test_file.path]))
        else:
            failed.append(test_file)
            print("FAILED   %-8s %-4d %6.1fs  %s" % (test_file.kind, test_file.checked, test_file.seconds, test_file.path))
            if test_file.checked == 0:
                print("         ran zero checks (unittest exit 5 is not a pass)")
            for line in test_file.output.splitlines()[-40:]:
                print("         | " + line)

    print("%d files: %d passed, %d known-failing (issue listed), %d failed"
          % (len(tests), len(passed), len(known), len(failed)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

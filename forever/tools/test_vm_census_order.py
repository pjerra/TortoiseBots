"""vm-census.sh: the slot lock comes first, then the upload, then the census; every run has its own output folder.

The script is run for real with a fake `ssh` (runs the remote command locally under a temporary HOME, so the real flock is
used), a fake `rsync` (rewrites VM:path to the temporary HOME and calls the real rsync, logging when it ran and whether the
slot lock was held at that moment) and a fake `docker` (logs the start and end of the census, writes a summary naming the
tree it saw in src). Two runs started together on the same slot must not overlap, and each summary must name its own tree.

Run: python3 -m unittest forever/tools/test_vm_census_order.py
"""
import os, pathlib, shutil, subprocess, tempfile, time, unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOLS = ROOT / "forever" / "tools"

FAKE_SSH = """#!/bin/bash
shift            # the VM name
exec bash -c "$*"
"""
FAKE_RSYNC = """#!/bin/bash
args=()
for a in "$@"; do args+=("${a/fakevm:/$HOME/}"); done
dest="${args[-1]}"
# is the slot lock held by someone (us, through the lock session)? flock -n fails when it is.
if flock -n "$HOME/forever-build/census${SLOT_SUF}.lock" true; then held=no; else held=yes; fi
mark=$(cat "${args[-2]}forever/MARK" 2>/dev/null || echo core)
echo "$(date +%s.%N) RSYNC $mark lock_held=$held" >> "$EVENTS"
sleep 0.5
exec /usr/bin/rsync -c "${args[@]}"
"""
FAKE_DOCKER = """#!/bin/bash
out=""; src=""
while [ $# -gt 0 ]; do
  if [ "$1" = -v ]; then
    case "$2" in *:/census) out=${2%%:/census};; *:/src:ro) src=${2%%:/src:ro};; esac
  fi
  shift
done
mark=$(cat "$src/src/modules/TortoiseBots/forever/MARK")
echo "$(date +%s.%N) CENSUS-START $mark out=$(basename $out)" >> "$EVENTS"
sleep 1
if [ "$mark" = EMPTY ]; then n=0; else n=3; fi
echo "files $n  compile clean $n  with errors 0  error places 0 (tree $mark)" > "$out/summary.txt"
: > "$out/where.tsv"; : > "$out/names.tsv"; : > "$out/kinds.tsv"; : > "$out/botpch_forever.pch"
echo "$(date +%s.%N) CENSUS-END $mark" >> "$EVENTS"
"""


class Env:
    def __init__(self, base):
        self.home = base / "home"
        self.bin = base / "bin"
        self.core = base / "core"
        self.events = base / "events.log"
        for d in (self.home / "forever-build", self.bin, self.core):
            d.mkdir(parents=True)
        (self.core / "src" / "modules").mkdir(parents=True)
        (self.core / "CORE").write_text("core")
        self.events.write_text("")
        for name, body in (("ssh", FAKE_SSH), ("rsync", FAKE_RSYNC), ("docker", FAKE_DOCKER)):
            (self.bin / name).write_text(body)
            (self.bin / name).chmod(0o755)
        self.base = base

    def worktree(self, mark):
        wt = self.base / ("wt-" + mark)
        (wt / "forever" / "tools").mkdir(parents=True)
        for f in ("vm-census.sh", "census.sh"):
            shutil.copy(TOOLS / f, wt / "forever" / "tools" / f)
        (wt / "forever" / "MARK").write_text(mark)
        return wt

    def run(self, wt, slot="", extra=None):
        env = dict(os.environ, HOME=str(self.home), PATH=f"{self.bin}:{os.environ['PATH']}", FOREVER_VM="fakevm",
                   FOREVER_CORE=str(self.core), EVENTS=str(self.events), SLOT_SUF=f"-{slot}" if slot else "")
        env.pop("FOREVER_SLOT", None)
        if slot:
            env["FOREVER_SLOT"] = slot
        env.update(extra or {})
        return subprocess.Popen(["bash", str(wt / "forever" / "tools" / "vm-census.sh"), "", "4"], env=env,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

    def events_list(self):
        return [l.split(" ", 1)[1] for l in self.events.read_text().splitlines()]


class VmCensusOrder(unittest.TestCase):
    def setUp(self):
        self.tmp = pathlib.Path(tempfile.mkdtemp(prefix="vmcensus-"))
        self.env = Env(self.tmp)

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_lock_is_held_during_the_upload_and_the_order_is_lock_upload_census(self):
        p = self.env.run(self.env.worktree("A"), slot="2")
        out, err = p.communicate(timeout=60)
        self.assertEqual(p.returncode, 0, err)
        ev = self.env.events_list()
        self.assertEqual([e.split()[0] for e in ev], ["RSYNC", "RSYNC", "CENSUS-START", "CENSUS-END"], ev)
        self.assertTrue(all("lock_held=yes" in e for e in ev if e.startswith("RSYNC")), ev)
        self.assertIn("tree A", out)
        self.assertLess(err.index("waiting for the slot"), err.index("lock held, uploading"))

    def test_two_runs_on_one_slot_do_not_overlap_and_each_reads_its_own_tree(self):
        a, b = self.env.worktree("A"), self.env.worktree("B")
        pa = self.env.run(a, slot="2")
        pb = self.env.run(b, slot="2")
        (oa, ea), (ob, eb) = pa.communicate(timeout=90), pb.communicate(timeout=90)
        self.assertEqual((pa.returncode, pb.returncode), (0, 0), (ea, eb))
        ev = self.env.events_list()
        marks = [e.split()[1] for e in ev]
        first = marks[1]
        second = "B" if first == "A" else "A"
        # all four events of the first run (core upload, tree upload, census start, census end) come before any of the second
        self.assertEqual(marks, ["core", first, first, first, "core", second, second, second], ev)
        self.assertIn("tree A", oa)
        self.assertIn("tree B", ob)
        folders_a = [l for l in oa.splitlines() if l.startswith("run folder:")]
        folders_b = [l for l in ob.splitlines() if l.startswith("run folder:")]
        self.assertNotEqual(folders_a, folders_b)
        self.assertTrue(folders_a and "census-runs/" in folders_a[0])

    def test_different_slots_do_not_wait_for_each_other(self):
        pa = self.env.run(self.env.worktree("A"), slot="2")
        pb = self.env.run(self.env.worktree("B"), slot="4")
        t0 = time.time()
        pa.communicate(timeout=60), pb.communicate(timeout=60)
        self.assertEqual((pa.returncode, pb.returncode), (0, 0))
        marks = [e.split()[0] + e.split()[1] for e in self.env.events_list()]
        # the two censuses overlap: both START events come before the first END
        starts = [i for i, m in enumerate(marks) if m.startswith("CENSUS-START")]
        ends = [i for i, m in enumerate(marks) if m.startswith("CENSUS-END")]
        self.assertLess(max(starts), min(ends), marks)

    def test_the_folder_is_never_a_bare_slot_number_and_the_name_can_be_passed(self):
        p = self.env.run(self.env.worktree("A"), slot="2", extra={"FOREVER_CENSUS_NAME": "mine-1"})
        out, err = p.communicate(timeout=60)
        self.assertEqual(p.returncode, 0, err)
        self.assertTrue((self.env.home / "forever-build" / "census-runs" / "mine-1" / "summary.txt").exists())
        self.assertFalse((self.env.home / "forever-build" / "census-2").exists())
        self.assertFalse((self.env.home / "forever-build" / "census-runs" / "mine-1" / "botpch_forever.pch").exists())
        q = self.env.run(self.env.worktree("B"), slot="2", extra={"FOREVER_CENSUS_NAME": "../x"})
        q.communicate(timeout=30)
        self.assertEqual(q.returncode, 2)

    def test_files_zero_is_not_a_measurement(self):
        p = self.env.run(self.env.worktree("EMPTY"), slot="2")
        out, err = p.communicate(timeout=60)
        self.assertEqual(p.returncode, 3, (out, err))
        self.assertIn("files 0, not a measurement", err)

    def test_the_lock_is_released_when_the_run_ends(self):
        p = self.env.run(self.env.worktree("A"), slot="2")
        p.communicate(timeout=60)
        time.sleep(0.5)
        lock = self.env.home / "forever-build" / "census-2.lock"
        self.assertEqual(subprocess.run(["flock", "-n", str(lock), "true"]).returncode, 0)


if __name__ == "__main__":
    unittest.main()

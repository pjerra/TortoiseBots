"""Wiring pins for `.tb removeitem [<character>] <item id> [count]` (the inverse of `.tb additem`).

Run:
  python3 -m unittest forever/tools/test_removeitem_wiring.py
  python3 forever/tools/test_removeitem_wiring.py --mutate   # must FAIL (exit non-zero)
"""
import pathlib
import re
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPTS = (ROOT / "forever/src/TbScripts.cpp").read_text()
H3_HDR = (ROOT / "forever/src/host/TbH3.h").read_text()
H3_OFF = (ROOT / "forever/src/host/TbH3Off.cpp").read_text()
KIT = (ROOT / "forever/src/host/TbTestKit.cpp").read_text()
DOCS = (ROOT / "forever/docs/TB-COMMANDS.md").read_text()


def check(scripts, hdr, off, kit, docs):
    if not re.search(r'\{\s*"removeitem"\s*,\s*TortoiseBots::h3::HandleRemoveItem\s*,\s*rbac::RBAC_PERM_COMMAND_GM', scripts):
        raise AssertionError('TbScripts.cpp missing the "removeitem" GM row -> HandleRemoveItem')
    if scripts.find('"removeitem"') < scripts.find('"additem"') or scripts.find('"removeitem"') > scripts.find('"use"'):
        raise AssertionError('"removeitem" row must sit next to "additem"')
    if "bool HandleRemoveItem(" not in hdr:
        raise AssertionError("TbH3.h missing HandleRemoveItem")
    if not re.search(r'bool HandleRemoveItem\([^)]*\)\s*\{\s*return NotBuilt\(handler,\s*args,\s*"removeitem"\);\s*\}', off):
        raise AssertionError("TbH3Off.cpp missing the NotBuilt stub")
    fn = kit[kit.index("bool HandleRemoveItem("):]
    fn = fn[:fn.index("\n}\n")]
    # same target rule as the other test-kit commands
    if 'TargetArg(handler, args, "removeitem", start)' not in fn:
        raise AssertionError("removeitem must resolve its target through TargetArg (host bot or sender)")
    if "DestroyItemCount(" not in fn:
        raise AssertionError("removeitem must use the core's Player::DestroyItemCount")
    if "target::RemoveAmount(" not in fn:
        raise AssertionError("removeitem must cap the amount with target::RemoveAmount (never equipped items)")
    for code in ("unknown-item", "bad-id", "missing-argument"):
        if f'"{code}"' not in fn:
            raise AssertionError(f"removeitem lacks the typed error {code}")
    if not re.search(r'Rec\("removeitem"\).*N\("removed", removed\).*N\("left", left\)', fn):
        raise AssertionError("removeitem reply must carry removed= and left=")
    if "`.tb removeitem " not in docs or "removed=<n> left=<n>" not in docs:
        raise AssertionError("TB-COMMANDS.md must document .tb removeitem and its reply")


class RemoveItemWiring(unittest.TestCase):
    def test_wiring(self):
        check(SCRIPTS, H3_HDR, H3_OFF, KIT, DOCS)

    def test_mutations_are_caught(self):
        for old, new in (('"removeitem", TortoiseBots', '"removeitemX", TortoiseBots'),):
            with self.assertRaises(AssertionError):
                check(SCRIPTS.replace(old, new), H3_HDR, H3_OFF, KIT, DOCS)
        with self.assertRaises(AssertionError):
            check(SCRIPTS, H3_HDR, H3_OFF, KIT.replace("DestroyItemCount(", "DestroyItem("), DOCS)
        with self.assertRaises(AssertionError):
            check(SCRIPTS, H3_HDR, H3_OFF, KIT.replace('TargetArg(handler, args, "removeitem"', 'BotArg(handler, args, "removeitem"'), DOCS)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        try:
            check(SCRIPTS.replace('"removeitem", TortoiseBots', 'x'), H3_HDR, H3_OFF, KIT, DOCS)
        except AssertionError as e:
            print("mutation caught:", e)
            sys.exit(1)
        sys.exit(0)
    unittest.main()

"""The carried count of `.tb itemscan` (the scan did not see what `.tb additem` gave).

`.tb additem` answers `Player::GetItemCount(entry, false)` (the bot's worn pieces and everything in its bags, never bank storage). The bench's
read-back reads `.tb itemscan`'s `inv=` as that count, so the record's `inv=` must be the count and the template's
inventory type must live under `type=`. Before the fix `inv=` carried the template's inventory type, which is 0 for
every consumable: `.tb additem Bheal 1708 20` answered "now has 20" and `.tb itemscan Bheal 1708` read inv=0.

Run:
  python3 -m unittest forever/tools/test_itemscan_wiring.py
  python3 forever/tools/test_itemscan_wiring.py --mutate   # must FAIL (exit non-zero)
"""
import pathlib
import re
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
KIT = (ROOT / "forever/src/host/TbTestKit.cpp").read_text()
SCRIPTS = (ROOT / "forever/src/TbScripts.cpp").read_text()
DOCS = (ROOT / "forever/docs/TB-COMMANDS.md").read_text()


def function(text, signature):
    fn = text[text.index(signature):]
    return fn[:fn.index("\n}\n")]


def check(kit=KIT, scripts=SCRIPTS, docs=DOCS):
    scan = function(kit, "bool HandleItemScan(")
    if not re.search(r'rec\.N\("inv",\s*bot->GetItemCount\(id,\s*false\)\)', scan):
        raise AssertionError(
            "itemscan must answer inv= with the count the bot carries (bot->GetItemCount(id, false)): "
            "the same number .tb additem answers")
    if re.search(r'N\("inv",\s*unsigned\(proto->GetInventoryType\(\)\)\)', scan):
        raise AssertionError(
            "itemscan must not put the template inventory type in inv=: it is 0 for every consumable, "
            "so additem then itemscan read inv=0")
    if not re.search(r'\.N\("type",\s*unsigned\(proto->GetInventoryType\(\)\)\)', scan):
        raise AssertionError("the template inventory type must stay readable, as type=")

    add = function(scripts, "static bool HandleAddItem(")
    if "GetItemCount(entry, false)" not in add:
        raise AssertionError(
            "additem's answer must read GetItemCount(entry, false): the same count itemscan answers, "
            "written down at both places")

    line = next((l for l in docs.splitlines() if l.startswith("* `.tb itemscan ")), "")
    if not line:
        raise AssertionError("TB-COMMANDS.md has no `.tb itemscan` contract line")
    for needle in ("inv=<count", "type=<inventory type>"):
        if needle not in line:
            raise AssertionError("TB-COMMANDS.md `.tb itemscan` line must carry %s" % needle)
    if "inv=<inventory" in line:
        raise AssertionError("TB-COMMANDS.md still names inv= the inventory type")


class ItemScanCountWiring(unittest.TestCase):
    def test_the_scan_answers_the_carried_count(self):
        check()

    def test_the_type_in_inv_fails(self):
        # The failing case this file exists for: inv= built from the template's inventory type again.
        mutated = KIT.replace(
            'rec.N("inv", bot->GetItemCount(id, false)).N("type", unsigned(proto->GetInventoryType()))',
            'rec.N("inv", unsigned(proto->GetInventoryType()))')
        if mutated == KIT:
            raise AssertionError("cannot find the inv/type record line to mutate")
        with self.assertRaises(AssertionError):
            check(kit=mutated)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        mutated = KIT.replace(
            'rec.N("inv", bot->GetItemCount(id, false)).N("type", unsigned(proto->GetInventoryType()))',
            'rec.N("inv", unsigned(proto->GetInventoryType()))')
        try:
            check(kit=mutated)
        except AssertionError:
            print("PASS: the mutation is caught")
            sys.exit(0)
        raise SystemExit("FAIL: the mutation was not caught")
    unittest.main()

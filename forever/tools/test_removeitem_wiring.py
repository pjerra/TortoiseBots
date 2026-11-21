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
    # bags only, structurally: destroy exactly the collected Items through the Item* overload; never the entry-based call (it sweeps equipment and bank)
    if not re.search(r'bot->DestroyItemCount\(it,\s*remaining,\s*true\)', fn):
        raise AssertionError("removeitem must destroy the collected Items with the core's Item*-based Player::DestroyItemCount(Item*, uint32&, bool)")
    if re.search(r'DestroyItemCount\(\s*item\s*,', fn):
        raise AssertionError("removeitem must not use the entry-based DestroyItemCount(entry, ...): it also destroys equipment and bank stacks")
    if "target::IsBagStack(" not in fn:
        raise AssertionError("removeitem must collect stacks with the pure target::IsBagStack rule")
    # the entry filter, IsBagStack's arguments in order (bag, slot, backpack size, in-trade) and the trade flag exactly as written
    if "it->GetEntry() == item && target::IsBagStack(it->GetBagSlot(), it->GetSlot(), backpackSlots, removable && it->IsInTrade())" not in fn:
        raise AssertionError("removeitem's visit must keep the entry filter and call IsBagStack(bag, slot, backpackSlots, removable && IsInTrade())")
    if "uint8 const backpackSlots = uint8(bot->GetInventorySlotCount());" not in fn:
        raise AssertionError("removeitem must size the backpack from the target's inventory slot count")
    if "ItemSearchLocation::Inventory" not in fn or "ItemSearchLocation::ReagentBank" not in fn:
        raise AssertionError("removeitem must walk the worn bags and the reagent bag")
    for loc in ("Equipment", "Bank"):
        if f"ItemSearchLocation::{loc}" in fn:
            raise AssertionError(f"removeitem must not walk {loc}")
    # left is read again from the bags after the removal, never computed
    if not re.search(r'uint32 const left = total\(collect\(false\)\);', fn):
        raise AssertionError("removeitem's left must be re-read from the bags after the removal")
    if re.search(r'left\s*=\s*\w+\s*-', fn):
        raise AssertionError("removeitem's left must not be computed by subtraction")
    if "target::RemoveAmount(" not in fn:
        raise AssertionError("removeitem must cap the amount with target::RemoveAmount (never equipped items)")
    # the id / count words are digits only
    pid = kit[kit.index("bool ParseId("):]
    pid = pid[:pid.index("\n}\n")]
    if "target::ParseIdWord(" not in pid or "strtoul" in pid:
        raise AssertionError("ParseId must be the digits-only target::ParseIdWord (strtoul took -1 and +3)")
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
            check(SCRIPTS, H3_HDR, H3_OFF, KIT.replace("bot->DestroyItemCount(it, remaining, true)", "bot->DestroyItemCount(item, remaining, true)"), DOCS)
        with self.assertRaises(AssertionError):
            check(SCRIPTS, H3_HDR, H3_OFF, KIT.replace("bot->DestroyItemCount(it, remaining, true)", "bot->DestroyItem(it)"), DOCS)
        with self.assertRaises(AssertionError):
            check(SCRIPTS, H3_HDR, H3_OFF, KIT.replace("uint32 const left = total(collect(false));", "uint32 const left = want - removed;"), DOCS)
        with self.assertRaises(AssertionError):
            check(SCRIPTS, H3_HDR, H3_OFF, KIT.replace("target::IsBagStack(", "target::IsAnyStack("), DOCS)
        with self.assertRaises(AssertionError):
            check(SCRIPTS, H3_HDR, H3_OFF, KIT.replace("it->GetEntry() == item && target::IsBagStack(", "target::IsBagStack("), DOCS)
        with self.assertRaises(AssertionError):
            check(SCRIPTS, H3_HDR, H3_OFF, KIT.replace("target::IsBagStack(it->GetBagSlot(), it->GetSlot(), ", "target::IsBagStack(it->GetSlot(), it->GetBagSlot(), "), DOCS)
        with self.assertRaises(AssertionError):
            check(SCRIPTS, H3_HDR, H3_OFF, KIT.replace("backpackSlots, removable && it->IsInTrade())", "backpackSlots, false)"), DOCS)
        with self.assertRaises(AssertionError):
            check(SCRIPTS, H3_HDR, H3_OFF, KIT.replace("bot->ForEachItem(ItemSearchLocation::ReagentBank, visit);", "bot->ForEachItem(ItemSearchLocation::Bank, visit);"), DOCS)
        with self.assertRaises(AssertionError):
            check(SCRIPTS, H3_HDR, H3_OFF, KIT.replace("return target::ParseIdWord(word, out);", "return std::strtoul(word.c_str(), nullptr, 10) != 0;"), DOCS)
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

"""Wiring pins for W6a package E6 (auto-equip upgrades, proficiency check, push poll, D4 free-bot gate).

Run:
  python3 -m unittest forever/tools/test_w6a_equip_wiring.py
  python3 forever/tools/test_w6a_equip_wiring.py --mutate   # must FAIL (exit non-zero)
"""
import pathlib
import re
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
F_HEADER = (ROOT / "forever/src/compat/TbCompat_W6.h").read_text()
T_HEADER = (ROOT / "ai/cmangos-compat-w6.h").read_text()
EQUIP = (ROOT / "ai/playerbot/strategy/actions/EquipAction.cpp").read_text()
ITEM_USAGE = (ROOT / "ai/playerbot/strategy/values/ItemUsageValue.cpp").read_text()
S9_HEADER = (ROOT / "forever/src/compat/TbCompat_S9.h").read_text()
SLICE9 = (ROOT / "forever/SLICE9.txt").read_text()
GEN = (ROOT / "forever/tools/slice_gen_context.py").read_text()

HELPER = re.compile(r"^(?:template\s*<[^>]*>\s*)?inline\s+(?:[\w:<>,\s\*&]+?)\s+(\w+Compat)\s*\(", re.M)


def names(text):
    return set(HELPER.findall(text))


def body(text, name):
    i = re.search(r"inline[^;{]*\b%s\s*\(" % re.escape(name), text).start()
    j = text.index("{", i)
    depth = 0
    for k in range(j, len(text)):
        if text[k] == "{":
            depth += 1
        elif text[k] == "}":
            depth -= 1
            if depth == 0:
                return text[i:k + 1]
    raise AssertionError(name)


def check_d4_gate(equip_src, f_header_src):
    exec_fn = equip_src[equip_src.index("bool EquipUpgradesAction::Execute("):]
    exec_fn = exec_fn[:exec_fn.index("\n}\n")]
    if "if (!MayAutoEquipUpgradesCompat(bot))" not in exec_fn:
        raise AssertionError("EquipUpgradesAction::Execute missing MayAutoEquipUpgradesCompat(bot) gate")
    gate = body(f_header_src, "MayAutoEquipUpgradesCompat")
    if "IsInCommandedGroup(bot)" not in gate:
        raise AssertionError("MayAutoEquipUpgradesCompat missing IsInCommandedGroup(bot) check")


class E6Wiring(unittest.TestCase):
    def test_both_headers_define_the_same_helpers(self):
        self.assertEqual(names(F_HEADER), names(T_HEADER))
        self.assertEqual(
            names(F_HEADER),
            {"AutoEquipCompat", "CanUseItemForEquipCompat", "MayAutoEquipUpgradesCompat"},
        )

    def test_the_shims_include_both_headers(self):
        self.assertIn('#include "TbCompat_W6.h"', (ROOT / "forever/src/compat/forever-compat-shim.h").read_text())
        self.assertIn('#include "cmangos-compat-w6.h"', (ROOT / "ai/cmangos-compat-shim.h").read_text())

    def test_turtle_bodies_are_original_expressions(self):
        self.assertIn("HandleAutoEquipItemOpcode(packet)", body(T_HEADER, "AutoEquipCompat"))
        self.assertIn("return bot->CanUseItem(proto);", body(T_HEADER, "CanUseItemForEquipCompat"))
        self.assertIn("return true;", body(T_HEADER, "MayAutoEquipUpgradesCompat"))

    def test_forever_auto_equip_uses_typed_packet(self):
        fn = body(F_HEADER, "AutoEquipCompat")
        self.assertIn("WorldPackets::Item::AutoEquipItem", fn)
        self.assertIn("Inv.Items.push_back", fn)
        self.assertIn("HandleAutoEquipItemOpcode(packet)", fn)
        self.assertIn("AutoEquipCompat(bot, bagIndex, slot);", EQUIP)

    def test_forever_proficiency_check_uses_template_skill(self):
        fn = body(F_HEADER, "CanUseItemForEquipCompat")
        self.assertIn("tpl->GetSkill()", fn)
        self.assertIn("GetSkillValue(itemSkill) == 0", fn)
        self.assertIn("EQUIP_ERR_PROFICIENCY_NEEDED", fn)
        self.assertIn("CanUseItemForEquipCompat(bot, itemProto)", ITEM_USAGE)

    def test_d4_auto_equip_only_for_free_bots(self):
        check_d4_gate(EQUIP, F_HEADER)

    def test_d4_mutation_fails(self):
        mutated_equip = EQUIP.replace("if (!MayAutoEquipUpgradesCompat(bot))", "if (false)")
        with self.assertRaises(AssertionError):
            check_d4_gate(mutated_equip, F_HEADER)
        mutated_header = F_HEADER.replace("!TortoiseBots::BotCommands::IsInCommandedGroup(bot)", "true")
        with self.assertRaises(AssertionError):
            check_d4_gate(EQUIP, mutated_header)

    def test_push_poll_and_slice_registration(self):
        poll = body(S9_HEADER, "PollPacketEventCompat")
        self.assertIn('"item push result"', poll)
        self.assertIn("PollItemPush", poll)
        listed = [l.strip() for l in SLICE9.splitlines() if l.strip() and not l.startswith("#")]
        self.assertIn("ai/playerbot/strategy/actions/EquipAction.cpp", listed)
        self.assertIn("ai/playerbot/strategy/generic/MaintenanceStrategy.cpp", listed)
        for key in ("maintenance", "equip upgrades", "equipment audit", "item push result"):
            self.assertIn(key, GEN)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        # Run D4 check against mutated source so --mutate exits non-zero as a smoke pin.
        check_d4_gate(EQUIP.replace("if (!MayAutoEquipUpgradesCompat(bot))", "if (false)"), F_HEADER)
        sys.exit(0)
    unittest.main()

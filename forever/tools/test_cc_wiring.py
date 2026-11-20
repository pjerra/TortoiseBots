#!/usr/bin/env python3
"""Wiring pins for issue #12 (crowd-control permissions, `.tb cc ... allow|deny|list`).

The decision logic is pure (ai/playerbot/policy/combat/CcPermissionPolicy.h);
the brain refuses a denied spell in its cast check; the host keeps the
memory-only per-(commander account, class) store and pushes each bot's mask.
No new engine keys, no slice block, no compat pair, no TbScripts row: the
permission path extends the existing `.tb cc` command.

Run:
  python3 -m unittest forever/tools/test_cc_wiring.py
  python3 forever/tools/test_cc_wiring.py --mutate   # must FAIL (exit non-zero)
"""
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
POLICY = (ROOT / "ai/playerbot/policy/combat/CcPermissionPolicy.h").read_text()
AI_H = (ROOT / "ai/playerbot/PlayerbotAI.h").read_text()
SPELLS = (ROOT / "ai/playerbot/PlayerbotAISpells.cpp").read_text()
H_CC = (ROOT / "forever/src/host/TbCc.h").read_text()
CC = (ROOT / "forever/src/host/TbCc.cpp").read_text()
BRAIN = (ROOT / "forever/src/host/TbEngineBrain.cpp").read_text()
GEN = (ROOT / "forever/tools/slice_gen_context.py").read_text()
STUBS = (ROOT / "forever/src/slice/SliceLinkStubs.cpp").read_text()
SCRIPTS = (ROOT / "forever/src/TbScripts.cpp").read_text()
PURE_TEST = (ROOT / "forever/tools/test_cc_permissions.cpp").read_text()


def check_policy(header):
    for kind in ("kFear", "kSheep", "kBanish", "kShackle", "kSap", "kSleep", "kTrap", "kCharm", "kAll", "kDefaultDenied"):
        if kind not in header:
            raise AssertionError("CcPermissionPolicy.h misses " + kind)
    for spell in ('"death coil"', '"psychic scream"', '"intimidating shout"', '"scare beast"',
                  '"turn undead"', '"shackle undead"', '"hibernate"', '"wyvern sting"',
                  '"freezing trap"', '"seduction"', '"enslave demon"', '"mind control"',
                  '"polymorph: pig"', '"polymorph: turtle"'):
        if spell not in header:
            raise AssertionError("CcPermissionPolicy.h spell table misses " + spell)
    for negative in ('"kick"', '"hamstring"', '"entangling roots"', '"razuvious mind control"'):
        if negative in header:
            raise AssertionError("CcPermissionPolicy.h maps a non-CC spell " + negative)
    if "kDefaultDenied = kFear" not in header:
        raise AssertionError("the default is not fear-denied")
    if "class Store" not in header or "DeniedFor(" not in header or "Version()" not in header:
        raise AssertionError("the store misses DeniedFor/Version")
    if "kAnyClass" not in header:
        raise AssertionError("the store misses the all-classes entry")


def check_brain_gate(ai_h, spells):
    if "m_ccDeniedMask" not in ai_h:
        raise AssertionError("PlayerbotAI.h misses m_ccDeniedMask")
    if "SetCcDeniedMask" not in ai_h or "GetCcDeniedMask" not in ai_h:
        raise AssertionError("PlayerbotAI.h misses the mask accessors")
    if "CcPermissionPolicy.h" not in spells:
        raise AssertionError("PlayerbotAISpells.cpp does not include the pure policy")
    string_fn = spells[spells.index("bool PlayerbotAI::CanCastSpell(std::string name"):]
    string_fn = string_fn[:string_fn.index("\n}\n")]
    if "KindFromSpellName(name)" not in string_fn or "CcKindDenied" not in string_fn:
        raise AssertionError("the string CanCastSpell misses the denied-kind gate")
    check_fn = spells[spells.index("bool PlayerbotAI::CheckCanCastSpell("):]
    check_fn = check_fn[:check_fn.index("\n}\n")]
    if "GetCcDeniedMask() != 0" not in check_fn:
        raise AssertionError("CheckCanCastSpell misses the mask early-out")
    if "LookupSpellInfo(spellid)" not in check_fn or "CcKindDenied" not in check_fn:
        raise AssertionError("CheckCanCastSpell misses the spell-name gate")
    # the gate runs before the pet bypass (a succubus casts Seduction from its own spellbook)
    if check_fn.index("GetCcDeniedMask()") > check_fn.index("pet->HasSpell(spellid)"):
        raise AssertionError("the gate must run before the pet bypass")


class CcWiring(unittest.TestCase):
    def test_pure_policy(self):
        check_policy(POLICY)

    def test_brain_gate(self):
        check_brain_gate(AI_H, SPELLS)

    def test_host_store_and_command(self):
        for decl in ("CcDeniedForOwner(", "CcDeniedVersion()", "CcOwnerOf(", "CcSetDenied(", "CcSyncBot("):
            self.assertIn(decl, H_CC)
        self.assertIn("CcPermissionStore()", CC)
        self.assertIn("kDefaultDenied", CC)
        self.assertIn("GetAccountId()", CC)   # owner = the commander's account, no database
        self.assertIn("CcPermOpAt(args, permPos, permClass, permOp)", CC)
        self.assertIn("HandleCcPermissions(handler, args)", CC)
        self.assertIn('"perms"', CC)
        self.assertIn('S("op", op)', CC)
        self.assertIn("verb\", \"perms", CC)
        self.assertIn('op = "list";', CC)   # a bare class word lists that class
        # the OFF build keeps its one-line engine-not-built answer and stubs the store
        off = CC[CC.index("#else   // not built with the slice"):]
        self.assertIn("NotBuilt(handler, args, \"cc\")", off)
        self.assertIn("void CcSyncBot(Player*) {}", off)

    def test_engine_push(self):
        self.assertIn("h8::CcSyncBot(bot);", BRAIN)
        self.assertIn("h8::CcDeniedVersion() != 0", BRAIN)
        self.assertIn('#include "host/TbCc.h"', BRAIN)

    def test_selftest_pins_perms(self):
        selftest = CC[CC.index("void SelfTestRecords("):]
        selftest = selftest[:selftest.index("\n}\n")]
        self.assertIn('"perms"', selftest)
        self.assertIn('"denied"', selftest)

    def test_no_new_engine_keys(self):
        # the gate adds no context key, so no slice key list, stub block or compat pair names it
        self.assertNotIn("CcPermission", GEN)
        self.assertNotIn("CcPermission", STUBS)
        self.assertNotIn("CcDenied", GEN)
        self.assertNotIn("CcDenied", STUBS)
        # no TbScripts row: the permission path extends the existing `cc` command
        self.assertEqual(SCRIPTS.count('{ "cc",'), 1)

    def test_pure_test_exists(self):
        self.assertIn("CcPermissionPolicy.h", PURE_TEST)
        self.assertIn("DeniedFor(", PURE_TEST)
        self.assertIn("Version()", PURE_TEST)

    def test_mutation_fails(self):
        mutated_spells = SPELLS.replace("if (GetCcDeniedMask() != 0 && spellid)", "if (false)")
        with self.assertRaises(AssertionError):
            check_brain_gate(AI_H, mutated_spells)
        mutated_header = POLICY.replace('{"death coil", kFear},', "")
        with self.assertRaises(AssertionError):
            check_policy(mutated_header)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        # Run the brain-gate check against a mutated source so --mutate exits non-zero as a smoke pin.
        check_brain_gate(AI_H, SPELLS.replace("if (GetCcDeniedMask() != 0 && spellid)", "if (false)"))
        sys.exit(0)
    unittest.main()

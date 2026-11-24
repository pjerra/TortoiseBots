#!/usr/bin/env python3
"""Wiring pins for the spell-school switch (issue #48 round 1): the config order of a reload, the conf wording and the
policy's gain rule.

    python3 forever/tools/test_spell_school_wiring.py
    python3 forever/tools/test_spell_school_wiring.py --mutate   # apply each pin's mutation; exit 0 only when all are caught
"""
import sys
import unittest
from pathlib import Path

FOREVER = Path(__file__).resolve().parent.parent
ROOT = FOREVER.parent


def sources():
    return {
        "scripts": (FOREVER / "src" / "TbScripts.cpp").read_text(),
        "conf": (FOREVER / "conf" / "tortoisebots.conf.dist").read_text(),
        "policy": (ROOT / "ai/playerbot/policy/combat/SpellSchoolChoicePolicy.h").read_text(),
    }


def member_body(text, signature):
    start = text.index(signature)
    end = text.index("\n    void ", start + len(signature))
    return text[start:end]


def check_config_before_build(s):
    # A reload that turns the switch from 0 to 1 must resolve the spell names with the new value: BuildIndex asks
    # Enabled() and only then fills every school's spell id, so LoadConfig has to run first.
    body = member_body(s["scripts"], "void OnConfigLoad(bool reload) override")
    build_at = body.index("TortoiseBots::school::BuildIndex();")
    assert "TortoiseBots::school::LoadConfig();" in body[:build_at], \
        "a reload builds the index from the new switch value: LoadConfig must run before BuildIndex"
    assert "if (reload)" in body[:build_at], "the spell-school index is built in the reload branch"


def check_conf_says_points(s):
    conf = s["conf"]
    start = conf.index("#    TortoiseBots.SpellSchool.VulnerableMinPct")
    block = conf[start:conf.index("TortoiseBots.SpellSchool.VulnerableMinPct =", start)]
    assert "percentage points above the main school's taken percent" in block, \
        "the conf description must say the gain is added to the main school's percent, not a share of it"
    assert "in percent of the main school" not in block, "the relative wording describes a rule the code does not use"


def check_policy_adds_points(s):
    # The conf text above is only true while the rule adds the gain to the main school's own taken percent.
    assert "taken < mainTaken + minGainPct" in s["policy"], "PickSchool must add the gain as percentage points"


MUTATIONS = {
    # An early BuildIndex, before the config load, is the bug this pin is about.
    "config_before_build": ("scripts", "        BotMgr::Instance().LoadConfig();\n",
                            "        BotMgr::Instance().LoadConfig();\n        TortoiseBots::school::BuildIndex();\n"),
    "conf_says_points": ("conf", "percentage points above the main school's taken percent",
                         "in percent of the main school's own taken percent"),
    "policy_adds_points": ("policy", "mainTaken + minGainPct", "mainTaken * (100 + minGainPct) / 100"),
}

CHECKS = {
    "config_before_build": check_config_before_build,
    "conf_says_points": check_conf_says_points,
    "policy_adds_points": check_policy_adds_points,
}


class SpellSchoolWiring(unittest.TestCase):
    def test_config_before_build(self):
        check_config_before_build(sources())

    def test_conf_says_points(self):
        check_conf_says_points(sources())

    def test_policy_adds_points(self):
        check_policy_adds_points(sources())


def mutate():
    caught = 0
    for name, (key, old, new) in MUTATIONS.items():
        mutated = sources()
        if old not in mutated[key]:
            print("mutation %s: no longer applies" % name, file=sys.stderr)
            return 1
        mutated[key] = mutated[key].replace(old, new, 1)
        try:
            CHECKS[name](mutated)
        except (AssertionError, ValueError):
            caught += 1
            print("mutation %s: caught" % name)
        else:
            print("mutation %s: SURVIVED" % name, file=sys.stderr)
    print("%d/%d mutations caught" % (caught, len(MUTATIONS)))
    return 0 if caught == len(MUTATIONS) else 1


if __name__ == "__main__":
    if "-h" in sys.argv or "--help" in sys.argv:
        print("usage: test_spell_school_wiring.py [--mutate] [unittest options]\n"
              "  --mutate  apply each source mutation and require its check to fail (exit 0 when all are caught)\n")
    if "--mutate" in sys.argv:
        sys.exit(mutate())
    unittest.main()

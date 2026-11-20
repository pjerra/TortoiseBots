"""Wiring pins for slice 10's fix round (H10 review): the role bit is not a tank kit, and the bear's pre-pull shift falls back to Bear Form.

The role policy itself (which class may take the tank order, which may lead a run) is tested standalone by
forever/tools/test_slice10_roles.cpp. These pins prove, by reading the sources, that the two call sites use it and where:

  1. `.tb role ... tank` refuses a class without a tank kit BEFORE it stores the role or queues the order (TbOrders.cpp, HandleRole);
  2. the run's start check tests the class and the live "pull" strategy after the tank-role test (TbRunOrders.cpp, StartCheck);
  3. PullStrategy::GetPreActionName falls back from "dire bear form" to "bear form" when the bot does not know Dire Bear Form (below 40),
     after the faerie-fire clear, so a command pull by a level-18 bear shifts before walking in;
  4. SliceAiFactory.cpp static_asserts the policy's class numbers against the core's constants.

Run: python3 -m unittest forever/tools/test_slice10_fix_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
ORDERS = (ROOT / "forever/src/host/TbOrders.cpp").read_text()
RUN_ORDERS = (ROOT / "forever/src/host/run/TbRunOrders.cpp").read_text()
PULL = (ROOT / "ai/playerbot/strategy/generic/PullStrategy.cpp").read_text()
FACTORY = (ROOT / "forever/src/slice/SliceAiFactory.cpp").read_text()
POLICY_PATH = ROOT / "forever/src/slice/SliceHybridPolicy.h"


def region(text, start, end):
    begin = text.index(start)
    return text[begin:text.index(end, begin)]


class RoleCommand(unittest.TestCase):
    def setUp(self):
        # The ON copy of HandleRole (the file has an OFF stub after it).
        self.body = region(ORDERS, "bool HandleRole(ChatHandler* handler", "// ---- `.tb master")

    def test_refuses_before_storing(self):
        self.assertIn("CanTakeTankOrder", self.body)
        self.assertIn("RoleTank", self.body)
        refusal = self.body.index("CanTakeTankOrder")
        self.assertLess(refusal, self.body.index("SetPlayerForcedRole"), "the refusal must come before the role is stored")
        self.assertLess(refusal, self.body.index('order.verb = "role"'), "the refusal must come before the order is queued")
        self.assertIn('"no-tank-kit"', self.body)
        self.assertIn('"refused"', self.body)


class RunAdmission(unittest.TestCase):
    def setUp(self):
        self.body = region(RUN_ORDERS, "bool StartCheck(ObjectGuid leader", "    for (Group::MemberSlot const& slot")

    def test_role_is_not_enough(self):
        self.assertIn("PlayerbotAI::IsTank(lead, true)", self.body)
        role = self.body.index("PlayerbotAI::IsTank(lead, true)")
        self.assertIn("CanLeadRun", self.body)
        self.assertLess(role, self.body.index("CanLeadRun"), "the class check follows the role test")
        self.assertIn('HasStrategy("pull", BotState::BOT_STATE_COMBAT)', self.body)
        self.assertLess(role, self.body.index('HasStrategy("pull"'), "the kit check follows the role test")
        self.assertIn('code = "no-tank"', self.body)


class BearPrePull(unittest.TestCase):
    def setUp(self):
        self.body = region(PULL, "std::string PullStrategy::GetPreActionName() const", "void PullStrategy::InitCombatTriggers")

    def test_falls_back_to_bear_form(self):
        self.assertIn('GetPullActionName() == "faerie fire"', self.body)
        self.assertIn('!ai->HasSpell("dire bear form")', self.body)
        self.assertIn('modPullActionName = "bear form"', self.body)
        # The fallback is the else of the faerie-fire clear: a faerie-fire pull still runs in caster form.
        clear = self.body.index("modPullActionName.clear()")
        fallback = self.body.index('modPullActionName = "bear form"')
        self.assertLess(clear, fallback)
        self.assertIn("else if", self.body[clear:fallback])


class ClassIds(unittest.TestCase):
    def test_factory_asserts_the_policy_ids(self):
        policy_text = POLICY_PATH.read_text() if POLICY_PATH.exists() else ""
        for policy, core in (("ClassWarrior", "CLASS_WARRIOR"), ("ClassPaladin", "CLASS_PALADIN"),
                             ("ClassShaman", "CLASS_SHAMAN"), ("ClassDruid", "CLASS_DRUID")):
            self.assertIn(policy, policy_text)
            self.assertRegex(FACTORY, r"static_assert\(unsigned\(TortoiseBots::slice::hybrid::%s\) == unsigned\(%s\)" % (policy, core))


if __name__ == "__main__":
    unittest.main()

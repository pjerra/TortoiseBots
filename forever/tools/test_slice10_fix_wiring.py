"""Wiring pins for slice 10's fix round (H10 review): the role bit is not a tank kit, and the bear's pre-pull shift falls back to Bear Form.

The role policy itself (which class may take the tank order, which may lead a run) is tested standalone by
forever/tools/test_slice10_roles.cpp. These pins prove, by reading the sources, that the two call sites use it and where:

  1. `.tb role ... tank` refuses a class without a tank kit BEFORE it stores the role or queues the order (TbOrders.cpp, HandleRole),
     and the refusal clears a tank bit already stored on the bot (the player-wide role and the AI's own), so a shaman cannot keep it;
  2. the run's start check tests the class and the live "pull" strategy after the tank-role test (TbRunOrders.cpp, StartCheck);
  3. PullStrategy::GetPreActionName falls back from "dire bear form" to "bear form" when the bot does not know Dire Bear Form (below 40),
     after the faerie-fire clear, so a command pull by a level-18 bear shifts before walking in;
  4. SliceAiFactory.cpp static_asserts the policy's class numbers against the core's constants;
  5. every run and party read of a member's tank-ness is capability-aware (`run::IsTankMember`, TbRunFacts.cpp), never the role bit alone.

Run: python3 -m unittest forever/tools/test_slice10_fix_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
ORDERS = (ROOT / "forever/src/host/TbOrders.cpp").read_text()
RUN_ORDERS = (ROOT / "forever/src/host/run/TbRunOrders.cpp").read_text()
FACTS = (ROOT / "forever/src/host/run/TbRunFacts.cpp").read_text()
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
        self.assertIn("PlanTankOrder", self.body)
        self.assertIn("RoleTank", self.body)
        refusal = self.body.index("plan.refuse")
        self.assertLess(refusal, self.body.index("SetPlayerForcedRole", refusal), "the refusal must come before the role is stored")
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


class RunStartTankRole(unittest.TestCase):
    """`.tb run start <tank>` commits the named tank's role, then logs which bot the AI treats as tank."""

    def setUp(self):
        self.run = (ROOT / "forever/src/host/run/TbRun.cpp").read_text()

    def test_bot_led_start_commits_the_role_before_the_check(self):
        body = region(self.run, "StartOutcome StartRun(", "StartOutcome StartPlayerLedRun(")
        self.assertIn("PrepareRunTank(leader)", body)
        self.assertLess(body.index("PrepareRunTank(leader)"), body.index("StartCheck("),
                        "the admission check reads the forced role first: commit it before")
        self.assertIn("LogRunTank(leader", body)

    def test_player_led_start_commits_the_role_before_the_check(self):
        body = self.run[self.run.index("StartOutcome StartPlayerLedRun("):]
        self.assertIn("PrepareRunTank(tank)", body)
        self.assertLess(body.index("PrepareRunTank(tank)"), body.index("PlayerLedStartCheck("),
                        "the admission check reads the forced role first: commit it before")
        self.assertIn("LogRunTank(tank", body)

    def test_only_classes_with_a_run_tank_are_ordered(self):
        body = region(self.run, "bool PrepareRunTank(", "void LogRunTank(")
        self.assertIn("CanLeadRun", body)
        self.assertIn("SetBotRole", body)
        self.assertIn("BOT_ROLE_TANK", body)
        log = region(self.run, "void LogRunTank(", "RunRec* Find(")
        self.assertIn("PlayerbotAI::IsTank", log)

    def test_bot_led_refusals_precede_the_role_commit(self):
        body = region(self.run, "StartOutcome StartRun(", "StartOutcome StartPlayerLedRun(")
        prepare = body.index("PrepareRunTank(leader)")
        for refusal in ('refuse("not-leader"', 'refuse("group-raid"', 'refuse("already-running"', 'refuse("wrong-map"',
                        'refuse("player-in-group"', 'refuse("bags"', 'refuse("not-commanded"', 'refuse("loot-method"', 'refuse("no-step"'):
            self.assertLess(body.index(refusal), prepare, refusal + " reads only the world: it goes before the role commit")
        # the one check that reads the committed role still refuses safely: it puts the old role back
        self.assertIn("RestoreRunTank(leader, previousRole, tankRoleSet)", body)

    def test_player_led_refusals_precede_the_role_commit(self):
        body = self.run[self.run.index("StartOutcome StartPlayerLedRun("):]
        prepare = body.index("PrepareRunTank(tank)")
        for refusal in ('refuse("already-running"', 'refuse("wrong-map"', 'refuse("bags"', 'refuse("loot-method"'):
            self.assertLess(body.index(refusal), prepare, refusal + " reads only the world: it goes before the role commit")
        self.assertIn("RestoreRunTank(tank, previousRole, tankRoleSet)", body)


class StaleTankRole(unittest.TestCase):
    """A refusal must leave no tank bit behind, and every run read of a member's tank-ness must be capability-aware."""

    def setUp(self):
        self.body = region(ORDERS, "bool HandleRole(ChatHandler* handler", "// ---- `.tb master")

    def test_refusal_clears_stored_and_ai_role(self):
        self.assertIn("PlanTankOrder", self.body)
        refusal = self.body.index("plan.refuse")
        end = self.body.index("if (!ai)", refusal)
        self.assertIn("plan.clearStored", self.body[refusal:end])
        self.assertIn("plan.clearAi", self.body[refusal:end])
        self.assertLess(self.body.index("plan.clearStored", refusal), end,
                        "the stored tank bit is cleared in the refusal branch")
        self.assertLess(self.body.index("plan.clearAi", refusal), end,
                        "the AI tank bit is cleared in the refusal branch")
        self.assertIn("Roles().erase", self.body[refusal:end], "the tick's role map is cleared too")
        self.assertIn('"cleared"', self.body[refusal:end], "the reply says the tank bit was cleared")

    def test_run_reads_are_capability_aware(self):
        # One raw IsTank read, the helper's; every member/lead read goes through it.
        self.assertIn("bool IsTankMember(Player* bot)", FACTS)
        self.assertEqual(FACTS.count("PlayerbotAI::IsTank("), 1)
        self.assertGreaterEqual(FACTS.count("IsTankMember("), 6)      # the definition and five reads
        self.assertIn("IsTankMember(bot)", RUN_ORDERS)
        self.assertNotIn("PlayerbotAI::IsTank(bot)", RUN_ORDERS)
        self.assertIn("IsTankMember(member)", ORDERS)
        self.assertNotIn("PlayerbotAI::IsTank(member, true)", ORDERS)
        self.assertIn("IsTankMember(bot)", ORDERS)
        self.assertNotIn("PlayerbotAI::IsTank(bot, true)", ORDERS)


if __name__ == "__main__":
    unittest.main()

"""Hired companions keep their group and their hirer (issue #1014).

LeaveFarAwayAction refuses for a hire or a bot in a commanded group
(ai/playerbot/GroupHygienePolicy.h FarAwayLeaveAllowed). PlayerbotAI's
group-master re-adoption asks runtime/HireMasterPolicy.h before it changes a
master, and a hire whose hirer is back in the world gets that hirer back as
its live master. The decisions are tested in tools/test_hire_master_policy.cpp
and tools/test_group_hygiene_policy.cpp; this checks the call sites use them.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
LEAVE = (ROOT / "ai/playerbot/strategy/actions/LeaveGroupAction.cpp").read_text()
AI = (ROOT / "ai/playerbot/PlayerbotAI.cpp").read_text()


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class HireNeverLeavesWiringTest(unittest.TestCase):
    def test_leave_far_away_refuses_hire_and_commanded_group(self):
        fn = body(LEAVE, "bool LeaveFarAwayAction::isUseful()", "\n    }\n}")
        guard = fn.index("ai::FarAwayLeaveAllowed(")
        self.assertIn("HireLifecycle::Instance().IsHired(bot->GetObjectGuid())", fn[guard:guard + 300])
        self.assertIn("BotCommands::IsInCommandedGroup(bot)", fn[guard:guard + 300])
        # Before anything can return true.
        self.assertLess(guard, fn.index("return true;"))

    def test_adoption_asks_the_hire_policy(self):
        fn = body(AI, "bool const dungeonCrew =", "// fix bots in BG not having proper strategies")
        self.assertIn("ObjectAccessor::FindPlayerNotInWorld(hirerGuid)", fn)
        self.assertIn("bool const hirerBetweenMaps = hirer && !hirer->IsInWorld();", fn)
        self.assertIn(
            "TortoiseBots::HireAllowsMasterAdoption(hired, hirerBetweenMaps, IsRealPlayer(newMaster))", fn)
        # The guard sits on the adoption itself, before any BindBotMaster.
        adopt = fn.index("HireAllowsMasterAdoption(")
        self.assertLess(adopt, fn.index("BindBotMaster(\n                    bot->GetObjectGuid(), newMaster->GetObjectGuid())"))

    def test_hirer_restored_before_the_master_strip(self):
        fn = body(AI, "bool const dungeonCrew =", "// fix bots in BG not having proper strategies")
        restore = fn.index("TortoiseBots::HireRestoresHirer(")
        self.assertIn("record->masterGuid == hirerGuid", fn[:restore])
        self.assertIn("masterGuid == hirerGuid", fn[:restore])
        self.assertLess(restore, fn.index("//Remove bot masters not in our group."))


if __name__ == "__main__":
    unittest.main()

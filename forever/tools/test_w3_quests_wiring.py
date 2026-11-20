"""Wiring pins for W3 (quests: compat pair, vanilla filter, reward choice, .tb quest command, no GM .quest shortcuts).

Run:
  python3 -m unittest forever/tools/test_w3_quests_wiring.py
  python3 forever/tools/test_w3_quests_wiring.py --mutate   # must FAIL (exit non-zero)
"""
import pathlib
import re
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
F_HEADER = (ROOT / "forever/src/compat/TbCompat_Quest.h").read_text()
T_HEADER = (ROOT / "ai/cmangos-compat-quest.h").read_text()
ACCEPT = (ROOT / "ai/playerbot/strategy/actions/AcceptQuestAction.cpp").read_text()
TALK = (ROOT / "ai/playerbot/strategy/actions/TalkToQuestGiverAction.cpp").read_text()
QUEST_ACT = (ROOT / "ai/playerbot/strategy/actions/QuestAction.cpp").read_text()
DROP = (ROOT / "ai/playerbot/strategy/actions/DropQuestAction.cpp").read_text()
SHARE = (ROOT / "ai/playerbot/strategy/actions/ShareQuestAction.cpp").read_text()
HOST_QUEST = (ROOT / "forever/src/host/TbHostQuest.cpp").read_text()
SCRIPTS = (ROOT / "forever/src/TbScripts.cpp").read_text()
H3_OFF = (ROOT / "forever/src/host/TbH3Off.cpp").read_text()
S9_HEADER = (ROOT / "forever/src/compat/TbCompat_S9.h").read_text()
SLICE9 = (ROOT / "forever/SLICE9.txt").read_text()
GEN = (ROOT / "forever/tools/slice_gen_context.py").read_text()

HELPER = re.compile(r"^(?:template\s*<[^>]*>\s*)?inline\s+(?:[\w:<>,\s\*&]+?)\s+(\w+Compat)\s*\(", re.M)


def names(text):
    return set(HELPER.findall(text))


def check_no_gm_quest_shortcut(host_src, accept_src):
    for label, src in (("TbHostQuest.cpp", host_src), ("AcceptQuestAction.cpp", accept_src)):
        if ".quest add" in src or ".quest complete" in src or "HandleQuestAdd" in src or "HandleQuestComplete" in src:
            raise AssertionError(f"{label} must not use GM .quest shortcuts")
    if "QuestIsVanillaEligibleCompat(" not in accept_src:
        raise AssertionError("AcceptQuestAction.cpp missing QuestIsVanillaEligibleCompat gate")


class W3QuestsWiring(unittest.TestCase):
    def test_both_headers_define_the_same_helpers(self):
        self.assertEqual(names(F_HEADER), names(T_HEADER))
        self.assertLessEqual(
            {
                "QuestTitleCompat",
                "RemoveQuestFromLogCompat",
                "QuestRequiredItemsCompat",
                "AcceptQuestDirectCompat",
                "RewardQuestDirectCompat",
                "PushQuestToPartyCompat",
                "QuestIsVanillaEligibleCompat",
                "AcceptQuestShareDirectCompat",
            },
            names(F_HEADER),
        )

    def test_the_shims_include_both_headers(self):
        self.assertIn('#include "TbCompat_Quest.h"', (ROOT / "forever/src/compat/forever-compat-shim.h").read_text())
        self.assertIn('#include "cmangos-compat-quest.h"', (ROOT / "ai/cmangos-compat-shim.h").read_text())

    def test_vanilla_filter_and_no_gm_shortcuts(self):
        check_no_gm_quest_shortcut(HOST_QUEST, ACCEPT)
        self.assertIn("IsVanillaOrCustomQuest", F_HEADER)
        self.assertIn("QuestIsVanillaEligibleCompat(q)", HOST_QUEST)

    def test_reward_choice_uses_item_usage_and_direct_reward(self):
        self.assertIn("BestRewards(quest)", TALK)
        self.assertIn("sRandomItemMgr.ItemStatWeight", TALK)
        self.assertIn("RewardQuestDirectCompat", TALK)

    def test_quest_share_poll_and_command_wiring(self):
        self.assertIn('"quest share"', S9_HEADER)
        self.assertIn("PollQuestShareCompat", S9_HEADER)
        self.assertIn("TortoiseBots::h3::HandleQuest", SCRIPTS)
        self.assertIn('NotBuilt(handler, args, "quest")', H3_OFF)

    def test_slice_list_and_context_keys(self):
        listed = [l.strip() for l in SLICE9.splitlines() if l.strip() and not l.startswith("#")]
        for f in (
            "ai/playerbot/strategy/generic/QuestStrategies.cpp",
            "ai/playerbot/strategy/actions/QuestAction.cpp",
            "ai/playerbot/strategy/actions/AcceptQuestAction.cpp",
            "ai/playerbot/strategy/actions/TalkToQuestGiverAction.cpp",
            "ai/playerbot/strategy/actions/DropQuestAction.cpp",
            "ai/playerbot/strategy/actions/ShareQuestAction.cpp",
            "ai/playerbot/strategy/actions/QuestRewardActions.cpp",
        ):
            self.assertIn(f, listed)
        for key in ("quest", "accept all quests", "talk to quest giver", "accept quest", "accept quest share", "quest share", "quest reward"):
            self.assertIn(key, GEN)

    def test_mutation_fails(self):
        with self.assertRaises(AssertionError):
            check_no_gm_quest_shortcut(HOST_QUEST + "\n// .quest add", ACCEPT)
        with self.assertRaises(AssertionError):
            check_no_gm_quest_shortcut(HOST_QUEST, ACCEPT.replace("QuestIsVanillaEligibleCompat", "true || "))


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        check_no_gm_quest_shortcut(HOST_QUEST + "\n// .quest add", ACCEPT)
        sys.exit(0)
    unittest.main()

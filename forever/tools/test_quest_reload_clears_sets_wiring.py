#!/usr/bin/env python3
"""Wiring pin for the quest-reload hook (TbScripts.cpp OnAfterReload): a quest reload while bots are disabled drops the sets.

Rebuilding only when enabled left stale eligibility sets behind: a re-enable read pre-reload quest rows. Now every quest
reload clears the sets when the module is off or the engine is not built (the next eligibility check rebuilds them
lazily), and rebuilds eagerly when both are up.
"""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCRIPTS = (ROOT / "src" / "TbScripts.cpp").read_text()
H3_H = (ROOT / "src" / "host" / "TbH3.h").read_text()
HOST_QUEST = (ROOT / "src" / "host" / "TbHostQuest.cpp").read_text()
COMPAT = (ROOT / "src" / "compat" / "TbCompat_Quest.h").read_text()


def hook_body():
    start = SCRIPTS.index("void OnAfterReload(std::string_view command) override")
    return SCRIPTS[start:SCRIPTS.index("void OnWorldInitialized() override", start)]


class QuestReloadClearsSets(unittest.TestCase):
    def test_hook_clears_when_off_and_rebuilds_when_on(self):
        body = hook_body()
        quest_branch = body[body.index('command.find("quest")'):]
        quest_branch = quest_branch[:quest_branch.index("if (command != \"reload all\"")]
        self.assertIn("TortoiseBots::h3::RebuildQuestEligibility();", quest_branch)
        self.assertIn("TortoiseBots::h3::ClearQuestEligibility();", quest_branch)
        # the clear is the else of the enabled-and-built check, not gated on it
        rebuild_at = quest_branch.index("RebuildQuestEligibility();")
        clear_at = quest_branch.index("ClearQuestEligibility();")
        enabled_at = quest_branch.index("BotMgr::Instance().Enabled()")
        self.assertLess(enabled_at, rebuild_at)
        self.assertLess(rebuild_at, clear_at)
        self.assertIn("else", quest_branch[rebuild_at:clear_at])

    def test_clear_is_declared_and_defined_through_the_compat_sets(self):
        self.assertIn("void ClearQuestEligibility();", H3_H)
        self.assertIn("void RebuildQuestEligibility();", H3_H)
        self.assertIn("ClearQuestEligibilityCompat();", HOST_QUEST)
        self.assertIn("inline void ClearQuestEligibilityCompat()", COMPAT)

    def test_clear_leaves_the_sets_unbuilt_so_the_next_check_rebuilds(self):
        clear = COMPAT[COMPAT.index("inline void ClearQuestEligibilityCompat()"):COMPAT.index("inline bool QuestIsVanillaEligibleCompat")]
        self.assertIn("= QuestEligibilitySets();", clear)
        check = COMPAT[COMPAT.index("inline bool QuestIsVanillaEligibleCompat"):]
        self.assertIn("if (!sets.built)", check)
        self.assertIn("RebuildQuestEligibilityCompat();", check)


if __name__ == "__main__":
    unittest.main()

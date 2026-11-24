#!/usr/bin/env python3
"""Wiring pins for the quest values (TBF #147, package B).

QuestValues.cpp is the stock value file the travel quest rows and the rpg quest verbs read (dialog status, free log slots,
need quest objective, need for quest, can use item on). It compiles on both cores through four new helpers of the quest
compat pair, so the pins:

  * the source stays on the slice list the Forever build reads;
  * both cores carry the relation walk, the source-item target list and the two reward-state answers;
  * no Turtle-only spelling survived in the body (log-row fields, flat objective arrays, the ObjectMgr subclass, the
    shared-value reads that do not link on ForeverCore).

Run: python3 -m unittest forever/tools/test_w4_quest_values_wiring.py
"""
import os
import re
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import slice_registry_check as chk  # noqa: E402

ROOT = chk.ROOT
LIST = "forever/SLICE-W4.txt"
SOURCE = "ai/playerbot/strategy/values/QuestValues.cpp"
HEADER = "ai/playerbot/strategy/values/QuestValues.h"
QUEST_F = "forever/src/compat/TbCompat_Quest.h"
QUEST_T = "ai/cmangos-compat-quest.h"

HELPERS = (
    "ForEachQuestRelationCompat",
    "QuestRequiredTargetEntriesCompat",
    "QuestEverRewardedCompat",
    "QuestRewardPendingCompat",
    "QuestLogCapacityCompat",
)

# Turtle-only spellings that would not compile (or would read the wrong table) on ForeverCore.
GONE = (
    "QuestObjectMgr",
    "m_rewarded",
    "m_status",
    "m_itemcount",
    "m_creatureOrGOcount",
    "ReqItemId[",
    "ReqItemCount[",
    "ReqCreatureOrGOId[",
    "ReqCreatureOrGOCount[",
    "QUEST_OBJECTIVES_COUNT",
    "GetMinLevel(",
    "GetQuestLevelForPlayer(",
    "IsAutoComplete(",
    "GetItemRequiredTargetMapBounds",
    "DoCreatureData(",
    "DoGOData(",
    "GAI_VALUE(",
    "GAI_VALUE2(",
    "GAI_VALUE_REF(",
    "GAI_VALUE2_REF(",
)


def read(relative):
    with open(os.path.join(ROOT, relative), encoding="utf-8") as f:
        return f.read()


def signature(text, name):
    """The declared `inline <ret> <name>(<types>)`, whitespace, comments and the parameter names dropped."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    match = re.search(r"inline\s+[A-Za-z_][\w:<>,\s\*&]*?\b" + name + r"\s*\(([^)]*)\)", text)
    if not match:
        return None
    params = [" ".join(p.split()) for p in match.group(1).split(",")]
    params = [re.sub(r"\s+[A-Za-z_]\w*$", "", p) for p in params]
    declared = " ".join(match.group(0).split())
    return declared[:declared.index("(")] + "(" + ", ".join(params) + ")"


class SliceMembership(unittest.TestCase):
    def test_the_source_is_on_the_slice_list(self):
        paths = {line.strip() for line in read(LIST).splitlines() if line.strip() and not line.startswith("#")}
        self.assertIn(SOURCE, paths)


class Pair(unittest.TestCase):
    def test_the_helpers_exist_on_both_cores(self):
        core = read(QUEST_F)
        turtle = read(QUEST_T)
        for name in HELPERS:
            self.assertIn(name, core, "ForeverCore side lacks " + name)
            self.assertIn(name, turtle, "Turtle side lacks " + name)

    def test_the_signatures_are_identical(self):
        core = read(QUEST_F)
        turtle = read(QUEST_T)
        for name in HELPERS:
            self.assertEqual(signature(core, name), signature(turtle, name),
                             name + " differs between the two cores")


# The field spellings differ per core (Turtle `QuestStatusData::m_status`, ForeverCore `Status`), so the body asks the
# pair: a raw field here compiles on one core and breaks the other.
CROSS_CORE = (
    "QuestStatusIncompleteWL4",
    "QuestStatusCompleteWL4",
    "QuestLogCapacityCompat",
)


class Adapted(unittest.TestCase):
    def test_no_turtle_only_spelling_survives(self):
        source = read(SOURCE)
        for gone in GONE:
            self.assertNotIn(gone, source, gone + " is still read by QuestValues.cpp")

    def test_the_cross_core_helpers_are_used(self):
        source = read(SOURCE)
        for name in CROSS_CORE:
            self.assertIn(name, source, name + " is not read by QuestValues.cpp")

    def test_the_objectmgr_subclass_is_gone_from_the_header(self):
        self.assertNotIn("QuestObjectMgr", read(HEADER))


if __name__ == "__main__":
    unittest.main()

"""The corpse run's door (Scarlet Monastery r20-19z39 slot 7, CorpseEntrancePolicy.h).

The ghosts of a Library wipe walked to the door nearest the GHOST (the
Graveyard wing's) and were raised in a wing with no way back. The pure rule
(the destination nearest the corpse wins) is tested in
tools/test_corpse_entrance_policy.cpp; this pins where
ai/playerbot/strategy/actions/ReviveFromCorpseAction.cpp calls it, since that
file only builds against the core:
- FindInstanceEntranceTrigger hands every door into the corpse's map to
  PickCorpseEntrance with the CORPSE's position, and the ghost's distance only
  as the tie-break;
- only external entrances are candidates: the source map (where the trigger
  sits) is read from the core's map table, an unknown or instanceable one never
  counts (CorpseEntranceRules carry the corpse's map, the ghost's map and the
  instance's ghostEntranceMap; the policy test proves the rule);
- the walk (timeout false) and the 300 s timeout (timeout true) both use it, the
  timeout keeping GetMapEntranceTrigger as the fallback only.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ACTION = (ROOT / "ai/playerbot/strategy/actions/ReviveFromCorpseAction.cpp").read_text()
COMPAT_T = (ROOT / "ai/cmangos-compat-w1.h").read_text()


def body(text, start, end):
    i = text.index(start)
    j = text.index(end, i + len(start))
    return text[i:j]


class CorpseEntranceWiringTest(unittest.TestCase):
    def setUp(self):
        # The candidate builder (FindEntranceTriggerByRules) and the corpse run's rules
        # (FindInstanceEntranceTrigger), up to the repop hook's answer.
        self.find = body(ACTION, "static bool FindEntranceTriggerByRules(", "uint32 ai::PickRepopEntrance(")
        self.corpse = body(ACTION, "bool FindCorpseAction::Execute(", "bool FindCorpseAction::isUseful()")

    def test_the_policy_header_is_included(self):
        self.assertIn('#include "playerbot/CorpseEntrancePolicy.h"', ACTION)

    def test_the_pick_is_by_the_corpse(self):
        self.assertIn("PickCorpseEntrance(candidates, corpsePos.getX(), corpsePos.getY(), corpsePos.getZ(), rules)",
                      self.find)
        # The candidates carry the trigger's destination, the ghost's distance to the trigger only on its own map. The fields are read
        # through the both-core pair; the Turtle side is the expression the code always made.
        for field in ("c.destX = destX;", "c.destY = destY;", "c.destZ = destZ;"):
            self.assertIn(field, self.find)
        self.assertIn("AreaTeleportPosCompat(at, destX, destY, destZ);", self.find)
        self.assertIn("x = at ? at->destination.x : 0.0f;", COMPAT_T)
        self.assertRegex(self.find, r"c\.ghostSqYd = onGhostMap \? botPos\.sqDistance2d\(")
        self.assertIn("bool const onGhostMap = atMap == rules.ghostMap;", self.find)
        # No nearest-to-the-ghost pick of its own any more.
        self.assertNotIn("bestDist", self.find)

    def test_only_doors_into_the_corpse_map_and_the_rules(self):
        self.assertIn("if (destMap != corpseMapId)", self.find)
        self.assertIn("destMap = AreaTeleportMapIdCompat(at);", self.find)
        self.assertIn("return at ? at->destination.mapId : 0;", COMPAT_T)
        self.assertIn("c.sourceMap = atMap;", self.find)
        self.assertIn("MapEntry const* sourceMap = MapEntryCompat(atMap);", self.find)
        self.assertIn("c.sourceInstanceable = !sourceMap || sourceMap->Instanceable();", self.find)
        self.assertIn("rules.destMap = corpseMapId;", self.find)
        self.assertIn("rules.ghostMap = botMapId;", self.find)
        self.assertIn("rules.timeout = timeout;", self.find)
        self.assertIn("rules.entranceMap = MapGhostEntranceMapCompat(corpseMapId);", self.find)
        self.assertIn("return entry ? entry->ghostEntranceMap : 0;", COMPAT_T)
        # No filter of its own that could let a door past the policy's rules.
        self.assertNotIn("anyMap", self.find)

    def test_the_walk_uses_the_ghost_map_doors(self):
        self.assertIn(
            "FindInstanceEntranceTrigger(corpsePos.GetMapId(), botPos.GetMapId(), botPos, corpsePos, false, "
            "entranceTrigger, entranceTeleport)", self.corpse)

    def test_the_timeout_uses_its_rules_and_falls_back(self):
        # The timeout is a teleport, so W1's ForeverCore side cuts it; the Turtle side answers true and keeps the block.
        self.assertIn("inline bool CorpseRunTeleportsEnabledCompat()\n{\n    return true;\n}", COMPAT_T)
        timeout = body(self.corpse, "if (CorpseRunTeleportsEnabledCompat() && corpseInDungeon && deadTime >= 5 * MINUTE)",
                       "if (botPos.GetMapId() != corpsePos.GetMapId())")
        pick = timeout.index("FindInstanceEntranceTrigger(dungeonMapId, botPos.GetMapId(), botPos, corpsePos, true,")
        fallback = timeout.index("entranceTeleport = sObjectMgr.GetMapEntranceTrigger(dungeonMapId);")
        self.assertLess(pick, fallback)
        self.assertRegex(timeout, r"if \(!FindInstanceEntranceTrigger\([^;]*\)\)\s*entranceTeleport = sObjectMgr\.GetMapEntranceTrigger")
        # The teleport goes to the picked destination, orientation included (the Turtle pair answers destination.o).
        self.assertIn("bot->TeleportTo(AreaTeleportMapIdCompat(entranceTeleport)", timeout)
        self.assertIn("AreaTeleportOrientationCompat(entranceTeleport));", timeout)
        self.assertIn("return at ? at->destination.o : 0.0f;", COMPAT_T)
        self.assertEqual(len(re.findall(r"sObjectMgr\.GetMapEntranceTrigger\(", ACTION)), 1)


if __name__ == "__main__":
    unittest.main()

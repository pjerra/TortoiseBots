"""A bot released inside a dungeon revives at the door nearest its corpse (SM-REVIVE-PATH-2026-10-23).

The core's Player::RepopAtGraveyard revives a managed bot that releases in a
dungeon at once, still on the dungeon map, at GetMapEntranceTrigger: the
lowest trigger id, so Scarlet Monastery always put it in the Graveyard wing.
Our core fork asks the module first through the PlayerScript hook
GetBotRepopEntrance(bot, mapId, corpse x/y/z), answered with an area trigger
id (0: none) that the core validates. The pure answer (RepopDoorMapValidated,
RepopEntranceRules + PickCorpseEntrance) is tested in
tools/test_corpse_entrance_policy.cpp; this pins the glue, which only builds
against the core:
- BotPlayerAdapter enables PLAYERHOOK_GET_BOT_REPOP_ENTRANCE and overrides the
  hook, answering only for its own bots through ai::PickRepopEntrance;
- PickRepopEntrance answers only on validated maps (Scarlet Monastery, Uldaman),
  builds the candidates with the corpse position and the repop rules (the
  dungeon map as the bot's map, ghost_entrance_map as the entrance map), and
  answers 0 when no door is eligible;
- with TORTOISE_CORE=<core checkout> set, the core side is checked too: the
  hook's signature matches, the ScriptMgr wrapper returns the script's answer,
  the core takes an answered id only when it is a registered teleport into
  this map with valid coordinates (an unknown id or a door into another map
  falls back), it asks before the revive, and with no answer the old trigger
  is kept. verify_all prints a SKIPPED line without it; the VM build gate
  runs it with TORTOISE_CORE set.
"""
import os
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ADAPTER_H = (ROOT / "host/BotPlayerAdapter.h").read_text()
ADAPTER = (ROOT / "host/BotPlayerAdapter.cpp").read_text()
ACTION = (ROOT / "ai/playerbot/strategy/actions/ReviveFromCorpseAction.cpp").read_text()
ACTION_H = (ROOT / "ai/playerbot/strategy/actions/ReviveFromCorpseAction.h").read_text()
POLICY = (ROOT / "ai/playerbot/CorpseEntrancePolicy.h").read_text()
COMPAT_T = (ROOT / "ai/cmangos-compat-w1.h").read_text()
GATE = (ROOT / "tools/verify_all.sh").read_text()


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class BotReviveDoorWiringTest(unittest.TestCase):
    def test_the_hook_is_registered(self):
        ctor = body(ADAPTER, "BotPlayerAdapter::BotPlayerAdapter()", "\n{\n")
        self.assertIn("PLAYERHOOK_GET_BOT_REPOP_ENTRANCE", ctor)
        self.assertIn("uint32 GetBotRepopEntrance(Player* bot, uint32 mapId, float x, float y, float z) override;",
                      ADAPTER_H)

    def test_the_hook_answers_for_its_own_bots_only(self):
        fn = body(ADAPTER, "uint32 BotPlayerAdapter::GetBotRepopEntrance(", "\n}\n")
        guard = fn.index("if (!bot || !BotManager::Instance().IsBot(bot->GetObjectGuid()))")
        answer = fn.index("return ai::PickRepopEntrance(bot, mapId, x, y, z);")
        self.assertLess(guard, answer)
        self.assertIn('#include "../ai/playerbot/strategy/actions/ReviveFromCorpseAction.h"', ADAPTER)
        self.assertIn("uint32 PickRepopEntrance(Player* bot, uint32 dungeonMapId,", ACTION_H)
        self.assertIn("return 0;", fn)

    def test_the_answer_uses_the_corpse_and_the_repop_rules(self):
        fn = body(ACTION, "uint32 ai::PickRepopEntrance(", "\n}\n")
        # Validated maps only, before any lookup.
        gate = fn.index("if (!bot || !RepopDoorMapValidated(dungeonMapId))")
        self.assertLess(gate, fn.index("FindEntranceTriggerByRules("))
        # The id comes through the both-core pair; the Turtle side answers the field the hook always answered.
        self.assertIn("return AreaTriggerIdCompat(atEntry);", fn)
        self.assertIn("return entry ? entry->id : 0;", COMPAT_T)
        allow = body(POLICY, "inline bool RepopDoorMapValidated(", "\n    }\n")
        self.assertIn("return dungeonMap == 189 || dungeonMap == 70;", allow)
        self.assertIn("WorldPosition const corpsePos(dungeonMapId, corpseX, corpseY, corpseZ);", fn)
        self.assertRegex(fn, r"FindEntranceTriggerByRules\(dungeonMapId, WorldPosition\(bot\), corpsePos,\s*"
                             r"RepopEntranceRules\(dungeonMapId, MapGhostEntranceMapCompat\(dungeonMapId\)\), atEntry, at\)")
        self.assertIn("return entry ? entry->ghostEntranceMap : 0;", COMPAT_T)
        # No eligible door: 0.
        self.assertRegex(fn, r"if \(!FindEntranceTriggerByRules\([^;]*\)\)\s*return 0;")
        self.assertIn("[BOT REPOP]", fn)
        # The rules: the bot stands on the dungeon map, the doors are the timeout's.
        rules = body(POLICY, "inline CorpseEntranceRules RepopEntranceRules(", "\n    }\n")
        for line in ("rules.destMap = dungeonMap;", "rules.ghostMap = dungeonMap;",
                     "rules.entranceMap = entranceMap;", "rules.timeout = true;"):
            self.assertIn(line, rules)

    def test_policy_test_is_in_the_gate(self):
        # the gate discovers every tools/test_*.py and tools/test_*.cpp, so being named test_* puts a test in it
        self.assertIn("python3 -m unittest discover -s tools -p 'test_*.py'", GATE)
        self.assertIn("tools/test_*.cpp", GATE)
        self.assertTrue((ROOT / "tools/test_bot_revive_door_wiring.py").is_file())
        self.assertTrue((ROOT / "tools/test_corpse_entrance_policy.cpp").is_file())


@unittest.skipUnless(os.environ.get("TORTOISE_CORE"), "set TORTOISE_CORE=<core checkout> to check the core side")
class CoreRepopHookTest(unittest.TestCase):
    def setUp(self):
        core = pathlib.Path(os.environ["TORTOISE_CORE"]) / "src/game"
        self.objects = (core / "ScriptObjects.h").read_text()
        self.mgr = (core / "ScriptMgr.cpp").read_text()
        self.player = (core / "Objects/Player.cpp").read_text()

    def test_the_core_hook_matches(self):
        self.assertIn("PLAYERHOOK_GET_BOT_REPOP_ENTRANCE,", self.objects)
        self.assertIn("virtual uint32 GetBotRepopEntrance(Player* /*bot*/, uint32 /*mapId*/, "
                      "float /*x*/, float /*y*/, float /*z*/) { return 0; }", self.objects)

    def test_the_wrapper_returns_the_answer(self):
        fn = body(self.mgr, "uint32 ScriptMgr::GetBotRepopEntrance(", "\n}\n")
        self.assertIn("ForEachEnabledHook(PLAYERHOOK_GET_BOT_REPOP_ENTRANCE,", fn)
        # The first answer is kept and returned (dropping the assignment loses every answer).
        self.assertIn("if (!triggerId) triggerId = script->GetBotRepopEntrance(bot, mapId, x, y, z);", fn)
        self.assertIn("uint32 triggerId = 0;", fn)
        self.assertIn("return triggerId;", fn)

    def test_the_core_asks_before_the_revive_and_keeps_its_trigger(self):
        fn = body(self.player, "void Player::RepopAtGraveyard()", "\n}\n")
        branch = body(fn, "if (!IsAlive() && repopAtEntrance && GetMap() && GetMap()->IsDungeon())", "return;")
        default = branch.index("AreaTriggerTeleport const* entrance = sObjectMgr.GetMapEntranceTrigger(GetMapId());")
        ask = branch.index("if (uint32 const pickId = sScriptMgr.GetBotRepopEntrance(this, GetMapId(), x, y, z))")
        revive = branch.index("ResurrectPlayer(1.0f);")
        self.assertLess(default, ask)
        self.assertLess(ask, revive)
        self.assertIn("if (entrance)", branch[ask:revive])

    def test_an_unknown_or_foreign_door_falls_back(self):
        fn = body(self.player, "void Player::RepopAtGraveyard()", "\n}\n")
        branch = body(fn, "if (!IsAlive() && repopAtEntrance && GetMap() && GetMap()->IsDungeon())", "return;")
        # The answer replaces the default only through the whole chain: a registered
        # teleport (an unknown id resolves to null), into this map (an outside-map
        # door is refused), with valid coordinates.
        self.assertRegex(branch,
            r"if \(uint32 const pickId = sScriptMgr\.GetBotRepopEntrance\(this, GetMapId\(\), x, y, z\)\)\s*"
            r"if \(AreaTriggerTeleport const\* pick = sObjectMgr\.GetAreaTriggerTeleport\(pickId\)\)\s*"
            r"if \(pick->destination\.mapId == GetMapId\(\) && MaNGOS::IsValidMapCoord\(pick->destination\.x,\s*"
            r"pick->destination\.y, pick->destination\.z, pick->destination\.o\)\)\s*"
            r"entrance = pick;")
        # No other write to the door.
        self.assertEqual(len(re.findall(r"(?m)^\s*entrance = ", branch)), 1)
        self.assertEqual(branch.count("AreaTriggerTeleport const* entrance = sObjectMgr.GetMapEntranceTrigger(GetMapId());"), 1)
        self.assertIn("TeleportTo(entrance->destination, TeleOptions);", branch)


if __name__ == "__main__":
    unittest.main()

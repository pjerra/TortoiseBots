"""Wiring pins for the stuck rescue (TBF #146): the legacy five-minute reset and the fifteen-minute rescue.

The behaviour decisions are pure headers with their own tests (LongStuckRescuePolicy.h,
GraveyardTeleportPolicy.h, CombatStuckPolicy.h, TravelRepickPolicy.h; tools/test_long_stuck_rescue_policy.cpp). These
pins read the sources and prove the port stays a port: the two files are on the slice list, the registry carries the
actions, triggers and values their strategies fire, and every Turtle-only spelling of UnstuckAction/StuckTriggers is
answered by a both-core helper (W1 pair for the graveyard, homebind area and duel; D4 pair for the homebind teleport).

Run: python3 -m unittest forever/tools/test_w4_stuck_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
UNSTUCK = (ROOT / "ai/playerbot/strategy/actions/UnstuckAction.cpp").read_text()
STUCK_H = (ROOT / "ai/playerbot/strategy/triggers/StuckTriggers.h").read_text()
STUCK_CPP = (ROOT / "ai/playerbot/strategy/triggers/StuckTriggers.cpp").read_text()
RELEASE = (ROOT / "ai/playerbot/strategy/actions/ReleaseSpiritAction.h").read_text()
COMPAT_F = (ROOT / "forever/src/compat/TbCompat_W1.h").read_text()
COMPAT_T = (ROOT / "ai/cmangos-compat-w1.h").read_text()
D4_F = (ROOT / "forever/src/compat/TbCompat_D4.h").read_text()
D4_T = (ROOT / "ai/cmangos-compat-d4.h").read_text()
GEN = (ROOT / "forever/src/slice/SliceAiObjectContext.cpp").read_text()
SLICE_KEYS_PY = (ROOT / "forever/tools/slice_gen_context.py").read_text()
LIST = (ROOT / "forever/SLICE-W4.txt").read_text().splitlines()

ACTIONS = {
    "unstuck": "new UnstuckAction(ai)",
    "reset": "new ResetAction(ai)",
    "hearthstone": "new UseHearthStoneAction(ai)",
    "repop": "new RepopAction(ai)",
}
TRIGGERS = {
    "move stuck": "new MoveStuckTrigger(ai)",
    "move long stuck": "new MoveLongStuckTrigger(ai)",
    "combat stuck": "new CombatStuckTrigger(ai)",
    "combat long stuck": "new CombatLongStuckTrigger(ai)",
}
VALUES = {
    "distance moved since": "new DistanceMovedSinceValue(ai)",
    "money": "new MoneyValue(ai)",
    "home bind": "new HomeBindValue(ai)",
    "travel target active": "new TravelTargetActiveValue(ai)",
    "no active travel destinations": "new NoActiveTravelDestinationsValue(ai)",
}


def creator(text, key):
    m = re.search(r'creators\["' + re.escape(key) + r'"\] = \[\]\(PlayerbotAI\* ai\) \{ return ' + r'(.*?); \};', text)
    return m.group(1) if m else None


class SliceAndRegistry(unittest.TestCase):
    def test_the_two_files_are_on_the_slice_list(self):
        for line in ("ai/playerbot/strategy/actions/UnstuckAction.cpp",
                     "ai/playerbot/strategy/triggers/StuckTriggers.cpp"):
            self.assertEqual(1, LIST.count(line), line)

    def test_the_stuck_key_group_is_joined_and_complete(self):
        m = re.search(r"STUCK_KEYS = \{(.*?)\n\}", SLICE_KEYS_PY, re.S)
        self.assertIsNotNone(m)
        group = m.group(1)
        for key in list(ACTIONS) + list(TRIGGERS) + list(VALUES):
            self.assertIn(key, group, key)
        self.assertIn("STUCK_KEYS)", SLICE_KEYS_PY)

    def test_the_registry_creates_the_same_classes_as_the_full_build(self):
        for table in (ACTIONS, TRIGGERS, VALUES):
            for key, expected in table.items():
                self.assertEqual(expected, creator(GEN, key), key)

    def test_the_trigger_header_is_what_the_build_compiles(self):
        # StuckTriggers.h is header-only; the 6-line TU exists so the header is parsed as the slice's own file.
        self.assertIn('#include "StuckTriggers.h"', STUCK_CPP)


class UnstuckActionPort(unittest.TestCase):
    def test_no_turtle_only_spelling_remains(self):
        for gone in ("bot->GetHomeBindLocation(", "bot->GetHomeBindAreaId(", "sObjectMgr.GetClosestGraveYard",
                     "TeleportToHomebind(0, false)", "TortoiseBots::BotManager::Instance().SendStrandedLowbieHome(",
                     "m_movementInfo.RemoveMovementFlag", '#include "runtime/BotManager.h"'):
            self.assertNotIn(gone, UNSTUCK, gone)

    def test_every_core_read_goes_through_the_pair(self):
        self.assertIn('#include "playerbot/TbCompatW1.h"', UNSTUCK)
        for used in ("ClosestGraveyardCompat(bot,", "SafeLocMapIdCompat(grave)", "SafeLocXCompat(grave)",
                     "HomeBindLocationCompat(bot)", "HomeBindAreaIdCompat(bot)",
                     "TeleportToHomebindCompat(bot, false)", "SendStrandedLowbieHomeCompat(bot)",
                     "RemoveOnTransportFlagCompat(bot)"):
            self.assertIn(used, UNSTUCK, used)

    def test_the_rescue_keeps_the_hearth_cooldown_on_turtle(self):
        # The last resort is a rescue, not a hearth: the Turtle side must receive false, unlike RepopAction's default.
        self.assertIn("TeleportToHomebindCompat(bot, false)", UNSTUCK)
        self.assertNotIn("TeleportToHomebindCompat(bot)", UNSTUCK.replace("TeleportToHomebindCompat(bot, false)", ""))

    def test_repop_sends_the_lowbie_home_through_the_pair(self):
        self.assertIn("SendStrandedLowbieHomeCompat(bot);", RELEASE)
        self.assertNotIn("TortoiseBots::BotManager::Instance().SendStrandedLowbieHome(bot);", RELEASE)


class StuckTriggerPort(unittest.TestCase):
    def test_the_navmesh_probe_is_the_core_independent_one(self):
        for gone in ("MMAP::MMapFactory", "loadMap(", "cell.GridX()", "cell.GridY()", "#include <Maps/MoveMap.h>"):
            self.assertNotIn(gone, STUCK_H, gone)
        self.assertIn("!NavMeshLoadedCompat(botPos.GetMapId(), bot->GetInstanceId())", STUCK_H)

    def test_both_stuck_triggers_read_the_duel_through_the_pair(self):
        self.assertEqual(2, STUCK_H.count("time_t const duelStart = DuelStartTimeCompat(bot);"))
        self.assertNotIn("bot->m_duel", STUCK_H)


class CompatPair(unittest.TestCase):
    def test_homebind_area_comes_from_each_cores_own_field(self):
        self.assertIn("inline uint32 HomeBindAreaIdCompat(Player* bot)\n{\n    return bot ? bot->m_homebindAreaId : 0;\n}",
                      COMPAT_F)
        self.assertIn("inline uint32 HomeBindAreaIdCompat(Player* bot)\n{\n    return bot ? bot->GetHomeBindAreaId() : 0;\n}",
                      COMPAT_T)

    def test_duel_start_comes_from_each_cores_own_field(self):
        self.assertIn("bot->duel ? bot->duel->StartTime : 0", COMPAT_F)
        self.assertIn("bot->m_duel ? bot->m_duel->startTime : 0", COMPAT_T)

    def test_the_homebind_teleport_carries_the_cooldown_flag_on_both_sides(self):
        self.assertIn("inline bool TeleportToHomebindCompat(Player* bot, bool /*hearthCooldown*/ = true)",
                      D4_F)
        self.assertIn("inline bool TeleportToHomebindCompat(Player* bot, bool hearthCooldown = true)",
                      D4_T)
        self.assertIn("bot->TeleportToHomebind(0, hearthCooldown);", D4_T)


if __name__ == "__main__":
    unittest.main()

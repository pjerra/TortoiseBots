"""Wiring pins for piece C (roll won): a bot that wins a roll notices it, logs it and equips the upgrade.

Proven by reading the sources: (1) the won poll lives in its own files and is fed by the trigger poll,
(2) the trigger and the log action are registered in the slice, (3) the trigger node still maps to
`log roll won` + `equip upgrades` (shared TB wiring, unchanged), (4) the synthetic event packet matches the
layout RollWonLogAction parses, (5) TB defaults run (AutoEquipUpgradeLoot=1 everywhere).

Run: python3 -m unittest forever/tools/test_roll_won_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
HOST = ROOT / "forever/src/host/TbRollWon.h"
HOST_CPP = ROOT / "forever/src/host/TbRollWon.cpp"
PURE = ROOT / "forever/src/host/TbRollWonPure.h"
S9 = (ROOT / "forever/src/compat/TbCompat_S9.h").read_text()
CTX = (ROOT / "forever/src/slice/SliceAiObjectContext.cpp").read_text()
GEN = (ROOT / "forever/tools/slice_gen_context.py").read_text()
STRATEGY = (ROOT / "ai/playerbot/strategy/generic/WorldPacketHandlerStrategy.cpp").read_text()
ACTION = (ROOT / "ai/playerbot/strategy/actions/LootRollAction.cpp").read_text()
TEMPLATE = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
FOREVER_CONF = (ROOT / "forever/conf/aiplayerbot.conf.dist").read_text()
CONFIG = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()


class RollWonWiring(unittest.TestCase):
    def test_the_poll_lives_in_its_own_files(self):
        self.assertTrue(HOST.exists())
        self.assertTrue(HOST_CPP.exists())
        self.assertTrue(PURE.exists())
        # The pure header names no core type: the standalone unit test compiles it with a bare g++.
        for line in PURE.read_text().splitlines():
            if line.strip().startswith("#include"):
                self.assertNotIn('"', line, "TbRollWonPure.h includes a quoted header: %s" % line.strip())

    def test_the_trigger_poll_routes_to_the_won_poll(self):
        poll = S9[S9.index("inline void PollPacketEventCompat("):]
        self.assertIn('if (name == "loot roll won")', poll)
        self.assertIn("rollwon::PollRollWon(bot, [&](WorldPacket& event) { trigger.ExternalEvent(event, nullptr); });", poll)
        self.assertIn('#include "host/TbRollWon.h"', S9)

    def test_trigger_and_log_action_are_registered(self):
        self.assertIn('creators["loot roll won"]', CTX)
        self.assertIn('creators["log roll won"]', CTX)
        start = GEN.index("SLICE9_KEYS = {")
        keys = GEN[start:GEN.index("\n}\n", start)]
        self.assertIn("loot roll won", keys)
        self.assertIn("log roll won", keys)

    def test_the_node_still_logs_then_equips(self):
        node = STRATEGY[STRATEGY.index('"loot roll won"'):]
        node = node[:node.index(");")]
        self.assertIn('"log roll won"', node)
        self.assertIn('"equip upgrades"', node)
        self.assertLess(node.index('"log roll won"'), node.index('"equip upgrades"'))

    def test_the_synthetic_packet_matches_what_the_action_parses(self):
        # RollWonLogAction reads: owner, slot, item id, skipped suffix, property id, winner; only the winner logs.
        action = ACTION[ACTION.index("bool RollWonLogAction::Execute("):]
        action = action[:action.index("\n}\n")]
        order = ["p >> lootGuid;", "p >> slot;", "p >> itemId;", "p >> randomPropertyId;", "p >> winner;"]
        for i in range(len(order) - 1):
            self.assertLess(action.index(order[i]), action.index(order[i + 1]))
        self.assertIn("if (winner != bot->getObjectGuid())", action)
        host = HOST_CPP.read_text()
        event = host[host.index("WorldPacket event;"):]
        event = event[:event.index("raiseEvent(event);")]
        writes = ["event << owner;", "event << uint32(won.id.slot);", "event << uint32(won.itemId);",
                  "event << uint32(0);", "event << botGuid;"]
        for i in range(len(writes) - 1):
            self.assertLess(event.index(writes[i]), event.index(writes[i + 1]))

    def test_tb_defaults_run(self):
        self.assertIn("AiPlayerbot.AutoEquipUpgradeLoot = 1", TEMPLATE)
        self.assertIn("# Default: 1 (enabled)", TEMPLATE)
        self.assertIn("AiPlayerbot.AutoEquipUpgradeLoot = 1", FOREVER_CONF)
        self.assertIn('config.GetBoolDefault("AiPlayerbot.AutoEquipUpgradeLoot", true)', CONFIG)


if __name__ == "__main__":
    unittest.main()

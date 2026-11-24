#!/usr/bin/env python3
"""Decision pins for #209 round 3: `disenchant random item` and `enchant random item` stay unregistered.

MaintenanceStrategy.cpp names both rows, and the full build's ActionContext maps them to classes in
CastCustomSpellAction.cpp. The slice leaves them out because the host already owns the behaviour:
TbHostProfessionUpkeep breaks down a free bot's junk and applies better gear enchants, gated by
TortoiseBots.World.Disenchant / .EnchantGear. The slice gives `maintenance` to every bot, so keeping
the legacy rows would dismantle a commanded player's loot and spend its reagents outside those
switches; TB itself adds maintenance only to a bot without a live master, and neither action's
isUseful tests the master. The enchant row would also need `has reagents for` (CraftValues.cpp, not
on the slice list), without which GetSpellPriority answers 0 for every spell.

Run: python3 -m unittest forever/tools/test_209c_maintenance_keys_wiring.py
"""
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import slice_gen_context as gen  # noqa: E402
import slice_registry_check as chk  # noqa: E402

ROOT = chk.ROOT
KEYS = ("disenchant random item", "enchant random item")


def read(rel):
    with open(os.path.join(ROOT, rel), "r", encoding="utf-8") as f:
        return f.read()


class LeftOut(unittest.TestCase):
    def test_generator_does_not_list_them(self):
        for key in KEYS:
            self.assertNotIn(key, gen.KEYS["action"], "%s is registered by slice_gen_context.py" % key)

    def test_generated_context_does_not_register_them(self):
        registered = chk.slice_registry()["action"]
        for key in KEYS:
            self.assertNotIn(key, registered, "%s is registered by SliceAiObjectContext.cpp" % key)

    def test_host_upkeep_owns_both_and_runs_for_free_bots_only(self):
        botmgr = read("forever/src/TbBotMgr.cpp")
        self.assertIn("if (bot.free && (_disenchantUpkeepEnabled || _enchantGearEnabled))", botmgr)
        self.assertIn("tbhost::upkeep::TickProfessionUpkeep(me, diff, _disenchantUpkeepEnabled, _enchantGearEnabled)", botmgr)
        header = read("forever/src/host/TbHostProfessionUpkeep.h")
        for symbol in ("TickProfessionUpkeep", "HasActiveUpkeepCast", "EnchantWeightFor"):
            self.assertIn(symbol, header, symbol)
        policy = read("ai/playerbot/policy/profession/EnchantUpkeepPolicy.h")
        for symbol in ("DisenchantUpkeepWants", "EnchantUpkeepWants"):
            self.assertIn(symbol, policy, symbol)

    def test_the_slice_gives_maintenance_to_every_bot(self):
        # The reason the keys must stay out: unlike TB (maintenance only without a live master,
        # AiFactory.cpp), the slice adds it to every non-combat engine, commanded bots included.
        factory = read("forever/src/slice/SliceAiFactory.cpp")
        self.assertIn('nonCombatEngine->addStrategy("maintenance");', factory)


if __name__ == "__main__":
    unittest.main()

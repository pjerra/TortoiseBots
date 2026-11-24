#!/usr/bin/env python3
"""Wiring pins for the free-bot life layer (TBF #147, package D).

Packages A (rpg brain), B (quest values) and C (town verbs) compiled the sources and the compat pair, but their rows only
fire when the slice registers the keys they read, a free bot receives the `rpg` strategy and the config the new files
read is shipped. This package owns the generated files, so the pins:

  * the eleven life sources stay on the slice list the Forever build reads;
  * every rpg, quest and town key stays in slice_gen_context.py and the generated SliceAiObjectContext.cpp registers it
    with the full build's own creator; slice_registry_check.py (a) is empty and no life key is left in (b);
  * the free kit array is unchanged and the factory adds `rpg` only under AiPlayerbot.AutoDoQuests (the full build's gate);
  * the four config keys the rpg files read ship with the defaults the code has, and the generated config is current;
  * the #146 trainer stand-ins are gone from the link stubs and A's three stubs answer "nothing there".

Run: python3 -m unittest forever/tools/test_w4_life_wiring.py
"""
import os
import re
import subprocess
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import slice_registry_check as chk  # noqa: E402
import slice_gen_context as gen  # noqa: E402

ROOT = chk.ROOT
LIST = "forever/SLICE-W4.txt"
STUBS = "forever/src/slice/SliceLinkStubs.cpp"
KIT = "ai/playerbot/policy/world/FreeKitPolicy.h"
KIT_TEST = "forever/tools/test_free_kit_policy.cpp"
FACTORY = "forever/src/slice/SliceAiFactory.cpp"
CONF = "forever/conf/aiplayerbot.conf.dist"

SOURCES = (
    "ai/playerbot/strategy/actions/RpgAction.cpp",
    "ai/playerbot/strategy/actions/RpgSubActions.cpp",
    "ai/playerbot/strategy/actions/ChooseRpgTargetAction.cpp",
    "ai/playerbot/strategy/actions/MoveToRpgTargetAction.cpp",
    "ai/playerbot/strategy/generic/RpgStrategy.cpp",
    "ai/playerbot/strategy/values/PossibleRpgTargetsValue.cpp",
    "ai/playerbot/strategy/triggers/RpgTriggers.cpp",
    "ai/playerbot/strategy/values/QuestValues.cpp",
    "ai/playerbot/strategy/values/VendorValues.cpp",
    "ai/playerbot/strategy/actions/DestroyItemAction.cpp",
    "ai/playerbot/strategy/actions/ServiceNearbyNpcAction.cpp",
)

# (kind, key): every key the compiled life rows need, in the full build's kind.
KEYS = [
    ("strategy", "rpg"),
    ("strategy", "rpg quest"),
    ("strategy", "rpg vendor"),
    ("strategy", "rpg explore"),
    ("strategy", "rpg maintenance"),
    ("action", "rpg"),
    ("action", "choose rpg target"),
    ("action", "move to rpg target"),
    ("action", "service nearby npc"),
    ("action", "rpg stay"),
    ("action", "rpg work"),
    ("action", "rpg emote"),
    ("action", "rpg cancel"),
    ("action", "rpg use"),
    ("action", "rpg start quest"),
    ("action", "rpg end quest"),
    ("action", "rpg sell"),
    ("action", "rpg buy"),
    ("action", "rpg repair"),
    ("action", "rpg train"),
    ("trigger", "no rpg target"),
    ("trigger", "far from rpg target"),
    ("trigger", "rpg"),
    ("trigger", "rpg wander"),
    ("trigger", "rpg start quest"),
    ("trigger", "rpg end quest"),
    ("trigger", "rpg sell"),
    ("trigger", "rpg buy"),
    ("trigger", "rpg repair"),
    ("trigger", "rpg train"),
    ("trigger", "rpg use"),
    ("trigger", "rpg quest use"),
    ("value", "possible rpg targets"),
    ("value", "next rpg action"),
    ("value", "can fight equal"),
    ("value", "following party"),
    ("value", "free quest log slots"),
    ("value", "has rewardable finished quest"),
    ("value", "dialog status"),
    ("value", "dialog status quest"),
    ("value", "can accept quest npc"),
    ("value", "can accept quest low level npc"),
    ("value", "need quest reward"),
    ("value", "can turn in quest npc"),
    ("value", "can repeat quest npc"),
    ("value", "need quest objective"),
    ("value", "can use item on"),
    ("value", "need for quest"),
    ("value", "has nearby quest taker"),
    ("action", "smart destroy item"),
    ("trigger", "quest log nearly full"),
    ("value", "can sell"),
    ("value", "can buy"),
    ("value", "can repair"),
    ("value", "should repair"),
    ("value", "should sell"),
    ("value", "can get mail"),
    ("value", "vendor has useful item"),
    ("value", "nearby service target"),
    ("value", "should service nearby npc"),
    ("value", "nearby service fail parks"),
]

# Rows the life strategies name but this round leaves inert: a missing trigger reads as null in RpgAction's search of
# the next rpg step, so the row is skipped, never mis-fired. `destroy all gray` has no creator in any build.
UNREGISTERED = [
    ("strategy", "rpg guild"),
    ("trigger", "rpg heal"),
    ("trigger", "rpg camp"),
    ("trigger", "rpg gossip talk"),
    ("trigger", "rpg repeat quest"),
    ("trigger", "rpg taxi"),
    ("trigger", "rpg discover"),
    ("trigger", "rpg home bind"),
    ("trigger", "rpg buy petition"),
    ("trigger", "rpg queue bg"),
    ("trigger", "rpg trade useful"),
    ("trigger", "rpg enchant"),
    ("trigger", "rpg duel"),
    ("trigger", "rpg spell"),
    ("trigger", "rpg craft"),
    ("trigger", "rpg item"),
    ("action", "destroy all gray"),
    ("value", "vendor map"),
    ("value", "item vendor list"),
    ("value", "entry quest relation"),
    ("value", "quest guidp map"),
    ("value", "quest givers"),
    ("value", "trainable spell map"),
]

# The rpg files read these (PlayerbotAIConfig.cpp); the config ships them with the code's defaults.
CONFIG_KEYS = {
    "AiPlayerbot.RpgDelay": "10000",
    "AiPlayerbot.TurnInRpg": "0",
    "AiPlayerbot.RandombotsWalkingRPG": "0",
    "AiPlayerbot.JumpRandomChance": "0.20",
}


def read(relative):
    with open(os.path.join(ROOT, relative), encoding="utf-8") as f:
        return f.read()


class SliceMembership(unittest.TestCase):
    def test_the_eleven_sources_are_on_the_slice_list(self):
        paths = {line.strip() for line in read(LIST).splitlines() if line.strip() and not line.startswith("#")}
        for path in SOURCES:
            self.assertIn(path, paths)


class Generator(unittest.TestCase):
    def test_every_life_key_is_in_the_generator(self):
        for kind, key in KEYS:
            self.assertIn(key, gen.KEYS[kind], "%s '%s' is not in slice_gen_context.py" % (kind, key))

    def test_the_deliberately_inert_rows_stay_out(self):
        for kind, key in UNREGISTERED:
            self.assertNotIn(key, gen.KEYS[kind], "%s '%s' must not be registered" % (kind, key))


# A key named inside a "group or"/"and" qualifier is invisible to slice_registry_check.py, which only parses macro
# arguments: an unregistered one reads the inert false at runtime and silently removes the whole row. The quest travel
# gates are the ones this bit: with `following party` missing, every quest destination - giver, taker and objective -
# answered inactive and no free bot walked to one.
GATE_SOURCE = "ai/playerbot/TravelMgr.cpp"


class QuestTravelGates(unittest.TestCase):
    def test_the_group_answer_and_every_verb_it_guards_are_registered(self):
        self.assertIn("following party", gen.KEYS["value"])
        gates = re.findall(r'"following party,([a-z ]+)::', read(GATE_SOURCE))
        self.assertGreaterEqual(len(gates), 4, "the quest travel gates no longer read 'following party' qualifiers")
        for name in gates:
            self.assertIn(name, gen.KEYS["value"], "value '%s' is gated in but not registered" % name)


class Registered(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.full = chk.full_registry()
        cls.registered = chk.slice_registry()

    def test_generated_context_registers_every_key_with_the_full_builds_creator(self):
        for kind, key in KEYS:
            self.assertIn(key, self.registered[kind], "%s '%s' is not in SliceAiObjectContext.cpp" % (kind, key))
            self.assertEqual(self.registered[kind][key]["expr"], self.full[kind][key]["expr"], key)

    def test_generated_context_is_up_to_date(self):
        proc = subprocess.run([sys.executable, os.path.join(HERE, "slice_gen_context.py"), "--check"],
                              capture_output=True, text=True, timeout=120)
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)

    def test_registry_check_no_longer_lists_them(self):
        proc = subprocess.run([sys.executable, os.path.join(HERE, "slice_registry_check.py"), "--list"],
                              capture_output=True, text=True, timeout=300)
        self.assertEqual(proc.returncode, 0, proc.stdout[-2000:] + proc.stderr)
        self.assertIn("(a) slice keys that are not in the full build, or map elsewhere: 0", proc.stdout)
        for kind, key in KEYS:
            self.assertIsNone(re.search(r"(?m)^    %s  " % re.escape(key), proc.stdout),
                              "%s '%s' is still on the missing list" % (kind, key))


class FreeKit(unittest.TestCase):
    def test_the_pure_kit_stays_the_same(self):
        self.assertIn('"grind", "travel", "food"', read(KIT))
        self.assertIn('"grind", "travel", "food"', read(KIT_TEST))
        kit = re.search(r"FreeWorldKit\(\)\s*\{\s*static char const\* const kit\[\] = \{([^}]*)\}", read(KIT))
        self.assertIsNotNone(kit)
        self.assertNotIn('"rpg"', kit.group(1))

    def test_the_factory_adds_rpg_to_a_free_bot_under_autodoquests(self):
        factory = read(FACTORY)
        free = re.search(r"IsFreeBot\(player\)[\s\S]*?return;\s*\}", factory)
        self.assertIsNotNone(free, "the free-kit branch is gone")
        self.assertRegex(free.group(0),
                         r'if \(sPlayerbotAIConfig\.autoDoQuests\)\s*\n\s*nonCombatEngine->addStrategy\("rpg"\);')

    def test_the_kit_comment_names_the_factory_add(self):
        self.assertIn("AiPlayerbot.AutoDoQuests", read(KIT))


class Config(unittest.TestCase):
    def test_the_rpg_config_keys_ship_with_the_code_defaults(self):
        conf = read(CONF)
        for key, value in CONFIG_KEYS.items():
            self.assertRegex(conf, r"(?m)^%s = %s$" % (re.escape(key), re.escape(value)))

    def test_the_generated_config_is_current(self):
        proc = subprocess.run([sys.executable, os.path.join(HERE, "gen_aiplayerbot_conf.py"), "--check"],
                              capture_output=True, text=True, timeout=300)
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)


class StandIns(unittest.TestCase):
    def test_the_146_trainer_stand_ins_are_gone(self):
        stubs = read(STUBS)
        self.assertNotIn("RpgTrainTrigger::IsTrainerOf", stubs)
        self.assertNotIn("RpgTrainTrigger::TeachesAffordableSpell", stubs)

    def test_the_unported_services_answer_nothing_exists(self):
        stubs = read(STUBS)
        for line in ("bool IsOrderFishing(PlayerbotAI*) { NoteMisc(); return false; }",
                     "bool DowntimeService::IsDowntimeActive(PlayerbotAI*) { NoteMisc(); return false; }",
                     "bool BuyPetitionAction::canBuyPetition(Player*) { NoteMisc(); return false; }"):
            self.assertIn(line, stubs)


if __name__ == "__main__":
    unittest.main()

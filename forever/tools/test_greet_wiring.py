#!/usr/bin/env python3
"""Wiring pins for the social greeting (TBF #158, package P3a).

The greeting already exists as legacy code (GreetAction, EmoteStrategy); this package compiles it on Forever, registers
the `emote` strategy / `greet` action / `already seen players` value and gives the strategy to a commanded bot's
non-combat kit and to a free bot's kit. The pins:

  * both sources stay on the slice list the Forever build reads;
  * every key stays in the generator and the generated SliceAiObjectContext.cpp registers it with the full build's own
    creator; slice_registry_check.py (a) is empty and none of the keys is left in (b);
  * the strategy is in a commanded bot's non-combat kit and in the free kit (the free kit test pins the array too);
  * only the greeting row of the strategy is live: the other rows name actions and triggers the slice leaves out, which the
    engine skips, so the bot greets and does not start emoting on its own;
  * the three config keys ship with the code's defaults: EnableGreet on, GreetCooldown 600 s, RandomBotGreet off (TB
    parity), and the shipped file equals the code fallbacks;
  * the guards that make the behaviour safe stay in the sources: EnableGreet gates the strategy row, GreetAction's own
    cooldown and the RandomBotGreet gate for pool-like bots, and `new player nearby` is both the trigger and the value.

Run: python3 -m unittest forever/tools/test_greet_wiring.py
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
FACTORY = "forever/src/slice/SliceAiFactory.cpp"
KIT = "ai/playerbot/policy/world/FreeKitPolicy.h"
KIT_TEST = "forever/tools/test_free_kit_policy.cpp"
CONF = "forever/conf/aiplayerbot.conf.dist"
CONFIG_CPP = "ai/playerbot/PlayerbotAIConfig.cpp"
CONFIG_DOC = "docs/server/config-keys.md"
GREET = "ai/playerbot/strategy/actions/GreetAction.cpp"
EMOTE_STRATEGY = "ai/playerbot/strategy/generic/EmoteStrategy.cpp"

SOURCES = (GREET, EMOTE_STRATEGY)

# (kind, key): the keys the greeting needs, in the full build's kind.
KEYS = [
    ("strategy", "emote"),
    ("action", "greet"),
    ("trigger", "new player nearby"),
    ("value", "already seen players"),
]

# The config keys the greeting reads and the TB defaults they must ship with.
CONFIG = (
    ("AiPlayerbot.EnableGreet", "1", "bool"),
    ("AiPlayerbot.GreetCooldown", "600", "int"),
    ("AiPlayerbot.RandomBotGreet", "0", "bool"),
)


def read(relative):
    with open(os.path.join(ROOT, relative), encoding="utf-8") as f:
        return f.read()


def code_only(text):
    return re.sub(r"//[^\n]*", "", text)


class SliceMembership(unittest.TestCase):
    def test_both_sources_are_on_the_slice_list(self):
        paths = {line.strip() for line in read(LIST).splitlines() if line.strip() and not line.startswith("#")}
        for path in SOURCES:
            self.assertIn(path, paths)


class Registered(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.full = chk.full_registry()

    def test_generator_lists_every_key(self):
        for kind, key in KEYS:
            self.assertIn(key, gen.KEYS[kind], "%s '%s' is not in slice_gen_context.py" % (kind, key))

    def test_generated_context_registers_every_key_with_the_full_builds_creator(self):
        registered = chk.slice_registry()
        for kind, key in KEYS:
            self.assertIn(key, registered[kind], "%s '%s' is not in SliceAiObjectContext.cpp" % (kind, key))
            self.assertEqual(registered[kind][key]["expr"], self.full[kind][key]["expr"], key)

    def test_generated_context_is_up_to_date(self):
        proc = subprocess.run([sys.executable, os.path.join(HERE, "slice_gen_context.py"), "--check"],
                              capture_output=True, text=True, timeout=120)
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)

    def test_registry_check_maps_every_slice_key(self):
        proc = subprocess.run([sys.executable, os.path.join(HERE, "slice_registry_check.py"), "--list"],
                              capture_output=True, text=True, timeout=300)
        self.assertEqual(proc.returncode, 0, proc.stdout[-2000:] + proc.stderr)
        self.assertIn("(a) slice keys that are not in the full build, or map elsewhere: 0", proc.stdout)

    def test_only_the_greeting_row_of_the_strategy_is_live(self):
        registered = chk.slice_registry()
        self.assertIn("new player nearby", registered["trigger"])
        self.assertIn("new player nearby", registered["value"])
        # The strategy's other rows name legacy actions and triggers this slice does not compile; the engine skips them
        # (forever/src/slice/SliceMissingKeyGuard.h), so the bot waves and does not start emoting or talking on its own.
        for kind, key in (("action", "emote"), ("action", "talk"), ("action", "mount anim"),
                          ("trigger", "seldom"), ("trigger", "receive text emote"), ("trigger", "receive emote")):
            self.assertNotIn(key, registered[kind], "%s '%s' must stay unregistered" % (kind, key))


class Kits(unittest.TestCase):
    def test_a_commanded_bot_gets_the_strategy(self):
        factory = code_only(read(FACTORY))
        self.assertEqual(factory.count('nonCombatEngine->addStrategy("emote");'), 1)
        body = factory.split("void AiFactory::AddDefaultNonCombatStrategies(", 1)
        self.assertEqual(len(body), 2, "TbSliceAiFactory lost AddDefaultNonCombatStrategies")
        body = body[1].split("\nvoid ", 1)[0]
        add = body.index('nonCombatEngine->addStrategy("emote");')
        free = body.index("sRandomBotFacade.IsFreeBot(player)")
        switch = body.index("switch (player->GetClass())")
        self.assertGreater(add, free, "a free bot's kit is the policy array; the emote line must follow its branch")
        self.assertLess(add, switch, "the emote line must run for every commanded class kit")

    def test_a_free_bot_gets_the_strategy_from_its_kit(self):
        self.assertIn('"default", "buff", "emote", nullptr', read(KIT))
        self.assertIn('"default", "buff", "emote", nullptr', read(KIT_TEST))


class Guards(unittest.TestCase):
    def test_enable_greet_gates_the_row(self):
        source = read(EMOTE_STRATEGY)
        self.assertIn("if (sPlayerbotAIConfig.enableGreet)", source)
        self.assertIn('NextAction("greet", 1.0f)', source)

    def test_greet_action_keeps_the_cooldown_and_the_pool_gate(self):
        source = code_only(read(GREET))
        self.assertIn("!ai->HasRealPlayerMaster() && !sPlayerbotAIConfig.randomBotGreet", source)
        self.assertIn("now - lastGreet->second < sPlayerbotAIConfig.greetCooldown", source)
        self.assertIn("AI_VALUE(ObjectGuid, \"new player nearby\")", source)


class Config(unittest.TestCase):
    def test_the_code_fallbacks_are_the_tb_defaults(self):
        source = read(CONFIG_CPP)
        self.assertIn('GetBoolDefault("AiPlayerbot.EnableGreet", true)', source)
        self.assertIn('GetIntDefault("AiPlayerbot.GreetCooldown", 600)', source)
        self.assertIn('GetBoolDefault("AiPlayerbot.RandomBotGreet", false)', source)

    def test_the_config_ships_those_defaults(self):
        conf = read(CONF)
        for key, value, _type in CONFIG:
            self.assertRegex(conf, r"(?m)^%s = %s$" % (re.escape(key), re.escape(value)))

    def test_the_key_doc_says_wired_to_the_reader(self):
        doc = read(CONFIG_DOC)
        for key, value, _type in CONFIG:
            row = re.search(r"(?m)^\| `%s` \| `%s` \| \w+ \| yes \| (.+?) \|$" % (re.escape(key), re.escape(value)), doc)
            self.assertIsNotNone(row, "%s is not documented as wired with %s" % (key, value))
            self.assertTrue(any(reader in row.group(1) for reader in (GREET, EMOTE_STRATEGY)),
                            "%s names the wrong reader: %s" % (key, row.group(1)))


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
"""Chat grammar A wiring (round R4 of PLAYER-COMMANDS-SPEC): the chat words whose action is already compiled reach the bot.

No build, no network, no core. Everything here is text: the key block in the generator, the generated registry and the legacy
strategy that names the actions, plus the build list that must compile every new action's source.

Run:
  python3 -m unittest forever/tools/test_chat_grammar_wiring.py
  python3 forever/tools/test_chat_grammar_wiring.py --mutate   # must FAIL (exit non-zero)

What it proves (each is a bug a green build does not catch):
  * the generator has a PC_CHAT_KEYS block and KEYS joins it (a block nothing joins registers nothing);
  * the block holds exactly the spec's words: 29 chat triggers and the 5 action keys their classes need (`buy back`, `corpse run`,
    `equip`, `free chat shortcut`, `position`), so a later round cannot silently drop one;
  * every one of those triggers is registered in the generated slice registry — today `repair` has an action but no trigger, so the
    bot repairs its gear on its own schedule but never on the master's word (the first failing case below);
  * for every one of those triggers, each action its TriggerNode in ChatCommandHandlerStrategy.cpp names is a registered action key
    (through the pass-through rule for supported words: trigger word -> action of the same name); a trigger whose action is missing
    fires into the missing-key stand-in and the word does nothing;
  * every new action's source is in forever/SLICE-W4.txt: a registered class whose .cpp is not built links nowhere or, worse, links
    the header's inline copy against an unbuilt implementation.
"""
import os
import re
import sys
import unittest

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))

GEN = "forever/tools/slice_gen_context.py"
CONTEXT = "forever/src/slice/SliceAiObjectContext.cpp"
STRATEGY = "ai/playerbot/strategy/generic/ChatCommandHandlerStrategy.cpp"
BUILD_LIST = "forever/SLICE-W4.txt"

# PLAYER-COMMANDS-SPEC section 6, R4: the words whose action (or trigger) is already registered in the slice.
R4_TRIGGERS = ["quest reward", "drop", "roll", "share", "loot", "add all loot", "release", "repair", "u", "use", "s", "b", "trainer",
               "attack rti", "pull", "end pull", "accept", "follow target", "revive", "mail", "ah", "ah bid", "ah cancel",
               # triggers plus actions whose class is already compiled
               "corpse run", "e", "equip", "bb", "free", "position"]
# The 5 action keys of the second group. The first group's actions are registered by earlier rounds.
R4_ACTIONS = ["buy back", "corpse run", "equip", "free chat shortcut", "position"]
# The legacy class each of those action keys creates and the source that defines it (the build list must name it).
R4_ACTION_SOURCES = {
    "buy back": "ai/playerbot/strategy/actions/BuyAction.cpp",
    "corpse run": "ai/playerbot/strategy/actions/ReleaseSpiritAction.cpp",   # inline in ReleaseSpiritAction.h
    "equip": "ai/playerbot/strategy/actions/EquipAction.cpp",
    "free chat shortcut": "ai/playerbot/strategy/actions/ChatShortcutActions.cpp",
    "position": "ai/playerbot/strategy/actions/PositionAction.cpp",
}

KINDS = ("strategy", "action", "trigger", "value")
BASE_KIND = {"Strategy": "strategy", "Action": "action", "Trigger": "trigger", "UntypedValue": "value"}


def read(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8", errors="replace") as f:
        return f.read()


def paren_body(text, start):
    """text[start] is just after '(': return (the body, the index after the matching ')')."""
    return brace_body(text, start, "(", ")")


def brace_body(text, start, opening="(", closing=")"):
    """text[start] is just after `opening`: return (the body, the index after the matching `closing`)."""
    depth = 1
    i = start
    while i < len(text):
        if text[i] == opening:
            depth += 1
        elif text[i] == closing:
            depth -= 1
            if depth == 0:
                return text[start:i], i + 1
        i += 1
    raise AssertionError("unbalanced %s%s" % (opening, closing))


def registered(context_text):
    """kind -> set of creator keys of the generated slice registry."""
    reg = {k: set() for k in KINDS}
    for m in re.finditer(r"class (Slice\w+) : public NamedObjectContext<(\w+)>", context_text):
        kind = BASE_KIND.get(m.group(2))
        if kind is None:
            raise AssertionError("generated context %s has an unknown base type %s" % (m.group(1), m.group(2)))
        body, _ = brace_body(context_text, context_text.index("{", m.end()) + 1, "{", "}")
        reg[kind].update(re.findall(r'creators\s*\[\s*"((?:[^"\\]|\\.)*)"\s*\]\s*=', body))
    return reg


def pc_chat_keys(gen_text):
    """kind -> list of keys of the generator's PC_CHAT_KEYS block."""
    m = re.search(r"^PC_CHAT_KEYS = \{", gen_text, re.M)
    if not m:
        raise AssertionError("%s has no PC_CHAT_KEYS block: the R4 words register nowhere" % GEN)
    end = gen_text.index("\n}", m.end())
    body = gen_text[m.end():end]
    out = {k: [] for k in KINDS}
    seen = re.findall(r'"(\w+)":\s*((?:"(?:[^"\\]|\\.)*"\s*)+)', body)
    for kind, quoted in seen:
        if kind not in out:
            raise AssertionError("PC_CHAT_KEYS has an unknown entry %r" % kind)
        value = "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', quoted))
        out[kind] = [x.strip() for x in value.split(",") if x.strip()]
    if not any(out.values()):
        raise AssertionError("PC_CHAT_KEYS is empty")
    return out


def supported_words(strategy_text):
    return set(re.findall(r'supported\.push_back\(\s*"([^"]+)"\s*\)', strategy_text))


def trigger_nodes(strategy_text):
    """trigger name -> list of action names its explicit TriggerNode fires (InitReactionTriggers)."""
    out = {}
    for m in re.finditer(r"triggers\.push_back\(\s*new TriggerNode\(", strategy_text):
        args, _ = paren_body(strategy_text, m.end())
        name = re.match(r'\s*"([^"]+)"', args)
        if not name:
            raise AssertionError("a TriggerNode does not start with a name: %r" % args[:60])
        out.setdefault(name.group(1), []).extend(re.findall(r'new NextAction\(\s*"([^"]+)"', args))
    return out


def actions_of(key, nodes, supported):
    """The actions a chat word fires: its explicit TriggerNode's, else the pass-through action of its own name, else none.

    A word with no TriggerNode (`use`: the strategy only has the `u` node) is registration only, exactly as in the full build.
    """
    if key in nodes:
        return nodes[key]
    if key in supported:
        return [key]
    return []


def check(gen_text, strategy_text, context_text, build_list):
    block = pc_chat_keys(gen_text)
    join = re.search(r"^KEYS = _join\(([^)]*)\)", gen_text, re.M)
    if not join:
        raise AssertionError("%s has no KEYS = _join(...) line" % GEN)
    if "PC_CHAT_KEYS" not in join.group(1):
        raise AssertionError("KEYS does not join PC_CHAT_KEYS: the block would never be registered")

    # the block is exactly this round's words (a later round starts its own block)
    if sorted(block["trigger"]) != sorted(R4_TRIGGERS):
        missing = [k for k in R4_TRIGGERS if k not in block["trigger"]]
        extra = [k for k in block["trigger"] if k not in R4_TRIGGERS]
        raise AssertionError("PC_CHAT_KEYS triggers differ from the spec: missing %s, extra %s" % (missing, extra))
    if sorted(block["action"]) != sorted(R4_ACTIONS):
        missing = [k for k in R4_ACTIONS if k not in block["action"]]
        extra = [k for k in block["action"] if k not in R4_ACTIONS]
        raise AssertionError("PC_CHAT_KEYS actions differ from the spec: missing %s, extra %s" % (missing, extra))
    if block["strategy"] or block["value"]:
        raise AssertionError("PC_CHAT_KEYS registers a strategy or a value: R4 adds triggers and actions only")

    reg = registered(context_text)
    for key in block["action"]:
        if key not in reg["action"]:
            raise AssertionError("action %r is in PC_CHAT_KEYS but not in the generated registry" % key)
    for key in block["trigger"]:
        if key not in reg["trigger"]:
            raise AssertionError("chat trigger %r is in PC_CHAT_KEYS but not in the generated registry "
                                 "(the word reaches the missing-key stand-in and does nothing)" % key)

    # every action the legacy strategy fires for one of those words must be a registered action key
    supported = supported_words(strategy_text)
    nodes = trigger_nodes(strategy_text)
    for key in block["trigger"]:
        for action in actions_of(key, nodes, supported):
            if action not in reg["action"]:
                raise AssertionError("chat trigger %r fires action %r, which the slice does not register" % (key, action))

    lines = set(l.strip() for l in build_list.splitlines() if l.strip() and not l.strip().startswith("#"))
    for key in block["action"]:
        src = R4_ACTION_SOURCES.get(key)
        if src and src not in lines:
            raise AssertionError("action %r is defined in %s, which %s does not list" % (key, src, BUILD_LIST))


class Grammar(unittest.TestCase):
    def setUp(self):
        self.gen = read(GEN)
        self.strategy = read(STRATEGY)
        self.context = read(CONTEXT)
        self.build_list = read(BUILD_LIST)

    def test_repair_trigger_registered(self):
        # The first failing case: `repair` has an action (W2) but no trigger, so the master's word does nothing.
        self.assertIn("repair", registered(self.context)["trigger"],
                      "the `repair` chat word has no trigger in the slice registry (its action alone never fires)")

    def test_r4_block_and_generated_registry(self):
        check(self.gen, self.strategy, self.context, self.build_list)

    def test_every_r4_word_is_reachable(self):
        # one assertion per word, so a failure names the word that is missing rather than the first one
        reg = registered(self.context)
        words = supported_words(self.strategy)
        nodes = trigger_nodes(self.strategy)
        for key in R4_TRIGGERS:
            with self.subTest(word=key):
                self.assertIn(key, reg["trigger"], "chat trigger %r is not registered" % key)
                for action in actions_of(key, nodes, words):
                    self.assertIn(action, reg["action"], "trigger %r fires unregistered action %r" % (key, action))

    def test_mutations_turn_it_red(self):
        base = (self.gen, self.strategy, self.context, self.build_list)
        block = pc_chat_keys(self.gen)
        mutations = [
            # a word dropped from the block (mutation 13 of the spec)
            (0, self.gen.replace('release, repair, u, use', 'release, u, use', 1)),
            # an action dropped from the block
            (0, self.gen.replace('"buy back, ', "", 1)),
            # the block no longer joined into KEYS
            (0, self.gen.replace(", CH_KEYS, PC_CHAT_KEYS,", ", CH_KEYS,")),
            # the block gone
            (0, re.sub(r"^PC_CHAT_KEYS = \{.*?\n\}\n", "", self.gen, count=1, flags=re.M | re.S)),
            # a new action's source not built
            (3, self.build_list.replace("ai/playerbot/strategy/actions/EquipAction.cpp\n", "")),
            # a word's action creator gone from the generated registry
            (2, self.context.replace('creators["position"] = [](PlayerbotAI* ai) { return new PositionAction(ai); };', "", 1)),
            # a word's trigger creator gone from the generated registry
            (2, self.context.replace('creators["repair"] = [](PlayerbotAI* ai) { return new ChatCommandTrigger(ai, "repair"); };', "", 1)),
        ]
        for i, (slot, mutated) in enumerate(mutations):
            args = list(base)
            self.assertNotEqual(mutated, args[slot], "mutation %d changed nothing" % i)
            args[slot] = mutated
            with self.assertRaises(AssertionError, msg="mutation %d was not caught" % i):
                check(*args)
        self.assertTrue(block["trigger"], "the block parsed empty")


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        check(read(GEN), read(STRATEGY),
              read(CONTEXT).replace('creators["position"] = [](PlayerbotAI* ai) { return new PositionAction(ai); };', ""),
              read(BUILD_LIST))
        sys.exit(0)
    unittest.main()

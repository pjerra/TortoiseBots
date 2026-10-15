"""`.bot cmd gossip` is gated by an allowlist and uses the core's own gossip path (issue #1012).

The hello alone runs the NPC's scripts (OnGossipHello, a menu's script_id, a quest
window), so nothing may reach the NPC unless its entry is in
AiPlayerbot.BotGossipNpcs (empty by default: off). These checks read the code's
structure (comments stripped): where the only gossip packets are sent, and that
the allowlist refusal comes before them on every path.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


def strip_comments(text):
    """C++ source without // and /* */ comments; string and char literals kept."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif text.startswith("//", i):
            i = text.find("\n", i)
            i = n if i < 0 else i
        elif text.startswith("/*", i):
            i = text.index("*/", i) + 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


COMMANDS = strip_comments((ROOT / "commands/BotCommands.cpp").read_text())
CONFIG = strip_comments((ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text())
TEMPLATE = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()


def function(text, signature):
    """The body of the one definition that starts with signature (brace matched)."""
    starts = [m.start() for m in re.finditer(re.escape(signature), text)]
    defs = [s for s in starts if 0 <= text.find("{", s) < (text.find(";", s) if text.find(";", s) >= 0 else len(text))]
    assert len(defs) == 1, (signature, len(defs))
    i = text.index("{", defs[0])
    depth = 0
    for j in range(i, len(text)):
        depth += {"{": 1, "}": -1}.get(text[j], 0)
        if depth == 0:
            return text[i:j + 1]
    raise AssertionError("unbalanced " + signature)


def guard(body, condition):
    """(start, statement) of the `if (` whose condition holds `condition`, up to the end of its body."""
    i = body.index(condition)
    start = body.rindex("if (", 0, i)
    rest = body[i:]
    if re.match(r"[^;{]*\)\s*\{", rest):
        end = body.index("}", i) + 1
    else:
        end = body.index(";", body.index(")", i)) + 1
    return start, body[start:end]


def squash(text):
    return re.sub(r"\s+", " ", text).strip()


class GossipOrderWiringTest(unittest.TestCase):
    def test_gossip_packets_are_sent_in_one_place(self):
        act = function(COMMANDS, "ActStart BeginGossipAct(")
        for call in ("HandleGossipHelloOpcode(", "HandleGossipSelectOptionOpcode(", "PlayerTalkClass->ClearMenus("):
            self.assertEqual(COMMANDS.count(call), 1, call)
            self.assertEqual(act.count(call), 1, call)
        # BeginGossipAct runs only as the act of a gossip task: its definition and one call.
        self.assertEqual(len(re.findall(r"\bBeginGossipAct\(", COMMANDS)), 2)
        dispatch = function(COMMANDS, "ActStart BeginTaskAct(")
        self.assertRegex(dispatch, r"TaskKind::Gossip\)\s*start = BeginGossipAct\(task, bot, target\);")

    def test_act_refuses_unlisted_npc_before_anything_reaches_it(self):
        act = function(COMMANDS, "ActStart BeginGossipAct(")
        start, statement = guard(act, "GossipOrder::IsAllowedNpc(sPlayerbotAIConfig.botGossipNpcs, npc->GetEntry())")
        self.assertRegex(squash(statement),
            r"^if \(npc && !GossipOrder::IsAllowedNpc\(.*\)\) return RefuseAct\(task, ActStart::Failed, GossipOrder::kCodeNotAllowed,")
        # The gate is the first statement after the npc is read; the hello comes after it.
        self.assertEqual(squash(act[:start]), "{ Creature* npc = target.creature;")
        self.assertLess(start, act.index("HandleGossipHelloOpcode("))

    def test_order_refuses_unlisted_npc_before_search_and_walk(self):
        order = function(COMMANDS, "static bool CommanderGossip(")
        start, statement = guard(order, "GossipOrder::IsAllowedNpc(sPlayerbotAIConfig.botGossipNpcs, request.entry)")
        self.assertRegex(squash(statement),
            r"^if \(!GossipOrder::IsAllowedNpc\(.*\)\) \{ SendActionError\(handler, intent, GossipOrder::kCodeNotAllowed, .*\); return true; \}$")
        self.assertEqual(squash(order[:start]), "{")
        for later in ("CommanderTaskGuard(", "FindGossipNpc(", "LaunchCommanderTask("):
            self.assertLess(start, order.index(later), later)

    def test_order_is_dispatched(self):
        handler = function(COMMANDS, "static bool HandleCommanderOrder(")
        self.assertRegex(handler, r'verb == "gossip"\)\s*\{\s*parsed = GossipOrder::ParseRequest\(rest, gossipRequest\);')
        self.assertRegex(handler, r'if \(verb == "gossip"\)\s*return CommanderGossip\(handler, order, bot, gossipRequest, intent\);')

    def test_walk_follows_a_moving_npc(self):
        step = function(COMMANDS, "bool StepCommanderTask(")
        wait = step[step.index("case RemoteCommander::TaskStep::Wait:"):step.index("case RemoteCommander::TaskStep::BeginAct:")]
        self.assertLess(wait.index("FollowGossipNpc(task, bot, ai, target.creature);"), wait.index("NudgeTaskWalk(task, bot, diff);"))
        follow = function(COMMANDS, "void FollowGossipNpc(")
        self.assertIn("npc->GetPositionX(), npc->GetPositionY(), npc->GetPositionZ()", follow)
        self.assertLess(follow.index("RemoteCommander::StandMoved(task.stand, stand)"), follow.index("task.stand = stand;"))

    def test_npc_search_is_bounded_and_living(self):
        fn = function(COMMANDS, "uint64 FindGossipNpc(")
        self.assertIn("Cell::VisitGridObjects(reference.x, reference.y, map, searcher, GossipOrder::kSearchYards);", fn)
        check = function(COMMANDS, "class EntryLivingCreatureCheck")
        self.assertIn("creature->IsAlive()", check)

    def test_allowlist_is_off_by_default(self):
        self.assertIn('config.GetStringDefault("AiPlayerbot.BotGossipNpcs", "")', CONFIG)
        lines = [l.rstrip() for l in TEMPLATE.splitlines() if l.lstrip("#").strip().startswith("AiPlayerbot.BotGossipNpcs")]
        # The example line shows the value that runs (empty); the opt-in is only described.
        self.assertEqual(lines, ["#AiPlayerbot.BotGossipNpcs ="])
        self.assertIn("# To allow both: AiPlayerbot.BotGossipNpcs = 7607,7604", TEMPLATE)


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
"""Wiring pins and mutation test for issue #15: `.tb cmd place|rally|fallback|resume`, the chat form and the cursor ping.

Run:
  python3 forever/tools/test_place_wiring.py
  PLACE_MUTATE=1 python3 forever/tools/test_place_wiring.py
"""
import os
import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SRC = ROOT / "forever" / "src"
CMD = SRC / "host" / "cmd"


def read(path):
    return Path(path).read_text()


def strip_comments(source):
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
    return re.sub(r"//[^\n]*", "", source)


class Wiring(unittest.TestCase):
    def setUp(self):
        self.place = read(CMD / "TbPlace.cpp")
        self.place_h = read(CMD / "TbPlace.h")
        self.pure = read(CMD / "TbPlacePure.h")
        self.cmd_orders = read(CMD / "TbCmdOrders.cpp")
        self.scripts = read(SRC / "TbScripts.cpp")
        self.test_pure = read(ROOT / "forever" / "tools" / "test_place_pure.cpp")
        self.plan = read(CMD / "TbPlacePlan.h")
        self.test_plan = read(ROOT / "forever" / "tools" / "test_place_plan.cpp")
        self.orders = read(SRC / "host" / "TbOrders.cpp")
        self.orders_api = read(SRC / "host" / "TbOrdersApi.h")

    # ---- the geometry is pure and the host only reads the world ----

    def test_geometry_lives_in_the_pure_header(self):
        pure = strip_comments(self.pure)
        for name in ("LayoutShape(", "FrameFor(", "FallbackSlots(", "ParseMinimapPing(", "SortByRank(", "Selected("):
            self.assertIn(name, pure, f"{name} must live in TbPlacePure.h")
        # The pure header names no core type: it stays compilable by the standalone unit test.
        for line in pure.splitlines():
            if line.strip().startswith("#include"):
                self.assertNotIn('"', line, f"TbPlacePure.h includes a quoted header: {line.strip()}")
        host = strip_comments(self.place)
        plan = strip_comments(self.plan)
        self.assertIn("LayoutShape(", plan)
        self.assertIn("FrameFor(", plan)
        self.assertIn("PlanPlace(", host)
        self.assertIn("PlanFallback(", host)
        self.assertNotIn("std::sin(2.0f", host)

    def test_every_shape_and_gap_is_pinned(self):
        pure = strip_comments(self.pure)
        for word in ("close", "circle", "arc", "spread", "line", "column", "spear", "arrow"):
            self.assertIn(f'return "{word}"', pure, f"shape word {word} missing")
        for value in ("kTightYd = 2.5f", "kNormalYd = 4.0f", "kWideYd = 8.0f", "kMaxRadiusYd = 35.0f",
                      "kSpreadGapFactor = 2.5f", "kHereClearYd", "kCursorWaitSeconds = 30", "kCommanderClearYd = 1.5f"):
            self.assertIn(value, pure, f"pure constant {value} missing")
        # The unit test covers every shape, the gap rules, the ring floor, the 35 yd ceiling, the slot order and the ping layout.
        test = strip_comments(self.test_pure)
        for name in ("TestWords", "TestLayouts", "TestFrames", "TestRanksAndWho", "TestFallback", "TestMinimapPing"):
            self.assertIn(f"static void {name}()", test)
        # The behaviour test drives the words `.tb cmd` hands over and checks who moved and to which slot.
        behaviour = strip_comments(self.test_plan)
        for name in ("TestCommandWords", "TestPlaceCircle", "TestPlaceRefusalsArePerBot", "TestCommanderSpot",
                     "TestFallbackFront", "TestHoldBookkeeping", "TestClock"):
            self.assertIn(f"static void {name}()", behaviour)
        for words in ('{ "fall", "back" }', '{ "place", "circle", "target" }', '{ "rally", "here" }', '{ "fallback" }'):
            self.assertIn(words, behaviour, f"the behaviour test must drive {words}")

    # ---- the world half ----

    def test_ownership_uses_the_commander_check(self):
        host = strip_comments(self.place)
        self.assertIn("ResolveBinding(", host)
        self.assertIn("IsCommandedBy(commander, bot)", host)
        self.assertIn("resolves the commander's own bots", ' '.join(self.place_h.split()))

    def test_slots_are_snapped_and_skipped(self):
        host = strip_comments(self.place)
        self.assertIn("GetHeightCompat(", host)
        self.assertIn("PathCalculateCompat(", host)
        self.assertIn("PathActualEndCompat(", host)
        self.assertIn("PathPointsCompat(", host)
        plan = strip_comments(self.plan)
        for code in ("no-ground", "unreachable", "too-far", "other-map", "dead", "no-slot"):
            self.assertIn(f'"{code}"', plan, f"per-slot refusal {code} missing")
        # A slot with no floor or no path is skipped, never queued: the plan decides it before any order is made.
        self.assertIn("probe.ground(", plan)
        self.assertIn("probe.reachable(", plan)
        self.assertRegex(plan, r'if \(!probe\.ground')
        self.assertIn("QueueCome(bot", host)

    def test_orders_are_the_modules_own_come_and_follow(self):
        host = strip_comments(self.place)
        self.assertIn('spec.verb = "come";', host)
        self.assertIn('spec.verb = "follow";', host)
        self.assertIn("h3::QueueOrder(", host)
        # No direct QueueOrder bypass: the pre-flight runs inside it (TbOrders.cpp).
        self.assertNotIn("h3::PreflightOrder(", host)

    def test_cursor_ping_is_read_only(self):
        host = strip_comments(self.place)
        self.assertIn("CMSG_MINIMAP_PING", host)
        self.assertIn("ParseMinimapPing(packet.data(), packet.size()", host)
        for mutation in ("SetOpcode", "packet.put", "packet >>", "packet.Clear", "packet <<", "packet.append"):
            self.assertNotIn(mutation, host, f"the ping packet must never be changed ({mutation})")
        self.assertIn("kCursorWaitSeconds", host)
        self.assertIn("Memory().pendings", host)
        # A malformed ping never eats the waiting request: the pending is taken only after the packet is valid.
        self.assertRegex(host, r'ParseMinimapPing\(packet\.data\(\), packet\.size\(\), point\)[\s\S]*?Memory\(\)\.pendings\.erase')

    def test_walk_back_and_fall_back_ends(self):
        host = strip_comments(self.place)
        self.assertIn("kWalkBackEveryMs", host)
        # Engine-facing file: after compat/botpch_forever.h the core's TC_LOG_* cannot compile (its sLog is the legacy proxy; see TbEngineBrain.cpp).
        self.assertNotIn("TC_LOG_", host)
        self.assertIn("sLog.outString(", host)
        self.assertIn("kSlotToleranceYd", host)
        self.assertIn("void NoteOrdered(", host)
        self.assertRegex(host, r'verb == "attack" \|\| verb == "pull" \|\| verb == "pullback" \|\| verb == "resume"')
        self.assertRegex(host, r'if \(verb == "follow"\)')
        # A newer order that left the bot off its stay releases the row too, so `.tb order ... follow` cannot leave a stale slot behind.
        self.assertIn("last.seq > row.seq", host)
        self.assertIn('ai->HasStrategy("stay"', host)
        # The walk back keeps its order number (ReleasePlaceStay matches it), and a dropped row's follow yields to a newer order.
        self.assertIn("AfterWalkBack(row, seq, now)", host)
        self.assertRegex(host, r"if \(NewerOrderOwns\(h3::LastOrder\(bot->GetGUID\(\)\)\.seq, row\.seq\)\)\s*return;")
        # A run owns its members: a hold never walks them (its `come` would read as an outside order).
        self.assertIn("bool InRun(Player* bot)", host)
        self.assertIn("if (InRun(bot))", host)
        # The walk back re-checks the path and gives up after three failures, saying so once.
        self.assertIn("if (!Reachable(bot, row.x, row.y, row.z))", host)
        self.assertIn("WalkGiveUp(row.walkFails)", host)
        self.assertIn("TortoiseBots place: giving up %s's slot after %u failed walk-backs", host)
        # Every terminal row releases the bot (follow while ours; only the placement's stay once it changed commander).
        self.assertIn("ReleaseRow(row, ReleaseOn(end, ours))", host)
        self.assertIn("h3::ReleasePlaceStay(bot, row.seq)", host)
        self.assertIn("bool ReleasePlaceStay(Player* bot, uint32 seq);", strip_comments(self.orders_api))
        self.assertIn("bool ReleasePlaceStay(Player* bot, uint32 seq)", strip_comments(self.orders))
        # Resume re-checks the ownership of every historical row before it queues an order.
        self.assertIn("h3::CommanderGuid(bot->GetGUID()) != commander->GetGUID()", host)
        self.assertIn("!cmd::IsCommandedBy(commander, bot)", host)
        # The 1 Hz tick uses the wrap-safe clock, not an int64 wall comparison.
        self.assertIn("static uint32 nextMs", host)
        self.assertIn("TimeReached(now, nextMs)", host)
        self.assertNotIn("static int64 nextMs", host)
        # A later command from the commander cancels a pending cursor request (`cancelled`, not silence).
        self.assertIn("void CancelPending(ObjectGuid commander, ChatHandler* handler)", host)
        self.assertIn('"cancelled"', host)
        self.assertIn("place::CancelPending(sender->GetGUID(), handler)", strip_comments(self.orders))
        self.assertIn("place::CancelPending(sender->GetGUID(), handler)", strip_comments(self.cmd_orders))
        # Logout forgets the commander's place state.
        self.assertIn("TortoiseBots::place::NoteLogout(player)", strip_comments(self.scripts))
        self.assertIn("Forget(Memory()", host)

    # ---- the verbs and their replies ----

    def test_cmd_routes_the_place_verbs(self):
        c = strip_comments(self.cmd_orders)
        # The words go over as typed, verb included: slicing the verb here made `.tb cmd fall back` a `back` and `place circle` no place at all.
        route = c.find("place::Handle(handler, args)")
        judge = c.find("run::Issue(party, order")
        self.assertGreaterEqual(route, 0, "TbCmdOrders.cpp does not route the whole command to place::Handle")
        self.assertGreaterEqual(judge, 0)
        self.assertLess(route, judge, "place must be answered before the lifted order judge")
        self.assertNotIn("args.w.begin() + 1", c, "the verb must never be sliced off before place::Handle")
        self.assertIn("place::CancelPending(sender->GetGUID(), handler)", c)
        self.assertIn("place::NoteOrdered(sender->GetGUID(), verb)", c)
        self.assertIn("place, rally, fallback", c)
        # The one parse both surfaces use: the pure header, not a second dispatcher in the host.
        host = strip_comments(self.place)
        self.assertIn("ParseCommand(args.w)", host)
        self.assertIn("ChatWordsAreCommand(args.w)", host)
        self.assertIn("IsPlaceVerbWord(", host)
        plan = strip_comments(self.plan)
        self.assertIn("inline Command ParseCommand(", plan)
        self.assertIn("inline bool ChatWordsAreCommand(", plan)

    def test_each_verb_is_dispatched(self):
        plan = strip_comments(self.plan)
        for word in ('"place"', '"rally"', '"fallback"', '"back"', '"resume"', '"fall"'):
            self.assertIn(word, plan, f"verb {word} not parsed")
        self.assertIn('Lower(words[1]) != "back"', plan)
        self.assertIn('Lower(words[1]) != "here"', plan)
        host = strip_comments(self.place)
        self.assertIn("CommandKind::PlaceFollow", host)
        self.assertIn("no-fallback", host)

    def test_reply_format(self):
        host = strip_comments(self.place)
        # Grammar v2: markers named after the command, one record kind, key=value words, the token last.
        self.assertIn('h3::Reply reply(handler, args, "cmd")', host)
        for key in ('"verb"', '"status"', '"bots"', '"shape"', '"gap"', '"anchor"', '"radius"', '"slots"',
                    '"slot"', '"seq"', '"placed"', '"refused"', '"released"', '"rally"', '"from"', '"wait"',
                    '"pending"', '"lead"'):
            self.assertIn(key, host, f"reply key {key} missing")
        self.assertIn('.Coord("pos"', host)
        for code in ("bad-target", "no-selection", "no-bots", "not-bound", "needs-player-sender", "timeout",
                     "cancelled", "no-fallback", "bad-token"):
            self.assertIn(f'"{code}"', host, f"typed error {code} missing")
        # The word-level refusals are the pure parse's, shared with the chat form.
        plan = strip_comments(self.plan)
        for code in ("bad-argument", "duplicate", "missing-argument", "unexpected-argument"):
            self.assertIn(f'"{code}"', plan, f"typed error {code} missing from the parse")
        # Free text is never sent as a bare word: every message goes through T (cut and encoded).
        self.assertIn('T("msg"', host)

    def test_chat_form(self):
        host = strip_comments(self.place)
        self.assertIn("bool HandleChat(Player* sender, std::string const& text)", host)
        self.assertIn("bool IsPlaceText(std::string const& text)", host)
        self.assertIn("ChatWordsAreCommand(", host)
        self.assertIn("cmd::ResolveBinding(sender->GetGUID()", host)
        scripts = strip_comments(self.scripts)
        self.assertIn("void OnChatSent(Player* player", scripts)
        self.assertIn("void OnChat(Player* player", scripts)
        self.assertIn("TortoiseBots::place::HandleChat(player, msg)", scripts)
        self.assertIn("TortoiseBots::place::OnPacket(session, packet)", scripts)
        self.assertIn("TortoiseBots::place::Update()", scripts)
        self.assertIn("class tortoisebots_server : public ServerScript", scripts)
        self.assertIn("new tortoisebots_server()", scripts)


# Each pin is the check the wiring test makes on one file. The mutation self-test runs the pin on the real source (it must hold) and on the
# mutant (it must fail): a pin that holds on both is vacuous and fails the self-test.
def route_pin(src):
    c = strip_comments(src)
    route = c.find("if (place::Handle(handler, args))")
    judge = c.find("run::Issue(party, order")
    return route >= 0 and judge >= 0 and route < judge and "args.w.begin() + 1" not in c


def ping_pin(src):
    return "packet.Clear" not in strip_comments(src)


def walk_back_seq_pin(src):
    return re.search(r"AfterWalkBack\(HoldRow& row, uint32_t seq, uint32_t now\)\s*\{\s*row\.walkMs = now;\s*row\.walkFails = 0;\s*row\.seq = seq;",
                     strip_comments(src)) is not None


def follow_guard_pin(src):
    return re.search(r"if \(NewerOrderOwns\(h3::LastOrder\(bot->GetGUID\(\)\)\.seq, row\.seq\)\)\s*return;", strip_comments(src)) is not None


MUTANTS = [
    # (name, file, original text, mutant text, pin)
    ("place verbs fall through to the judge", CMD / "TbCmdOrders.cpp",
     "if (place::Handle(handler, args))", "if (false)", route_pin),
    ("ping packet written instead of read", CMD / "TbPlace.cpp",
     "ParseMinimapPing(packet.data(), packet.size()", "packet.Clear(), ParseMinimapPing(packet.data(), packet.size()", ping_pin),
    ("walk back leaves the row's order number stale", CMD / "TbPlacePlan.h",
     "    row.seq = seq;", "", walk_back_seq_pin),
    ("dropped row follows past a newer order", CMD / "TbPlace.cpp",
     "if (NewerOrderOwns(h3::LastOrder(bot->GetGUID()).seq, row.seq))", "if (false)", follow_guard_pin),
]


if __name__ == "__main__":
    if os.environ.get("PLACE_MUTATE") == "1":
        survivors = []
        for name, path, original, mutant, pin in MUTANTS:
            src = read(path)
            if src.count(original) != 1:
                print(f"ERROR: {name}: anchor text found {src.count(original)} times, expected 1", file=sys.stderr)
                sys.exit(1)
            if not pin(src):
                print(f"ERROR: {name}: the pin fails on the real source", file=sys.stderr)
                sys.exit(1)
            if pin(src.replace(original, mutant, 1)):
                survivors.append(name)
            else:
                print(f"MUTATION CAUGHT: {name}")
        if survivors:
            print(f"ERROR: mutations survived: {survivors}", file=sys.stderr)
            sys.exit(1)
        sys.exit(0)
    unittest.main()

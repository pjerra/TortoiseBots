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
        self.binding = read(CMD / "TbCmdBinding.cpp")

    # ---- the geometry is pure and the host only reads the world ----

    def test_geometry_lives_in_the_pure_header(self):
        pure = strip_comments(self.pure)
        for name in ("LayoutShape(", "FrameFor(", "FallbackSlots(", "ParseMinimapPing(", "SortByRank(", "Selected("):
            self.assertIn(name, pure, f"{name} must live in TbPlacePure.h")
        # The pure header names no core type: it stays compilable by the standalone unit test. The one quoted
        # include is the run's own pure liquid rule (no core header), so the path verdict asks the ONE rule.
        for line in pure.splitlines():
            if line.strip().startswith("#include"):
                self.assertTrue('"' not in line or "../run/TbRunLiquid.h" in line,
                                f"TbPlacePure.h includes a non-pure header: {line.strip()}")
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
        for name in ("TestWords", "TestLayouts", "TestFrames", "TestRanksAndWho", "TestFallback", "TestMinimapPing",
                     "TestGapYards", "TestWalkVerdict", "TestFaceOverride", "TestPointAnchor", "TestTankAnchor", "TestClearSpots"):
            self.assertIn(f"static void {name}()", test)
        # The behaviour test drives the words `.tb cmd` hands over and checks who moved and to which slot.
        behaviour = strip_comments(self.test_plan)
        for name in ("TestCommandWords", "TestPlaceCircle", "TestPlaceRefusalsArePerBot", "TestCommanderSpot",
                     "TestFallbackFront", "TestHoldBookkeeping", "TestClock", "TestLedgeSlotIsRefused",
                     "TestLiquidSlotIsNudged", "TestNudgeGoesInward", "TestPathVerdicts", "TestTankExcluded",
                     "TestFallbackRefusedStillDisengages", "TestFallbackCommanderValidated",
                     "TestComePolicy", "TestDisengageEnds", "TestFallbackCapSurvivesWalkBacks", "TestFallbackPassiveOwnership",
                     "TestWhisperFollowEndsTheHold", "TestWalkBackPassiveOwner", "TestLateWalkBackIsNotPassive",
                     "TestComePassiveReadsBeforeAdding",
                     "TestRallyAndFallbackWords", "TestRallyDecision", "TestStoredRallyFilter",
                     "TestTargetNudgeGuard", "TestCardHoldKeys"):
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
        # The floor is the core's own bounded vmap read, never the compat helper's 10,000 yd retry, and it starts high
        # enough to find a slot up to the ledge limit uphill.
        self.assertIn("GroundUnder(", host)
        self.assertIn("kMaxSlotStepYd", host)
        self.assertNotIn("GetHeightCompat(", host)
        self.assertNotIn("10000.0f", host)
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
        # The row's own `come` still waiting in the queue has not written its stay yet; any other loss of the stay (a newer order, or a whisper
        # shortcut that numbered nothing) ends the row (#316).
        self.assertIn("ownComeQueued = last.seq == row.seq && last.verb == \"come\" && last.result == \"queued\"", host)
        self.assertIn("RowStayGone(ai != nullptr, ai && ai->HasStrategy(\"stay\", BotState::BOT_STATE_NON_COMBAT), ownComeQueued)", host)
        self.assertIn("end = RowEnd::StayGone;", host)
        self.assertNotIn("last.seq > row.seq", host)
        # The walk back keeps its order number (ReleasePlaceStay matches it), and a dropped row's follow yields to a newer order.
        self.assertIn("AfterWalkBack(row, seq, now)", host)
        self.assertIn("if (!NewerOrderOwns(h3::LastOrder(bot->GetGUID()).seq, row.seq))", host)
        # A run owns its members: a hold never walks them (its `come` would read as an outside order).
        self.assertIn("bool InRun(Player* bot)", host)
        self.assertIn("if (InRun(bot))", host)
        # The walk back asks the SAME path verdict the placement used (liquid path and detour included) and gives up after
        # three failures, saying which rule refused.
        self.assertIn("PathVerdict const verdict = VerdictFor(bot, { row.x, row.y }, row.z);", host)
        self.assertNotIn("Reachable(", host)
        self.assertIn("WalkGiveUp(row.walkFails)", host)
        self.assertIn("failed walk-backs (%s)", host)
        # A fall back's passive hold ends at its spot, when the bot stops short in a fight, or at its cap; an attack/pull that ends the
        # hold clears its stay/passive, and a dropped row releases the passive on every exit path (cancel, death, logout, unbind).
        self.assertIn("ReleasePlaceDisengage(bot, row.seq)", host)
        self.assertIn("ReleasePlaceHold(bot, row.x, row.y, row.z)", host)
        # F1: every exit passes the row's own order number, and there is no unconditional clear left: only the passive the fall back
        # set itself is released. #316: the passive belongs to the order that set it (the fall back's `come`), never to a walk-back's newer
        # movement number - asking with that number found no record, "succeeded" and left the bot passive.
        self.assertEqual(host.count("ReleasePlacePassive(bot, PassiveOwner(row))"), 7)
        self.assertNotIn("ReleasePlacePassive(bot, row.seq)", host)
        self.assertNotIn("ReleasePlacePassive(bot);", host)
        self.assertIn("bool const passiveGone = h3::ReleasePlacePassive(bot, PassiveOwner(row));", host)
        self.assertIn("DisengageSettled(row, passiveGone);", host)
        self.assertNotIn("row.disengage = false;", host)                       # only DisengageSettled clears it
        self.assertTrue(passive_owner_pin(read(CMD / "TbPlacePlan.h")))
        # #316 follow-up: the record's owner is the row's token, kept by the walk back that runs; the release consumes it either way.
        self.assertTrue(passive_book_pin(read(CMD / "TbPlacePlan.h")))
        # #321 review: a walk back that runs after the 10 s cap carries the row's token and adds nothing; the come's passive step reads
        # "already passive" before it adds, so a bot the owner held passive keeps that record (a behavioural test covers both).
        self.assertTrue(come_passive_pin(read(CMD / "TbPlacePlan.h")))
        # F1b (live 11-24): the cap counts from the fall back's start, not from the last walk back (which restarts every 5 s while the
        # bot stands idle off its slot, so a cap on walkMs never fired and the passive lasted until `resume`).
        self.assertIn("RowDisengageEnds(row, distance, bot->IsInCombat(), bot->movespline->Finalized(), now)", host)
        self.assertNotIn("now - row.walkMs", host)
        self.assertIn("StartRowClock(row, now);", host)
        self.assertTrue(fallback_cap_pin(read(CMD / "TbPlacePlan.h")))
        self.assertIn("if (row.disengage)", host)
        # Only a placement's own `come` takes the combat-time behaviour; a run's walk, a rally and a plain come reset as on tbf/main.
        orders = strip_comments(self.orders)
        self.assertIn("PlanCome(o.placed, o.disengage, bot->IsInCombat())", orders)
        self.assertIn("if (come.reset)", orders)
        self.assertIn("if (come.keepCombatStayOut)", orders)
        self.assertIn("if (come.passive)", orders)
        self.assertIn("bool placed = false;", strip_comments(self.orders_api))
        self.assertNotIn("else if (!bot->IsInCombat())", orders)
        self.assertIn("bot->AttackStop();", orders)
        self.assertIn("bot->InterruptNonMeleeSpells(false);", orders)
        self.assertIn('ChangeStrategy("+passive"', orders)
        self.assertIn("place::ApplyComePassive(engine, book, o.seq, o.passiveToken)", orders)
        self.assertIn("place::PassiveBook* book = nullptr;", orders)
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
        self.assertIn('word == "clear"', plan)
        host = strip_comments(self.place)
        self.assertIn("CommandKind::PlaceFollow", host)
        self.assertIn("no-fallback", host)

    def test_reply_format(self):
        host = strip_comments(self.place)
        # Grammar v2: markers named after the command, one record kind, key=value words, the token last.
        self.assertIn('h3::Reply reply(handler, args, "cmd")', host)
        for key in ('"verb"', '"status"', '"bots"', '"shape"', '"gap"', '"anchor"', '"radius"', '"slots"',
                    '"slot"', '"seq"', '"placed"', '"refused"', '"released"', '"rally"', '"from"', '"wait"',
                    '"pending"', '"lead"', '"face"', '"at"', '"anchor_bot"', '"shown"', '"set"', '"cleared"'):
            self.assertIn(key, host, f"reply key {key} missing")
        self.assertIn('.Coord("pos"', host)
        for code in ("bad-target", "no-selection", "no-bots", "not-bound", "needs-player-sender", "timeout",
                     "cancelled", "no-fallback", "bad-token", "no-tank"):
            self.assertIn(f'"{code}"', host, f"typed error {code} missing")
        # The word-level refusals are the pure parse's, shared with the chat form.
        plan = strip_comments(self.plan)
        for code in ("bad-argument", "duplicate", "missing-argument", "unexpected-argument"):
            self.assertIn(f'"{code}"', plan, f"typed error {code} missing from the parse")
        # Free text is never sent as a bare word: every message goes through T (cut and encoded).
        self.assertIn('T("msg"', host)

    def test_terrain_rules_are_the_run_rules(self):
        host = strip_comments(self.place)
        pure = strip_comments(self.pure)
        # The stand rule and the move rule are the module's ONE liquid rules, not a second copy. The move rule is asked
        # ONCE over the whole path (PathPrefixRefused), never per leg.
        self.assertIn("run::liquid::PointRefusedOnMap(", host)
        self.assertIn("run::liquid::PathPrefixRefused(", pure)
        self.assertNotIn("SegmentPrefixRefusedOnMap(", host)
        self.assertIn("WalkVerdict(", host)
        self.assertIn("IsDetour(", pure)
        self.assertIn("PathVerdict::LiquidPath", pure)
        self.assertIn("PathVerdict::Detour", pure)
        plan = strip_comments(self.plan)
        for code in ("ledge", "liquid", "liquid-path", "detour"):
            self.assertIn(f'"{code}"', plan, f"per-slot refusal {code} missing")
        self.assertIn("inline bool JudgeSlot(", plan)
        self.assertIn("JudgeSlot(candidate", plan)
        self.assertIn("kNudgeStepsYd", strip_comments(self.pure))

    def test_new_anchor_and_wire_words(self):
        plan = strip_comments(self.plan)
        pure = strip_comments(self.pure)
        for word in ("Anchor::Tank", "Anchor::Point", "RallyShow", "RallyClear", "RallyAt",
                     "FallbackTo::At", "CardHoldKeys("):
            self.assertIn(word, plan, f"{word} missing from the pure plan")
        for word in ("GapText(", "ParseFace(", "ParsePoint(", "ParseGapWord(", "ClearSpots(", "FrameInput"):
            self.assertIn(word, pure, f"{word} missing from the pure header")
        host = strip_comments(self.place)
        for key in ('"anchor_bot"', '"nudged"', '"to"', '"shown"', '"cleared"', "GapText(command.gapSpec)"):
            self.assertIn(key, host, f"reply/head key {key} missing")
        self.assertIn("if (command.hasFace)", host)
        self.assertIn('head.Coord("at"', host)
        self.assertIn('"no-rally"', plan)                 # the target choice is pure
        self.assertIn("JudgeTypedPoint(", host)

    def test_anchor_tank_is_removed(self):
        host = strip_comments(self.place)
        plan = strip_comments(self.plan)
        self.assertIn("FindAnchorTank(", host)
        # The exclusion is the plan's (SelectBots' `exclude`), so the host cannot forget it and the behaviour test drives it.
        self.assertIn("SelectBots(bots, who, anchorName)", plan)
        self.assertIn("std::string_view anchorName", plan)
        self.assertIn("WorldProbe(commander, picked.bots, anchor.z), tankName)", host)
        self.assertNotIn("picked.bots.erase(", host)

    def test_card_keys_are_written_once(self):
        b = strip_comments(self.binding)
        self.assertIn("place::ViewFor(", b)
        self.assertIn("place::CardHoldKeys(", b)
        for key in ('"hold"', 'Coord("slot"', '"rmap"', '"rinst"'):
            self.assertIn(key, b, f"card key {key} missing from TbCmdBinding.cpp")
        # No other command source writes the card's hold/rally keys: the card reads place::ViewFor once.
        for path in CMD.glob("*.cpp"):
            if path.name == "TbCmdBinding.cpp":
                continue
            source = strip_comments(read(path))
            self.assertNotIn('"rmap"', source, f"{path.name} writes the card's rally map key")
            self.assertNotIn('"rinst"', source, f"{path.name} writes the card's rally instance key")

    def test_facing_is_the_core_convention(self):
        host = strip_comments(self.place)
        pure = strip_comments(self.pure)
        thrown = strip_comments(read(SRC / "host" / "TbThrown.cpp"))
        # ONE helper: Position::RelocateOffset/GetNearPoint2D's x += cos(o), y += sin(o).
        self.assertIn("FrontFromOrientation(ori)", host)
        self.assertIn("FrontFromOrientation(tankOri)", host)
        self.assertIn("FaceFront(command.faceDeg)", host)
        self.assertIn("FrontFromOrientation(bot->GetOrientation())", thrown)
        self.assertIn("inline Vec2 FrontFromOrientation(float radians)", pure)
        for source in (host, thrown):
            self.assertNotIn("std::sin(ori)", source)
            self.assertNotIn("std::cos(ori)", source)
            self.assertNotIn("std::sin(tankOri)", source)

    def test_rally_state_is_committed_after_the_judge(self):
        host = strip_comments(self.place)
        plan = strip_comments(self.plan)
        self.assertIn("DecideRally(", plan)
        self.assertIn("DecideRally(", host)
        self.assertIn("Memory().rallies[key] = decision.value;", host)
        # No rally entry is created before the decision; a refused point stores nothing.
        self.assertNotIn("rallies[key];", host)
        self.assertNotIn("rally.set = true;", host)
        # The stored point only applies on its own map and instance.
        self.assertIn("StoredRallyFor(Memory()", host)
        self.assertIn("StoredRallyFor(State const& state", plan)

    def test_access_on_every_entry(self):
        host = strip_comments(self.place)
        self.assertIn("bool CheckAccess(ChatHandler* handler, Binding const& binding", host)
        # Only the lead refuses the whole command; a foreign member is a refused row (PickBots asks the same check per bot).
        self.assertIn("h3::CheckBotAccess(handler, binding.lead", host)
        self.assertNotIn("for (ObjectGuid const& member : binding.bots)", host)
        self.assertIn("h3::CheckBotAccess(commander, bot->GetGUID()", host)
        # The chat form and the deferred ping run the same check; party chat must hold the bots it would order.
        self.assertIn("CheckAccess(handler, binding, code, detail)", host)
        self.assertIn("group->IsMember(lead)", host)
        self.assertIn("group->IsMember(members[i])", host)
        self.assertIn("place::HandleChat(player, msg, group)", strip_comments(self.scripts))
        self.assertIn("HandleChat(Player* sender, std::string const& text, Group* group", strip_comments(self.place_h))

    def test_typed_point_bounds(self):
        host = strip_comments(self.place)
        # Finite, on the map (with its z), bounded on every axis from the commander, floor-snapped over a bounded band,
        # dry, and walkable by the commander.
        self.assertIn("std::isfinite(point.x)", host)
        self.assertIn("Trinity::IsValidMapCoord(point.x, point.y, command.pointZ)", host)
        self.assertRegex(host, r"std::fabs\(command\.pointZ - commanderZ\) > kPointReachYd")
        self.assertRegex(host, r"std::fabs\(z - commanderZ\) > kPointReachYd")
        self.assertIn("VerdictFor(commander, point, z)", host)

    def test_fallback_emergency(self):
        plan = strip_comments(self.plan)
        host = strip_comments(self.place)
        # A fall back nudge may use a slot that only collides with the commander/tank; both emergency destinations are
        # judged (never an unchecked commander point), and a target-ring nudge cannot enter the target.
        self.assertIn("rules.nudgeCleared = true", plan)
        self.assertIn("if (!JudgeSlot(commander, commanderZ, step.bot, probe, commanderCode, commanderPointZ))", plan)
        self.assertIn("rules.guardHalf = anchor.targetHalf;", plan)
        self.assertIn("if (std::sqrt(gx * gx + gy * gy) < rules.guardHalf)", plan)
        self.assertIn("ApplyPlan(reply, \"fallback\", plan, picked, facts, false, true,", host)
        self.assertIn("ApplyPlan(reply, \"place\", plan, picked, anchor, true, false,", host)

    def test_chat_form(self):
        host = strip_comments(self.place)
        self.assertIn("bool HandleChat(Player* sender, std::string const& text, Group* group)", host)
        self.assertIn("bool IsPlaceText(std::string const& text)", host)
        self.assertIn("ChatWordsAreCommand(", host)
        self.assertIn("cmd::ResolveBinding(sender->GetGUID()", host)
        scripts = strip_comments(self.scripts)
        self.assertIn("void OnChatSent(Player* player", scripts)
        self.assertIn("void OnChat(Player* player", scripts)
        self.assertIn("TortoiseBots::place::HandleChat(player, msg, group)", scripts)
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
    return re.search(r"if \(!NewerOrderOwns\(h3::LastOrder\(bot->GetGUID\(\)\)\.seq, row\.seq\)\)", strip_comments(src)) is not None


def liquid_pin(src):
    c = strip_comments(src)
    return "probe.liquid = [map]" in c and "run::liquid::PointRefusedOnMap(" in c


def facing_pin(src):
    c = strip_comments(src)
    return "FrontFromOrientation(ori)" in c and "FrontFromOrientation(tankOri)" in c \
        and "std::sin(ori)" not in c and "std::cos(ori)" not in c


def rally_pin(src):
    c = strip_comments(src)
    return "Memory().rallies[key] = decision.value;" in c and "rallies[key];" not in c


def card_pin(src):
    c = strip_comments(src)
    return "place::ViewFor(" in c and "place::CardHoldKeys(" in c


def placed_come_pin(src):
    c = strip_comments(src)
    return "PlanCome(o.placed," in c and "come.keepCombatStayOut" in c and "come.reset" in c


def dropped_passive_pin(src):
    c = strip_comments(src)
    body = re.search(r"void ReleaseRow\([^)]*\)\s*\{([\s\S]*?)\n\}", c)
    return body is not None and "ReleasePlacePassive(bot, PassiveOwner(row));" in body.group(1)


def hold_ends_pin(src):
    """#316 (1): the row ends when its stay is gone, whether a newer order or a whisper shortcut (which numbers nothing) dropped it; only its
    own `come` still waiting in the queue is exempt."""
    c = strip_comments(src)
    return ('RowStayGone(ai != nullptr, ai && ai->HasStrategy("stay", BotState::BOT_STATE_NON_COMBAT), ownComeQueued)' in c
            and 'ownComeQueued = last.seq == row.seq && last.verb == "come" && last.result == "queued"' in c
            and "end = RowEnd::StayGone;" in c)


def disengage_flag_pin(src):
    c = strip_comments(src)
    return "DisengageSettled(row, passiveGone);" in c and "row.disengage = false;" not in c


def passive_owner_pin(src):
    """#316: the emergency passive is owned by the fall back's own `come`, and the walk back moves only the movement number (`row.seq`). Releasing
    with the newest number found no record and left the bot passive."""
    c = strip_comments(src)
    start = re.search(r"inline void StartRowClock\([^)]*\)\s*\{([\s\S]*?)\n\}", c)
    back = re.search(r"inline void AfterWalkBack\([^)]*\)\s*\{([\s\S]*?)\n\}", c)
    owner = re.search(r"inline uint32_t PassiveOwner\([^)]*\)\s*\{([\s\S]*?)\n\}", c)
    settled = re.search(r"inline bool DisengageSettled\([^)]*\)\s*\{([\s\S]*?)\n\}", c)
    return (start is not None and "row.passiveSeq = row.disengage ? row.seq : 0;" in start.group(1)
            and back is not None and "passiveSeq" not in back.group(1)
            and owner is not None and "row.passiveSeq ? row.passiveSeq : row.seq" in owner.group(1)
            and settled is not None and "if (!passiveGone)" in settled.group(1)
            and "row.disengage = false;" in settled.group(1))


def passive_book_pin(src):
    """#316 follow-up: the walk back that RUNS is the ordinary case, so the record's owner is the row's token; the newer number must not take
    the passive over, and the release consumes the record whether or not it drops `passive`."""
    c = strip_comments(src)
    claim = re.search(r"inline uint32_t RanComePassiveOwner\([^)]*\)\s*\{([\s\S]*?)\n\}", c)
    note = re.search(r"inline void NoteRanPassive\([^)]*\)\s*\{([\s\S]*?)\n\}", c)
    take = re.search(r"inline bool TakeOwnerPassive\([^)]*\)\s*\{([\s\S]*?)\n\}", c)
    return (claim is not None and "return carried ? book.owner : seq;" in claim.group(1)
            and note is not None and "book.last = book.owner;" in note.group(1)
            and take is not None and "book.owner != token" in take.group(1)
            and "PlacePassiveReleasable(true, token, book.last)" in take.group(1))


def come_passive_pin(src):
    """#321: the come's passive step reads the strategies BEFORE it adds `passive`, records only the passive it added, and a walk back whose
    row the cap already ended (its token is no longer the record) adds nothing."""
    c = strip_comments(src)
    keeps = re.search(r"inline bool ComeKeepsPassive\([^)]*\)\s*\{([\s\S]*?)\n\}", c)
    apply = re.search(r"inline void ApplyComePassive\([^)]*\)\s*\{([\s\S]*?)\n\}", c)
    if keeps is None or apply is None:
        return False
    body = apply.group(1)
    read = body.find("bool const alreadyPassive = engine.PassiveOn();")
    add = body.find("engine.AddPassive();")
    return ("token == 0 || book.owner == token" in keeps.group(1)
            and read != -1 and add != -1 and read < add
            and "book->owner = RanComePassiveOwner(*book, alreadyPassive, seq);" in body
            and "if (book && !ComeKeepsPassive(token, *book))" in body)


def walk_back_token_pin(src):
    """#321 follow-up: the walk back's queued `come` carries the row's passive owner, so it can tell its own row from a released one."""
    c = strip_comments(src)
    return "row.placed, row.disengage, PassiveOwner(row), seq, code, detail)" in c


def fallback_cap_pin(src):
    c = strip_comments(src)
    ends = re.search(r"inline bool RowDisengageEnds\([^)]*\)\s*\{([\s\S]*?)\n\}", c)
    start = re.search(r"inline void StartRowClock\([^)]*\)\s*\{([\s\S]*?)\n\}", c)
    back = re.search(r"inline void AfterWalkBack\([^)]*\)\s*\{([\s\S]*?)\n\}", c)
    return (ends is not None and "now - row.disengageMs" in ends.group(1)
            and start is not None and "row.disengageMs = now;" in start.group(1)
            and back is not None and "disengageMs" not in back.group(1) and "passiveSeq" not in back.group(1))


MUTANTS = [
    # (name, file, original text, mutant text, pin)
    ("place verbs fall through to the judge", CMD / "TbCmdOrders.cpp",
     "if (place::Handle(handler, args))", "if (false)", route_pin),
    ("ping packet written instead of read", CMD / "TbPlace.cpp",
     "ParseMinimapPing(packet.data(), packet.size()", "packet.Clear(), ParseMinimapPing(packet.data(), packet.size()", ping_pin),
    ("walk back leaves the row's order number stale", CMD / "TbPlacePlan.h",
     "    row.seq = seq;", "", walk_back_seq_pin),
    ("dropped row follows past a newer order", CMD / "TbPlace.cpp",
     "if (!NewerOrderOwns(h3::LastOrder(bot->GetGUID()).seq, row.seq))", "if (false)", follow_guard_pin),
    ("the liquid stand rule is never asked", CMD / "TbPlace.cpp",
     "probe.liquid = [map](Vec2 const& point, float z)\n    {\n        return !map || run::liquid::PointRefusedOnMap(map, point.x, point.y, z);\n    };",
     "probe.liquid = [](Vec2 const&, float) { return false; };", liquid_pin),
    ("the card never reads the hold", CMD / "TbCmdBinding.cpp",
     "place::CardHoldKeys(place::ViewFor(PlayerGuid(v.commanderGuid)))", "place::CardHoldKeys(place::HoldView{})", card_pin),
    ("the player facing is 90 degrees off", CMD / "TbPlace.cpp",
     "facts.playerFront = FrontFromOrientation(ori);", "facts.playerFront = { std::sin(ori), std::cos(ori) };", facing_pin),
    ("a refused rally still creates the entry", CMD / "TbPlace.cpp",
     "Memory().rallies[key] = decision.value;", "RallyState& rally = Memory().rallies[key]; rally.set = true;", rally_pin),
    ("a run's come takes the placement's combat path", SRC / "host" / "TbOrders.cpp",
     "PlanCome(o.placed, o.disengage, bot->IsInCombat())", "PlanCome(true, o.disengage, bot->IsInCombat())", placed_come_pin),
    ("a dropped fall-back row keeps its passive", CMD / "TbPlace.cpp",
     "        h3::ReleasePlacePassive(bot, PassiveOwner(row));      // the emergency hold never outlives its row (cancel, death, logout, unbind), and never strips another's",
     "", dropped_passive_pin),
    ("the walk back hands the passive to its own new order", CMD / "TbPlacePlan.h",
     "    row.walkFails = 0;\n    row.seq = seq;", "    row.walkFails = 0;\n    row.passiveSeq = seq;\n    row.seq = seq;", passive_owner_pin),
    ("the cap clears the flag without dropping the passive", CMD / "TbPlace.cpp",
     "                DisengageSettled(row, passiveGone);", "                row.disengage = false;", disengage_flag_pin),
    ("a whispered follow leaves the placed hold standing", CMD / "TbPlace.cpp",
     "                if (RowStayGone(ai != nullptr, ai && ai->HasStrategy(\"stay\", BotState::BOT_STATE_NON_COMBAT), ownComeQueued))",
     "                if (last.seq > row.seq && ai && !ai->HasStrategy(\"stay\", BotState::BOT_STATE_NON_COMBAT))", hold_ends_pin),
    ("the fall back drops passive whatever set it", CMD / "TbPlacePlan.h",
     "bool const drop = PlacePassiveReleasable(true, token, book.last);", "bool const drop = true;", passive_book_pin),
    ("the walk back's come takes the passive over", CMD / "TbPlacePlan.h",
     "return carried ? book.owner : seq;", "return seq;", passive_book_pin),
    ("the fall back's come records no passive of its own", CMD / "TbPlacePlan.h",
     "        book->owner = RanComePassiveOwner(*book, alreadyPassive, seq);", "        book->owner = 0;", come_passive_pin),
    ("a walk back whose row ended turns the passive back on", CMD / "TbPlacePlan.h",
     "    return token == 0 || book.owner == token;", "    return true;", come_passive_pin),
    ("the come reads the strategies after adding the passive", CMD / "TbPlacePlan.h",
     "    bool const alreadyPassive = engine.PassiveOn();\n    engine.AddPassive();",
     "    engine.AddPassive();\n    bool const alreadyPassive = engine.PassiveOn();", come_passive_pin),
    ("the walk back's come carries no token", CMD / "TbPlace.cpp",
     "QueueCome(bot, row.x, row.y, row.z, row.placed, row.disengage, PassiveOwner(row), seq, code, detail)",
     "QueueCome(bot, row.x, row.y, row.z, row.placed, row.disengage, 0, seq, code, detail)", walk_back_token_pin),
    ("the fall back's cap restarts with every walk back", CMD / "TbPlacePlan.h",
     "now - row.disengageMs", "now - row.walkMs", fallback_cap_pin),
    ("the walk back restarts the fall back's cap", CMD / "TbPlacePlan.h",
     "    row.walkFails = 0;\n    row.seq = seq;", "    row.walkFails = 0;\n    row.disengageMs = now;\n    row.seq = seq;", fallback_cap_pin),
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

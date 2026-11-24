"""Wiring pins for slice 4, package P4 (death and resurrection helpers).

The both-core helpers of forever/src/compat/TbCompat_D4.h have a Turtle twin, ai/cmangos-compat-d4.h, whose bodies are the calls the
bot always made. These pins prove, by reading the sources, that (1) the two headers define the same helper names, (2) each Turtle body
still holds the original expression, (3) the three ported files no longer name a core-specific spelling, and (4) the accept polarity is
the one the two cores read (Turtle 1 = accept, ForeverCore Response 0 = accept).

Run: python3 -m unittest forever/tools/test_slice4_d4_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
F_HEADER = (ROOT / "forever/src/compat/TbCompat_D4.h").read_text()
T_HEADER = (ROOT / "ai/cmangos-compat-d4.h").read_text()
RELEASE = (ROOT / "ai/playerbot/strategy/actions/ReleaseSpiritAction.h").read_text()
ACCEPT = (ROOT / "ai/playerbot/strategy/actions/AcceptResurrectAction.h").read_text()
TO_RES = (ROOT / "ai/playerbot/strategy/values/PartyMemberToResurrect.cpp").read_text()

HELPER = re.compile(r"^inline\s+(?:[\w:<>,\s\*&]+?)\s+(\w+Compat)\s*\(", re.M)


def names(text):
    return set(HELPER.findall(text))


def body(text, name):
    """The text of the function `name` from its parameter list to its closing brace (braces matched)."""
    i = re.search(r"inline[^;{]*\b%s\s*\(" % re.escape(name), text).start()
    j = text.index("{", i)
    depth = 0
    for k in range(j, len(text)):
        if text[k] == "{":
            depth += 1
        elif text[k] == "}":
            depth -= 1
            if depth == 0:
                return text[i:k + 1]
    raise AssertionError(name)


class D4Wiring(unittest.TestCase):
    def test_both_headers_define_the_same_helpers(self):
        self.assertEqual(names(F_HEADER), names(T_HEADER))
        self.assertGreaterEqual(len(names(F_HEADER)), 13)

    def test_the_shim_lists_include_both_headers(self):
        self.assertIn('#include "TbCompat_D4.h"', (ROOT / "forever/src/compat/forever-compat-shim.h").read_text())
        self.assertIn('#include "cmangos-compat-d4.h"', (ROOT / "ai/cmangos-compat-shim.h").read_text())

    def test_turtle_bodies_are_the_original_expressions(self):
        self.assertIn("WorldPacket packet(CMSG_REPOP_REQUEST);", T_HEADER)
        self.assertIn("packet << uint8(0);", body(T_HEADER, "RepopRequestCompat"))
        self.assertIn("HandleRepopRequestOpcode(packet);", T_HEADER)
        self.assertIn("return true;", body(T_HEADER, "RepopRequestCompat"))
        self.assertIn("bot->CastSpell(bot, 2584, TRIGGERED_OLD_TRIGGERED);", T_HEADER)
        self.assertIn("player->IsRessurectRequested()", T_HEADER)
        self.assertIn("p.rpos(0);", T_HEADER)
        self.assertIn("p >> guid;", T_HEADER)
        self.assertIn("group->RemoveMember(guid, 0)", T_HEADER)
        self.assertIn("sObjectMgr.GetClosestGraveYard(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetMapId(), bot->GetTeam())", T_HEADER)
        self.assertIn("bot->GetRace() == RACE_GOBLIN || bot->GetRace() == RACE_HIGH_ELF", T_HEADER)
        self.assertIn("bot->TeleportToHomebind();", T_HEADER)
        self.assertIn("bot->GetUInt32Value(PLAYER_SELF_RES_SPELL)", T_HEADER)
        self.assertIn("bot->GetSession()->HandleSelfResOpcode(packet);", T_HEADER)

    def test_turtle_accepts_everybody_and_answers_true(self):
        offer = body(T_HEADER, "ResurrectOffererAcceptableCompat")
        self.assertIn("return true;", offer)
        acc = body(T_HEADER, "AcceptResurrectCompat")
        self.assertIn("packet << offerer;", acc)
        self.assertIn("packet << uint8(1);", acc)               # accept
        self.assertIn("HandleResurrectResponseOpcode(packet);", acc)
        self.assertIn("return true;", acc)
        dec = body(T_HEADER, "DeclineResurrectCompat")
        self.assertIn("packet << uint8(0);", dec)

    def test_forever_polarity_is_the_typed_packets(self):
        acc = body(F_HEADER, "AcceptResurrectCompat")
        self.assertIn("packet.Response = 0;", acc)               # 0 = accept on ForeverCore
        self.assertIn("HandleResurrectResponse(packet);", acc)
        self.assertIn("IsResurrectRequestedBy(offerer)", acc)    # nothing is reported when the core had no request
        dec = body(F_HEADER, "DeclineResurrectCompat")
        self.assertIn("packet.Response = 1;", dec)
        rep = body(F_HEADER, "RepopRequestCompat")
        self.assertIn("HandleRepopRequest(packet);", rep)
        self.assertIn("PLAYER_FLAGS_GHOST", rep)                 # the answer is what the player is afterwards

    def test_forever_fallbacks_do_less(self):
        # a request from outside the group and not the commander is declined; the bg aura is counted and not done
        offer = body(F_HEADER, "ResurrectOffererAcceptableCompat")
        self.assertIn("group->IsMember(offerer)", offer)
        self.assertIn("master->GetGUID() == offerer", offer)
        self.assertIn("Feature::BattlegroundRelease", F_HEADER)
        self.assertNotIn("Feature::SelfResurrection", F_HEADER)

    def test_forever_self_resurrection_uses_the_core_list_and_typed_packet(self):
        # the list is the player's own (soulstone, nether, Reincarnation); an id without a SpellInfo would be retried every tick
        possible = body(F_HEADER, "SelfResurrectPossibleCompat")
        self.assertIn("SelfResSpells.empty()", possible)
        self.assertIn("HasAuraType(SPELL_AURA_PREVENT_RESURRECTION)", possible)
        self.assertIn("sSpellMgr.GetSpellInfo(spellId, DIFFICULTY_NONE)", possible)
        res = body(F_HEADER, "SelfResurrectCompat")
        self.assertIn("WorldPackets::Spells::SelfRes packet{ WorldPacket(CMSG_SELF_RES) };", res)
        self.assertIn("packet.SpellID = spellId;", res)
        self.assertIn("HandleSelfResOpcode(packet);", res)
        self.assertIn("SelfResSpells.FindIndex(spellId) < 0", res)
        self.assertLess(res.index("continue;"), res.index("SelfRes packet"))       # an unknown id is skipped, not sent

    def test_a_dungeon_release_waits_for_an_entrance_on_forever_only(self):
        self.assertIn("return true;", body(T_HEADER, "InstanceEntranceKnownCompat"))
        fn = body(F_HEADER, "InstanceEntranceKnownCompat")
        self.assertIn("map->IsDungeon()", fn)
        self.assertIn("script->GetEntranceLocation()", fn)
        self.assertIn("GetWorldSafeLoc(", fn)                    # a real row, as the core's own lookup needs
        i = RELEASE.index("virtual bool isUseful() override", RELEASE.index("class AutoReleaseSpiritAction"))
        useful = RELEASE[i:RELEASE.index("RunReleaseHold::Decide", i)]
        self.assertIn("if (!InstanceEntranceKnownCompat(bot))\n                return false;", useful)   # waits: does less
        self.assertIn('return "no-entrance";', RELEASE)
        self.assertIn("char const* ReleaseHoldReason() const", RELEASE)

    def test_ported_files_name_no_core_specific_spelling(self):
        for text in (RELEASE, ACCEPT, TO_RES):
            for spelling in ("HandleRepopRequestOpcode", "HandleResurrectResponseOpcode", "HandleSelfResOpcode", "IsRessurectRequested",
                             "HasFlag(PLAYER_FLAGS", "TRIGGERED_OLD_TRIGGERED", "RACE_HIGH_ELF", "GetClosestGraveYard(", "->TeleportToHomebind(",
                             "->map_id", "positionX", "RemoveMember("):
                self.assertNotIn(spelling, text)
        self.assertIn("RepopRequestCompat(bot)", RELEASE)
        self.assertEqual(RELEASE.count("RepopRequestCompat(bot)"), 2)      # release and auto release
        self.assertIn("AcceptResurrectCompat(bot, guid)", ACCEPT)
        self.assertIn("ResurrectOffererAcceptableCompat(ai, guid)", ACCEPT)

    def test_release_fails_when_the_core_did_not_release(self):
        # a release the core ignored is not reported as one (ForeverCore answers false; Turtle's helper is always true)
        for cls in ("class ReleaseSpiritAction", "class AutoReleaseSpiritAction"):
            i = RELEASE.index(cls)
            j = RELEASE.index("if (!RepopRequestCompat(bot))", i)
            self.assertIn("return false;", RELEASE[j:j + 80])

    def test_slice4_list_is_slice3_plus_the_four_files(self):
        def files(name):
            return [l.strip() for l in (ROOT / "forever" / name).read_text().splitlines() if l.strip() and not l.startswith("#")]
        s3, s4 = files("SLICE3.txt"), files("SLICE4.txt")
        self.assertEqual(s4[:len(s3)], s3)
        self.assertEqual(s4[len(s3):], [
            "ai/playerbot/strategy/generic/DeadStrategy.cpp",
            "ai/playerbot/strategy/actions/ReleaseSpiritAction.cpp",
            "ai/playerbot/strategy/actions/AcceptResurrectAction.cpp",
            "ai/playerbot/strategy/values/PartyMemberToResurrect.cpp"])
        for f in s4:
            self.assertTrue((ROOT / f).exists(), f)


if __name__ == "__main__":
    unittest.main()

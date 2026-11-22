"""Wiring pins for slice 9, package L9 (item loot, group loot rolls, the item usage value).

The both-core helpers of forever/src/compat/TbCompat_S9.h have a Turtle twin, ai/cmangos-compat-s9.h, whose bodies are the calls the bot
always made. These pins prove, by reading the sources, that (1) the two headers define the same helper names, (2) the Turtle bodies hold
the original expressions, (3) the policy of the slice holds in the code: GREED ONLY, one sink for every vote, no auto-equip wiring, a roll is
announced once, (4) the two files the package adds are in the slice list and the build reads that list, and (5) the registry keys of the
package are in the generator (the generator --check and the registry check prove the rest).

Run: python3 -m unittest forever/tools/test_slice9_l9_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
F_HEADER = (ROOT / "forever/src/compat/TbCompat_S9.h").read_text()
T_HEADER = (ROOT / "ai/cmangos-compat-s9.h").read_text()
ROLL = (ROOT / "ai/playerbot/strategy/actions/LootRollAction.cpp").read_text()
LOOT = (ROOT / "ai/playerbot/strategy/actions/LootAction.cpp").read_text()
TRIGGER = (ROOT / "ai/playerbot/strategy/triggers/WorldPacketTrigger.h").read_text()
I2 = (ROOT / "forever/src/compat/TbCompat_I2.h").read_text()
V2 = (ROOT / "forever/src/compat/TbCompat_V2.h").read_text()
GEN = (ROOT / "forever/tools/slice_gen_context.py").read_text()

HELPER = re.compile(r"^(?:template\s*<[^>]*>\s*)?inline\s+(?:[\w:<>,\s\*&]+?)\s+(\w+Compat)\s*\(", re.M)


def names(text):
    return set(HELPER.findall(text))


def body(text, name):
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


class L9Wiring(unittest.TestCase):
    def test_both_headers_define_the_same_helpers(self):
        self.assertEqual(names(F_HEADER), names(T_HEADER))
        self.assertGreaterEqual(len(names(F_HEADER)), 20)

    def test_the_shims_include_both_headers(self):
        self.assertIn('#include "TbCompat_S9.h"', (ROOT / "forever/src/compat/forever-compat-shim.h").read_text())
        self.assertIn('#include "cmangos-compat-s9.h"', (ROOT / "ai/cmangos-compat-shim.h").read_text())

    def test_turtle_bodies_are_the_original_expressions(self):
        self.assertIn("group->CountRollVote(bot, lootGuid, slot, vote)", body(T_HEADER, "RollVoteCompat"))
        self.assertIn("return vote;", body(T_HEADER, "GreedOnlyVoteCompat"))
        self.assertIn("loot->LootItemInSlot(slot, bot->GetGUIDLow())", body(T_HEADER, "LootItemOfSlotCompat"))
        self.assertIn("member->IsWithinLootXPDist(target)", body(T_HEADER, "MemberInLootRangeCompat"))
        self.assertIn("creature->GetGroupLootTimer()", body(T_HEADER, "GroupLootTimerCompat"))
        self.assertIn("WorldPacket data(MSG_RANDOM_ROLL);", body(T_HEADER, "RandomRollCompat"))
        self.assertIn("return false;", body(T_HEADER, "LootBagFloorCompat"))
        self.assertIn("sAuctionMgr.GetAItem(auction.itemGuidLow)", body(T_HEADER, "AuctionItemCountCompat"))
        self.assertIn("getQuestStatusMap()[quest->GetQuestId()]", body(T_HEADER, "QuestIncompleteCompat"))
        self.assertIn("using BotRollVote = RollVote;", T_HEADER)
        self.assertIn("FindEquipSlot(proto, NULL_SLOT, true)", body(T_HEADER, "EquipSlotOfProtoCompat"))

    def test_greed_only_on_forevercore(self):
        # the F helper ignores the vote it is given
        self.assertIn("return ROLL_GREED;", body(F_HEADER, "GreedOnlyVoteCompat"))
        # the one sink: RollOnItemInSlot normalises the vote, then asks the helper, THEN votes through the core
        sink = ROLL[ROLL.index("bool RollAction::RollOnItemInSlot("):]
        sink = sink[:sink.index("\n}\n")]
        self.assertLess(sink.index("GreedOnlyVoteCompat(vote)"), sink.index("RollVoteCompat("))
        # nothing else in the file votes through the core
        self.assertEqual(len(re.findall(r"RollVoteCompat\(", ROLL)), 1)
        # the F vote is the core's own handler with the typed packet
        vote = body(F_HEADER, "RollVoteCompat")
        self.assertIn("HandleLootRoll(packet)", vote)
        self.assertIn("packet.RollType = uint8(vote);", vote)

    def test_no_auto_equip_and_no_unfed_roll_keys(self):
        start = GEN.index("SLICE9_KEYS = {")
        keys = GEN[start:GEN.index("\n}\n", start)]
        for fed in ("loot start roll", "auto loot roll", "release loot", "active rolls", "item usage"):
            self.assertIn(fed, keys)
        for unfed in ("equip upgrades", "loot roll won", "log roll won", "delayed roll", "item push result"):
            self.assertNotIn(unfed, keys)

    def test_a_roll_and_a_window_are_announced_once(self):
        poll = body(F_HEADER, "PollPacketEventCompat")
        self.assertIn("memo.rolls.count({ owner, slot })", poll)
        self.assertIn("memo.rolls.insert({ owner, slot });", poll)
        self.assertIn("memo.windows.count(loot->GetOwnerGUID())", poll)
        self.assertIn("IsGameObject()", poll)        # a corpse window is raised by LootOpenCompat, an area-loot neighbour never
        self.assertIn("NoteLootWindowAnnouncedCompat(bot, guid);", I2)
        self.assertIn("PollPacketEventCompat(bot, getName(), *this);", TRIGGER)

    def test_rolls_are_announced_and_voted_in_group_loot_and_nbg_only(self):
        gate = body(F_HEADER, "BotRollsInGroupLootCompat")
        # the gate is the one table both calls read: the group's method and the roll's own loot must each take rolls
        self.assertIn("rollmethods::MethodTakesRolls(int(group->GetLootMethod()))", gate)
        self.assertIn("rollmethods::MethodTakesRolls(int(loot->GetLootMethod()))", gate)
        self.assertIn("TbRollMethodsPure.h", F_HEADER)
        poll = body(F_HEADER, "PollPacketEventCompat")
        # the roll event is raised only after the gate says so (free-for-all, round robin, master loot and no group: nothing is announced)
        self.assertIn("BotRollsInGroupLootCompat(bot, roll->GetLoot())", poll)
        self.assertLess(poll.index("BotRollsInGroupLootCompat("), poll.index("trigger.ExternalEvent(event, nullptr);"))
        vote = body(F_HEADER, "RollVoteCompat")
        self.assertIn("BotRollsInGroupLootCompat(bot, live->GetLoot())", vote)
        self.assertLess(vote.index("BotRollsInGroupLootCompat("), vote.index("HandleLootRoll(packet)"))

    def test_a_roll_vote_respects_the_bag_floor(self):
        vote = body(F_HEADER, "RollVoteCompat")
        floor = vote.index("LootBagFloorCompat(bot, live->GetItem()->itemid, live->GetItem()->count)")
        self.assertLess(floor, vote.index("HandleLootRoll(packet)"))
        # a refused floor is NO vote: it returns before the packet is built (not a pass, not a greed)
        self.assertIn("return false;", vote[floor:vote.index("WorldPackets::Loot::LootRoll packet")])

    def test_the_item_take_uses_the_core_handlers(self):
        self.assertIn("HandleAutostoreLootItemOpcode(packet)", body(I2, "LootAutostoreCompat"))
        self.assertIn("GetUiTypeForPlayer", body(I2, "LootSlotFactsCompat"))
        self.assertIn("GetUiTypeForPlayer", I2[I2.index("inline bool LootWindowReadCompat("):])
        self.assertIn("return false;", body(I2, "ItemLootingUnportedCompat"))
        self.assertIn("LootAutostoreCompat(bot, loot, itemindex);", LOOT)
        self.assertIn("LootBagFloorCompat(bot, itemid, itemcount)", LOOT)

    def test_loot_tables_are_read_through_the_guard(self):
        self.assertIn("WorldQueryCompat(sql.c_str(), &schemaMissing)", V2)
        self.assertNotIn("WorldDatabase.Query(", V2)
        self.assertNotIn("Unported(tbcompat::Feature::LootTables)", body(V2, "ReferenceLootTemplateCompat"))

    def test_the_slice_list_and_the_build(self):
        listed = [l.strip() for l in (ROOT / "forever/SLICE-W4.txt").read_text().splitlines() if l.strip() and not l.startswith("#")]
        for path in ("ai/playerbot/strategy/actions/LootRollAction.cpp", "ai/playerbot/strategy/values/ItemUsageValue.cpp"):
            self.assertIn(path, listed)
        slice4 = [l.strip() for l in (ROOT / "forever/SLICE4.txt").read_text().splitlines() if l.strip() and not l.startswith("#")]
        self.assertTrue(set(slice4) <= set(listed))
        self.assertIn("forever/SLICE-W4.txt", (ROOT / "CMakeLists.txt").read_text())
        stubs = (ROOT / "forever/src/slice/SliceLinkStubs.cpp").read_text()
        self.assertNotIn("bool ItemUsageValue::IsNeededForQuest", stubs)


if __name__ == "__main__":
    unittest.main()

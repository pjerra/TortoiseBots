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
LOOTVAL = (ROOT / "ai/playerbot/strategy/values/LootValues.cpp").read_text()
LOOTVAL_H = (ROOT / "ai/playerbot/strategy/values/LootValues.h").read_text()
CONTEXT = (ROOT / "forever/src/slice/SliceAiObjectContext.cpp").read_text()
WPKT_CONTEXT = (ROOT / "ai/playerbot/strategy/actions/WorldPacketActionContext.h").read_text()
TRIGGER = (ROOT / "ai/playerbot/strategy/triggers/WorldPacketTrigger.h").read_text()
I2 = (ROOT / "forever/src/compat/TbCompat_I2.h").read_text()
V2 = (ROOT / "forever/src/compat/TbCompat_V2.h").read_text()
GEN = (ROOT / "forever/tools/slice_gen_context.py").read_text()

HELPER = re.compile(r"^(?:template\s*<[^>]*>\s*)?inline\s+(?:[\w:<>,\s\*&]+?)\s+(\w+Compat)\s*\(", re.M)


def names(text):
    return set(HELPER.findall(text))


def body(text, name):
    for m in re.finditer(r"inline[^;{]*\b%s\s*\(" % re.escape(name), text):
        i = m.start()
        j = text.find("{", i)
        semi = text.find(";", i)
        if j < 0 or (0 <= semi < j):
            continue                                  # a forward declaration, not the definition
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
        # the F helper turns every vote into greed, except the ordered pass the caller marks
        self.assertIn("orderedPass && vote == ROLL_PASS ? ROLL_PASS : ROLL_GREED", body(F_HEADER, "GreedOnlyVoteCompat"))
        # the one sink: RollOnItemInSlot normalises the vote, then asks the helper, THEN votes through the core
        sink = ROLL[ROLL.index("bool RollAction::RollOnItemInSlot("):]
        sink = sink[:sink.index("\n}\n")]
        self.assertLess(sink.index("GreedOnlyVoteCompat(vote, orderedPass)"), sink.index("RollVoteCompat("))
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

    def test_rolls_are_announced_and_voted_only_in_group_loot(self):
        gate = body(F_HEADER, "BotRollsInGroupLootCompat")
        self.assertIn("GetLootMethod() == GROUP_LOOT", gate)
        poll = body(F_HEADER, "PollPacketEventCompat")
        # the roll event is raised only after the gate says so (Need Before Greed, master loot and no group: nothing is announced)
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
        # a pass wins nothing, so it is not held by the floor
        self.assertIn("vote != ROLL_PASS && LootBagFloorCompat(", vote)

    def test_the_bag_floor_counts_only_room_the_item_can_use(self):
        floor = body(F_HEADER, "LootBagFloorCompat")
        # GetFreeInventorySlotCount counts the free slots of a quiver, an ammo pouch and a soul bag too
        self.assertNotIn("GetFreeInventorySlotCount()", floor)
        self.assertIn("ItemCanGoIntoBag(proto, bag->GetTemplate())", floor)
        self.assertIn("GetFreeSlots()", floor)

    def test_a_refused_vote_drops_the_roll_and_is_not_logged(self):
        sink = ROLL[ROLL.index("bool RollAction::RollOnItemInSlot("):]
        sink = sink[:sink.index("\n}\n")]
        refused = sink[sink.index("if (!RollVoteCompat("):]
        # the announcement is forgotten, so the auto vote neither retries nor logs it every tick until the timeout
        self.assertLess(refused.index("ActiveRolls::CleanUp("), refused.index("return false;"))
        # the polite vote logs the vote the core took, after the vote, never before it
        etiquette = ROLL[ROLL.index("bool RollAction::RollWithEtiquette("):]
        etiquette = etiquette[:etiquette.index("\n}\n")]
        self.assertLess(etiquette.rindex("RollOnItemInSlot("), etiquette.rindex("LogLootRoll("))
        # the ordered roll too
        ordered = ROLL[ROLL.index("bool RollAction::Execute("):]
        ordered = ordered[:ordered.index("\n}\n")]
        self.assertLess(ordered.index("RollOnItemInSlot(cast"), ordered.index("LogLootRoll(ai, itemQualifier, cast"))

    def test_an_ordered_pass_stays_a_pass(self):
        # an explicit "roll pass" and a hired companion with looting off are orders, not the bot's own judgment
        self.assertIn("RollOnItemInSlot(cast, roll.first, roll.second, doVote == ROLL_PASS)", ROLL)
        self.assertIn("RollOnItemInSlot(hiredVote, lootGuid, slot, true)", ROLL)
        # an automatic pass (the item is no use) still becomes greed at the sink
        self.assertIn("BotRollVote cast = vote;", ROLL)

    def test_cleanup_erases_only_the_named_slot(self):
        # ForeverCore's loot list ids start at 0, so 0 is a real slot and cannot mean "all"
        self.assertNotIn("if (slot && roll->second != slot)", LOOTVAL)
        self.assertIn("if (slot != UINT32_MAX && roll->second != slot)", LOOTVAL)
        self.assertIn("uint32 slot = UINT32_MAX", LOOTVAL_H)

    def test_the_loot_table_item_column_is_read_like_the_core(self):
        table = V2[V2.index("inline LootTableCopy const& LootTableOf("):]
        table = table[:table.index("\n}\n")]
        self.assertIn("fields[2].GetUInt64()", table)
        self.assertNotIn("fields[2].GetUInt32()", table)
        self.assertIn("rawItem > std::numeric_limits<uint32>::max()", table)

    def test_the_roll_actions_are_registered_without_an_unrelated_cast(self):
        for text in (CONTEXT, WPKT_CONTEXT):
            self.assertIn("new LootStartRollAction(ai)", text)
            self.assertNotIn("(QueryItemUsageAction*)new Loot", text)

    def test_the_ammo_id_is_the_equipped_ammo(self):
        self.assertIn("player->GetAmmoId()", body(F_HEADER, "PlayerAmmoIdCompat"))

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
        listed = [l.strip() for l in (ROOT / "forever/SLICE9.txt").read_text().splitlines() if l.strip() and not l.startswith("#")]
        for path in ("ai/playerbot/strategy/actions/LootRollAction.cpp", "ai/playerbot/strategy/values/ItemUsageValue.cpp"):
            self.assertIn(path, listed)
        slice4 = [l.strip() for l in (ROOT / "forever/SLICE4.txt").read_text().splitlines() if l.strip() and not l.startswith("#")]
        self.assertTrue(set(slice4) <= set(listed))
        self.assertIn("forever/SLICE9.txt", (ROOT / "CMakeLists.txt").read_text())
        stubs = (ROOT / "forever/src/slice/SliceLinkStubs.cpp").read_text()
        self.assertNotIn("bool ItemUsageValue::IsNeededForQuest", stubs)


if __name__ == "__main__":
    unittest.main()

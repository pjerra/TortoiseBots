"""Wiring pins for the world-life slice W4, round A2 (travel destinations on ForeverCore): TravelMgr.cpp and TravelValues.cpp.

The decisions themselves are exercised by standalone tests (forever/tools/test_zone_level_policy.cpp,
test_travel_purpose_gate_policy.cpp); these pins read the sources and prove the ORDER and the CUTS stay where
FOREVER-W0-W4-SPEC 2.3 and the notes put them: the two shared files read the core only through the WL4 pair, the loader reads its
three data sets through the pair (the shared values are not registered on this core), the area-level resolution asks the parent
zone before the DBC level and has no sub-area average, no module table is read outside the guard, the fish locations and the
taxi zones do less, and the Turtle side keeps the legacy expressions verbatim.

Round A1 owns the graph: its pins (no std::async on F, getRoute costs, a taxi link skipped) belong in the same file and are
appended below this round's classes.

Run: python3 -m unittest forever/tools/test_wl4_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
MGR = (ROOT / "ai/playerbot/TravelMgr.cpp").read_text()
VALUES = (ROOT / "ai/playerbot/strategy/values/TravelValues.cpp").read_text()
COMPAT_F = (ROOT / "forever/src/compat/TbCompat_WL4.h").read_text()
COMPAT_F_CPP = (ROOT / "forever/src/compat/TbCompat_WL4.cpp").read_text()
COMPAT_T = (ROOT / "ai/cmangos-compat-wl4.h").read_text()
WRAPPER = (ROOT / "ai/playerbot/TbCompatWL4.h").read_text()
LIST_W1 = (ROOT / "forever/SLICE-W1.txt").read_text()
LIST_W4 = (ROOT / "forever/SLICE-W4.txt").read_text()
CMAKE = (ROOT / "CMakeLists.txt").read_text()
STUBS = (ROOT / "forever/src/slice/SliceLinkStubs.cpp").read_text()
ZONE_POLICY = (ROOT / "ai/playerbot/policy/travel/ZoneLevelPolicy.h").read_text()
PURPOSE_POLICY = (ROOT / "ai/playerbot/policy/travel/TravelPurposeGatePolicy.h").read_text()


def func(text, signature):
    """The text of the function whose definition line contains `signature` (up to its closing brace)."""
    i = text.index(signature)
    j = text.index("{", i)
    depth = 0
    for k in range(j, len(text)):
        if text[k] == "{":
            depth += 1
        elif text[k] == "}":
            depth -= 1
            if depth == 0:
                return text[i:k + 1]
    raise AssertionError(signature)


class PairAndFiles(unittest.TestCase):
    def test_both_files_read_the_core_only_through_the_pair(self):
        for text, name in ((MGR, "TravelMgr.cpp"), (VALUES, "TravelValues.cpp")):
            self.assertIn('#include "playerbot/TbCompatWL4.h"', text, name)
            self.assertNotIn("#if TORTOISEBOTS_CORE_FOREVER", text, name)   # the bot brain keeps no core switch
        self.assertIn('#if __has_include("TbCompat_WL4.h")', WRAPPER)
        self.assertIn('#include "cmangos-compat-wl4.h"', WRAPPER)

    def test_the_build_and_census_lists_carry_both_files(self):
        for line in ("ai/playerbot/TravelMgr.cpp", "ai/playerbot/strategy/values/TravelValues.cpp"):
            self.assertIn(line, LIST_W1.splitlines())
            self.assertIn(line, LIST_W4.splitlines())
        # The pair's out-of-line part is slice-only: the OFF build must not compile it.
        self.assertIn("TbCompat_WL4", CMAKE)
        self.assertIn("if(NOT TORTOISEBOTS_SLICE1)", CMAKE)

    def test_the_stand_ins_of_the_two_files_are_gone(self):
        for gone in ("void TravelMgr::LoadAreaLevels() {", "void TravelMgr::LoadQuestTravelTable() {",
                     "int32 TravelMgr::GetAreaLevel(uint32) {", "TravelTarget::TravelTarget(PlayerbotAI* ai)",
                     "std::string EntryTravelDestination::GetShortName()", "bool GrindTravelDestination::IsPossible("):
            self.assertNotIn(gone, STUBS)
        # The node graph is round A1's: its stand-ins stay until its file is in the slice.
        self.assertIn("TravelNodeMap::getMapOffset", STUBS)


class LoaderData(unittest.TestCase):
    def test_the_three_data_sets_come_from_the_pair_not_the_shared_context(self):
        loader = func(MGR, "void TravelMgr::LoadQuestTravelTable()")
        self.assertIn("TravelEntryGuidpsCompat()", loader)
        self.assertIn("TravelQuestRelationMapCompat()", loader)
        self.assertIn("TravelEntryPurposeMapCompat()", loader)
        self.assertNotIn('GAI_VALUE_REF(EntryGuidps', MGR)
        self.assertNotIn('GAI_VALUE_REF(EntryQuestRelationMap', MGR)
        self.assertNotIn('GAI_VALUE_REF(EntryTravelPurposeMap', MGR)
        self.assertNotIn('GAI_VALUE(trainableSpellMap', MGR)
        # The values themselves build from the pair's own data (never back through their own shared key).
        self.assertIn("TravelEntryGuidpsLegacyCompat()", VALUES)
        self.assertIn("TravelEntryPurposeMapLegacyCompat()", VALUES)

    def test_the_loader_opens_logs_loads_a_graph_and_fishes_only_where_they_exist(self):
        loader = func(MGR, "void TravelMgr::LoadQuestTravelTable()")
        self.assertIn("TravelOpenRunLogsCompat();", loader)
        self.assertIn("TravelGraphLoadCompat();", loader)
        self.assertIn("if (TravelFishingCompat())", loader)
        self.assertNotIn('sPlayerbotAIConfig.openLog(', loader)
        self.assertNotIn("sTravelNodeMap.loadNodeStore()", loader)
        # ForeverCore loads no graph (round A1 wires its node store there); the Turtle side keeps the original five calls.
        self.assertIn("void TravelGraphLoadCompat();", COMPAT_F)
        self.assertIn("the travel node store is not ported", COMPAT_F_CPP)
        self.assertNotIn("sTravelNodeMap.loadNodeStore()", COMPAT_F_CPP)
        self.assertIn("sTravelNodeMap.loadNodeStore();", COMPAT_T)
        self.assertIn("if (sPlayerbotAIConfig.generateTravelNodes)", COMPAT_T)

    def test_no_module_table_is_read_outside_the_guard(self):
        self.assertNotIn("WorldDatabase.Query(", MGR)
        self.assertNotIn("WorldDatabase.PQuery(", MGR)
        self.assertIn('TravelWorldQueryCompat("SELECT `id`, `level` FROM `ai_playerbot_zone_level`", &schemaMissing)', MGR)
        self.assertIn("WorldQueryCompat(sql, schemaMissing)", COMPAT_F)
        # Every query the pair's own data builders run goes through the guard too.
        self.assertEqual(COMPAT_F_CPP.count("WorldQueryCompat("), 1)
        self.assertIn("creature_queststarter", COMPAT_F_CPP)
        self.assertIn("gameobject_questender", COMPAT_F_CPP)


class AreaLevels(unittest.TestCase):
    def test_parent_before_dbc_and_no_sub_area_average_on_fervercore(self):
        area = func(MGR, "int32 TravelMgr::GetAreaLevel(")
        parent = area.index("if (AreaPreferParentCompat())")
        loop = area.index("for (uint32 i = 0; i < AreaStoreCountWL4(); i++)")
        self.assertLess(parent, loop)            # the ForeverCore answer returns before the average
        self.assertIn("GetAreaLevel(AreaZoneIdCompat(area))", area[parent:loop])
        self.assertIn("AreaLevelCompat(area) > 0 ? AreaLevelCompat(area) : -1", area[parent:loop])
        self.assertIn("inline bool AreaPreferParentCompat()", COMPAT_F)
        self.assertIn("return true;", func(COMPAT_F, "inline bool AreaPreferParentCompat()"))
        self.assertIn("return false;", func(COMPAT_T, "inline bool AreaPreferParentCompat()"))
        # The zone-level rule itself is pure and tested (less than five samples answers the parent zone).
        self.assertIn("kMinSamples = 5", ZONE_POLICY)
        self.assertIn("inputs.parentLevel > 0", ZONE_POLICY)

    def test_the_runtime_reads_no_terrain(self):
        # The spawn index resolves the area flag once, at build time; the rules only read the cached flag.
        self.assertIn("point.FetchArea();", COMPAT_F_CPP)
        area = func(MGR, "int32 TravelMgr::GetAreaLevel(")
        self.assertNotIn("= point->GetArea();", area)
        self.assertNotIn("WorldPosition().GetArea()", area)
        self.assertNotIn("sTerrainMgr", MGR)


class PurposeGate(unittest.TestCase):
    def test_the_gate_reads_the_registry_and_refuses_what_its_slice_does_not_serve(self):
        self.assertIn("travel_purpose::Allows", COMPAT_F_CPP)
        self.assertIn('actions.count("sell")', COMPAT_F_CPP)
        self.assertIn('actions.count("repair")', COMPAT_F_CPP)
        self.assertIn('strategies.count("maintenance")', COMPAT_F_CPP)
        self.assertIn('strategies.count("rpg quest")', COMPAT_F_CPP)
        self.assertIn("Refusal::VendorKeysMissing", PURPOSE_POLICY)
        self.assertIn("Refusal::QuestKeysMissing", PURPOSE_POLICY)
        # The reduced ForeverCore purpose map builds exactly the four purposes the plan keeps.
        purpose = func(COMPAT_F_CPP, "EntryTravelPurposeMap const& TravelEntryPurposeMapData()")
        for kept in ("TravelDestinationPurpose::Grind", "TravelDestinationPurpose::GenericRpg",
                     "TravelDestinationPurpose::Vendor", "TravelDestinationPurpose::Repair"):
            self.assertIn(kept, purpose)
        for dropped in ("TravelDestinationPurpose::AH", "TravelDestinationPurpose::Mail",
                        "TravelDestinationPurpose::Trainer", "TravelDestinationPurpose::Boss"):
            self.assertNotIn(dropped, purpose)
        # Turtle keeps its gate open: the slice pair never refuses a legacy pick.
        self.assertIn("return true;", func(COMPAT_T, "inline bool TravelPurposeRegisteredCompat("))


class TurtleSide(unittest.TestCase):
    def test_the_turtle_expressions_are_the_legacy_ones(self):
        for legacy in ("quest->GetMinLevel()", "quest->ReqItemId[index]", "area->Id", "area->Team",
                       "area->ExploreFlag", "TRAINER_TYPE_MOUNTS", "TortoiseBots::GuildOrders::Find(",
                       "sAreaStore.GetMaxEntry()", "sObjectMgr.GetTaxiNodeEntry(nodeId)"):
            self.assertIn(legacy, COMPAT_T)
        # The four value bodies moved there verbatim: the guild-order, alt/hire and trainer branches are still the full ones.
        self.assertIn("sRandomBotFacade.IsFreeBot(bot) && !ai->HasRealPlayerMaster()", COMPAT_T)
        self.assertIn("AI_VALUE2(time_t, \"manual time\", parkKey)", COMPAT_T)
        self.assertIn("outgrown_yield::HasHandInAbleQuest(ai, bot)", COMPAT_T)

    def test_the_fervercore_value_answers_do_less(self):
        named = func(COMPAT_F_CPP, "bool TravelNamedPurposeCompat(")
        self.assertIn("return false;", named)
        need = func(COMPAT_F_CPP, "bool TravelPurposeNeedCompat(")
        self.assertIn("TravelPurposeRegisteredCompat(ai, purpose)", need)
        self.assertIn("TravelDestinationPurpose::Vendor", need)
        self.assertIn("TravelDestinationPurpose::Repair", need)
        self.assertIn("TravelDestinationPurpose::Grind", need)
        self.assertIn("TravelDestinationPurpose::GenericRpg", need)
        self.assertNotIn("TravelDestinationPurpose::AH", need)
        leave = func(COMPAT_F_CPP, "bool TravelLeaveOutgrownCompat(")
        self.assertIn("TryGetValidatedAreaLevel", leave)
        self.assertIn("zoneLevel + 5 < int32(bot->GetLevel())", leave)


class FishAndTaxi(unittest.TestCase):
    def test_fishing_and_taxi_do_less_on_fervercore(self):
        self.assertIn("inline bool TravelFishingCompat() { return false; }", COMPAT_F)
        self.assertIn("inline bool TravelFishingCompat() { return true; }", COMPAT_T)
        self.assertIn("if (!TravelFishingCompat())", func(MGR, "void TravelMgr::LoadFishLocations()"))
        self.assertIn("if (!TravelFishingCompat())", func(MGR, "void TravelMgr::SaveFishLocations()"))
        self.assertNotIn("sObjectMgr.GetFishingBaseSkillLevel(", MGR)
        self.assertIn("TravelLoadTaxiZonesCompat(taxiNodeZones);", func(MGR, "void TravelMgr::LoadTaxiNodeZones()"))
        self.assertIn("zones.clear();", COMPAT_F)
        self.assertIn("sObjectMgr.GetMaxTaxiNodeId()", COMPAT_T)


if __name__ == "__main__":
    unittest.main()

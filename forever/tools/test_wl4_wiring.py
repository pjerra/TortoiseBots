"""Wiring pins for the travel slice W4: round A1 (the node graph on ForeverCore) and round A2 (travel destinations).

Round A1: the decisions are text only here: the graph is read through the schema guard, a taxi or transport link is loaded but
never used on ForeverCore, the generator runs its map passes on the world thread there, and the stand-ins of the Travel block in
SliceLinkStubs are gone now that ai/playerbot/TravelNode.cpp is in the slice (FOREVER-W0-W4-SPEC 2.3, W4-A1).

Round A2: TravelMgr.cpp and TravelValues.cpp. The decisions themselves are exercised by standalone tests
(forever/tools/test_zone_level_policy.cpp, test_travel_purpose_gate_policy.cpp); these pins read the sources and prove the ORDER
and the CUTS stay where FOREVER-W0-W4-SPEC 2.3 and the notes put them: the two shared files read the core only through the WL4
pair, the loader reads its three data sets through the pair (the shared values are not registered on this core), the area-level
resolution asks the parent zone before the DBC level and has no sub-area average, no module table is read outside the guard, the
fish locations and the taxi zones do less, and the Turtle side keeps the legacy expressions verbatim.

Run: python3 -m unittest forever/tools/test_wl4_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
MGR = (ROOT / "ai/playerbot/TravelMgr.cpp").read_text()
TRAVEL = (ROOT / "ai/playerbot/TravelNode.cpp").read_text()
VALUES = (ROOT / "ai/playerbot/strategy/values/TravelValues.cpp").read_text()
COMPAT_F = (ROOT / "forever/src/compat/TbCompat_WL4.h").read_text()
COMPAT_F_CPP = (ROOT / "forever/src/compat/TbCompat_WL4.cpp").read_text()
COMPAT_FN = (ROOT / "forever/src/compat/TbCompat_WL4n.h").read_text()
COMPAT_T = (ROOT / "ai/cmangos-compat-wl4.h").read_text()
COMPAT_TN = (ROOT / "ai/cmangos-compat-wl4n.h").read_text()
WRAPPER = (ROOT / "ai/playerbot/TbCompatWL4.h").read_text()
WRAPPER_N = (ROOT / "ai/playerbot/TbCompatWL4n.h").read_text()
STUBS = (ROOT / "forever/src/slice/SliceLinkStubs.cpp").read_text()
LIST_W1 = (ROOT / "forever/SLICE-W1.txt").read_text()
LIST_W4 = (ROOT / "forever/SLICE-W4.txt").read_text()
CMAKE = (ROOT / "CMakeLists.txt").read_text()
SCHEMA = (ROOT / "data/sql/world/20260824090000_world.sql").read_text()
GRAPH_POLICY = (ROOT / "ai/playerbot/policy/travel/TravelGraphPolicy.h").read_text()
LEVEL_POLICY = (ROOT / "ai/playerbot/policy/travel/CreatureLevelPolicy.h").read_text()
BOOT_CHECK = (ROOT / "forever/tools/travel_graph_boot_check.sh").read_text()
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
        # The node graph is in the slice too (round A1): its stand-ins went with that file.
        self.assertNotIn("TravelNodeMap::getMapOffset", STUBS)


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
        # Both sides load the node store through the same call; each pair answers it for its own core.
        self.assertIn("void TravelGraphLoadCompat();", COMPAT_F)
        self.assertIn("sTravelNodeMap.loadNodeStore();", COMPAT_F_CPP)
        self.assertIn("sTravelNodeMap.loadNodeStore();", COMPAT_T)

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


class NodeStore(unittest.TestCase):
    def test_the_three_tables_are_read_through_the_guard(self):
        body = func(TRAVEL, "void TravelNodeMap::loadNodeStore()")
        for table in ("ai_playerbot_travelnode`", "ai_playerbot_travelnode_link`", "ai_playerbot_travelnode_path`"):
            self.assertIn("FROM `" + table, body)
        self.assertEqual(body.count("TravelNodeQueryCompat("), 3)
        self.assertNotIn("WorldDatabase", body)
        self.assertNotIn("CharacterDatabase", body)
        self.assertNotIn("LoginDatabase", TRAVEL)

    def test_a_missing_table_is_an_empty_graph_with_one_line(self):
        body = func(TRAVEL, "void TravelNodeMap::loadNodeStore()")
        self.assertEqual(body.count("if (missing)"), 3)
        self.assertIn("emptyGraph(", body)
        empty = func(body, "auto const emptyGraph")
        self.assertEqual(empty.count("sLog.out"), 1)
        self.assertIn("m_nodes.clear()", empty)
        self.assertIn("m_map_nodes.clear()", empty)
        self.assertIn("hasToGen = false;", empty)

    def test_the_shipped_schema_has_the_columns_the_queries_name(self):
        for table in ("ai_playerbot_travelnode", "ai_playerbot_travelnode_link", "ai_playerbot_travelnode_path"):
            self.assertIn("CREATE TABLE IF NOT EXISTS `" + table + "`", SCHEMA)
        self.assertIn("`max_creature_2`", SCHEMA)


class LinkTypes(unittest.TestCase):
    def test_get_cost_refuses_a_link_the_slice_does_not_use(self):
        body = func(TRAVEL, "float TravelNodePath::getCost(TravelCostContext& context")
        self.assertLess(body.index("TravelLinkUsableCompat(uint8(getPathType()))"), body.index("float modifier"))

    def test_the_f_side_refuses_taxi_and_transport_and_turtle_keeps_all(self):
        f = COMPAT_FN
        t = COMPAT_TN
        # Taxi, transport, teleport-spell and static-portal links are refused (decision 1 and D4); the rule is the tested policy.
        self.assertIn("return ai::travel_graph::LinkUsableOnForever(pathType);", f)
        self.assertIn("return true;", func(t, "inline bool TravelLinkUsableCompat"))
        # The A* is the only user: every link cost goes through getCost.
        route = func(TRAVEL, "TravelNodeRoute TravelNodeMap::getRoute(TravelNode* start, TravelNode* goal")
        self.assertIn("link.second->getCost(costContext, currentNode->currentGold)", route)

    def test_the_a_star_adds_no_portal_edge_on_f(self):
        route = func(TRAVEL, "TravelNodeRoute TravelNodeMap::getRoute(TravelNode* start, TravelNode* goal")
        self.assertIn("TravelTeleportLinksCompat() && AI_VALUE2(bool, \"action useful\", \"hearthstone\")", route)
        self.assertLess(route.index("TravelTeleportLinksCompat()"),
            route.index("if (TravelTeleportLinksCompat())", route.index("TravelTeleportLinksCompat()") + 1))
        self.assertIn("return false;", func(COMPAT_FN, "inline bool TravelTeleportLinksCompat()"))
        self.assertIn("return true;", func(COMPAT_TN, "inline bool TravelTeleportLinksCompat()"))


class Generator(unittest.TestCase):
    def test_every_async_pass_sits_behind_the_pair(self):
        for signature in (
            "void TravelNodeMap::generateWalkPaths()",
            "void TravelNodeMap::generateHelperNodes()",
            "void TravelNodeMap::removeUselessPaths()",
            "void TravelNodeMap::calculatePathCosts()",
        ):
            body = func(TRAVEL, signature)
            self.assertIn("std::async", body, signature)
            self.assertIn("TravelGenParallelCompat()", body, signature)
            self.assertLess(body.index("if (!TravelGenParallelCompat())"), body.index("std::async"), signature)

    def test_the_sequential_pass_runs_on_the_calling_thread(self):
        walk = func(TRAVEL, "void TravelNodeMap::generateWalkPaths()")
        self.assertIn("generateWalkPathMap(map.first, &bar);", walk)
        helper = func(TRAVEL, "void TravelNodeMap::generateHelperNodes()")
        self.assertIn("generateHelperNodes(map.first, &bar);", helper)
        useless = func(TRAVEL, "void TravelNodeMap::removeUselessPaths()")
        self.assertIn("removeUselessPathMap(map.first);", useless)

    def test_forevercore_is_serial_and_turtle_keeps_its_threads(self):
        self.assertIn("return false;", func(COMPAT_FN, "inline bool TravelGenParallelCompat()"))
        self.assertIn("return true;", func(COMPAT_TN, "inline bool TravelGenParallelCompat()"))

    def test_the_mmap_tile_load_is_the_cores_own(self):
        f = func(COMPAT_FN, "inline bool MmapTileLoadCompat")
        self.assertIn("LoadMapAndVMap(x, y)", f)
        self.assertIn("LoadMMap(instanceId, x, y)", f)


class StandIns(unittest.TestCase):
    def test_the_travel_stand_ins_are_gone(self):
        for gone in (
            "TravelNodeMap::getMapOffset",
            "TravelNodeMap::getFullPath",
            "TravelPath::ClipPath",
            "TravelPath::UpcommingSpecialMovement",
            "TravelPath::cutTo",
            "TravelPath::makeShortCut",
        ):
            self.assertNotIn(gone, STUBS)
            self.assertIn(gone, TRAVEL)
        # TravelMgr.cpp is in the slice too (round A2): the generator's map transfer call is the real one.
        self.assertNotIn("void TravelMgr::LoadMapTransfers()", STUBS)
        self.assertIn("void TravelMgr::LoadMapTransfers()", MGR)

    def test_the_slice_list_and_the_cmake_pointer(self):
        paths = [line.strip() for line in LIST_W4.splitlines() if line.strip() and not line.startswith("#")]
        w1 = [line.strip() for line in LIST_W1.splitlines() if line.strip() and not line.startswith("#")]
        self.assertEqual(paths[:len(w1)], w1, "the merged W1 list comes first, unchanged")
        self.assertIn("ai/playerbot/TravelNode.cpp", paths)
        self.assertTrue((ROOT / "ai/playerbot/TravelNode.cpp").exists())
        self.assertIn("forever/SLICE-W4.txt", CMAKE)
        self.assertNotIn("set(TB_SLICE_LIST ${CMAKE_CURRENT_SOURCE_DIR}/forever/SLICE-W1.txt", CMAKE)

    def test_the_pair_and_its_wrapper(self):
        self.assertIn('#include "cmangos-compat-wl4n.h"', WRAPPER_N)
        self.assertIn('#if __has_include("TbCompat_WL4n.h")', WRAPPER_N)
        self.assertIn('#include "playerbot/TbCompatWL4n.h"', TRAVEL)
        for name in ("TravelNodeQueryCompat", "TravelGenParallelCompat", "MmapTileLoadCompat", "TravelLinkUsableCompat",
                     "TravelTeleportLinksCompat", "TravelNodeStoreWritableCompat", "TravelNodeExecuteCompat",
                     "AreaTriggerKnownCompat", "AreaTriggerZoneContainsCompat"):
            self.assertIn(name, COMPAT_FN)
            self.assertIn(name, COMPAT_TN)

    def test_the_two_sides_define_the_same_helpers(self):
        def names(text):
            return set(re.findall(r"^inline [A-Za-z0-9_:<> ]+ ([A-Za-z0-9_]+)\(", text, re.M))
        self.assertEqual(names(COMPAT_FN), names(COMPAT_TN))
        self.assertGreater(len(names(COMPAT_FN)), 30)


class PointIdentity(unittest.TestCase):
    def test_the_key_carries_both_guid_halves(self):
        # ForeverCore: the high half (type, map, entry) is part of the key, not only the spawn counter in the low half.
        self.assertIn("MakeTravelPointKey(guid.GetRawValue(1), guid.GetRawValue(0))", COMPAT_F)
        # Turtle keeps the legacy 64-bit identity exactly.
        self.assertIn("MakeTravelPointKey(0, guid.GetRawValue())", COMPAT_T)
        self.assertIn("std::unordered_map<TravelPointKey, AsyncGuidPosition, TravelPointKeyHash> pointsMap;",
                      (ROOT / "ai/playerbot/TravelMgr.h").read_text())

    def test_the_boot_index_is_released_after_the_destination_build(self):
        loader = func(MGR, "void TravelMgr::LoadQuestTravelTable()")
        self.assertLess(loader.index("GetPopulatedGrids();"), loader.index("TravelEntryGuidpsReleaseCompat();"))
        self.assertIn("void TravelEntryGuidpsReleaseCompat();", COMPAT_F)
        self.assertIn("TravelReleaseEntryGuidps();", COMPAT_F_CPP)
        # Turtle's index is a shared value other code reads: nothing to release.
        self.assertIn("inline void TravelEntryGuidpsReleaseCompat() {}", COMPAT_T)
        # Only templates with an indexed spawn enter the purpose map (both loops).
        purpose = func(COMPAT_F_CPP, "EntryTravelPurposeMap const& TravelEntryPurposeMapData()")
        self.assertEqual(purpose.count("spawns.find("), 2)


class PathPoints(unittest.TestCase):
    def test_path_points_are_appended_in_place(self):
        body = func(TRAVEL, "void TravelNodeMap::loadNodeStore()")
        self.assertIn("path->appendPathPoint(WorldPosition(", body)
        self.assertNotIn("std::vector<WorldPosition> ppath = path->GetPath();", body)
        header = (ROOT / "ai/playerbot/TravelNode.h").read_text()
        self.assertIn("path.push_back(point);", func(header, "appendPathPoint"))

    def test_the_graph_load_reads_the_node_store(self):
        body = func(COMPAT_F_CPP, "void TravelGraphLoadCompat()")
        self.assertLess(body.index("sTravelNodeMap.loadNodeStore();"), body.index("sTravelNodeMap.generateAll();"))
        self.assertIn("if (sPlayerbotAIConfig.generateTravelNodes)", body)


class ReviewFixes(unittest.TestCase):
    """The round-A review's findings (claude-w4a.md, codex-w4a-r2.txt). The decisions are exercised by standalone tests
    (test_travel_graph_policy.cpp, test_travel_purpose_gate_policy.cpp); these pin that the production code asks them."""

    def test_reachability_and_map_transfers_ask_the_link_rule(self):
        node_map = func(TRAVEL, "std::vector<TravelNode*> TravelNode::getNodeMap(")
        self.assertIn("ai::travel_graph::CollectReachable<TravelNode*>", node_map)
        self.assertIn("TravelLinkUsableCompat(uint8(path->getPathType()))", node_map)
        transfers = func(MGR, "void TravelMgr::LoadMapTransfers()")
        self.assertIn("ai::travel_graph::MapTransferWanted(TravelLinkUsableCompat(uint8(path->getPathType()))", transfers)
        self.assertLess(transfers.index("MapTransferWanted"), transfers.index("AddMapTransfer("))
        # Teleport-spell and static-portal links are refused too until W5 (D4), and the tram exit is off.
        self.assertIn("type != kTeleportSpell && type != kStaticPortal", GRAPH_POLICY)
        tram = func(TRAVEL, "bool LeaveDeeprunTram(")
        self.assertLess(tram.index("TravelTramExitAllowedCompat()"), tram.index("TeleportTo"))
        self.assertIn("return false;", func(COMPAT_FN, "inline bool TravelTramExitAllowedCompat()"))
        self.assertIn("return true;", func(COMPAT_TN, "inline bool TravelTramExitAllowedCompat()"))

    def test_max_creature_comes_from_the_classic_level(self):
        helper = func(COMPAT_FN, "inline uint32 CreatureTemplateMaxLevelCompat(")
        self.assertIn("CreatureLevelMaxWL4(info->Entry)", helper)
        self.assertNotRegex(helper, r"return 0;")
        cost = func(TRAVEL, "void TravelNodePath::calculateCost(")
        self.assertIn("ai::travel_graph::RecordCreatureLevel(maxLevelCreature, CreatureTemplateMaxLevelCompat(cInfo)", cost)

    def test_an_unknown_creature_level_is_not_zero(self):
        body = func(COMPAT_F, "inline int32 CreatureLevelMaxWL4(")
        self.assertIn("ai::travel_level::FromClassicRow(", body)
        self.assertNotIn(": 0;", body)
        # Turtle keeps its template level.
        self.assertIn("info->level_max", COMPAT_T)

    def test_the_f_needs_use_the_tested_policies(self):
        need = func(COMPAT_F_CPP, "bool TravelPurposeNeedCompat(")
        self.assertIn("travel_purpose::GrindPhaseOpen(", need)
        self.assertNotIn("rpgPhase <= 45", need)
        self.assertIn("travel_purpose::VendorErrandNeeded(", need)
        self.assertIn('AI_VALUE(bool, "vendor trip needed")', need)
        self.assertNotIn('"should sell"', need)
        self.assertNotIn('"can sell"', need)
        self.assertIn('values.count("vendor trip needed")', COMPAT_F_CPP)

    def test_the_area_team_is_the_cores(self):
        body = func(COMPAT_F, "inline uint32 AreaTeamWL4(")
        self.assertIn("AreaTeamCompat(area)", body)
        self.assertNotIn("return AREATEAM_NONE;", body)

    def test_turtle_keeps_the_legacy_zone_level_table_check_and_the_log_line(self):
        self.assertIn("SHOW TABLES LIKE '%s'", COMPAT_T)
        self.assertIn("return false;", func(COMPAT_F, "inline bool TravelWorldTableMissingCompat("))
        levels = func(MGR, "void TravelMgr::LoadAreaLevelRows()")
        self.assertIn('TravelWorldTableMissingCompat("ai_playerbot_zone_level")', levels)
        self.assertLess(levels.index("TravelWorldTableMissingCompat"), levels.index("TravelWorldQueryCompat("))
        loader = func(MGR, "void TravelMgr::LoadQuestTravelTable()")
        self.assertLess(loader.index('"Opening log files (previous run retained as .1)."'), loader.index("TravelOpenRunLogsCompat();"))

    def test_the_loader_refuses_cross_map_walk_links(self):
        body = func(TRAVEL, "void TravelNodeMap::loadNodeStore()")
        self.assertIn("ai::travel_graph::WalkLinkMapsConsistent(", body)
        self.assertIn("ai::travel_graph::WalkPathPointOnLinkMap(", body)
        self.assertIn("TravelLoaderChecksWalkMapsCompat()", body)
        self.assertIn("return true;", func(COMPAT_FN, "inline bool TravelLoaderChecksWalkMapsCompat()"))
        self.assertIn("return false;", func(COMPAT_TN, "inline bool TravelLoaderChecksWalkMapsCompat()"))

    def test_the_saver_writes_in_one_transaction(self):
        self.assertIn("WorldDatabase.BeginTransaction()", func(COMPAT_FN, "inline void TravelDbBeginCompat()"))
        self.assertIn("WorldDatabase.DirectCommitTransaction(transaction)", func(COMPAT_FN, "inline void TravelDbCommitCompat()"))
        execute = func(COMPAT_FN, "inline void TravelNodeExecuteCompat(")
        self.assertIn("transaction->Append(", execute)
        save = func(TRAVEL, "void TravelNodeMap::saveNodeStore(")
        self.assertLess(save.index("TravelDbBeginCompat();"), save.index("DELETE FROM ai_playerbot_travelnode"))
        self.assertLess(save.rindex("TravelNodeExecuteCompat("), save.index("TravelDbCommitCompat();"))

    def test_the_area_level_cache_is_filled_at_boot_and_then_read_only(self):
        rows = func(MGR, "void TravelMgr::LoadAreaLevels()")
        self.assertLess(rows.index("LoadAreaLevelRows();"), rows.index("FreezeAreaLevels();"))
        freeze = func(MGR, "void TravelMgr::FreezeAreaLevels()")
        self.assertIn("TravelPrecomputeAreaLevelsCompat()", freeze)
        self.assertLess(freeze.index("GetAreaLevel(areaId);"), freeze.index("areaLevelsFrozen = true;"))
        get = func(MGR, "int32 TravelMgr::GetAreaLevel(uint32 area_id)")
        self.assertLess(get.index("if (areaLevelsFrozen)"), get.index("areaLevels[area_id]"))
        self.assertIn("return true;", func(COMPAT_F, "inline bool TravelPrecomputeAreaLevelsCompat()"))
        self.assertIn("return false;", func(COMPAT_T, "inline bool TravelPrecomputeAreaLevelsCompat()"))

    def test_a_released_spawn_index_is_not_rebuilt_silently(self):
        data = func(COMPAT_F_CPP, "EntryGuidps const& TravelEntryGuidpsData()")
        self.assertLess(data.index("TravelEntryGuidpsReleased()"), data.index("TravelBuildEntryGuidps()"))
        self.assertIn("sLog.outError(", data)
        self.assertIn("TravelEntryGuidpsReleased() = true;", func(COMPAT_F_CPP, "void TravelReleaseEntryGuidps()"))

    def test_the_boot_check_refuses_a_database_that_is_not_scratch(self):
        call = BOOT_CHECK.index("\nrefuse_unless_scratch\n")
        self.assertLess(call, BOOT_CHECK.index("trap cleanup EXIT"))
        self.assertLess(call, BOOT_CHECK.index("for s in $WANT"))
        self.assertNotIn("DB=${FOREVER_WORLD_DB:-world}", BOOT_CHECK)


if __name__ == "__main__":
    unittest.main()

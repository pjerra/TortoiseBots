"""Wiring pins for the travel slice W4, round A1 (the node graph on ForeverCore).

The decisions themselves are text only here: the graph is read through the schema guard, a taxi or transport link is loaded but
never used on ForeverCore, the generator runs its map passes on the world thread there, and the stand-ins of the Travel block in
SliceLinkStubs are gone now that ai/playerbot/TravelNode.cpp is in the slice (FOREVER-W0-W4-SPEC 2.3, W4-A1).

Run: python3 -m unittest forever/tools/test_wl4_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
TRAVEL = (ROOT / "ai/playerbot/TravelNode.cpp").read_text()
COMPAT_F = (ROOT / "forever/src/compat/TbCompat_WL4n.h").read_text()
COMPAT_T = (ROOT / "ai/cmangos-compat-wl4n.h").read_text()
WRAPPER = (ROOT / "ai/playerbot/TbCompatWL4n.h").read_text()
STUBS = (ROOT / "forever/src/slice/SliceLinkStubs.cpp").read_text()
LIST = (ROOT / "forever/SLICE-W4.txt").read_text()
LIST_W1 = (ROOT / "forever/SLICE-W1.txt").read_text()
CMAKE = (ROOT / "CMakeLists.txt").read_text()
SCHEMA = (ROOT / "data/sql/world/20260824090000_world.sql").read_text()


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
        f = COMPAT_F
        t = COMPAT_T
        self.assertIn("return pathType != 3 && pathType != 4;", f)
        self.assertIn("return true;", func(t, "inline bool TravelLinkUsableCompat"))
        # The A* is the only user: every link cost goes through getCost.
        route = func(TRAVEL, "TravelNodeRoute TravelNodeMap::getRoute(TravelNode* start, TravelNode* goal")
        self.assertIn("link.second->getCost(costContext, currentNode->currentGold)", route)


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
        self.assertIn("return false;", func(COMPAT_F, "inline bool TravelGenParallelCompat()"))
        self.assertIn("return true;", func(COMPAT_T, "inline bool TravelGenParallelCompat()"))

    def test_the_mmap_tile_load_is_the_cores_own(self):
        f = func(COMPAT_F, "inline bool MmapTileLoadCompat")
        self.assertIn("LoadMapAndVMap(x, y)", f)
        self.assertIn("LoadMMap(instanceId, x, y)", f)


class StandIns(unittest.TestCase):
    def test_the_travel_node_stand_ins_are_gone(self):
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
        # The one TravelMgr call the generator makes is answered (TravelMgr.cpp is W4-A2).
        self.assertIn("void TravelMgr::LoadMapTransfers()", STUBS)

    def test_the_slice_list_and_the_cmake_pointer(self):
        paths = [line.strip() for line in LIST.splitlines() if line.strip() and not line.startswith("#")]
        w1 = [line.strip() for line in LIST_W1.splitlines() if line.strip() and not line.startswith("#")]
        self.assertEqual(paths[:len(w1)], w1, "the merged W1 list comes first, unchanged")
        self.assertIn("ai/playerbot/TravelNode.cpp", paths)
        self.assertTrue((ROOT / "ai/playerbot/TravelNode.cpp").exists())
        self.assertIn("forever/SLICE-W4.txt", CMAKE)
        self.assertNotIn("set(TB_SLICE_LIST ${CMAKE_CURRENT_SOURCE_DIR}/forever/SLICE-W1.txt", CMAKE)

    def test_the_pair_and_its_wrapper(self):
        self.assertIn('#include "cmangos-compat-wl4n.h"', WRAPPER)
        self.assertIn('#if __has_include("TbCompat_WL4n.h")', WRAPPER)
        self.assertIn('#include "playerbot/TbCompatWL4n.h"', TRAVEL)
        for name in ("TravelNodeQueryCompat", "TravelGenParallelCompat", "MmapTileLoadCompat", "TravelLinkUsableCompat"):
            self.assertIn(name, COMPAT_F)
            self.assertIn(name, COMPAT_T)


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
"""Pins the Forever config-key census behind forever/tools/gen_aiplayerbot_conf.py.

The generator is run by tools/verify_all.sh (section 8c) and refuses to write a template that advertises a key no
compiled file consumes. These tests pin the two halves that a plausible mistake would break: the compiled file list
(read from CMakeLists.txt, never hand kept) and the wired/unread classification that decides what ships.

Run: python3 -m unittest forever/tools/test_config_key_wiring.py
"""
import importlib.util
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
SPEC = importlib.util.spec_from_file_location(
    "gen_aiplayerbot_conf", ROOT / "forever" / "tools" / "gen_aiplayerbot_conf.py")
GEN = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GEN)


class CompiledFileList(unittest.TestCase):
    def setUp(self):
        self.sources, _ = GEN.compiled_sources()
        self.relative = {GEN._relative(path) for path in self.sources}

    def test_the_legacy_loader_is_compiled(self):
        self.assertIn("ai/playerbot/PlayerbotAIConfig.cpp", self.relative)

    def test_files_the_slice_block_adds_are_compiled(self):
        self.assertIn("forever/src/slice/SliceAiFactory.cpp", self.relative)
        self.assertIn("forever/src/host/TbHostBotManager.cpp", self.relative)
        self.assertIn("runtime/CombatMonitorService.cpp", self.relative)

    def test_files_the_build_definition_excludes_stay_out(self):
        self.assertNotIn("forever/src/host/TbH3Off.cpp", self.relative)   # the option-OFF stub
        self.assertNotIn("ai/playerbot/AiFactory.cpp", self.relative)     # replaced by the slice
        self.assertNotIn("runtime/RandomBotService.cpp", self.relative)   # Turtle-only runtime

    def test_headers_included_by_a_compiled_unit_are_in_the_closure(self):
        _, include_dirs = GEN.compiled_sources()
        closure = set(GEN.include_closure(self.sources, include_dirs))
        self.assertIn("runtime/DungeonRunPolicy.h", closure)


class Classification(unittest.TestCase):
    def test_member_consumed_by_a_compiled_file_is_wired(self):
        entries = {"AiPlayerbot.On": {}, "AiPlayerbot.Off": {}, "AiPlayerbot.Direct": {}}
        keys_by_member = {"onMember": {"AiPlayerbot.On"}, "offMember": {"AiPlayerbot.Off"}}
        findings = ["offMember: AiPlayerbot.Off: read but used nowhere"]
        engine_index = {
            "forever/src/host/Consumer.cpp": {"onMember"},
            "ai/playerbot/PlayerbotAIConfig.h": {"offMember"},
            "ai/playerbot/PlayerbotAIConfig.cpp#Accessor": {"offMember"},
        }
        read_keys = {"AiPlayerbot.On", "AiPlayerbot.Off", "AiPlayerbot.Direct"}
        wired, readers = GEN.classify(entries, read_keys, keys_by_member, findings, engine_index, set())
        self.assertTrue(wired["AiPlayerbot.On"])
        self.assertFalse(wired["AiPlayerbot.Off"])
        # Values used at the read itself have no member and are consumed by the compiled loader.
        self.assertTrue(wired["AiPlayerbot.Direct"])
        self.assertEqual(readers["AiPlayerbot.On"], ["forever/src/host/Consumer.cpp"])

    def test_an_allowlisted_member_keeps_its_key(self):
        entries = {"AiPlayerbot.Kept": {}}
        findings = ["keptMember: AiPlayerbot.Kept: read but used nowhere"]
        wired, _ = GEN.classify(entries, {"AiPlayerbot.Kept"}, {"keptMember": {"AiPlayerbot.Kept"}},
                                findings, {}, {"keptMember"})
        self.assertTrue(wired["AiPlayerbot.Kept"])

    def test_a_key_no_compiled_file_reads_stays_unwired(self):
        entries = {"AiPlayerbot.Unread": {}}
        wired, _ = GEN.classify(entries, set(), {"member": {"AiPlayerbot.Unread"}}, [], {}, set())
        self.assertFalse(wired["AiPlayerbot.Unread"])

    def test_an_unread_key_listed_in_the_config_is_caught(self):
        wired = {"AiPlayerbot.On": True, "AiPlayerbot.Off": False}
        text = ("# AiPlayerbot.On  (bool, PlayerbotAIConfig.cpp:1)\n"
                "AiPlayerbot.On = 1\n"
                "AiPlayerbot.Off = 0\n")
        self.assertEqual(GEN.advertised_unread(text, wired), ["AiPlayerbot.Off"])

    def test_a_commented_computed_default_is_still_advertised(self):
        wired = {"AiPlayerbot.Computed": True}
        text = "# AiPlayerbot.Computed =\n#   default is computed in the code: 6 * 3600\n"
        self.assertEqual(GEN.advertised_unread(text, wired), [])


class Render(unittest.TestCase):
    def test_unread_keys_are_not_shipped(self):
        entries = {
            "AiPlayerbot.On": {"kind": "Bool", "lit": "1", "expr": "true", "line": 1, "notes": []},
            "AiPlayerbot.Off": {"kind": "Bool", "lit": "0", "expr": "false", "line": 2, "notes": []},
        }
        out = GEN.render(entries, ["AiPlayerbot.On", "AiPlayerbot.Off"], 0,
                         {"AiPlayerbot.On": True, "AiPlayerbot.Off": False})
        self.assertIn("AiPlayerbot.On = 1", out)
        self.assertNotIn("AiPlayerbot.Off = 0", out)
        self.assertIn("1 key(s) the loader reads are consumed by no compiled file", out)

    def test_docs_mark_both_states(self):
        entries = {
            "AiPlayerbot.On": {"kind": "Bool", "lit": "1", "expr": "true", "line": 1, "notes": []},
            "AiPlayerbot.Off": {"kind": "Bool", "lit": "0", "expr": "false", "line": 2, "notes": []},
        }
        docs = GEN.render_docs(entries, ["AiPlayerbot.On", "AiPlayerbot.Off"],
                               {"AiPlayerbot.On": True, "AiPlayerbot.Off": False},
                               {"AiPlayerbot.On": ["forever/src/host/Consumer.cpp"], "AiPlayerbot.Off": []})
        self.assertIn("| `AiPlayerbot.On` | `1` | bool | yes |", docs)
        self.assertIn("| `AiPlayerbot.Off` | `0` | bool | no |", docs)
        self.assertIn("**not wired**", docs)


class LiveRepository(unittest.TestCase):
    def test_the_generator_check_passes_on_the_shipped_files(self):
        old_argv = sys.argv
        sys.argv = ["gen_aiplayerbot_conf.py", "--check"]
        try:
            self.assertEqual(GEN.main(), 0)
        finally:
            sys.argv = old_argv
        self.assertTrue((ROOT / "forever" / "conf" / "aiplayerbot.conf.dist").is_file())
        self.assertTrue((ROOT / "docs" / "server" / "config-keys.md").is_file())


if __name__ == "__main__":
    unittest.main()

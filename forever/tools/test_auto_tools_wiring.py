#!/usr/bin/env python3
"""Wiring pins for profession auto-tools (forever/src/host/TbAutoTools.{h,cpp} and hooks).

Run:
    python3 forever/tools/test_auto_tools_wiring.py
    python3 forever/tools/test_auto_tools_wiring.py --mutate
"""
import re
import sys
import unittest
from pathlib import Path

FOREVER = Path(__file__).resolve().parent.parent
ROOT = FOREVER.parent


def sources():
    return {
        "scripts": (FOREVER / "src" / "TbScripts.cpp").read_text(),
        "host": (FOREVER / "src" / "host" / "TbAutoTools.cpp").read_text(),
        "host_h": (FOREVER / "src" / "host" / "TbAutoTools.h").read_text(),
        "prof": (FOREVER / "src" / "host" / "TbHostProfession.cpp").read_text(),
        "conf": (FOREVER / "conf" / "tortoisebots.conf.dist").read_text(),
        "cmake": (ROOT / "CMakeLists.txt").read_text(),
    }


def member_body(text, signature):
    start = text.index(signature)
    end = text.index("\n    void ", start + len(signature))
    return text[start:end]


def check_login_sync(s):
    body = member_body(s["scripts"], "void OnLogin(Player* player, bool /*firstLogin*/) override")
    assert "if (BotMgr::Instance().IsBot(player->GetGUID()))" in body
    assert 'TortoiseBots::autotools::Sync(player, "login");' in body


def check_level_sync(s):
    body = member_body(s["scripts"], "void OnLevelChanged(Player* player, uint8 /*oldLevel*/) override")
    assert "BotMgr::Instance().IsBot(player->GetGUID())" in body
    assert 'TortoiseBots::autotools::Sync(player, "level");' in body


def check_config_load(s):
    body = member_body(s["scripts"], "void OnConfigLoad(bool reload) override")
    assert "TortoiseBots::autotools::LoadConfig();" in body


def check_conf_key(s):
    assert re.search(r"^TortoiseBots\.AutoTools = 1$", s["conf"], re.M)
    assert 'GetBoolDefault("TortoiseBots.AutoTools", true)' in s["host"]


def check_pool_free_guard_and_bag_store(s):
    assert "!mgr.IsPoolBot(guid) && !mgr.IsFreeBot(guid)" in s["host"]
    assert "CanStoreNewItem(INVENTORY_SLOT_BAG_0, NULL_SLOT, dest, itemId, 1)" in s["host"]
    assert "StoreNewItemInBestSlots" not in s["host"]


def check_prof_reuse(s):
    assert "TortoiseBots::autotools::HasRequiredGatherTool(" in s["prof"]


def check_off_build_has_it(s):
    source = "forever/src/host/TbAutoTools.cpp"
    assert (ROOT / source).is_file()
    assert "${CMAKE_CURRENT_SOURCE_DIR}/forever/src/*.cpp" in s["cmake"]
    match = re.search(r'EXCLUDE REGEX "/forever/src/\((.*?)\)\\\\\.cpp\$"', s["cmake"])
    assert match, "the module's source glob filter is missing"
    excluded = re.compile(r"/forever/src/(%s)\.cpp$" % match.group(1))
    assert not excluded.search("/" + source), "the OFF build would leave TbAutoTools out"


MUTATIONS = {
    "login_sync": ("scripts", '            TortoiseBots::autotools::Sync(player, "login");\n', ""),
    "level_sync": ("scripts", '            TortoiseBots::autotools::Sync(player, "level");', '            TortoiseBots::autotools::Sync(player, "login");'),
    "config_load": ("scripts", "        TortoiseBots::autotools::LoadConfig();", ""),
    "conf_key": ("conf", "TortoiseBots.AutoTools = 1", "TortoiseBots.AutoTools = 0"),
    "pool_free_guard": ("host", "if (!mgr.IsPoolBot(guid) && !mgr.IsFreeBot(guid))", "if (false)"),
    "prof_reuse": ("prof", "TortoiseBots::autotools::HasRequiredGatherTool(", "HasLegacyTool("),
    "off_build_has_it": ("cmake", "host/TbHost[^/]*", "host/TbHost[^/]*|host/TbAutoTools"),
}

CHECKS = {
    "login_sync": check_login_sync,
    "level_sync": check_level_sync,
    "config_load": check_config_load,
    "conf_key": check_conf_key,
    "pool_free_guard": check_pool_free_guard_and_bag_store,
    "prof_reuse": check_prof_reuse,
    "off_build_has_it": check_off_build_has_it,
}


class AutoToolsWiring(unittest.TestCase):
    pass


for _name, _fn in CHECKS.items():
    def _make_test(fn):
        return lambda self: fn(sources())
    setattr(AutoToolsWiring, "test_" + _name, _make_test(_fn))


def run_mutations():
    base = sources()
    for name, (key, old, new) in MUTATIONS.items():
        assert old in base[key], f"mutation {name}: target not found in {key}"
        mutated = dict(base)
        mutated[key] = base[key].replace(old, new, 1)
        try:
            CHECKS[name](mutated)
        except AssertionError:
            continue
        raise AssertionError(f"mutation {name} was NOT caught!")
    print(f"MUTATE OK ({len(MUTATIONS)} mutations caught)")


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        sys.argv.remove("--mutate")
        run_mutations()
    unittest.main()

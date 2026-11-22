#!/usr/bin/env python3
"""Wiring pins for automatic starter hunter pets (forever/src/host/TbAutoHunterPet.{h,cpp} and its hooks).

Run:
    python3 forever/tools/test_auto_hunter_pet_wiring.py
    python3 forever/tools/test_auto_hunter_pet_wiring.py --mutate
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
        "host": (FOREVER / "src" / "host" / "TbAutoHunterPet.cpp").read_text(),
        "host_h": (FOREVER / "src" / "host" / "TbAutoHunterPet.h").read_text(),
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
    assert 'TortoiseBots::autohunterpet::Sync(player, "login");' in body


def check_level_sync(s):
    body = member_body(s["scripts"], "void OnLevelChanged(Player* player, uint8 /*oldLevel*/) override")
    assert "BotMgr::Instance().IsBot(player->GetGUID())" in body
    assert 'TortoiseBots::autohunterpet::Sync(player, "level");' in body


def check_config_load(s):
    body = member_body(s["scripts"], "void OnConfigLoad(bool reload) override")
    assert "TortoiseBots::autohunterpet::LoadConfig();" in body


def check_conf_key(s):
    assert re.search(r"^TortoiseBots\.AutoHunterPet = 1$", s["conf"], re.M)
    assert 'GetBoolDefault("TortoiseBots.AutoHunterPet", true)' in s["host"]


def check_factory_call(s):
    assert "Decide(in)" in s["host"]
    assert "factory.InitPet();" in s["host"]
    assert "factory.InitPetSpells();" in s["host"]
    for declared in ("void LoadConfig();", "bool Enabled();", "bool Sync(Player* bot, char const* why);"):
        assert declared in s["host_h"]


def check_off_build_has_it(s):
    source = "forever/src/host/TbAutoHunterPet.cpp"
    assert (ROOT / source).is_file()
    assert "${CMAKE_CURRENT_SOURCE_DIR}/forever/src/*.cpp" in s["cmake"]
    match = re.search(r'EXCLUDE REGEX "/forever/src/\((.*?)\)\\\\\.cpp\$"', s["cmake"])
    assert match, "the module's source glob filter is missing"
    excluded = re.compile(r"/forever/src/(%s)\.cpp$" % match.group(1))
    assert not excluded.search("/" + source), "the OFF build would leave the host half out"


MUTATIONS = {
    "login_sync": ("scripts", '            TortoiseBots::autohunterpet::Sync(player, "login");\n', ""),
    "level_sync": ("scripts", '            TortoiseBots::autohunterpet::Sync(player, "level");',
                   '            TortoiseBots::autohunterpet::Sync(player, "login");'),
    "config_load": ("scripts", "        TortoiseBots::autohunterpet::LoadConfig();", ""),
    "conf_key": ("conf", "TortoiseBots.AutoHunterPet = 1", "TortoiseBots.AutoHunterPet = 0"),
    "factory_call": ("host", "    factory.InitPet();\n", ""),
    "off_build_has_it": ("cmake", "host/TbHost[^/]*", "host/TbHost[^/]*|host/TbAutoHunterPet"),
}

CHECKS = {
    "login_sync": check_login_sync,
    "level_sync": check_level_sync,
    "config_load": check_config_load,
    "conf_key": check_conf_key,
    "factory_call": check_factory_call,
    "off_build_has_it": check_off_build_has_it,
}


class AutoHunterPetWiring(unittest.TestCase):
    def test_login_sync(self):
        check_login_sync(sources())

    def test_level_sync(self):
        check_level_sync(sources())

    def test_config_load(self):
        check_config_load(sources())

    def test_conf_key(self):
        check_conf_key(sources())

    def test_factory_call(self):
        check_factory_call(sources())

    def test_off_build_has_it(self):
        check_off_build_has_it(sources())


def run_mutations():
    base = sources()
    failed = []
    for name, (key, old, new) in MUTATIONS.items():
        if old not in base[key]:
            failed.append(f"{name}: target text not found in {key}")
            continue
        mutated = dict(base)
        mutated[key] = base[key].replace(old, new, 1)
        try:
            CHECKS[name](mutated)
        except AssertionError:
            continue
        failed.append(f"{name}: mutation was NOT caught")
    if failed:
        print("MUTATE FAIL:\n  " + "\n  ".join(failed))
        return 1
    print(f"MUTATE OK: {len(MUTATIONS)}/{len(MUTATIONS)} mutations caught")
    return 0


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        sys.exit(run_mutations())
    unittest.main()

#!/usr/bin/env python3
"""Wiring pins for automatic class-spell training (forever/src/host/TbAutoTrain.{h,cpp} and its hooks).

Run:
    python3 forever/tools/test_autotrain_wiring.py
    python3 forever/tools/test_autotrain_wiring.py --mutate
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
        "host": (FOREVER / "src" / "host" / "TbAutoTrain.cpp").read_text(),
        "host_h": (FOREVER / "src" / "host" / "TbAutoTrain.h").read_text(),
        "conf": (FOREVER / "conf" / "tortoisebots.conf.dist").read_text(),
        "docs": (ROOT / "docs" / "server" / "configuration.md").read_text(),
        "cmake": (ROOT / "CMakeLists.txt").read_text(),
    }


def member_body(text, signature):
    start = text.index(signature)
    end = text.index("\n    void ", start + len(signature))
    return text[start:end]


def check_login_sync(s):
    body = member_body(s["scripts"], "void OnLogin(Player* player, bool /*firstLogin*/) override")
    assert "if (BotMgr::Instance().IsBot(player->GetGUID()))" in body
    assert 'tbhost::autotrain::Sync(player, "login");' in body


def check_level_sync(s):
    body = member_body(s["scripts"], "void OnLevelChanged(Player* player, uint8 /*oldLevel*/) override")
    assert "BotMgr::Instance().IsBot(player->GetGUID())" in body
    assert 'tbhost::autotrain::Sync(player, "level");' in body


def check_config_and_index(s):
    cfg = member_body(s["scripts"], "void OnConfigLoad(bool reload) override")
    assert "tbhost::autotrain::LoadConfig();" in cfg
    assert "tbhost::autotrain::BuildIndex();" in cfg
    init = member_body(s["scripts"], "void OnWorldInitialized() override")
    assert "tbhost::autotrain::LoadConfig();" in init
    assert "tbhost::autotrain::BuildIndex();" in init
    assert init.index("tbcompat::InitSpellTable();") < init.index("tbhost::autotrain::BuildIndex();")
    rel = member_body(s["scripts"], "void OnAfterReload(std::string_view command) override")
    assert "tbhost::autotrain::BuildIndex();" in rel


def check_conf_and_docs(s):
    assert re.search(r"^TortoiseBots\.AutoLearnOnLevel = 1$", s["conf"], re.M)
    assert 'GetBoolDefault("TortoiseBots.AutoLearnOnLevel", true)' in s["host"]
    assert "TortoiseBots.AutoLearnOnLevel" in s["docs"]


def check_handle_train_delegates(s):
    start = s["scripts"].index("static bool HandleTrain(")
    end = s["scripts"].index("\n    static bool HandleHurt(", start)
    body = s["scripts"][start:end]
    assert "tbhost::autotrain::TeachClassSpells(bot, tally);" in body


def check_off_build_has_it(s):
    assert (ROOT / "forever/src/host/TbAutoTrain.cpp").is_file()
    match = re.search(r'EXCLUDE REGEX "/forever/src/\((.*?)\)\\\\\.cpp\$"', s["cmake"])
    assert match
    assert not re.search(match.group(1), "host/TbAutoTrain"), "TbAutoTrain must compile in both ON and OFF builds"


PINS = {
    "login_sync": check_login_sync,
    "level_sync": check_level_sync,
    "config_and_index": check_config_and_index,
    "conf_and_docs": check_conf_and_docs,
    "handle_train_delegates": check_handle_train_delegates,
    "off_build_has_it": check_off_build_has_it,
}

MUTATIONS = {
    "login_sync": ("scripts", 'tbhost::autotrain::Sync(player, "login");', "// removed"),
    "level_sync": ("scripts", 'tbhost::autotrain::Sync(player, "level");', "// removed"),
    "config_and_index": ("scripts", "tbhost::autotrain::LoadConfig();", "// removed"),
    "conf_and_docs": ("conf", "TortoiseBots.AutoLearnOnLevel = 1", "TortoiseBots.AutoLearnOnLevel = 0"),
    "handle_train_delegates": ("scripts", "tbhost::autotrain::TeachClassSpells(bot, tally);", "// removed"),
    "off_build_has_it": ("cmake", "host/TbHost[^/]*", "host/TbHost[^/]*|host/TbAutoTrain"),
}


class AutoTrainWiring(unittest.TestCase):
    pass


for _name, _fn in PINS.items():
    def _make(fn):
        return lambda self: fn(sources())
    setattr(AutoTrainWiring, f"test_{_name}", _make(_fn))


def run_mutations():
    base = sources()
    failed = 0
    for name, fn in PINS.items():
        key, old, new = MUTATIONS[name]
        mutated = dict(base)
        assert old in mutated[key], f"mutation {name}: target not found"
        mutated[key] = mutated[key].replace(old, new, 1)
        try:
            fn(mutated)
        except AssertionError:
            print(f"mutation {name}: caught")
        else:
            print(f"mutation {name}: NOT CAUGHT")
            failed += 1
    return 1 if failed else 0


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        sys.argv.remove("--mutate")
        raise SystemExit(run_mutations())
    unittest.main()

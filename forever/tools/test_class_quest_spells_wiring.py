#!/usr/bin/env python3
"""Wiring pins for the class-quest spells (forever/src/host/TbClassQuestSpells.{h,cpp} and its hooks).

Run on the build VM (the core pins read the slot's source tree):
    python3 forever/tools/test_class_quest_spells_wiring.py
    FOREVER_CORE=~/forever-build/src-4 python3 forever/tools/test_class_quest_spells_wiring.py
    FOREVER_CORE=~/forever-build/src-4 python3 forever/tools/test_class_quest_spells_wiring.py --mutate

--mutate applies the one mutation of each pin and requires the pin to fail; it exits 0 only when every mutation is caught.
"""
import os
import re
import sys
import unittest
from pathlib import Path

FOREVER = Path(__file__).resolve().parent.parent
ROOT = FOREVER.parent
CORE = Path(os.environ.get("FOREVER_CORE", str(Path.home() / "forever-build" / "src-4")))


def sources():
    return {
        "scripts": (FOREVER / "src" / "TbScripts.cpp").read_text(),
        "host": (FOREVER / "src" / "host" / "TbClassQuestSpells.cpp").read_text(),
        "host_h": (FOREVER / "src" / "host" / "TbClassQuestSpells.h").read_text(),
        "conf": (FOREVER / "conf" / "tortoisebots.conf.dist").read_text(),
        "cmake": (ROOT / "CMakeLists.txt").read_text(),
        "questdef": (CORE / "src/server/game/Quests/QuestDef.h").read_text(),
        "player": (CORE / "src/server/game/Entities/Player/Player.cpp").read_text(),
        "scriptmgr": (CORE / "src/server/game/Scripting/ScriptMgr.h").read_text(),
    }


def member_body(text, signature):
    start = text.index(signature)
    end = text.index("\n    void ", start + len(signature))
    return text[start:end]


def check_login_sync(s):
    body = member_body(s["scripts"], "void OnLogin(Player* player, bool /*firstLogin*/) override")
    assert "if (BotMgr::Instance().IsBot(player->GetGUID()))" in body
    assert 'TortoiseBots::questspells::Sync(player, "login");' in body


def check_level_sync(s):
    body = member_body(s["scripts"], "void OnLevelChanged(Player* player, uint8 /*oldLevel*/) override")
    assert "BotMgr::Instance().IsBot(player->GetGUID())" in body
    assert 'TortoiseBots::questspells::Sync(player, "level");' in body


def check_config_load(s):
    body = member_body(s["scripts"], "void OnConfigLoad(bool reload) override")
    assert "TortoiseBots::questspells::LoadConfig();" in body
    # A config reload builds (switch on) or drops (switch off) the index at once.
    load_at = body.index("TortoiseBots::questspells::LoadConfig();")
    assert "if (reload)\n            TortoiseBots::questspells::BuildIndex();" in body[load_at:]


def check_world_init_builds(s):
    body = member_body(s["scripts"], "void OnWorldInitialized() override")
    assert "TortoiseBots::questspells::BuildIndex();" in body
    # The spell store must be ready before the index reads it.
    assert body.index("tbcompat::InitSpellTable();") < body.index("TortoiseBots::questspells::BuildIndex();")


def check_reload_rebuilds(s):
    body = member_body(s["scripts"], "void OnAfterReload(std::string_view command) override")
    build_at = body.index("TortoiseBots::questspells::BuildIndex();")
    assert 'command == "reload all"' in body[:build_at]
    # Not gated on the switch: a disabled server must still drop the index that points into the replaced quest store.
    assert "questspells::Enabled()" not in body[:build_at]
    assert 'command.find("quest")' in body[:build_at]
    # Outside the slice block: the index is not slice-1 code.
    if "#ifdef TORTOISEBOTS_SLICE1" in body:
        assert build_at < body.index("#ifdef TORTOISEBOTS_SLICE1")


def check_conf_key(s):
    assert re.search(r"^TortoiseBots\.ClassQuestSpells = 0$", s["conf"], re.M)
    assert 'GetBoolDefault("TortoiseBots.ClassQuestSpells", false)' in s["host"]


def check_learn_only(s):
    assert "bot->LearnSpell(row->spellId, false);" in s["host"]
    assert "CastSpell(" not in s["host"]
    assert "LearnQuestRewardedSpells" not in s["host"]
    assert "SetQuestStatus" not in s["host"]
    assert "CollectTaught(" in s["host"] and "Decide(" in s["host"]
    for declared in ("void LoadConfig();", "bool Enabled();", "void BuildIndex();", "uint32_t Sync(Player* bot, char const* why);"):
        assert declared in s["host_h"]


def check_off_build_has_it(s):
    source = "forever/src/host/TbClassQuestSpells.cpp"
    assert (ROOT / source).is_file()
    assert "${CMAKE_CURRENT_SOURCE_DIR}/forever/src/*.cpp" in s["cmake"]
    match = re.search(r'EXCLUDE REGEX "/forever/src/\((.*?)\)\\\\\.cpp\$"', s["cmake"])
    assert match, "the module's source glob filter is missing"
    excluded = re.compile(r"/forever/src/(%s)\.cpp$" % match.group(1))
    assert not excluded.search("/" + source), "the OFF build would leave the host half out"


def check_core_pins(s):
    assert "int32 GetClassicMinLevel() const" in s["questdef"]
    assert "uint32 GetSrcSpell() const" in s["questdef"]
    assert "std::vector<QuestRewardDisplaySpell> RewardDisplaySpell;" in s["questdef"]
    reward = s["player"].index("if (quest->GetRewSpell() > 0)")
    display = s["player"].index("for (QuestRewardDisplaySpell displaySpell : quest->RewardDisplaySpell)", reward)
    assert reward < display
    assert "virtual void OnLevelChanged(Player* player, uint8 oldLevel);" in s["scriptmgr"]


# One mutation per pin: (source key, old text, new text). Each must make its pin fail.
MUTATIONS = {
    "login_sync": ("scripts", '            TortoiseBots::questspells::Sync(player, "login");\n', ""),
    "level_sync": ("scripts", 'TortoiseBots::questspells::Sync(player, "level");', 'TortoiseBots::questspells::Sync(player, "login");'),
    "world_init_builds": ("scripts", "        if (TortoiseBots::questspells::Enabled())\n            TortoiseBots::questspells::BuildIndex();\n", ""),
    "reload_rebuilds": ("scripts", "        if (command == \"reload all\" || command.find(\"quest\") != std::string_view::npos)\n            TortoiseBots::questspells::BuildIndex();",
                        "        if (TortoiseBots::questspells::Enabled() && (command == \"reload all\" || command.find(\"quest\") != std::string_view::npos))\n            TortoiseBots::questspells::BuildIndex();"),
    "config_reload_builds": ("scripts", "        if (reload)\n            TortoiseBots::questspells::BuildIndex();\n", ""),
    "conf_key": ("conf", "TortoiseBots.ClassQuestSpells = 1", "TortoiseBots.ClassQuestSpells = 0"),
    "config_load": ("scripts", "        TortoiseBots::questspells::LoadConfig();", ""),
    "learn_only": ("host", "bot->LearnSpell(row->spellId, false);", "bot->CastSpell(bot, row->spellId, true);"),
    "off_build_has_it": ("cmake", "host/TbHost[^/]*", "host/TbHost[^/]*|host/TbClassQuestSpells"),
    "core_pins": ("scriptmgr", "virtual void OnLevelChanged(Player* player, uint8 oldLevel);",
                  "void OnLevelChangedRenamed(Player* player, uint8 oldLevel);"),
}

CHECKS = {
    "login_sync": check_login_sync,
    "level_sync": check_level_sync,
    "config_load": check_config_load,
    "world_init_builds": check_world_init_builds,
    "reload_rebuilds": check_reload_rebuilds,
    "config_reload_builds": check_config_load,
    "conf_key": check_conf_key,
    "learn_only": check_learn_only,
    "off_build_has_it": check_off_build_has_it,
    "core_pins": check_core_pins,
}


class ClassQuestSpellsWiring(unittest.TestCase):
    def test_login_sync(self):
        check_login_sync(sources())

    def test_level_sync(self):
        check_level_sync(sources())

    def test_config_load(self):
        check_config_load(sources())

    def test_world_init_builds(self):
        check_world_init_builds(sources())

    def test_reload_rebuilds(self):
        check_reload_rebuilds(sources())

    def test_conf_key(self):
        check_conf_key(sources())

    def test_learn_only(self):
        check_learn_only(sources())

    def test_off_build_has_it(self):
        check_off_build_has_it(sources())

    def test_core_pins(self):
        check_core_pins(sources())


def mutate():
    caught = 0
    for name, (key, old, new) in MUTATIONS.items():
        mutated = sources()
        if old not in mutated[key]:
            print("mutation %s: no longer applies" % name, file=sys.stderr)
            return 1
        mutated[key] = mutated[key].replace(old, new, 1)
        try:
            CHECKS[name](mutated)
        except AssertionError:
            caught += 1
            print("mutation %s: caught" % name)
        else:
            print("mutation %s: SURVIVED" % name, file=sys.stderr)
    print("%d/%d mutations caught" % (caught, len(MUTATIONS)))
    return 0 if caught == len(MUTATIONS) else 1


if __name__ == "__main__":
    if "-h" in sys.argv or "--help" in sys.argv:
        print("usage: test_class_quest_spells_wiring.py [--mutate] [unittest options]\n"
              "  --mutate  apply each source mutation and require its check to fail (exit 0 when all are caught)\n")
    if "--mutate" in sys.argv:
        sys.exit(mutate())
    unittest.main()

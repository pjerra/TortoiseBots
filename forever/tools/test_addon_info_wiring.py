#!/usr/bin/env python3
"""Wiring pins for the addon info readers (TBF #130 f-info).

Run:
  python3 forever/tools/test_addon_info_wiring.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
HOST = ROOT / "forever" / "src" / "host"


def read(path):
    return Path(path).read_text() if Path(path).exists() else ""


def strip_comments(source):
    return re.sub(r"//[^\n]*", "", source)


def check_verbs():
    src = strip_comments(read(HOST / "TbAddonInfo.cpp"))
    for verb in ('"features"', '"run"'):
        if f"RegisterVerb({verb}" not in src:
            raise AssertionError(f"TbAddonInfo.cpp never registers verb {verb}")
    # Only the two polled verbs: mutations belong to no v1 piece yet.
    verbs = re.findall(r"RegisterVerb\(\s*\"([^\"]+)\"", src)
    if sorted(verbs) != ["features", "run"]:
        raise AssertionError(f"unexpected registered verbs: {verbs}")
    for token in ("run:status", "run:here", "run:list"):
        if f'Advertise("{token}")' not in src:
            raise AssertionError(f"TbAddonInfo.cpp never advertises {token}")


def check_header():
    header = read(HOST / "TbAddonInfo.h")
    for pin in ("void RegisterAddonVerbsInfo();", "void AppendCcAssign(Player*"):
        if pin not in header:
            raise AssertionError(f"TbAddonInfo.h lost `{pin}`")


def check_pure_core_free():
    pure = strip_comments(read(HOST / "TbAddonInfoPure.h"))
    for pin in ("Player.h", "Config.h", "ObjectGuid.h", "Group.h", "Chat.h", "class Player"):
        if pin in pure:
            raise AssertionError(f"TbAddonInfoPure.h names core type `{pin}`")


def check_tb_parity():
    src = strip_comments(read(HOST / "TbAddonInfo.cpp"))
    for pin in (
        "TortoiseBots.Slice.EngineByDefault",
        "TortoiseBots.Orders.Tactical",
        "AiPlayerbot.",
        "StatusLine(rec->run)",
        "CcOrderValues(",
        "run::Find(",
        "ai::GetDungeonPlans()",
    ):
        if pin not in src:
            raise AssertionError(f"TbAddonInfo.cpp lost its data source `{pin}`")


def check_scripts_registration():
    scripts = read(ROOT / "forever" / "src" / "TbScripts.cpp")
    if "TortoiseBots::addoninfo::RegisterAddonVerbsInfo();" not in scripts:
        raise AssertionError("TbScripts.cpp never calls RegisterAddonVerbsInfo")
    call = scripts.find("RegisterAddonVerbsInfo();")
    hook = scripts.find("TortoiseBots::addon::AddScripts();")
    if hook < 0 or call < hook:
        raise AssertionError("RegisterAddonVerbsInfo must run after addon::AddScripts()")


def main():
    check_verbs()
    check_header()
    check_pure_core_free()
    check_tb_parity()
    check_scripts_registration()
    print("test_addon_info_wiring: pins hold")

if __name__ == "__main__":
    try:
        main()
    except AssertionError as error:
        print(f"FAIL: {error}")
        sys.exit(1)

#!/usr/bin/env python3
"""Wiring pins for bot automatic talent spending (TbTalents.{h,cpp} and hooks).

Run:
    python3 forever/tools/test_talents_wiring.py
    python3 forever/tools/test_talents_wiring.py --mutate
"""
import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FOREVER = ROOT / "forever"


def load():
    return {
        "scripts": (FOREVER / "src" / "TbScripts.cpp").read_text(encoding="utf-8"),
        "talents_h": (FOREVER / "src" / "host" / "TbTalents.h").read_text(encoding="utf-8"),
        "talents_cpp": (FOREVER / "src" / "host" / "TbTalents.cpp").read_text(encoding="utf-8"),
        "conf": (FOREVER / "conf" / "tortoisebots.conf.dist").read_text(encoding="utf-8"),
    }


def check(files):
    errors = []

    def pin(cond, msg):
        if not cond:
            errors.append(msg)

    s = files["scripts"]
    t = files["talents_cpp"]
    c = files["conf"]

    pin('#include "host/TbTalents.h"' in s, "TbScripts.cpp must include host/TbTalents.h")
    pin("TortoiseBots::talents::LoadConfig()" in s, "OnConfigLoad must call talents::LoadConfig()")
    pin('TortoiseBots::talents::Sync(player, "login")' in s, "OnLogin must call talents::Sync(player, \"login\")")
    pin('TortoiseBots::talents::Sync(player, "level")' in s, "OnLevelChanged must call talents::Sync(player, \"level\")")
    pin('{ "talents", TortoiseBots::talents::HandleTalentsCommand' in s, "tbTable must register 'talents' command")

    pin("TraitMgr::ValidateConfigForCommit" in t, "TbTalents.cpp must validate each step via TraitMgr::ValidateConfigForCommit")
    pin("UpdateTraitConfig" in t, "TbTalents.cpp must commit via Player::UpdateTraitConfig")
    pin('GetBoolDefault("TortoiseBots.AutoTalentsOnLevel", true)' in t, "TbTalents.cpp must read TortoiseBots.AutoTalentsOnLevel default true")
    pin('GetBoolDefault("TortoiseBots.Pool.CensusSpec", true)' in t, "TbTalents.cpp must read TortoiseBots.Pool.CensusSpec default true")
    pin("BotMgr::Instance().IsPoolBot(bot->GetGUID())" in t, "TbTalents.cpp must apply the census spec roll to pool bots only")
    pin("BotMgr::Instance().IsBot" in t, "TbTalents.cpp must check BotMgr::Instance().IsBot")

    pin(re.search(r"^TortoiseBots\.AutoTalentsOnLevel\s*=\s*1\b", c, re.M) is not None,
        "tortoisebots.conf.dist must set TortoiseBots.AutoTalentsOnLevel = 1")
    pin(re.search(r"^TortoiseBots\.Pool\.CensusSpec\s*=\s*1\b", c, re.M) is not None,
        "tortoisebots.conf.dist must set TortoiseBots.Pool.CensusSpec = 1")

    return errors


def mutate_check(files):
    mutations = [
        ("drop_config_load", "scripts", "TortoiseBots::talents::LoadConfig()", "// removed"),
        ("drop_login_sync", "scripts", 'TortoiseBots::talents::Sync(player, "login")', "// removed"),
        ("drop_level_sync", "scripts", 'TortoiseBots::talents::Sync(player, "level")', "// removed"),
        ("drop_cmd", "scripts", '{ "talents", TortoiseBots::talents::HandleTalentsCommand', "// removed"),
        ("drop_validate", "talents_cpp", "TraitMgr::ValidateConfigForCommit", "TraitMgr::ValidateConfig"),
        ("drop_conf_key", "conf", "TortoiseBots.AutoTalentsOnLevel = 1", "# removed"),
    ]
    for name, key, old, new in mutations:
        mutated = dict(files)
        mutated[key] = mutated[key].replace(old, new)
        errs = check(mutated)
        if not errs:
            print(f"FAIL: mutation {name} was not caught!")
            return 1
    print(f"OK: all {len(mutations)} mutations caught")
    return 0


def run_pure_test():
    """Compile and run the pure spec test TbTalents.cpp builds on."""
    gxx = shutil.which("g++")
    if not gxx:
        print("SKIP: no g++, the pure spec test did not run")
        return 0
    root = Path(__file__).resolve().parents[2]
    with tempfile.TemporaryDirectory(prefix="talents-") as tmp:
        exe = Path(tmp) / "test_talents"
        build = subprocess.run([gxx, "-std=c++17", "-O1", "-Wall", "-Wextra", "-I", str(root),
                                str(root / "forever/tools/test_talents.cpp"), "-o", str(exe)],
                               capture_output=True, text=True)
        if build.returncode != 0:
            print("FAIL: test_talents.cpp did not compile:\n" + build.stderr[-4000:])
            return 1
        out = subprocess.run([str(exe)], capture_output=True, text=True)
    if out.returncode != 0 or "PASS: test_talents" not in out.stdout:
        print("FAIL: the pure spec test failed:\n" + out.stdout + out.stderr)
        return 1
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mutate", action="store_true")
    args = ap.parse_args()

    files = load()
    errs = check(files)
    if errs:
        for e in errs:
            print("FAIL:", e)
        return 1
    if run_pure_test():
        return 1
    print("OK: all talent wiring pins passed")
    if args.mutate:
        return mutate_check(files)
    return 0


if __name__ == "__main__":
    sys.exit(main())

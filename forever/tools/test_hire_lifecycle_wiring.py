#!/usr/bin/env python3
"""Wiring pins for the hired-companion ledger (package P1 of the hire port).

The decisions themselves are the pure half's (forever/tools/test_hire_lifecycle_pure.cpp, compiled and run below
when g++ is there). This file pins what a source read can: the lifecycle owns every method of its contract, the
stub left it (TbHostOwnership.cpp), the hooks are registered in both the slice-only places, the table and its
columns are shipped, and the folder is built only with TORTOISEBOTS_SLICE1.
"""
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
checks = 0
failed = 0


def read(relative):
    with open(os.path.join(ROOT, relative), encoding="utf-8") as handle:
        return handle.read()


def check(condition, message):
    global checks, failed
    checks += 1
    if condition:
        print("ok: " + message)
    else:
        failed += 1
        print("FAIL: " + message)


def main():
    lifecycle = read("forever/src/host/hire/TbHireLifecycle.cpp")
    ownership = read("forever/src/host/TbHostOwnership.cpp")
    scripts = read("forever/src/TbScripts.cpp")
    cmake = read("CMakeLists.txt")
    sql = read("forever/sql/characters/2026_11_25_01_tb_hire.sql")
    accounts = read("forever/src/host/hire/TbHireAccounts.cpp")
    header = read("runtime/HireLifecycle.h")

    # Every method of the contract has a definition here (the pure decisions are not methods).
    methods = [
        "Claim", "DismissNow", "DismissForRefund", "EnsureDeletionIntent", "Forget", "IsHired", "GetMaster",
        "IsBoundByHirer", "MarkGrouped", "SetGearTier", "GearTier", "BeginCommand", "ConfirmCommand",
        "CancelCommand", "EndCommand", "CommandedHireCount", "KeepInstanceAfterPartyMove", "OnGroupMemberRemoved",
        "OnGroupDisband", "OnMasterLogin", "OnMasterLogout", "Update",
    ]
    for method in methods:
        check(method + "(" in header, "the contract declares " + method)
        check("HireLifecycle::" + method + "(" in lifecycle, "the Forever owner defines " + method)

    # The stub is gone: no second definition of Instance/IsHired/GetMaster in the host-services file.
    check("HireLifecycle::" not in ownership, "TbHostOwnership.cpp no longer defines HireLifecycle")
    check('runtime/HireLifecycle.h' not in ownership, "TbHostOwnership.cpp no longer includes the hire header")

    # Hooks: the world tick, group removal/disband and master login/logout.
    check("HireLifecycle::Instance().Update(diff)" in scripts, "the world tick drives the lifecycle")
    check("HireLifecycle::Instance().OnGroupMemberRemoved(group, guid)" in scripts, "group removal reaches the ledger")
    check("HireLifecycle::Instance().OnGroupDisband(group)" in scripts, "group disband reaches the ledger")
    check("HireLifecycle::Instance().OnMasterLogin(player)" in scripts, "a master's login reaches the ledger")
    check("HireLifecycle::Instance().OnMasterLogout(player)" in scripts, "a master's logout reaches the ledger")

    # Build gating: slice-only, exactly like the other engine-facing host files.
    off = cmake.split("if(NOT TORTOISEBOTS_SLICE1)", 1)[1].split("else()", 1)[0]
    check('host/hire/[^/]*\\\\.cpp$"' in off, "host/hire is excluded from the option-OFF build")

    # The durable side.
    check("CREATE TABLE IF NOT EXISTS `tb_hire`" in sql, "the migration creates tb_hire")
    for column in ["character_guid", "master_guid", "owner_account_id", "character_account_id", "gear_tier",
                   "state", "hired_at", "dismissed_at"]:
        check("`" + column + "`" in sql, "tb_hire has " + column)
    check("CREATE TABLE IF NOT EXISTS `tb_hire`" in lifecycle, "the module can create its own table")
    check("DecideHireDeletion(" in lifecycle, "deletion goes through the three-fact guard")
    check("hire::IsHireAccount(ledgerAccountId)" in lifecycle, "the guard proves the hire account")
    check("Player::DeleteFromDB(guid, accountId, true, true)" in lifecycle, "the character is deleted finally")
    check("return tbs_poolgear::IsHired(bot)" in read("forever/src/compat/TbCompat_PoolGear.h"),
          "the pool gear top-up asks the ledger whether a bot is a hire")

    # Accounts: the required entry point and the naming/stamp proof.
    check("std::uint32_t AcquireHireAccount(std::string& error)" in read("forever/src/host/hire/TbHireAccounts.h"),
          "AcquireHireAccount keeps its contract signature")
    check("sAccountMgr->CreateAccount(" in accounts, "a hire account is created through AccountMgr")
    check("pure::IsAccountStamp(email)" in accounts, "the stamp proves a hire account")

    # Run the pure test when a compiler is available.
    compiler = shutil.which("g++")
    if compiler:
        with tempfile.TemporaryDirectory() as tmp:
            exe = os.path.join(tmp, "test_hire_lifecycle_pure")
            build = subprocess.run(
                [compiler, "-std=c++17", "-O1", "-Wall", "-Wextra", "-I", ROOT, "-I", os.path.join(ROOT, "forever", "src"),
                 os.path.join(ROOT, "forever", "tools", "test_hire_lifecycle_pure.cpp"), "-o", exe],
                capture_output=True, text=True)
            check(build.returncode == 0, "the pure hire test compiles")
            if build.returncode == 0:
                run = subprocess.run([exe], capture_output=True, text=True)
                check(run.returncode == 0 and "0 failed" in run.stdout, "the pure hire decisions pass")
    else:
        print("ok: g++ absent; the pure hire test was not compiled")

    print("PASS: hire lifecycle wiring" if not failed else "FAIL: hire lifecycle wiring")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

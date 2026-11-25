#!/usr/bin/env python3
"""Wiring pins for the resistance-gear swap (issue #20 round 1, SPEC-TortoiseBotsForever-20.md).

The rules themselves are pure and unit-tested (forever/tools/test_resist_gear_policy.cpp); this file pins the wiring
that makes them reach the game:
  * the auto-equip gate consults the hold (a bot wearing resist gear is not "upgraded" back out of it);
  * a logout restores the bot's own pieces BEFORE the Player is deleted (BotMgr::Instance().OnPlayerLogout);
  * the host swap touches nothing but `Player::SwapItem`: no item is ever destroyed, sold, created or mailed;
  * `Apply` records the hold it took, so `Holding` answers and the read-out has something to show, and it refuses the
    swap when that recovery record cannot be written first (no piece ever goes on with nothing recording it);
  * a dead bot keeps its hold, a partial restore keeps only the pieces it could not move, the world tick never stops
    retrying (fast passes, then one a minute), an Enable=0 release keeps a hold it could not resolve, a login/module
    start recovers the characters-database store (the rules themselves are pure:
    ai/playerbot/policy/raid/ResistHoldPolicy.h, cases in test_resist_gear_policy.cpp);
  * the hold's own translation unit runs against a fake core (forever/tools/resist_gear_host_check.cpp): the world tick
    leaves a fresh set on (and a wipe keeps it), a release that cannot finish is retried, bag space that frees up after
    the fast retries still restores the set, an own piece parked in the bank keeps the hold, disabling while dead or in
    combat restores later, a store that refuses the record blocks the swap, the feature off asks one existence question
    and nothing else, a stored hold still restores with the feature off, and a logout hands its hold to the next login;
  * `.tb state` carries `resist=`/`rgear=` and `.tb gear ... show` carries `res=` (the shared read-out of #26);
  * the three config keys are in tortoisebots.conf.dist with issue #20's defaults, and the data table is current
    (forever/tools/gen_data_tables.py --check) with the entries the spec seeds.

Run:
    python3 -m unittest forever/tools/test_resist_gear_wiring.py
    RESIST_MUTATE=1 python3 forever/tools/test_resist_gear_wiring.py     # the pins' mutations, and the host check built
                                                                         # with a mutated host: tank-armour, no-hold,
                                                                         # destroy-on-restore, dead-hold-erased,
                                                                         # retry-unconditional, disable-ignored,
                                                                         # login-queries, persist-not-required,
                                                                         # disable-drops-hold, retry-stops,
                                                                         # held-not-retried, off-creates,
                                                                         # bank-piece-swapped
"""
import csv
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]

POLICY = "ai/playerbot/policy/raid/ResistGearPolicy.h"
HOLD_POLICY = "ai/playerbot/policy/raid/ResistHoldPolicy.h"
HOST_H = "forever/src/host/TbResistGear.h"
HOST_CPP = "forever/src/host/TbResistGear.cpp"
W6 = "forever/src/compat/TbCompat_W6.h"
RESIST_COMPAT = "forever/src/compat/TbCompat_Resist.h"
RESIST_DISPATCH = "ai/playerbot/TbCompatResist.h"
TURTLE_TWIN = "ai/cmangos-compat-resist.h"
SCRIPTS = "forever/src/TbScripts.cpp"
PARTY = "forever/src/host/TbPartyState.cpp"
H3_H = "forever/src/host/TbH3.h"
CONF = "forever/conf/tortoisebots.conf.dist"
CSV = "forever/data/raid/boss-resist.csv"
HOLD_SQL = "forever/sql/characters/2026_11_24_11_tb_resist_hold.sql"
HOLD_STATE_SQL = "forever/sql/characters/2026_11_25_00_tb_resist_hold_state.sql"
HOST_CHECK = "forever/tools/resist_gear_host_check.cpp"
HOST_FAKES = "forever/tools/resist_gear_host_fakes"
CHAR_STORE = "forever/src/compat/TbCompat_CharStore.h"
RESIST_DATA = "forever/src/host/data/TbResistData.gen.h"
TOOLS = pathlib.Path(__file__).resolve().parent


def read(rel):
    return (ROOT / rel).read_text(encoding="utf-8")


def code_only(text):
    """The source without its comments: the pins below are about code, and a comment may name a ban word."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return "\n".join(line.split("//", 1)[0] for line in text.splitlines())


def check_hold_gate(w6_text):
    code = code_only(w6_text)
    if "ResistGearHoldingCompat(bot)" not in code:
        raise AssertionError("TbCompat_W6.h's MayAutoEquipUpgradesCompat must consult the resist-gear hold")
    if not re.search(r"ExplicitEquipOrderFlag\(\)\)\s*&&\s*!ResistGearHoldingCompat\(bot\)", code):
        raise AssertionError("the hold must be an AND on the auto-equip answer, not a replacement for it")


def check_logout_order(scripts_text):
    code = code_only(scripts_text)
    logout = code.index("void OnLogout(Player* player) override")
    end = code.index("BotMgr::Instance().OnPlayerLogout(player);", logout)
    restore = code.find('TortoiseBots::resistgear::Restore(player, "logout")', logout)
    if restore < 0:
        raise AssertionError('OnLogout does not call resistgear::Restore(player, "logout")')
    if restore > end:
        raise AssertionError("the resist-gear restore runs after BotMgr::Instance().OnPlayerLogout (too late)")
    if 'TortoiseBots::resistgear::Restore(player, "logout")' in code[:logout]:
        raise AssertionError("the restore call is outside OnLogout")


def check_no_item_removal(host_cpp_text):
    code = code_only(host_cpp_text)
    banned = ("DestroyItem", "SellItem", "StoreNewItem", "EquipNewItem", "MailDraft", "AddItem", "CheckSell")
    for word in banned:
        if word in code:
            raise AssertionError("%s: the resist-gear swap must not %s (%s found)" % (HOST_CPP, word, word))


def check_apply_registers_hold(host_cpp_text):
    code = code_only(host_cpp_text)
    if "holds.emplace(bot->GetGUID(), Hold{" not in code:
        raise AssertionError("Apply does not record the hold it took (Holding would answer no and rgear= would stay empty)")
    if not re.search(r"bool Holding\(Player\* bot\)\s*\{\s*return bot && holds\.find\(bot->GetGUID\(\)\) != holds\.end\(\);", code):
        raise AssertionError("Holding does not read the recorded holds")


def check_slot_rule(policy_text):
    code = code_only(policy_text)
    if not re.search(r"case 1: case 5: case 8: case 10: case 11: case 14:\s*return true;", code):
        raise AssertionError("the always-allowed slots (neck, waist, wrists, both fingers, back) changed")
    if not re.search(r"case 0: case 2: case 4: case 6: case 7: case 9:\s*return !tank;", code):
        raise AssertionError("the armour slots must be allowed for non-tanks only (return !tank)")
    if "kResistGain = 5" not in code:
        raise AssertionError("the 5-point gain threshold changed")


def check_hold_wiring(host_cpp_text, scripts_text):
    """The review fixes: the hold policy is asked at the places that matter, and the store goes through the guard."""
    code = code_only(host_cpp_text)
    for needle in ("ai::MayRestoreHold(bot->IsAlive())", "ai::PlanPieceBack(", "ai::ShouldRetryRestore(",
                   "ai::ShouldRetryHold(", "ai::RecoverOnLogin(", "ai::ReleaseOnDisable("):
        if needle not in code:
            raise AssertionError("%s does not ask %s (a dead bot would lose its hold, the retry would be unbounded or the disabled feature would work)" % (HOST_CPP, needle))
    update = function_body(code, "void Update(uint32 diff)")
    if "ai::ShouldRetryHold(" not in update:
        raise AssertionError("Update does not ask ShouldRetryHold: a held set would be taken off by the world tick")
    if "CharacterDatabase." in code:
        raise AssertionError("%s makes a direct characters-database call; use the guarded helpers of compat/TbCompat_CharStore.h" % HOST_CPP)
    for helper in ("CharacterSchemaHasCompat(", "CharacterQueryGuardedCompat(", "CharacterExecuteGuardedCompat(",
                   "CharacterTransactGuardedCompat("):
        if helper not in code:
            raise AssertionError("%s does not use %s (the store must stay behind the schema guard)" % (HOST_CPP, helper))
    if "tortoise_bots_resist_hold" not in code:
        raise AssertionError("the hold is not persisted to tortoise_bots_resist_hold")

    scripts = code_only(scripts_text)
    if "TortoiseBots::resistgear::Update(diff)" not in scripts:
        raise AssertionError("the world tick does not retry an unfinished resist-gear restore")
    if "TortoiseBots::resistgear::LoadAtStartup()" not in scripts:
        raise AssertionError("the module start does not read the stored holds")
    login = scripts.index("void OnLogin(Player* player")
    end = scripts.index("TortoiseBots::cmd::NoteLogin(player);", login)
    recover = scripts.find("TortoiseBots::resistgear::RecoverLogin(player)", login)
    if recover < 0:
        raise AssertionError("OnLogin does not recover a stored resist-gear hold")
    if recover > end:
        raise AssertionError("the resist-gear recovery runs after NoteLogin (the hold should go back first)")


def check_off_is_inert(host_cpp_text):
    """With the feature off: no CREATE, no login query, no re-probe; stored holds still restore from the snapshot."""
    code = code_only(host_cpp_text)
    startup = function_body(code, "void LoadAtStartup()")
    if "EnsureStore(config.enabled)" not in startup:
        raise AssertionError("LoadAtStartup does not pass the feature's state to EnsureStore (the CREATE would run with the key off)")
    login = function_body(code, "void RecoverLogin(Player* bot)")
    if "ReadStoredHold(" in login:
        raise AssertionError("RecoverLogin queries the store; a login must read the startup snapshot only")
    ensure = function_body(code, "bool EnsureStore(bool mayCreate)")
    if "if (mayCreate)" not in ensure or "CharacterExecuteGuardedCompat(kHoldCreate)" not in ensure:
        raise AssertionError("EnsureStore does not keep the CREATE behind mayCreate")


def function_body(code, signature):
    """The text of one function, from its signature to the closing brace at column 0."""
    start = code.index(signature)
    end = code.index("\n}\n", start)
    return code[start:end]


def check_swap_needs_a_persisted_hold(host_cpp_text):
    """No recovery record, no swap: a resist piece never goes on with nothing that could put it back."""
    code = code_only(host_cpp_text)
    body = function_body(code, "bool Apply(Player* bot, int school)")
    guard = body.find("if (!PersistHold(")
    if guard < 0:
        raise AssertionError("Apply does not refuse the swap when the recovery record cannot be written")
    swap = body.find("bot->SwapItem(")
    if swap < 0 or guard > swap:
        raise AssertionError("Apply swaps before the recovery record is written (a crash would leave a resist set on with no row)")
    if "return false;" not in body[guard:swap]:
        raise AssertionError("Apply does not return false when the recovery record cannot be written")
    persist = function_body(code, "bool PersistHold(uint64 playerKey, Hold const& hold)")
    if "ReadStoredHold(playerKey)" not in persist:
        raise AssertionError("PersistHold does not read its rows back (a write that did not land would look saved)")
    if "return false;" not in persist or "return true;" not in persist:
        raise AssertionError("PersistHold must answer whether the rows really are in the store")


def check_disable_keeps_an_unresolved_hold(host_cpp_text):
    """Switching the key off must not drop a hold the bot could not put back: only Restore clears a hold."""
    code = code_only(host_cpp_text)
    body = function_body(code, "void ReleaseAll(char const* why)")
    if "ai::ShouldRetryRestore(" not in body:
        raise AssertionError("ReleaseAll does not ask whether the bot can take its pieces back now")
    if "holds.erase" in body:
        raise AssertionError("ReleaseAll erases a hold it could not restore (a dead or fighting bot would keep the resist set for good)")


class ResistGearWiring(unittest.TestCase):
    def test_auto_equip_gate_consults_the_hold(self):
        check_hold_gate(read(W6))

    def test_logout_restores_before_the_player_is_deleted(self):
        check_logout_order(read(SCRIPTS))

    def test_swap_removes_no_item(self):
        check_no_item_removal(read(HOST_CPP))

    def test_apply_records_the_hold(self):
        check_apply_registers_hold(read(HOST_CPP))

    def test_hold_retry_recovery_and_disable_are_wired(self):
        check_hold_wiring(read(HOST_CPP), read(SCRIPTS))

    def test_swap_needs_a_persisted_hold(self):
        check_swap_needs_a_persisted_hold(read(HOST_CPP))

    def test_disable_keeps_an_unresolved_hold(self):
        check_disable_keeps_an_unresolved_hold(read(HOST_CPP))

    def test_the_off_switch_is_inert(self):
        check_off_is_inert(read(HOST_CPP))

    def test_hold_store_migration_matches_the_host_create(self):
        sql = read(HOLD_SQL)
        for column in ("guid", "slot", "school", "own_guid", "resist_guid"):
            self.assertIn("`%s`" % column, sql)
        self.assertIn("PRIMARY KEY (`guid`, `slot`)", sql)
        host = read(HOST_CPP)
        self.assertIn("CREATE TABLE IF NOT EXISTS `tortoise_bots_resist_hold`", host)
        # The state is durable: the host's CREATE writes it and the follow-up migration adds it to an older table.
        self.assertIn("`releasing` tinyint unsigned NOT NULL DEFAULT '0'", host)
        state_sql = read(HOLD_STATE_SQL)
        self.assertIn("ADD COLUMN `releasing` tinyint unsigned NOT NULL DEFAULT 0", state_sql)
        self.assertIn("information_schema.COLUMNS", state_sql)   # idempotent: only when the column is missing

    def test_slot_rule_is_the_specs(self):
        check_slot_rule(read(POLICY))

    def test_host_swap_uses_the_cores_swap_item(self):
        code = code_only(read(HOST_CPP))
        self.assertIn("bot->SwapItem(", code)
        self.assertIn("bot->GetItemByGuid(", code)     # restore by guid, as the spec's held map says
        self.assertIn("bot->CanEquipItem(", code)      # the per-slot candidate check
        self.assertIn("IsRaid()", code)
        self.assertIn("playerbot/strategy/values/ItemUsageValue.h", read(HOST_CPP))   # the pin read

    def test_config_keys_and_read(self):
        code = code_only(read(HOST_CPP))
        self.assertIn('GetIntDefault("TortoiseBots.RaidResistGear.Enable", 0)', code)
        self.assertIn('GetIntDefault("TortoiseBots.RaidResistGear.Range", 60)', code)
        self.assertIn('GetIntDefault("TortoiseBots.RaidResistGear.ReleaseSeconds", 15)', code)
        conf = read(CONF)
        for line in ("TortoiseBots.RaidResistGear.Enable = 0",
                     "TortoiseBots.RaidResistGear.Range = 60",
                     "TortoiseBots.RaidResistGear.ReleaseSeconds = 15"):
            self.assertIn(line, conf)
        self.assertIn("resistgear::LoadConfig();", code_only(read(SCRIPTS)))

    def test_build_index_checks_each_evidence_spell_school(self):
        code = code_only(read(HOST_CPP))
        self.assertIn("#include \"host/data/TbResistData.gen.h\"", read(HOST_CPP))
        self.assertIn("tbdata::resist::kBossResist", code)
        self.assertIn("tbdata::resist::kResistRowCount", code)
        self.assertIn("->School", code)               # the spell mirror's school, compared with the row
        self.assertIn("MaxSpellId() == 0", code)     # an unready mirror leaves the index unbuilt (a module off at boot)
        self.assertIn("resistgear::BuildIndex();", code_only(read(SCRIPTS)))

    def test_read_outs(self):
        party = code_only(read(PARTY))
        self.assertIn('rec.Csv("resist", resist)', party)
        self.assertIn('rec.P("rgear"', party)
        self.assertIn('verb == "show"', party)
        self.assertIn('rec.Csv("res"', party)
        self.assertIn("Csv(std::string_view key, std::vector<int64> const& values);", read(H3_H))

    def test_compat_trio(self):
        forever = read(RESIST_COMPAT)
        self.assertIn("ResistGearHoldingCompat", forever)
        self.assertIn("ResistGearApplyCompat", forever)
        self.assertIn("ResistGearRestoreCompat", forever)
        self.assertIn("ResistGearSchoolForCompat", forever)
        self.assertIn('#include "TbCompat_Resist.h"', read(RESIST_DISPATCH))
        self.assertIn('#include "cmangos-compat-resist.h"', read(RESIST_DISPATCH))
        turtle = read(TURTLE_TWIN)
        self.assertEqual(turtle.count("return false;"), 3)   # every helper is a no-op on the Turtle core
        self.assertEqual(turtle.count("return 0;"), 1)

    def test_data_table_is_current_and_seeded(self):
        proc = subprocess.run([sys.executable, str(ROOT / "forever/tools/gen_data_tables.py"), "--check"],
                              capture_output=True, text=True, timeout=120)
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
        with open(ROOT / CSV, encoding="utf-8", newline="") as handle:
            rows = list(csv.DictReader(handle))
        schools = {"fire": 2, "nature": 3, "frost": 4, "shadow": 5}
        seeded = {
            11982: "fire", 12056: "fire", 11988: "fire", 11502: "fire", 10184: "fire", 12435: "fire", 13020: "fire",
            12118: "shadow", 12259: "shadow", 11583: "shadow", 15509: "nature", 15299: "nature", 15989: "frost",
        }
        got = {}
        for row in rows:
            entry = int(row["entry"])
            self.assertNotIn(entry, got, "duplicate entry %d" % entry)
            self.assertIn(row["school"], schools, row["school"])
            self.assertTrue(int(row["evidence_spell"]) > 0)
            self.assertTrue(row["note"].strip())
            got[entry] = row["school"]
        self.assertEqual(got, seeded)
        generated = read("forever/src/host/data/TbResistData.gen.h")
        for entry, school in seeded.items():
            self.assertRegex(generated, r"\{ %du, \"[^\"]+\", %d, \d+u, " % (entry, schools[school]))


def build_host_check(tmp, host_cpp_text=None):
    """Compiles the hold's own translation unit against the fake core, then runs it; returns the run's result.

    The copies are what a build tree looks like: the host file under forever/src/host, the real guarded store helper,
    the generated data table and the policy headers, with the fakes first on the include path.
    """
    gxx = shutil.which("g++")
    if not gxx:
        return None
    tmp = pathlib.Path(tmp)
    for rel in (HOST_H, CHAR_STORE, RESIST_DATA):
        (tmp / rel).parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / rel, tmp / rel)
    (tmp / HOST_CPP).write_text(host_cpp_text if host_cpp_text is not None else read(HOST_CPP))
    fakes = tmp / HOST_FAKES
    if fakes.exists():
        shutil.rmtree(fakes)
    shutil.copytree(TOOLS / "resist_gear_host_fakes", fakes)
    exe = tmp / "resist_gear_host_check"
    subprocess.run([gxx, "-std=c++17", "-O1", "-Wall", "-Wextra", "-DTB_RESIST_GEAR_TEST", "-I", str(fakes), "-I", str(tmp / "forever/src"),
                    "-I", str(ROOT), str(ROOT / HOST_CHECK), str(tmp / HOST_CPP), "-o", str(exe)],
                   check=True, capture_output=True, text=True)
    return subprocess.run([str(exe)], capture_output=True, text=True)


class HostBehaviour(unittest.TestCase):
    """The hold's own translation unit against a fake core: the three paths where a bot could be left in resist gear."""

    def test_the_hold_check_passes(self):
        if not shutil.which("g++"):
            self.skipTest("no g++")
        with tempfile.TemporaryDirectory(prefix="resist-hold-") as tmp:
            out = build_host_check(tmp)
        self.assertEqual(out.returncode, 0, out.stdout + out.stderr)
        self.assertIn("RESULT: PASS", out.stdout)


def mutations():
    policy = read(POLICY)
    host = read(HOST_CPP)
    caught = 0

    mutated = policy.replace("case 0: case 2: case 4: case 6: case 7: case 9:\n            return !tank;",
                             "case 0: case 2: case 4: case 6: case 7: case 9:\n            return true;")
    try:
        check_slot_rule(mutated)
    except AssertionError:
        print("MUTATION CAUGHT: tank-armour")
        caught += 1
    else:
        print("ERROR: tank-armour mutation survived", file=sys.stderr)

    mutated = re.sub(r"holds\.emplace\(bot->GetGUID\(\), Hold\{[^;]*;", ";", host, count=1)
    try:
        check_apply_registers_hold(mutated)
    except AssertionError:
        print("MUTATION CAUGHT: no-hold")
        caught += 1
    else:
        print("ERROR: no-hold mutation survived", file=sys.stderr)

    mutated = host.replace("bot->SwapItem(", "bot->DestroyItem(0, 0, true); bot->SwapItem(", 1)
    try:
        check_no_item_removal(mutated)
    except AssertionError:
        print("MUTATION CAUGHT: destroy-on-restore")
        caught += 1
    else:
        print("ERROR: destroy-on-restore mutation survived", file=sys.stderr)

    mutated = host.replace("if (!ai::MayRestoreHold(bot->IsAlive()))", "if (false)")
    try:
        check_hold_wiring(mutated, read(SCRIPTS))
    except AssertionError:
        print("MUTATION CAUGHT: dead-hold-erased")
        caught += 1
    else:
        print("ERROR: dead-hold-erased mutation survived", file=sys.stderr)

    mutated = re.sub(r"ai::ShouldRetryRestore\([^)]*\)", "true", host)
    try:
        check_hold_wiring(mutated, read(SCRIPTS))
    except AssertionError:
        print("MUTATION CAUGHT: retry-unconditional")
        caught += 1
    else:
        print("ERROR: retry-unconditional mutation survived", file=sys.stderr)

    mutated = re.sub(r"if \(ai::ReleaseOnDisable\([^)]*\)\)", "if (false)", host, count=1)
    try:
        check_hold_wiring(mutated, read(SCRIPTS))
    except AssertionError:
        print("MUTATION CAUGHT: disable-ignored")
        caught += 1
    else:
        print("ERROR: disable-ignored mutation survived", file=sys.stderr)

    mutated = host.replace("""    if (stored == storedHolds.end() && left == leftHolds.end())
        return;""",
"""    if (stored == storedHolds.end() && left == leftHolds.end())
    {
        ReadStoredHold(key);
        return;
    }""")
    try:
        check_off_is_inert(mutated)
    except AssertionError:
        print("MUTATION CAUGHT: login-queries")
        caught += 1
    else:
        print("ERROR: login-queries mutation survived", file=sys.stderr)

    for name, (old, new) in (
            ("persist-not-required",
             ("if (!PersistHold(key, Hold{ school, planned }))", "if (false)")),
            ("disable-drops-hold",
             ("""        it = holds.find(guid);
        if (it == holds.end())
            continue;
        LogAt("tortoisebots", LOG_LEVEL_WARN, "TortoiseBots resist gear: {}: the feature is off;""",
              """        it = holds.find(guid);
        if (it != holds.end())
            holds.erase(it);
        if (true)
            continue;
        LogAt("tortoisebots", LOG_LEVEL_WARN, "TortoiseBots resist gear: {}: the feature is off;""")),
            ("retry-stops",
             ("""        if (!ai::ShouldRetryRestore(bot->IsAlive(), bot->IsInCombat()))
        {
            // Dead or fighting""",
              """        if (!ai::ShouldRetryRestore(bot->IsAlive(), bot->IsInCombat()) || it->second.attempts >= ai::kMaxRestoreAttempts)
        {
            // Dead or fighting""")),
            ("held-not-retried",
             ("if (ai::ShouldRetryHold(entry.second.state) && entry.second.nextRetryMs <= clockMs)",
              "if (entry.second.nextRetryMs <= clockMs)")),
            ("off-creates",
             ("""    if (!EnsureStore(config.enabled))
        return;
    LoadStoredHolds();""",
              """    if (!EnsureStore(true))
        return;
    LoadStoredHolds();""")),
            ("bank-piece-swapped",
             ("Item* own = ownWasEmpty ? nullptr : FindBagItem(bot, piece.own);",
              "Item* own = ownWasEmpty ? nullptr : bot->GetItemByGuid(piece.own);")),
    ):
        if old not in host:
            print("ERROR: %s mutation no longer applies" % name, file=sys.stderr)
            continue
        with tempfile.TemporaryDirectory(prefix="resist-mutate-") as tmp:
            out = build_host_check(tmp, host.replace(old, new, 1))
        if out is not None and out.returncode != 0:
            print("MUTATION CAUGHT: %s" % name)
            caught += 1
        else:
            print("ERROR: %s mutation survived" % name, file=sys.stderr)

    return 0 if caught == 13 else 1


if __name__ == "__main__":
    if os.environ.get("RESIST_MUTATE") == "1":
        sys.exit(mutations())
    unittest.main()

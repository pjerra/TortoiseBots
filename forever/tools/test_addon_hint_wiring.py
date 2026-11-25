#!/usr/bin/env python3
"""Wiring pins for the add-on hint (TBF #46).

Each check names the mutation it catches, so a passing run means the live half
still does what the spec decided:
  * NoteLogin is never reached for a bot or a bot session (mutation: drop the
    IsBot guards and a pool bot logs in as a candidate);
  * NoteAddonSeen sits after the TBM prefix check (mutation: move it before it
    and any whisper marks the character as an add-on user);
  * Tick runs on the world tick after Drain() (mutation: drop the call and no
    hint is ever sent);
  * the hint is a system message, not a whisper from a bot (mutation: send it
    with addon::Send);
  * a pending entry's observation is read and answered under the one lock
    NoteAddonSeen takes (mutation: decide from the swept copy after the lock is
    released, and a TBM message that lands after the sweep is still nagged);
  * a character seen with the add-on leaves the pending set in that same sweep
    (mutation: drop the erase and the tick keeps looking it up every pass);
  * the row is written on send AND on the first TBM message (mutation: drop
    one FlagCharacter call and `reset` becomes the only way to see it again);
  * the SQL file's CREATE equals the code's CREATE (mutation: edit one only);
  * the conf defaults equal the code fallbacks (mutation: change one only);
  * the feature ships off: the template, the code fallback and the addon Info
    row all say 0 (mutation: turn one on and an upgrade enables the hint);
  * an elevated account leaves a settled entry without a line or a row
    (mutation: drop the DropStaff branch and a GM is nagged);
  * one tick writes its rows in one statement (mutation: one INSERT per
    character, as the first version did);
  * the live half, compiled against the fakes in addon_hint_fakes/, keeps the
    seen-check, the decision and the send under the one seenMutex (the copy
    that unlocks before SendSysMessage makes the live test fail);
  * the call sites sit inside the slice guard (mutation: build the OFF slice
    against a header its sources do not compile);
  * no module query bypasses the characters-DB guard.

Run:
  python3 forever/tools/test_addon_hint_wiring.py
"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SRC = ROOT / "forever" / "src"
HOST = SRC / "host"
SQL = ROOT / "forever" / "sql" / "characters" / "2026_11_24_10_tb_player_flag.sql"
CONF = ROOT / "forever" / "conf" / "tortoisebots.conf.dist"

GUARD = "#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1"
DIRECT = re.compile(r"\b(?:World|Character|Login|Hotfix)Database\s*(?:\.|->)\s*"
                    r"(?:Query|PQuery|AsyncQuery|AsyncPQuery|Execute|PExecute|DirectExecute|DirectPExecute|GetPreparedStatement)\b")


def read(path):
    return Path(path).read_text() if Path(path).exists() else ""


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def body_after(source, marker):
    start = source.find(marker)
    if start < 0:
        raise AssertionError(f"missing `{marker}`")
    return source[start:]


def guarded_call(source, call, label):
    """`call` appears, and the nearest open slice guard has no #endif before it."""
    idx = source.find(call)
    if idx < 0:
        raise AssertionError(f"{label}: never calls {call}")
    guard = source.rfind(GUARD, 0, idx)
    if guard < 0:
        raise AssertionError(f"{label}: {call} is not inside the slice guard")
    if "#endif" in source[guard:idx]:
        raise AssertionError(f"{label}: {call} sits outside the slice guard")


def check_slice_guards():
    scripts = strip_comments(read(SRC / "TbScripts.cpp"))
    channel = strip_comments(read(HOST / "TbAddonChannel.cpp"))
    guarded_call(scripts, "addonhint::NoteLogin(", "TbScripts.cpp")
    guarded_call(scripts, "addonhint::Forget(", "TbScripts.cpp")
    guarded_call(scripts, "addonhint::LoadConfig(", "TbScripts.cpp")
    guarded_call(scripts, "addonhint::LoadAtStartup(", "TbScripts.cpp")
    guarded_call(channel, "addonhint::NoteAddonSeen(", "TbAddonChannel.cpp")
    guarded_call(channel, "addonhint::Tick(", "TbAddonChannel.cpp")


def check_login_skips_bots():
    host = strip_comments(read(HOST / "TbHostAddonHint.cpp"))
    body = body_after(host, "void NoteLogin(Player* player)")
    body = body[:body.find("\nvoid ")] if "\nvoid " in body else body
    for pin in ("BotMgr::Instance().IsBot(guid)", "player->GetSession()->IsBot()", "s.pending[key]"):
        if pin not in body:
            raise AssertionError(f"NoteLogin lost `{pin}`")


def check_seen_after_prefix():
    channel = strip_comments(read(HOST / "TbAddonChannel.cpp"))
    body = body_after(channel, "void OnAddonText(Player* player")
    prefix = body.find("prefix != pure::kPrefix")
    seen = body.find("addonhint::NoteAddonSeen(")
    if prefix < 0 or seen < 0 or seen < prefix:
        raise AssertionError("NoteAddonSeen must run after the TBM prefix check")


def check_tick_after_drain():
    channel = strip_comments(read(HOST / "TbAddonChannel.cpp"))
    body = body_after(channel, "class tortoisebots_addon_tick")
    drain = body.find("Drain();")
    tick = body.find("addonhint::Tick(")
    if drain < 0 or tick < 0 or tick < drain:
        raise AssertionError("addonhint::Tick must run on the world tick, after Drain()")


def check_system_message():
    host = strip_comments(read(HOST / "TbHostAddonHint.cpp"))
    if "SendSysMessage(" not in host:
        raise AssertionError("the hint is never sent as a system message")
    for forbidden in ("WhisperAddon", "addon::Send(", "addon::Ack("):
        if forbidden in host:
            raise AssertionError(f"the hint must not go out as an addon whisper (`{forbidden}`)")


def lock_block(source, marker):
    """The `{ ... }` block that holds the first `lock(s.seenMutex)` after `marker`."""
    idx = source.find("std::lock_guard<std::mutex> lock(s.seenMutex);", marker)
    if idx < 0:
        raise AssertionError(f"no `lock(s.seenMutex)` after `{marker}`")
    depth = 0
    open_at = -1
    for i in range(idx, -1, -1):
        if source[i] == "}":
            depth += 1
        elif source[i] == "{":
            if depth == 0:
                open_at = i
                break
            depth -= 1
    if open_at < 0:
        raise AssertionError("the seenMutex lock sits outside any block")
    depth = 0
    for i in range(open_at, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[open_at:i + 1]
    raise AssertionError("unbalanced braces around the seenMutex lock")


def check_decision_under_seen_lock():
    # Mutation: copy `seen` into a vector, release seenMutex, then decide from
    # that copy — a TBM message landing after the copy is nagged anyway.
    host = strip_comments(read(HOST / "TbHostAddonHint.cpp"))
    body = body_after(host, "void Tick(uint32_t nowMs)")
    body = body[:body.find("\nvoid ")] if "\nvoid " in body else body
    if "std::find(seen.begin()" in body:
        raise AssertionError("Tick decides a pending entry from the swept copy, not the live seen set")
    loop = body.find("for (auto it = s.pending.begin()")
    if loop < 0:
        raise AssertionError("Tick lost its pending loop")
    block = lock_block(body, loop)
    for pin in ("s.seen.erase(key)", "pure::Decide(", "SendSysMessage("):
        if pin not in block:
            raise AssertionError(f"the observation read and the send must share one seenMutex block (missing `{pin}`)")


def check_seen_leaves_pending():
    # Mutation: flag a seen character but keep its pending entry — the tick
    # looks the character up on every pass and `addonhint status` keeps
    # answering `not-told pending` for a character that has the add-on.
    host = strip_comments(read(HOST / "TbHostAddonHint.cpp"))
    body = body_after(host, "void Tick(uint32_t nowMs)")
    start = body.find("for (uint64_t const key : seen)")
    end = body.find("uint32_t sent = 0;", start)
    if start < 0 or end < 0:
        raise AssertionError("Tick lost its seen sweep")
    sweep = body[start:end]
    for pin in ("FlagCharacter(s, batch, key);", "s.pending.erase(key);"):
        if pin not in sweep:
            raise AssertionError(f"a seen character must leave the pending set at once (missing `{pin}`)")


def check_row_on_both_paths():
    host = strip_comments(read(HOST / "TbHostAddonHint.cpp"))
    if host.count("INSERT IGNORE INTO `tortoise_bots_player_flag`") != 1:
        raise AssertionError("exactly one INSERT IGNORE should write the row")
    # definition plus the seen sweep and the send path
    if host.count("FlagCharacter(") != 3:
        raise AssertionError(f"expected FlagCharacter twice (seen and send), found {host.count('FlagCharacter(') - 1}")
    if "HintStep::Send" not in host:
        raise AssertionError("the send branch never reads Decide's Send step")
    # One statement per tick: FlagCharacter only queues the key, FlushFlags builds the VALUES list and issues it.
    flag = body_after(host, "void FlagCharacter(State& s")
    flag = flag[:flag.find("\nvoid FlushFlags(")]
    if "CharacterExecuteGuardedCompat(" in flag or "INSERT" in flag:
        raise AssertionError("FlagCharacter writes its own row; it must queue for the tick's one statement")
    flush = body_after(host, "void FlushFlags(")
    flush = flush[:flush.find("\nPlayer* ConnectedPlayer(")]
    if flush.count("CharacterExecuteGuardedCompat(") != 1:
        raise AssertionError("each tick must issue exactly one characters-store statement for its rows")
    if "VALUES " not in flush or 'sql += ", "' not in flush:
        raise AssertionError("FlushFlags does not build one multi-row VALUES list")


def check_staff_skip():
    host = strip_comments(read(HOST / "TbHostAddonHint.cpp"))
    pure = strip_comments(read(HOST / "TbHostAddonHintPure.h"))
    if "session->GetSecurity() > SEC_PLAYER" not in host:
        raise AssertionError("the live half must treat every session above SEC_PLAYER as staff")
    if "HintStep::DropStaff" not in host:
        raise AssertionError("the live half never reads Decide's DropStaff step")
    if "HintStep::DropStaff" not in pure:
        raise AssertionError("the pure rule lost its staff step")


def check_sql_matches_create():
    sql = read(SQL)
    if not sql:
        raise AssertionError(f"{SQL.relative_to(ROOT)} is missing")
    sql = re.sub(r"--[^\n]*", "", sql)
    start = sql.find("CREATE TABLE")
    end = sql.find(";", start)
    if start < 0 or end < 0:
        raise AssertionError("the SQL file has no CREATE statement")
    sql_ddl = re.sub(r"\s+", "", sql[start:end])
    sql = read(SQL)

    host = read(HOST / "TbHostAddonHint.cpp")
    block = re.search(r"char const\* const kCreate =\s*(.*?);", host, re.S)
    if not block:
        raise AssertionError("TbHostAddonHint.cpp has no kCreate")
    code_ddl = re.sub(r"\s+", "", "".join(re.findall(r'"([^"]*)"', block.group(1))))
    if sql_ddl != code_ddl:
        raise AssertionError(f"the SQL CREATE and the code's kCreate differ:\n  sql:  {sql_ddl}\n  code: {code_ddl}")
    for pin in ("tortoise_bots_player_flag", "'addon-hint'"):
        if pin not in sql:
            raise AssertionError(f"the SQL file lost `{pin}`")
    if "PRIMARY KEY (`guid`, `flag`)" not in sql:
        raise AssertionError("the SQL file lost its (guid, flag) primary key")


def check_conf_defaults():
    conf = read(CONF)
    host = read(HOST / "TbHostAddonHint.cpp")
    pins = {
        "TortoiseBots.Player.AddonHint": "0",
        "TortoiseBots.Player.AddonHintDelay": "45",
        "TortoiseBots.Player.AddonHintUrl": '"https://github.com/TortoiseBots/TortoiseBotsForeverManager/releases"',
    }
    for key, value in pins.items():
        if not re.search(rf"^{re.escape(key)}\s*=\s*{re.escape(value)}$", conf, re.M):
            raise AssertionError(f"{CONF.name} lost `{key} = {value}`")
    block = re.search(r"#\s+TortoiseBots\.Player\.AddonHint\n(.*?)\n\n", conf, re.S)
    if not block or not re.search(r"Default:\s+0\b", block.group(1)):
        raise AssertionError(f"{CONF.name} does not state the off default for TortoiseBots.Player.AddonHint (Default: 0)")
    for pin in ('kKeyEnabled = "TortoiseBots.Player.AddonHint"',
                "bool g_enabled = false",
                'kKeyDelay = "TortoiseBots.Player.AddonHintDelay"',
                'kKeyUrl = "TortoiseBots.Player.AddonHintUrl"',
                "GetBoolDefault(kKeyEnabled, false)",
                "GetIntDefault(kKeyDelay, kDefaultDelaySeconds)",
                "GetStringDefault(kKeyUrl, kDefaultUrl)",
                "kDefaultDelaySeconds = 45",
                'kDefaultUrl = "https://github.com/TortoiseBots/TortoiseBotsForeverManager/releases"'):
        if pin not in host:
            raise AssertionError(f"TbHostAddonHint.cpp lost its conf read `{pin}`")


def check_feature_row_and_command():
    info = strip_comments(read(HOST / "TbAddonInfo.cpp"))
    if not re.search(r'\{\s*"addon-hint",\s*"TortoiseBots\.Player\.AddonHint",\s*true,\s*0,\s*"Add-on hint for new players"\s*\}', info):
        raise AssertionError("TbAddonInfo.cpp lost the addon-hint feature row, or it is not off by default")
    scripts = strip_comments(read(SRC / "TbScripts.cpp"))
    if not re.search(r'\{\s*"addonhint",\s*HandleAddonHint,\s*rbac::RBAC_PERM_COMMAND_GM,\s*Console::Yes\s*\}', scripts):
        raise AssertionError("TbScripts.cpp lost the `.tb addonhint` GM command row")
    if "addonhint::HandleCommand(" not in scripts:
        raise AssertionError("the command handler never reaches addonhint::HandleCommand")


def check_pure_core_free():
    pure = strip_comments(read(HOST / "TbHostAddonHintPure.h"))
    for pin in ("Player.h", "Config.h", "ObjectGuid.h", "Chat.h", "class Player", "DatabaseEnv.h"):
        if pin in pure:
            raise AssertionError(f"TbHostAddonHintPure.h names core type `{pin}`")


def check_no_raw_db():
    found = {}
    for path in list(SRC.rglob("*.cpp")) + list(SRC.rglob("*.h")):
        text = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
        hits = [text.count("\n", 0, m.start()) + 1 for m in DIRECT.finditer(text)]
        if hits:
            found[str(path.relative_to(ROOT))] = hits
    allowed = {"forever/src/compat/TbCompat_S5.h", "forever/src/compat/TbCompat_CharStore.h",
               "forever/src/compat/TbCompat_Unported.h", "forever/src/compat/TbCompat_WL4n.h",
               "forever/src/TbBotMgr.cpp"}
    extra = {k: v for k, v in found.items() if k not in allowed}
    if extra:
        raise AssertionError(f"direct database calls outside the guards: {extra}")


def compile_and_run(sources, binary, include_dirs):
    command = ["g++", "-std=c++17", "-O1", "-Wall", "-Wextra", "-pthread"]
    for directory in include_dirs:
        command += ["-I", str(directory)]
    command += [str(source) for source in sources] + ["-o", str(binary)]
    built = subprocess.run(command, capture_output=True, text=True, timeout=180)
    if built.returncode != 0:
        raise AssertionError("compile failed:\n" + built.stdout + built.stderr)
    return subprocess.run([str(binary)], capture_output=True, text=True, timeout=120)


def check_behavioural_tests():
    # The pure rule and the live half, both compiled and run. The live half is
    # the real TbHostAddonHint.cpp against addon_hint_fakes/, so these checks
    # are behaviour, not source text.
    import shutil
    if shutil.which("g++") is None:
        print("test_addon_hint_wiring: no g++, behavioural tests skipped")
        return
    build = Path(tempfile.mkdtemp(prefix="addon-hint-wiring-"))

    ran = compile_and_run([ROOT / "forever/tools/test_addon_hint_pure.cpp"],
                          build / "test_addon_hint_pure", [ROOT, ROOT / "forever" / "src"])
    if ran.returncode != 0 or "all checks passed" not in ran.stdout:
        raise AssertionError("the pure add-on hint test failed:\n" + ran.stdout + ran.stderr)

    live_includes = [ROOT / "forever" / "tools" / "addon_hint_fakes", ROOT / "forever" / "src", ROOT]
    live_sources = [ROOT / "forever" / "tools" / "test_addon_hint_live.cpp", HOST / "TbHostAddonHint.cpp"]
    ran = compile_and_run(live_sources, build / "test_addon_hint_live", live_includes)
    if ran.returncode != 0 or "all checks passed" not in ran.stdout:
        raise AssertionError("the live add-on hint test failed:\n" + ran.stdout + ran.stderr)

    # The mutation the live test exists for: keep the same token order but
    # release seenMutex before SendSysMessage. The worker's NoteAddonSeen then
    # returns while the send is running, and the live test fails.
    source = read(HOST / "TbHostAddonHint.cpp")
    guard = ("            std::lock_guard<std::mutex> lock(s.seenMutex);\n"
             "            bool const seenNow = s.seen.erase(key) != 0;")
    if guard not in source:
        raise AssertionError("cannot find the seenMutex block to mutate")
    mutated = source.replace(guard, guard.replace("std::lock_guard", "std::unique_lock"))
    send = ("                std::string const text = pure::HintText(g_url);\n"
            "                ChatHandler(player->GetSession()).SendSysMessage(text);")
    if send not in mutated:
        raise AssertionError("cannot find the send to mutate")
    mutated = mutated.replace(send, send.replace(
        "ChatHandler(player->GetSession()).SendSysMessage(text);",
        "lock.unlock();\n                ChatHandler(player->GetSession()).SendSysMessage(text);"))
    mutated_source = build / "TbHostAddonHint_mutated.cpp"
    mutated_source.write_text(mutated)
    ran = compile_and_run([ROOT / "forever" / "tools" / "test_addon_hint_live.cpp", mutated_source],
                          build / "test_addon_hint_live_mutated", live_includes)
    if ran.returncode == 0:
        raise AssertionError("the live test did not catch a seenMutex released before the send")
    if "FAIL line" not in ran.stdout:
        raise AssertionError("the mutated live test failed for another reason:\n" + ran.stdout + ran.stderr)


def main():
    check_pure_core_free()
    check_slice_guards()
    check_login_skips_bots()
    check_seen_after_prefix()
    check_tick_after_drain()
    check_system_message()
    check_decision_under_seen_lock()
    check_seen_leaves_pending()
    check_row_on_both_paths()
    check_staff_skip()
    check_sql_matches_create()
    check_conf_defaults()
    check_feature_row_and_command()
    check_no_raw_db()
    check_behavioural_tests()
    print("test_addon_hint_wiring: pins hold")


if __name__ == "__main__":
    try:
        main()
    except AssertionError as error:
        print(f"FAIL: {error}")
        sys.exit(1)

#!/usr/bin/env python3
"""Wiring pins for the module's ForeverLua bot provider (host/TbLuaBots.{h,cpp}, TbLuaBotsPure.h) and its mapping test.

Run:
  python3 -m unittest forever/tools/test_lua_bots_wiring.py
  python3 forever/tools/test_lua_bots_wiring.py --mutate

The pure mapping is compiled and run from test_lua_bots_check.cpp; the pins below cover what a source test can: the
slice/engine guards, the CMake option, the one registration and the never-unregister rule, every field the engine's
BotProvider.h names, and that the snapshot comes from the host tables (no database, no core header in the pure half).
"""
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOLS = ROOT / "forever" / "tools"
PURE = (ROOT / "forever/src/host/TbLuaBotsPure.h").read_text()
HEADER = (ROOT / "forever/src/host/TbLuaBots.h").read_text()
CPP = (ROOT / "forever/src/host/TbLuaBots.cpp").read_text()
SCRIPTS = (ROOT / "forever/src/TbScripts.cpp").read_text()
CMAKE = (ROOT / "CMakeLists.txt").read_text()
H3 = (ROOT / "forever/src/host/TbH3.h").read_text()
PARTY = (ROOT / "forever/src/host/TbPartyState.cpp").read_text()
BOTMGR = (ROOT / "forever/src/TbBotMgr.h").read_text()
BOTMGR_CPP = (ROOT / "forever/src/TbBotMgr.cpp").read_text()
ORDERS = (ROOT / "forever/src/host/TbOrders.cpp").read_text()
ORDERS_API = (ROOT / "forever/src/host/TbOrdersApi.h").read_text()
RUN_CPP = (ROOT / "forever/src/host/run/TbRun.cpp").read_text()
CHECK_CPP = TOOLS / "test_lua_bots_check.cpp"
RING_CPP = TOOLS / "test_order_ring_check.cpp"
RING_H = (ROOT / "forever/src/host/TbOrderRingPure.h").read_text()

GUARD = "#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1 && defined(TBF_WITH_FOREVER_LUA) && TBF_WITH_FOREVER_LUA"


def check_scripts_registration(scripts_src):
    if scripts_src.count("TortoiseBots::luabots::Register();") != 1:
        raise AssertionError("AddSC_tortoisebots must call luabots::Register() exactly once")
    addsc = scripts_src.index("void AddSC_tortoisebots()")
    shutdown_def = scripts_src.index("void OnShutdown() override")
    register = scripts_src.index("TortoiseBots::luabots::Register();")
    if register < addsc:
        raise AssertionError("Register() must be called from AddSC_tortoisebots")
    if shutdown_def > register:
        raise AssertionError("Shutdown() must be in the world script's OnShutdown")
    guarded = scripts_src[max(0, register - 220):register]
    if "TBF_WITH_FOREVER_LUA" not in guarded:
        raise AssertionError("Register() must be inside the engine guard")
    # TBF-B1 third review finding 8: available() follows TortoiseBots.Enable (synced at every config load), and the shutdown call comes
    # after the bots are logged out and updated (available() may not flip before the last snapshot/events of the shutdown).
    if scripts_src.count("TortoiseBots::luabots::SetEnabled(BotMgr::Instance().Enabled());") != 2:
        raise AssertionError("OnConfigLoad and OnWorldInitialized must sync available() from TortoiseBots.Enable")
    on_shutdown = scripts_src[shutdown_def:scripts_src.index("class tortoisebots_unit")]
    call = on_shutdown.index("TortoiseBots::luabots::Shutdown();")
    update = on_shutdown.index("BotMgr::Instance().Update(0);")
    logout = on_shutdown.index("BotMgr::Instance().LogoutAll(")
    if not logout < update < call:
        raise AssertionError("luabots::Shutdown() must run AFTER LogoutAll() and Update(0)")
    guard = on_shutdown.rfind("#if", 0, call)
    if guard < 0 or "TBF_WITH_FOREVER_LUA" not in on_shutdown[guard:call]:
        raise AssertionError("Shutdown() must be inside the engine guard")
    if "#include \"host/TbLuaBots.h\"" not in scripts_src:
        raise AssertionError("TbScripts must include host/TbLuaBots.h")
    # TBF-B1 review finding 1: the cached role/spec words are invalidated on the level and talent hooks.
    invalidate = "TortoiseBots::luabots::Invalidate("
    if scripts_src.count(invalidate) != 3:
        raise AssertionError("OnLevelChanged, OnFreeTalentPointsChanged and OnTalentsReset must each invalidate the bot cache once")
    for hook in ("void OnLevelChanged(", "void OnFreeTalentPointsChanged(", "void OnTalentsReset("):
        at = scripts_src.index(hook)
        end = scripts_src.find("\n    void On", at + 1)
        body = scripts_src[at:end if end > 0 else len(scripts_src)]
        if invalidate not in body:
            raise AssertionError(f"{hook.strip()} must invalidate the cached spec/role")
        call = body.index(invalidate)
        guard = body.rfind("#if", 0, call)
        closed = body.rfind("#endif", 0, call)
        if guard < 0 or guard < closed or "TBF_WITH_FOREVER_LUA" not in body[guard:call]:
            raise AssertionError(f"{hook.strip()} must call Invalidate inside the engine guard")


def check_cmake(cmake_src):
    if not re.search(r'host/\([^)]*TbLuaBots[^)]*\)\\\\\.cpp', cmake_src):
        raise AssertionError("CMakeLists must exclude host/TbLuaBots.cpp from the option-OFF build (the slice-only filter)")
    block = re.search(r"\nif\(BUILD_FOREVER_LUA\)(.*?)\nendif\(\)", cmake_src, re.S)
    if not block:
        raise AssertionError("CMakeLists must have an if(BUILD_FOREVER_LUA) block")
    body = block.group(1)
    if "TBF_WITH_FOREVER_LUA=1" not in body or "forever-lua-engine" not in body:
        raise AssertionError("the BUILD_FOREVER_LUA block must define TBF_WITH_FOREVER_LUA=1 and link forever-lua-engine")
    if "EXISTS" not in body or "BotProvider.h" not in body or "FATAL_ERROR" not in body:
        raise AssertionError("BUILD_FOREVER_LUA must fail at configure when the engine's BotProvider.h is absent")


def check_provider(cpp_src):
    if GUARD not in cpp_src:
        raise AssertionError("TbLuaBots.cpp must be wrapped in the slice+engine guard")
    if '#include "engine/BotProvider.h"' not in cpp_src:
        raise AssertionError("the provider must implement engine/BotProvider.h")
    if '#include "host/TbLuaBotsPure.h"' not in cpp_src:
        raise AssertionError("the provider must map through TbLuaBotsPure.h")
    if "registerBotProvider(nullptr)" in cpp_src or "registerBotProvider(nullptr)" in HEADER:
        raise AssertionError("the provider is never unregistered")
    for token in ("std::atomic<bool> g_available", "bool available() override",
                  "g_available.store(false", "compare_exchange_strong",
                  "fl::registerBotProvider(&Provider())"):
        if token not in cpp_src:
            raise AssertionError(f"provider lost {token}")
    # TBF-B1 third review finding 8: availability follows TortoiseBots.Enable, set from the world thread; Register no longer forces it on.
    if "void SetEnabled(bool enabled)" not in cpp_src or "g_available.store(enabled" not in cpp_src:
        raise AssertionError("available() must follow TortoiseBots.Enable (SetEnabled)")
    if "g_available.store(true" in cpp_src:
        raise AssertionError("Register() must not force available() on before the config is read")
    for token in ("bots.BotGuids()", "bots.SnapshotBot(", "bots.MasterOf(", "h3::CommanderGuid(",
                  "bots.OwnerBnetOf(", "h3::RoleText(", "h3::RoleForced(", "h3::SpecText(", "h3::EngineStateText(",
                  "h3::PollOrderEvent(", "h3::LastOrder(", "run::All()", "run::StateName(", "run::Ended(",
                  "run::SetFlushListener(", "rec.runId", "GetMemberSlots()",
                  "ObjectAccessor::FindPlayer("):
        if token not in cpp_src:
            raise AssertionError(f"provider lost its source {token}")
    for bad in ("CharacterDatabase.", "WorldDatabase.", "LoginDatabase.", "->Query(", "PQuery("):
        if bad in cpp_src:
            raise AssertionError(f"provider must not query the database ({bad})")
    # TBF-B1 review finding 1: the snapshot reads the per-bot role/spec cache, it does not rescan the talents per tick.
    for token in ("SpecRoleCache", "g_specRole", "uint32 const nowMs = getMSTime()",
                  "if (!specRole.Fresh(key, nowMs))", "specRole.Store(", "specRole.Retain(",
                  "state.role = cached.role;", "state.spec = cached.spec;", "void Invalidate(ObjectGuid const& bot)"):
        if token not in cpp_src:
            raise AssertionError(f"provider lost its spec/role cache: {token}")
    for bad in ("state.role = h3::RoleText(", "state.spec = h3::SpecText("):
        if bad in cpp_src:
            raise AssertionError(f"the snapshot must read the cache, not rescan the talents ({bad})")
    # Review finding 5: master comes from the bot's master, never from the commander; a dead member travels alive=false.
    if "state.master = playerAt(bots.MasterOf(guid));" not in cpp_src:
        raise AssertionError("master must come from BotMgr::MasterOf (not from the commander)")
    # TBF-B1 third review finding 4: the commander is the bound one, resolved one hop through a bot binding, and a handle without a
    # place (offline / out of the world) is empty (the pure PlayerHandle).
    if "state.commander = playerAt(commanderGuid(guid));" not in cpp_src:
        raise AssertionError("commander must be resolved one hop (commanderGuid)")
    if "if (bound.IsEmpty() || !BotMgr::Instance().IsBot(bound))" not in cpp_src or \
            "return h3::CommanderGuid(bound);" not in cpp_src:
        raise AssertionError("a bot's bound commander must resolve to its own commander (one hop)")
    if "return PlayerHandle(halves(guid), player != nullptr, player && player->IsInWorld()" not in cpp_src:
        raise AssertionError("an offline or out-of-world player handle must be empty (PlayerHandle)")
    # TBF-B1 third review finding 2: the snapshot takes one unread order transition per tick, then the record's newest word.
    if "if (!h3::PollOrderEvent(guid, order))" not in cpp_src:
        raise AssertionError("the order the snapshot reports must come from the transition ring")
    # TBF-B1 third review finding 3: an ended run whose events are all read is not walked again; a removed record is flushed first.
    if "run::Ended(*rec) && _cursor.Seen(halves(rec->leader), rec->runId) == rec->eventSeq" not in cpp_src:
        raise AssertionError("an ended, fully read run must not be walked every tick")
    if "void Flush(run::RunRec const& rec)" not in cpp_src or "        Take(rec, _pending);" not in cpp_src:
        raise AssertionError("the flush listener must take a removed record's unread events")
    if "kMaxPendingFlush" not in cpp_src:
        raise AssertionError("the flushed-events buffer must stay bounded")
    # Final check of TBF-B1 (PR 264): the level/talent hooks can fire on a map worker (MapUpdate.Threads > 1), so Invalidate only
    # queues the guid under a mutex and the world thread's snapshot applies the queue; it never touches g_specRole itself.
    if "std::mutex g_invalidMutex;" not in cpp_src or "g_invalid.insert(halves(bot));" not in cpp_src or \
            "queued.swap(g_invalid);" not in cpp_src or "specRole.Invalidate(key);" not in cpp_src:
        raise AssertionError("Invalidate must queue under a mutex and the snapshot must apply the queue")
    if "g_specRole.Invalidate(" in cpp_src:
        raise AssertionError("Invalidate must not touch the world thread's cache from a hook thread")
    if "run::SetFlushListener(&FlushRun);" not in cpp_src:
        raise AssertionError("the provider must install its run flush listener")
    # TBF-B1 third review finding 7: an ended run is not reported as active.
    if "!run::Ended(*runRec->second)" not in cpp_src:
        raise AssertionError("readSnapshot must not report an ended run as active")
    if "MemberFrom(halves(slot.guid), bots.IsBot(slot.guid), online != nullptr," not in cpp_src or \
            "online && online->IsInWorld(), online && online->IsAlive()" not in cpp_src:
        raise AssertionError("a group member's alive flag must be its real state (a dead member must report alive=false)")
    # Every field of BotProvider.h's BotInfo/RunInfo/OrderInfo/RunEventInfo is assigned by the .cpp.
    body = cpp_src[cpp_src.index("void toEngine(BotRow const& in"):cpp_src.index("void toEngine(GroupRow const& in")]
    for field in ("player", "name", "level", "cls", "role", "roleForced", "spec", "hasEngine", "engineState",
                  "alive", "inCombat", "master", "commander", "group", "owner", "lastOrder", "hasRun", "run"):
        if f"out.{field} = " not in body and f"out.{field})" not in body and f"out.{field}," not in body:
            raise AssertionError(f"BotInfo.{field} is not copied")
    order = cpp_src[cpp_src.index("void toEngine(OrderRow const& in"):cpp_src.index("void toEngine(RunRow const& in")]
    for field in ("seq", "verb", "result", "state", "code", "message"):
        if f"out.{field} = " not in order:
            raise AssertionError(f"OrderInfo.{field} is not copied")
    run = cpp_src[cpp_src.index("void toEngine(RunRow const& in"):cpp_src.index("void toEngine(BotRow const& in")]
    for field in ("leader", "plan", "state", "step", "steps", "why"):
        if f"out.{field} = " not in run:
            raise AssertionError(f"RunInfo.{field} is not copied")
    members = cpp_src[cpp_src.index("void toEngine(GroupRow const& in"):cpp_src.index("SnapshotRows readSnapshot(")]
    for field in ("guid", "isBot", "alive", "inWorld"):
        if f"info.{field} = " not in members:
            raise AssertionError(f"GroupMemberInfo.{field} is not copied")
    events = cpp_src[cpp_src.index("void takeRunEvents"):cpp_src.index("private:\n    RunCursor")]
    for field in ("kind", "leader", "plan", "step", "leg", "why"):
        if f"info.{field} = " not in events:
            raise AssertionError(f"RunEventInfo.{field} is not copied")
    if "RunKind(event.kind)" not in events:
        raise AssertionError("takeRunEvents must filter the ring by kind")


def check_pure_purity(pure_src, ring_src=None):
    for line in pure_src.splitlines():
        stripped = line.strip()
        if stripped.startswith("#include") and not re.match(r'#include <(cstddef|cstdint|map|set|string|utility|vector)>', stripped):
            raise AssertionError(f"TbLuaBotsPure.h must stay game-free and engine-free: {stripped}")
    for line in (ring_src or "").splitlines():
        stripped = line.strip()
        if stripped.startswith("#include") and not re.match(r'#include <(cstddef|cstdint|map|set|string|utility|vector)>', stripped):
            raise AssertionError(f"TbOrderRingPure.h must stay game-free and engine-free: {stripped}")


def check_exports(h3_src, party_src, botmgr_src):
    for name in ("RoleText", "RoleForced", "SpecText", "EngineStateText"):
        if name not in h3_src:
            raise AssertionError(f"TbH3.h must declare h3::{name}")
        if f"std::string {name}(" not in party_src and f"bool {name}(" not in party_src:
            raise AssertionError(f"TbPartyState.cpp must define h3::{name}")
    if "OwnerBnetOf" not in botmgr_src:
        raise AssertionError("BotMgr must expose OwnerBnetOf (the memoised Battle.net owner)")
    # TBF-B1 third review finding 5: `owner` is the character's own game account's Battle.net id, cached from its session at login.
    if "bot.characterBnet = bot.session->GetBattlenetAccountId();" not in BOTMGR_CPP:
        raise AssertionError("Login must cache the character's own Battle.net id from its session")
    if "return itr == _bots.end() ? 0 : itr->second.characterBnet;" not in botmgr_src:
        raise AssertionError("OwnerBnetOf must answer the character's own Battle.net id")
    # TBF-B1 third review finding 6: the role word is never empty (RoleWord; no 'none' fall-through).
    if "luabots::RoleWord(PlayerbotAI::IsTank(bot, true), PlayerbotAI::IsHeal(bot, true))" not in party_src:
        raise AssertionError("RoleOf must read the role without an AI object and fall back to dps")
    if "? \"dps\" : \"none\"" in party_src:
        raise AssertionError("RoleOf must never answer an empty role")
    # Review finding 4: the four words are engine-only; a slice build without ForeverLua carries none of their code.
    if not re.search(r'#if defined\(TBF_WITH_FOREVER_LUA\) && TBF_WITH_FOREVER_LUA\s*\nstd::string RoleText\(.*?\n#endif',
                     party_src, re.S):
        raise AssertionError("the four h3 bot-API definitions must sit inside the TBF_WITH_FOREVER_LUA guard")
    if not re.search(r'#if defined\(TBF_WITH_FOREVER_LUA\) && TBF_WITH_FOREVER_LUA\s*\nstd::string RoleText\(.*?\n#endif',
                     h3_src, re.S):
        raise AssertionError("the four h3 bot-API declarations must sit inside the TBF_WITH_FOREVER_LUA guard")

def check_order_and_run(orders_src, orders_api, run_src, botmgr_cpp):
    # TBF-B1 third review finding 2: the order record carries the transition ring, every transition site pushes, the provider polls it,
    # and the record (ring included) is erased at logout.
    if "OrderTransitionRing ring;" not in orders_src:
        raise AssertionError("BotRec must carry the order transition ring")
    for push in ("rec.ring.Push(order.seq, order.verb, \"queued\");",
                 "rec->second.ring.Push(order.seq, order.verb, result);"):
        if push not in orders_src:
            raise AssertionError(f"the order transitions must be pushed: {push}")
    if "bool PollOrderEvent(ObjectGuid bot, OrderStatus& out)" not in orders_src:
        raise AssertionError("the provider's transition poll must be defined")
    if "void ForgetBotRecords(ObjectGuid bot)" not in orders_src or "Recs().erase(bot);" not in orders_src:
        raise AssertionError("the per-bot order record must be erased at logout")
    if "void ForgetBotRecords(ObjectGuid bot);" not in orders_api:
        raise AssertionError("TbOrdersApi.h must declare ForgetBotRecords")
    if "h3::ForgetBotRecords(leaving);" not in botmgr_cpp or "run::OnBotLogout(leaving);" not in botmgr_cpp:
        raise AssertionError("BotMgr::OnPlayerLogout must erase the order record and the ended run record")
    # TBF-B1 third review finding 3: a monotonic run id, the flush listener called before the record dies.
    if "rec->runId = NextRunId();" not in run_src:
        raise AssertionError("Create must assign a monotonic runId")
    remove_at = run_src.index("bool Remove(ObjectGuid leader)")
    remove_body = run_src[remove_at:run_src.index("std::vector<RunRec*> All()")]
    flush = remove_body.index("FlushHook(*it->second);")
    erase = remove_body.index("Table().erase(it);")
    if not flush < erase:
        raise AssertionError("Remove must flush the record's unread events BEFORE the record dies")
    # TBF-B1 third review finding 7: an ended run record of a logged-out leader is removed.
    if "void OnBotLogout(ObjectGuid leader)" not in run_src or "if (rec && Ended(*rec))" not in run_src:
        raise AssertionError("an ended run record must be removed at its leader's logout")


class LuaBotsWiring(unittest.TestCase):
    def test_scripts_wiring(self):
        check_scripts_registration(SCRIPTS)

    def test_cmake_guard(self):
        check_cmake(CMAKE)

    def test_provider_mapping(self):
        check_provider(CPP)

    def test_pure_header_is_game_free(self):
        check_pure_purity(PURE, RING_H)

    def test_word_exports(self):
        check_exports(H3, PARTY, BOTMGR)

    def test_order_and_run(self):
        check_order_and_run(ORDERS, ORDERS_API, RUN_CPP, BOTMGR_CPP)

    def test_order_ring_runs(self):
        gxx = shutil.which("g++") or shutil.which("c++")
        if not gxx:
            self.skipTest("no C++ compiler")
        with tempfile.TemporaryDirectory() as tmp:
            exe = pathlib.Path(tmp) / "test_order_ring_check"
            build = subprocess.run([gxx, "-std=c++17", "-O1", "-Wall", "-Wextra", "-I", str(ROOT), str(RING_CPP), "-o", str(exe)],
                                   capture_output=True, text=True)
            if build.returncode != 0:
                self.fail("test_order_ring_check.cpp does not compile:\n" + build.stderr[-3000:])
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            if run.returncode != 0 or "0 failed" not in run.stdout:
                self.fail("order ring test failed:\n" + run.stdout + run.stderr)

    def test_pure_mapping_runs(self):
        gxx = shutil.which("g++") or shutil.which("c++")
        if not gxx:
            self.skipTest("no C++ compiler")
        with tempfile.TemporaryDirectory() as tmp:
            exe = pathlib.Path(tmp) / "test_lua_bots_check"
            build = subprocess.run([gxx, "-std=c++17", "-O1", "-Wall", "-Wextra", "-I", str(ROOT), str(CHECK_CPP), "-o", str(exe)],
                                   capture_output=True, text=True)
            if build.returncode != 0:
                self.fail("test_lua_bots_check.cpp does not compile:\n" + build.stderr[-3000:])
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            if run.returncode != 0 or "0 failed" not in run.stdout:
                self.fail("pure mapping test failed:\n" + run.stdout + run.stderr)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        mutations = [
            ("register-once", lambda: check_scripts_registration(SCRIPTS.replace("TortoiseBots::luabots::Register();", ""))),
            ("cmake-slice-filter", lambda: check_cmake(CMAKE.replace("|TbLuaBots", ""))),
            ("atomic-available", lambda: check_provider(CPP.replace("std::atomic<bool> g_available", "bool g_available"))),
            ("owner-copy", lambda: check_provider(CPP.replace("out.owner = in.owner;", ""))),
            ("pure-include", lambda: check_pure_purity(PURE + '\n#include "Player.h"\n')),
            ("master-from-commander", lambda: check_provider(CPP.replace(
                "state.master = playerAt(bots.MasterOf(guid));",
                "state.master = playerAt(h3::CommanderGuid(guid));"))),
            ("member-alive-true", lambda: check_provider(CPP.replace(
                "online && online->IsInWorld(), online && online->IsAlive()",
                "online && online->IsInWorld(), true"))),
            ("spec-uncached", lambda: check_provider(CPP.replace("state.spec = cached.spec;", "state.spec = h3::SpecText(bot);"))),
            ("no-invalidate", lambda: check_scripts_registration(SCRIPTS.replace(
                "TortoiseBots::luabots::Invalidate(player->GetGUID());", "// removed", 1))),
            ("invalidate-unsafe", lambda: check_provider(CPP.replace(
                "g_invalid.insert(halves(bot));", "g_specRole.Invalidate(halves(bot));"))),
            ("no-cmake-guard", lambda: check_cmake(CMAKE.replace('if(NOT EXISTS "${TB_ENGINE_BOT_PROVIDER}")', 'if(FALSE)'))),
        ]
        failed = False
        for name, mutate in mutations:
            try:
                mutate()
            except AssertionError:
                print(f"MUTATION CAUGHT: {name}")
            else:
                print(f"ERROR: mutation survived: {name}", file=sys.stderr)
                failed = True
        sys.exit(1 if failed else 0)
    else:
        unittest.main(argv=[sys.argv[0]])

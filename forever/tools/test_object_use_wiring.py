#!/usr/bin/env python3
"""Wiring pins for issue #14 round 1: the ritual fill (a real player's multi-user object, joined by the player's own bots).

The core calls cannot run without a server, so these pin the shape of the live side; the decisions themselves are tested
standalone by forever/tools/test_object_use_policy.cpp. Each assertion fails on the code before the fill:
  1 the service is a `host/TbHost*.cpp`, which CMake compiles only with TORTOISEBOTS_SLICE1 (the option-OFF build has no file)
  2 the packet hook, the world tick and the logout forget all call the service under the slice guard
  3 the decisions come from host/TbObjectUsePure.h (MissingUsers, PickFillers, JudgeHold) and the header is core-free
  4 the engine hold is taken BEFORE the use packet is sent, and it is released on every path
  5 the use goes through the typed packet (run::tasks::SendGameObjectUse), never GameObject::Use
  6 the pick re-checks chatin::StillAuthorized and the hold re-checks it every second
  7 a bot in combat is released (the JudgeHold facts carry the bot's own combat state)
  8 a bot's own use never starts a fill (the hook drops bots, the drain drops them again)
  9 no database query and no core file is touched
 10 the four config keys are in the template, the service reads its three, and `.tb feature` lists them
 11 the off switch records no click and drops what was already queued (nothing is replayed on the next switch-on)
 12 a target nobody holds keeps its `used` record while the object still fills (once per object per bot)

OBJECT_USE_MUTATE=1 runs the fill mutations against mutated text and expects each check to fail.
"""
import os
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / "forever/src"
SERVICE_PATH = SRC / "host/TbHostRitualFill.cpp"
HEADER_PATH = SRC / "host/TbHostRitualFill.h"
PURE_PATH = SRC / "host/TbObjectUsePure.h"
SCRIPTS_PATH = SRC / "TbScripts.cpp"
MGR_PATH = SRC / "TbBotMgr.cpp"
ADDON_PATH = SRC / "host/TbAddonInfo.cpp"
CONF_PATH = ROOT / "forever/conf/tortoisebots.conf.dist"
CMAKE_PATH = ROOT / "CMakeLists.txt"
GUARD = "#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1"


def read(path, overrides=None, key=None):
    if overrides and key in overrides:
        return overrides[key]
    return pathlib.Path(path).read_text()


def strip_comments(source):
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
    return re.sub(r"//[^\n]*", "", source)


def guarded(text, call, span=400):
    """`call` sits under the slice guard: the guard line shortly before it and its #endif shortly after."""
    at = text.find(call)
    if at < 0:
        return False
    before = text.rfind(GUARD, 0, at)
    if before < 0 or at - before > span:
        return False
    if text.rfind("#endif", before, at) > before:      # an #endif between the guard and the call
        return False
    after = text.find("#endif", at)
    return 0 <= after - at <= span


def sources(overrides=None):
    return {
        "service": read(SERVICE_PATH, overrides, "service"),
        "header": read(HEADER_PATH, overrides, "header"),
        "pure": read(PURE_PATH, overrides, "pure"),
        "scripts": read(SCRIPTS_PATH, overrides, "scripts"),
        "mgr": read(MGR_PATH, overrides, "mgr"),
        "addon": read(ADDON_PATH, overrides, "addon"),
        "conf": read(CONF_PATH, overrides, "conf"),
        "cmake": read(CMAKE_PATH, overrides, "cmake"),
    }


# ---- the checks, each a function of the source texts so the mutations reuse them ----

def check_slice_only(src):
    assert SERVICE_PATH.name.startswith("TbHost"), "the service must be a host/TbHost*.cpp"
    assert SERVICE_PATH.parent.name == "host"
    assert 'EXCLUDE REGEX "/forever/src/(slice/[^/]*|host/TbHost[^/]*)\\\\.cpp$"' in src["cmake"], \
        "CMake must keep host/TbHost*.cpp out of the default build"
    assert "${CMAKE_CURRENT_SOURCE_DIR}/forever/src/host/TbHost*.cpp" in src["cmake"], \
        "CMake must add host/TbHost*.cpp in the SLICE1 branch"
    # The TbScripts include is inside the slice block; nothing outside it names the service.
    scripts = src["scripts"]
    include_at = scripts.find('#include "host/TbHostRitualFill.h"')
    assert include_at > 0
    ifdef = scripts.rfind("#ifdef TORTOISEBOTS_SLICE1", 0, include_at)
    assert ifdef > 0 and scripts.rfind("#endif", ifdef, include_at) < ifdef, "the include must sit under #ifdef TORTOISEBOTS_SLICE1"


def check_hooks(src):
    scripts = src["scripts"]
    assert "void OnPacketReceive(WorldSession* session, WorldPacket& packet) override" in scripts
    assert guarded(scripts, "tbhost::ritualfill::OnPacket(session, packet);"), "the packet hook must be slice-guarded"
    assert (scripts.find("TortoiseBots::place::OnPacket(session, packet);")
            < scripts.find("tbhost::ritualfill::OnPacket")), "the fill hooks the same OnPacketReceive, after the place hook"
    assert guarded(scripts, "tbhost::ritualfill::NoteLogout(player);"), "the logout forget must be slice-guarded"
    assert guarded(src["mgr"], "tbhost::ritualfill::Tick(diff);"), "the tick must be slice-guarded"
    tick_at = src["mgr"].find("tbhost::ritualfill::Tick(diff);")
    assert src["mgr"].find("TortoiseBots::chatin::DrainArrivals();") < tick_at, "the tick runs after the chat drain"
    assert "void BotMgr::Update(uint32 diff)" in src["mgr"]


def check_decisions(src):
    service = strip_comments(src["service"])
    for call in ("objuse::MissingUsers(", "objuse::PickFillers(", "objuse::JudgeHold("):
        assert call in service, call
    assert '#include "host/TbObjectUsePure.h"' in service
    pure = src["pure"]
    for forbidden in ("Player.h", "Group.h", "ObjectGuid.h", "GameObject.h", "playerbot/", "Define.h"):
        assert forbidden not in strip_comments(pure), f"the pure header must stay core-free ({forbidden})"


def check_hold_before_use(src):
    service = strip_comments(src["service"])
    hold_at = service.find("RitualHolds().Hold(")
    use_at = service.find("run::tasks::SendGameObjectUse(")
    assert hold_at > 0, "the engine hold must be taken"
    assert use_at > hold_at, "the hold must be taken before the use packet (a held bot's engine is idle)"
    assert "RitualHolds().Release(" in service, "every release must let the engine go"
    assert service.count("ReleaseHold(") >= 3, "release paths: judge, missing bot, logout"


def check_no_core_use(src):
    service = strip_comments(src["service"])
    assert "SendGameObjectUse(" in service
    for forbidden in ("->Use(", "HandleGameObjectUseOpcode", "UseItemAction"):
        assert forbidden not in service, f"the use is the typed packet only ({forbidden})"


def check_auth(src):
    service = strip_comments(src["service"])
    assert service.count("chatin::StillAuthorized(") >= 2, "authorization is checked at the pick and every second"
    assert "initiator->GetGroup()" in service, "only the initiator's own group is filled"
    assert "BotMgr::Instance().IsBot(" in service


def check_combat_release(src):
    service = strip_comments(src["service"])
    assert "facts.inCombat = bot->IsInCombat();" in service, "the held bot's own combat state reaches JudgeHold"
    assert "facts.dead = !bot->IsAlive();" in service
    assert "facts.holdElapsed" in service, "HoldSeconds reaches JudgeHold"


def check_bot_use_ignored(src):
    service = strip_comments(src["service"])
    assert "PlayerbotAIStorage::Instance().GetBotGuid(player, botGuid)" in service, "the hook drops a bot's own use"
    assert "BotMgr::Instance().IsBot(" in service, "the tick re-checks the sender"


def check_no_sql(src):
    for text in (src["service"], src["header"]):
        stripped = strip_comments(text)
        for forbidden in ("Database", "Query", "SELECT "):
            assert forbidden not in stripped, f"no direct database query ({forbidden})"


def check_disabled_no_record(src):
    # Off: the hook records nothing, and the tick drops what was queued before the switch went off (never replayed on).
    service = strip_comments(src["service"])
    hook_at = service.find("void OnPacket(WorldSession* session, WorldPacket& packet)")
    assert hook_at >= 0
    hook = service[hook_at:]
    gate_at = hook.find("if (!Enabled())")
    record_at = hook.find("Clicks().Record(")
    assert gate_at >= 0 and 0 < record_at and gate_at < record_at, \
        "the packet hook must return before recording when the fill is off"
    tick = service[service.find("void Tick("):]
    lock_at = tick.find("std::lock_guard<std::mutex> guard(ClickLock());")
    clear_at = tick.find("Clicks().SetEnabled(enabled);")
    disabled_at = tick.find('ReleaseAll("disabled");')
    assert 0 <= lock_at < clear_at < disabled_at, "the off path drops queued clicks under the lock"


def check_target_memory(src):
    # A released bot's "already used" record lives on the target while the object fills: a bot is never held twice for one object.
    service = strip_comments(src["service"])
    assert "lastSeenMs" in service, "a target remembers when it was last seen"
    prune = service[service.find("void PruneTargets("):]
    assert prune and "it = Targets().erase(it);" in prune
    remember_at = prune.find("objuse::RemembersTarget(")
    assert remember_at >= 0, "a target nobody holds is kept while its object still fills"
    assert service.count("Targets()[") >= 2, "both the click drain and the channel scan reuse the target"


def check_config(src):
    conf = src["conf"]
    for key, value in (("TortoiseBots.RitualFill.Enable", "1"), ("TortoiseBots.RitualFill.Range", "30"),
                       ("TortoiseBots.RitualFill.HoldSeconds", "120"), ("TortoiseBots.ObjectUse.Commands", "1")):
        assert f"{key} = {value}" in conf, f"{key} = {value} in the template"
    service = strip_comments(src["service"])
    assert 'sConfigMgr->GetBoolDefault("TortoiseBots.RitualFill.Enable", true' in service
    assert 'sConfigMgr->GetIntDefault("TortoiseBots.RitualFill.Range"' in service
    assert 'sConfigMgr->GetIntDefault("TortoiseBots.RitualFill.HoldSeconds", 120' in service
    addon = src["addon"]
    for feature in ("ritual-fill", "ritual-fill-range", "ritual-fill-hold", "object-use"):
        assert f'"{feature}"' in addon, feature


def check_no_engine_key(src):
    # The fill is host-driven: no strategy, action, trigger or value was added, and no slice list changed.
    for name in ("SLICE-W4.txt",):
        assert "TbHostRitualFill" not in (ROOT / "forever" / name).read_text()
    assert "AiObjectContext" not in strip_comments(src["service"])


class Wiring(unittest.TestCase):
    def test_slice_only(self):
        check_slice_only(sources())

    def test_hooks(self):
        check_hooks(sources())

    def test_decisions(self):
        check_decisions(sources())

    def test_hold_before_use(self):
        check_hold_before_use(sources())

    def test_no_core_use(self):
        check_no_core_use(sources())

    def test_auth(self):
        check_auth(sources())

    def test_combat_release(self):
        check_combat_release(sources())

    def test_bot_use_ignored(self):
        check_bot_use_ignored(sources())

    def test_no_sql(self):
        check_no_sql(sources())

    def test_disabled_no_record(self):
        check_disabled_no_record(sources())

    def test_target_memory(self):
        check_target_memory(sources())

    def test_config(self):
        check_config(sources())

    def test_no_engine_key(self):
        check_no_engine_key(sources())


@unittest.skipUnless(os.environ.get("OBJECT_USE_MUTATE") == "1", "mutations are run on purpose: OBJECT_USE_MUTATE=1")
class Mutations(unittest.TestCase):
    """Each check must fail on a broken tree: the mutation is the smallest change that removes the guarantee."""

    def assertFails(self, fn):
        with self.assertRaises(AssertionError):
            fn(self.src)

    def mutate(self, key, old, new):
        assert old in self.src[key], old
        self.src[key] = self.src[key].replace(old, new, 1)

    def setUp(self):
        self.src = sources()

    def test_hold_after_use(self):
        self.mutate("service", "    TortoiseBots::RemoteCommander::RitualHolds().Hold(GuidKeyCompat(bot->GetGUID()));\n", "")
        self.assertFails(check_hold_before_use)

    def test_hold_release_dropped(self):
        self.mutate("service", "TortoiseBots::RemoteCommander::RitualHolds().Release(", "/* released */ (")
        self.assertFails(check_hold_before_use)

    def test_release_on_combat_dropped(self):
        self.mutate("service", "facts.inCombat = bot->IsInCombat();\n", "")
        self.assertFails(check_combat_release)

    def test_auth_skipped(self):
        self.mutate("service", "candidate.authorized = TortoiseBots::chatin::StillAuthorized(member, initiator);",
                    "candidate.authorized = true;")
        self.mutate("service", "|| !TortoiseBots::chatin::StillAuthorized(bot, initiator)", "")
        self.assertFails(check_auth)

    def test_slice_guard_dropped(self):
        self.mutate("scripts", GUARD + "\n        // #14: a real player's use of a multi-user object; the packet is only read here "
                    "(host/TbHostRitualFill.h).\n        tbhost::ritualfill::OnPacket(session, packet);\n",
                    "        tbhost::ritualfill::OnPacket(session, packet);\n")
        self.assertFails(check_hooks)

    def test_core_use_dropped_in(self):
        self.mutate("service", "TortoiseBots::run::tasks::SendGameObjectUse(bot, go)", "go->Use(bot)")
        self.assertFails(check_no_core_use)

    def test_disabled_records_clicks(self):
        self.mutate("service", "    if (!Enabled())\n        return;", "")
        self.assertFails(check_disabled_no_record)

    def test_disabled_queue_not_cleared(self):
        self.mutate("service", "Clicks().SetEnabled(enabled);", "")
        self.assertFails(check_disabled_no_record)

    def test_target_dropped_on_release(self):
        self.mutate("service",
                    "        if (TortoiseBots::objuse::RemembersTarget(go && go->isSpawned(), go && go->GetGoState() == GO_STATE_READY,\n"
                    "            seenAgoMs, TortoiseBots::objuse::kTargetMemoryMs))\n"
                    "        {\n            ++it;\n            continue;\n        }\n",
                    "")
        self.assertFails(check_target_memory)


if __name__ == "__main__":
    unittest.main()

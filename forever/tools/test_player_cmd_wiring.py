#!/usr/bin/env python3
"""Wiring pins and mutations for the `.bot` entry (round R1 of PLAYER-COMMANDS-SPEC: forever/docs/PC-R1-NOTES.md).

The core calls cannot run without a server, so these tests pin where the hook stands, what the entry may reply and which keys it reads; reach
into the pure rule through forever/tools/test_player_auth.cpp. Every assertion fails on the pre-R1 source (no hook, no `.bot`, no keys). With
PLAYER_CMD_MUTATE=1 (run on the build VM) each mutation of the pure header is compiled with the real unit test and must turn it red:
  gm-branch-dropped        a GM refused like anyone else
  account-zero-allowed     two characters without an account treated as one owner
  logout-tactical          logout classified as a tactical verb (a master may log the owner's bot out)
  leader-bot-counted       a bot leading the party counted as a real player
  claim-prefix             `botany` claimed as `bot`
  in-run-after-gm          the run refusal skipped for GMs
  login-cap-dropped        the owned-login cap not enforced
  cooldown-gm-not-exempt   the summon cooldown kept for GMs
"""
import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SRC = ROOT / "forever" / "src"
PLAYER = SRC / "host" / "player"
TOOLS = ROOT / "forever" / "tools"
TEST = TOOLS / "test_player_auth.cpp"


def read(path):
    return Path(path).read_text()


def strip_comments(source):
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
    return re.sub(r"//[^\n]*", "", source)


def function_body(source, signature_regex):
    m = re.search(signature_regex, source)
    assert m, f"function not found: {signature_regex}"
    start = source.index("{", m.end() - 1 if source[m.end() - 1] == "{" else m.end())
    depth = 0
    for i in range(start, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[start:i + 1]
    raise AssertionError("unbalanced braces")


class Wiring(unittest.TestCase):
    def setUp(self):
        self.cpp = read(PLAYER / "TbPlayerCmd.cpp")
        self.hdr = read(PLAYER / "TbPlayerCmd.h")
        self.pure = read(PLAYER / "TbPlayerAuthPure.h")
        self.scripts = read(SRC / "TbScripts.cpp")
        self.conf = read(ROOT / "forever" / "conf" / "tortoisebots.conf.dist")
        self.unit = read(TEST)

    def test_on_command_claims_the_word_bot(self):
        body = function_body(self.cpp, r"bool OnCommand\(Player\* player, std::string_view command\) override\s*\{")
        self.assertIn("ClaimsCommand(command, rest)", body)
        self.assertIn("return false;", body)                       # not ours: the core answers it
        self.assertIn("return Dispatch(player, rest);", body)
        # The script is the core's PlayerScript, named for the module.
        self.assertIn('PlayerScript("tortoisebots_player_cmd")', self.cpp)
        self.assertIn("bool OnCommand(Player* player, std::string_view command) override", self.cpp)

    def test_module_off_leaves_the_command_to_the_core(self):
        body = function_body(self.cpp, r"bool Dispatch\(Player\* player, std::string_view rest\)\s*\{")
        self.assertIn("BotMgr::Instance().Enabled()", body)
        self.assertIn("engine::Built()", body)
        self.assertIn("return false;", body)                       # TortoiseBots.Enable = 0 or no engine: the core's answer stands
        gate = body.index("engine::Built()")
        self.assertLess(body.index("return false;"), body.index("Say(", gate) if "Say(" in body[gate:] else len(body))
        # The header names the same contract.
        self.assertIn("bool Dispatch(Player* player, std::string_view rest);", self.hdr)

    def test_gm_is_the_rbac_permission(self):
        body = function_body(self.cpp, r"bool Dispatch\(Player\* player, std::string_view rest\)\s*\{")
        self.assertIn("HasPermission(rbac::RBAC_PERM_COMMAND_GM)", body)
        # `.bot` never grants more than `.tb`: the legacy security level is not the test here.
        source = strip_comments(self.cpp)
        self.assertNotIn("GetSecurity()", source)

    def test_players_off_keeps_the_gm_path(self):
        body = function_body(self.cpp, r"bool Dispatch\(Player\* player, std::string_view rest\)\s*\{")
        self.assertIn("!s_commands && !caller.gm", body)
        self.assertIn('"players-off"', body)

    def test_registered_from_addsc_and_config_reloaded(self):
        self.assertIn("TortoiseBots::player::AddScripts();", self.scripts)
        self.assertIn("TortoiseBots::player::LoadConfig();", self.scripts)

    def test_no_account_identity_in_a_reply(self):
        # The account id is read for ownership (the pure Caller); it never reaches the text a player sees, and neither does an
        # account name or the owner's other characters.
        for line in self.cpp.splitlines():
            if "Say(" in line or "Refuse(player" in line or '"TortoiseBots: "' in line:
                self.assertNotIn("account", line)
                self.assertNotIn("AccountMgr", line)
                self.assertNotIn("GetAccount", line)
        self.assertNotIn("AccountMgr::GetName", strip_comments(self.cpp))

    def test_reply_grammar_and_codes(self):
        self.assertIn('"TortoiseBots: "', self.cpp)
        self.assertIn('" bot(s)"', self.cpp)
        self.assertIn('": refused "', self.cpp)
        for code in ("unknown-verb", "players-off", "usage"):
            self.assertIn(f'"{code}"', self.cpp, f"reply code {code} missing")
        for code in ("not-a-bot", "not-yours", "in-run"):
            self.assertIn(f'"{code}"', self.pure, f"refusal code {code} missing from the pure rule")
        # A list verb prints the count first, then one line per bot.
        who = function_body(self.cpp, r"bool HandleWho\(Player\* player, Caller const& caller, OwnerScope const& scope, std::string_view tail\)\s*\{")
        self.assertIn('" bot(s)"', who)
        self.assertLess(who.index('" bot(s)"'), who.index("for (Player* bot : bots)"))

    def test_who_lists_online_bots_of_the_caller(self):
        who = function_body(self.cpp, r"bool HandleWho\(Player\* player, Caller const& caller, OwnerScope const& scope, std::string_view tail\)\s*\{")
        self.assertIn("BotMgr::Instance().BotGuids()", who)
        self.assertIn("IsInWorld()", who)
        self.assertIn("Visible(caller, FactsOf(player, bot, scope))", who)
        self.assertIn("caller.gm", who)                             # `who all` is the GM widening
        self.assertIn("level ", who)
        self.assertIn("map ", who)
        self.assertIn('"follow"', self.cpp)
        self.assertIn('"stay"', self.cpp)
        self.assertIn('"free"', self.cpp)
        self.assertIn('"engine"', self.cpp)
        self.assertIn('"spike"', self.cpp)
        # Visible is the four D-4 faces, not the GM (that is `who all`) and not the run (a run refuses orders, not the listing).
        visible = function_body(self.cpp, r"bool Visible\(Caller const& caller, BotFacts const& bot\)\s*\{")
        self.assertIn("bot.account", visible)
        self.assertIn("bot.master", visible)
        self.assertIn("bot.commandedByCaller", visible)
        self.assertIn("bot.groupLeader", visible)
        self.assertIn("!bot.leaderIsBot", visible)
        self.assertNotIn("inRun", visible)
        self.assertNotIn("gm", visible)

    def test_ownership_is_the_battlenet_account(self):
        # D-1 amendment (2026-11-22): a requester owns a bot when the bot character's game account belongs to the
        # requester's Battle.net account. The caller's Battle.net id is its session's; the bot's game account is checked
        # against the game accounts that Battle.net account holds NOW (read per command), never a raw game-account compare.
        dispatch = function_body(self.cpp, r"bool Dispatch\(Player\* player, std::string_view rest\)\s*\{")
        self.assertIn("GetSession()->GetBattlenetAccountId()", dispatch)
        self.assertIn("ScopeOf(player)", dispatch)
        scope = function_body(self.cpp, r"OwnerScope ScopeOf\(Player const\* player\)\s*\{")
        self.assertIn("GetBattlenetAccountId()", scope)
        # The game-account -> Battle.net check is the core's own Battlenet::AccountMgr::GetIdByGameAccount (a fresh login-DB read;
        # the module makes no raw query, test_forever_no_direct_queries.py), asked in OwnerScope::Owns.
        owns = function_body(self.cpp, r"bool Owns\(std::uint32_t gameAccount\) const\s*\{")
        self.assertIn("Battlenet::AccountMgr::GetIdByGameAccount(gameAccount)", owns)
        self.assertIn("if (!bnet || !gameAccount)", owns)   # fail closed
        self.assertIn("return it->second == bnet", owns)
        # A live unlink of the caller's own game account leaves the session's old Battle.net id: the scope holds only while
        # the caller's game account is still one of that Battle.net account's now.
        self.assertIn("!scope.Owns(session->GetAccountId())", scope)
        self.assertIn("caller.account = scope.bnet", dispatch)
        facts = function_body(self.cpp, r"BotFacts FactsOf\(Player\* requester, Player\* bot, OwnerScope const& scope\)\s*\{")
        self.assertIn("sCharacterCache->GetCharacterAccountIdByGuid(guid)", facts)
        self.assertIn("scope.Owns(gameAccount)", facts)
        self.assertIn("facts.account = scope.bnet", facts)
        # The pure rule's account field carries a Battle.net account, then compared for equality.
        self.assertIn("caller.account == bot.account", self.pure)
        self.assertIn("battle.net", self.pure.lower())

    def test_ownership_is_never_cached_across_commands(self):
        # A `.bnetaccount link`/`unlink` runs live: a mapping kept past one command would keep a moved game account's bots
        # visible (and, from R2, orderable) to the old Battle.net account until a restart. No static or namespace-scope
        # container may hold account ids; the scope lives on the stack of one Dispatch.
        code = strip_comments(self.cpp)
        self.assertNotRegex(code, r"(?m)^\s*(static\s+)?std::unordered_map<std::uint32_t,\s*std::uint32_t>\s+s_")
        self.assertNotIn("s_bnetOf", code)
        self.assertNotRegex(code, r"(?:Login)Database\s*(?:\.|->)")   # no raw module query on the login pool
        self.assertRegex(code, r"mutable std::unordered_map<std::uint32_t, std::uint32_t> memo;")   # the memo is per OwnerScope (one line)
        self.assertNotRegex(code, r"(?m)^(static\s+)?std::(unordered_)?(map|set)<[^;]*>\s+s_\w+\s*;")

    def test_facts_come_from_the_core_objects(self):
        body = function_body(self.cpp, r"BotFacts FactsOf\(Player\* requester, Player\* bot, OwnerScope const& scope\)\s*\{")
        self.assertIn("BotMgr::Instance().IsBot(guid)", body)
        self.assertIn("sCharacterCache->GetCharacterAccountIdByGuid(guid)", body)
        self.assertIn("BotMgr::Instance().MasterOf(guid)", body)
        self.assertIn("cmd::IsCommandedBy(requester, bot)", body)
        self.assertIn("group->GetLeaderGUID()", body)
        self.assertIn("BotMgr::Instance().IsBot(leader)", body)
        self.assertIn("run::BotInRunningRun(bot)", body)

    def test_required_cases_are_named(self):
        # The spec's R1 case list, each name on the block that checks it (the unit test runs them all).
        for case in (
            "owner_same_account_follow_allowed", "stranger_follow_refused_not_yours", "master_logout_refused",
            "leader_real_player_follow_allowed", "leader_bot_not_counted", "gm_any_verb_allowed", "zero_account_never_owner",
            "in_run_refused_even_for_gm", "unknown_verb_is_owner_tier", "claims_bot_not_botany", "claims_case_insensitive",
            "may_login_owned_cap", "may_login_owned_other_account_refused", "may_login_owned_gm_any_account",
            "cooldown_left_and_gm_exempt",
        ):
            self.assertIn(f"case: {case}", self.unit, f"missing case {case}")

    def test_the_pure_rule_is_core_free(self):
        for line in self.pure.splitlines():
            if line.strip().startswith("#include"):
                self.assertNotIn('"', line, f"TbPlayerAuthPure.h includes a core header: {line.strip()}")
        for name in ("TierOf(", "Decide(", "CodeOf(", "ClaimsCommand(", "MayLoginOwned(", "SummonCooldownLeft(", "EncDecode("):
            self.assertIn(name, self.pure)
            self.assertIn(name.rstrip("("), self.unit, f"the unit test does not cover {name}")

    def test_config_keys_match_their_fallbacks(self):
        self.assertIn("TortoiseBots.Player.Commands = 1", self.conf)
        self.assertIn("TortoiseBots.Player.MaxBots = 4", self.conf)
        self.assertIn('GetBoolDefault("TortoiseBots.Player.Commands", true)', self.cpp)
        self.assertIn('GetIntDefault("TortoiseBots.Player.MaxBots", 4)', self.cpp)

    def test_the_off_build_still_links(self):
        # TbScripts.cpp calls both in every build; the engine-off half answers without naming the engine.
        off = self.cpp.split("#else", 1)[1]
        self.assertIn("void LoadConfig()", off)
        self.assertIn("void AddScripts()", off)
        self.assertIn("bool Dispatch(Player*, std::string_view) { return false; }", off)


    # ---- R2: add, logout, follow, stay, attack, flee, owned login, battlenet id ----

    def test_login_owned_skips_account_rule_only_for_owner(self):
        mgr = read(SRC / "TbBotMgr.cpp")
        login = function_body(mgr, r"std::string BotMgr::Login\(ObjectGuid characterGuid, ObjectGuid master, bool free, bool pool, bool owned, bool engine\)")
        self.assertIn("if (AccountHasBot(accountId))", login)
        # The owner (the master) must be in the world now, with a Battle.net account; fail closed otherwise.
        self.assertIn("ObjectAccessor::FindPlayer(master)", login)
        self.assertIn("!owner->IsInWorld()", login)
        self.assertIn('return "the owner is not in the world";', login)
        self.assertIn("if (ownerBnet == 0)", login)
        # Sharing: the character's game account is the owner's Battle.net account's, and every bot already on it is an owned
        # bot of the same owner (a pool bot keeps the one-bot rule).
        self.assertIn("Battlenet::AccountMgr::GetIdByGameAccount(accountId) == ownerBnet", login)
        self.assertIn("!other.owned || other.ownerBnet != ownerBnet", login)
        self.assertNotIn("sWorld->FindSession(accountId)", login)
        self.assertIn("bot.owned = owned;", login)
        self.assertIn("bot.ownerBnet = ownerBnet;", login)
        self.assertIn("bot.useEngine = _engineByDefault || free || owned || engine;", login)
        hdr = read(SRC / "TbBotMgr.h")
        self.assertIn("bool owned = false", hdr)
        self.assertIn("uint32 ownerBnet = 0;", hdr)

    def test_owned_cap_counts_the_battlenet_owner(self):
        # Codex R2: the cap keyed on the master guid was bypassed by a relog or a second linked game account.
        mgr = read(SRC / "TbBotMgr.cpp")
        owned = function_body(mgr, r"uint32 BotMgr::OwnedBotsOf\(uint32 ownerBnet\) const")
        self.assertIn("bot.owned && bot.ownerBnet == ownerBnet", owned)
        self.assertNotIn("bot.master", owned)
        self.assertIn("if (ownerBnet == 0)", owned)
        self.assertIn("uint32 OwnedBotsOf(uint32 ownerBnet) const", read(SRC / "TbBotMgr.h"))
        cmd = read(PLAYER / "TbPlayerCmd.cpp")
        self.assertIn("OwnedBotsOf(player->GetSession() ? player->GetSession()->GetBattlenetAccountId() : 0)", cmd)
        self.assertNotIn("OwnedBotsOf(player->GetGUID())", cmd)

    def test_owned_bots_leave_with_their_owner(self):
        # D-3 amended: owned bots stay only while the owner is in the world: no master grace for them.
        mgr = read(SRC / "TbBotMgr.cpp")
        logout = function_body(mgr, r"void BotMgr::OnPlayerLogout\(Player\* player\)")
        self.assertIn("bot.session->LogoutRequested() || bot.owned", logout)
        self.assertIn('RequestLogout("its owner left the world")', logout)
        self.assertLess(logout.index("|| bot.owned"), logout.index("DetachFromMaster(bot);"))

    def test_bot_session_uses_bnet_id(self):
        session = read(SRC / "TbBotSession.cpp")
        self.assertIn('#include "BattlenetAccountMgr.h"', session)
        self.assertIn("Battlenet::AccountMgr::GetIdByGameAccount(accountId)", session)
        self.assertNotIn("WorldSession(accountId, std::move(accountName), accountId,", session)

    def test_flee_is_a_verb(self):
        orders = read(SRC / "host" / "TbOrders.cpp")
        # PR #194 (tactical orders M1) moved the tactical verbs' execution out of TbOrders.cpp into TbTactics.cpp;
        # TbOrders still lists `flee` among the verbs and refuses an argument to it.
        tactics = read(SRC / "host" / "TbTactics.cpp")
        self.assertIn('"flee"', orders)
        self.assertIn("flee chat shortcut", tactics)
        self.assertIn('verb == "flee"', tactics)
        self.assertIn('act("flee chat shortcut"', tactics)
        self.assertIn("fail:flee", tactics)

    def test_add_handler_present(self):
        cmd = read(PLAYER / "TbPlayerCmd.cpp")
        self.assertIn("HandleAdd", cmd)
        self.assertIn("MayLoginOwned", cmd)
        self.assertIn("Login(guid, player->GetGUID(), false, false, true)", cmd)
        self.assertIn("not-found", cmd)
        self.assertIn("already-online", cmd)
        self.assertIn("not-leader", cmd)
        self.assertIn("too-many", cmd)
        self.assertIn("queued for login", cmd)

    def test_logout_handler_present(self):
        cmd = read(PLAYER / "TbPlayerCmd.cpp")
        self.assertIn("HandleLogout", cmd)
        self.assertIn("Tier::Owner", cmd)
        self.assertIn("Logout(", cmd)
        self.assertIn("logged out by its owner", cmd)

    def test_order_handlers_present(self):
        cmd = read(PLAYER / "TbPlayerCmd.cpp")
        self.assertIn("HandleOrderVerb", cmd)
        self.assertIn("QueueOrder", cmd)
        self.assertIn("no-selection", cmd)
        self.assertIn("GetSelectedUnit", cmd)
        self.assertIn("Tier::Tactical", cmd)

    def test_help_lists_new_verbs(self):
        cmd = read(PLAYER / "TbPlayerCmd.cpp")
        help_body = function_body(cmd, r"bool HandleHelp\(Player\* player, Caller const& caller\)")
        for verb in ("add", "logout", "follow", "stay", "attack", "flee", "maintenance", "summon", "stats"):
            self.assertIn(verb, help_body)

    def test_maintenance_handler_present(self):
        cmd = read(PLAYER / "TbPlayerCmd.cpp")
        upkeep = read(PLAYER / "TbUpkeep.cpp")
        pure = read(PLAYER / "TbUpkeepPure.h")
        self.assertIn('if (verb == "maintenance")', cmd)
        self.assertIn("HandleMaintenance(player, caller, scope, AfterFirstWord(rest))", cmd)
        self.assertIn("bool HandleMaintenance(Player* player, Caller const& caller", cmd)
        self.assertIn('TierOf("maintenance")', cmd)
        self.assertIn("IsPoolBot", cmd)
        self.assertIn("upkeep::Run(bot, summary)", cmd)
        self.assertIn("ownedBotMaintenanceEnabled", upkeep)
        self.assertIn("DurabilityRepairAll", upkeep)
        self.assertIn("SaveToDB", upkeep)
        self.assertIn("InitAmmo", upkeep)
        self.assertIn("pool", pure)
        self.assertIn("disabled", pure)
    # ---- R3: summon, stats, the run refusal on every verb, the cooldown ----

    SUMMON = r"bool HandleSummon\(Player\* player, Caller const& caller, OwnerScope const& scope, std::string_view tail\)\s*\{"
    STATS = r"bool HandleStats\(Player\* player, Caller const& caller, OwnerScope const& scope, std::string_view tail\)\s*\{"

    def test_summon_uses_shared_teleport(self):
        # THE R3 failing case: one teleport path. `.bot summon` must not call Player::TeleportTo itself; it goes through
        # h4::TeleportBotNextTo, the function `.tb tele <character> to <character>` already uses (D-8: every map, instance and
        # combat rule is that function's, never a second copy).
        cmd = read(PLAYER / "TbPlayerCmd.cpp")
        summon = function_body(cmd, self.SUMMON)
        self.assertIn("h4::TeleportBotNextTo(", summon)
        self.assertNotIn("TeleportTo(", strip_comments(cmd))          # no second teleport path anywhere in the entry
        # The shared function is exported: declared in TbH4.h, defined in namespace h4, and `.tb tele` calls it unchanged.
        hdr = read(SRC / "host" / "TbH4.h")
        self.assertIn("class Rec;", hdr)
        self.assertIn("bool TeleportBotNextTo(Player* source, Player* dest, bool revive, h3::Rec& rec);", hdr)
        h4 = read(SRC / "host" / "TbH4.cpp")
        self.assertIn("bool TeleportBotNextTo(Player* source, Player* dest, bool revive, Rec& rec)", h4)
        self.assertIn("TeleportBotNextTo(source, dest, revive, rec)", h4)   # `.tb tele` form 1
        self.assertNotIn("bool TeleportNextTo(", h4)                        # one definition, no leftover anonymous copy

    def test_cooldown_stamped_only_on_start(self):
        # D-8: the cooldown is checked before the teleport and stamped only after the teleport STARTED (a refused summon
        # stamps nothing, so the next try is not locked out); the stamp is per bot guid, in memory.
        cmd = read(PLAYER / "TbPlayerCmd.cpp")
        summon = function_body(cmd, self.SUMMON)
        self.assertIn("SummonCooldownLeft(", summon)
        self.assertIn("s_summonCooldown", summon)                            # the legacy key's value, read at config load
        tele = summon.index("h4::TeleportBotNextTo(")
        self.assertLess(summon.index("SummonCooldownLeft("), tele)           # the check precedes the action
        refused = summon.index("if (!started)")
        stamp = summon.index("SummonStamps()[")
        self.assertLess(refused, stamp)                                     # the refusal branch comes first ...
        self.assertIn("Refuse(", summon[refused:stamp])
        self.assertNotIn("SummonStamps()[", summon[refused:stamp])           # ... and stamps nothing
        self.assertIn("GetCounter()", summon)                                # keyed on the bot's guid, the file's own accessor

    def test_summon_owner_tier_master_rule_and_flight_refusal(self):
        cmd = read(PLAYER / "TbPlayerCmd.cpp")
        summon = function_body(cmd, self.SUMMON)
        self.assertIn("Decide(caller, facts, Tier::Owner)", summon)          # the run refusal is Decide's, for GMs too
        self.assertIn('"not-master"', summon)                               # D-8: only the master, unless GM
        self.assertIn('"on-cooldown"', summon)
        self.assertIn("IsInFlight()", summon)                               # D-8: never from a taxi
        self.assertIn('"in-flight"', summon)
        self.assertLess(summon.index("Decide(caller, facts, Tier::Owner)"), summon.index("SummonCooldownLeft("))
        self.assertIn("h4::TeleportBotNextTo(bot, player, false, rec)", summon)   # no revive for players

    def test_summon_clock_is_64_bit_monotonic(self):
        # Review R3: getMSTime() is a 32-bit millisecond count that wraps after 49.7 days of uptime. A stamp taken just before the
        # wrap reads as "the clock went backwards", which the pure rule treats as just stamped: that bot could not be summoned again
        # until a restart. The handler's clock is a 64-bit monotonic one that never wraps and never goes back.
        cmd = read(PLAYER / "TbPlayerCmd.cpp")
        code = strip_comments(cmd)
        self.assertNotIn("getMSTime", code)
        summon = function_body(cmd, self.SUMMON)
        self.assertIn("steady_clock", summon)
        self.assertIn("std::uint64_t const nowMs", summon)
        self.assertIn("#include <chrono>", cmd)

    def test_shared_teleport_refuses_a_bot_on_a_taxi(self):
        # Review R3 (Codex + cold review): Player::TeleportTo does not end a taxi flight, so a bot in flight would keep its flight
        # state after the teleport and the cooldown would be spent. The shared rule refuses either side in flight, before the
        # teleport, so `.tb tele` and `.bot summon` both get it (D-8: one copy).
        h4 = read(SRC / "host" / "TbH4.cpp")
        body = function_body(h4, r"bool TeleportBotNextTo\(Player\* source, Player\* dest, bool revive, Rec& rec\)\s*\{")
        self.assertIn("source->IsInFlight()", body)
        self.assertIn("dest->IsInFlight()", body)
        self.assertIn('"in-flight"', body)
        self.assertLess(body.index('"in-flight"'), body.index("TeleportTo("))

    def test_stats_is_host_built(self):
        # D-9: one line built from the Player objects on the world thread, in the command's own channel; not the engine's
        # `stats` whisper (that needs an engine tick and answers on another channel).
        cmd = read(PLAYER / "TbPlayerCmd.cpp")
        stats = function_body(cmd, self.STATS)
        for needle in ('"TortoiseBots: stats "', "GetLevel()", "ClassName(", "GetHealthPct()", "GetPowerPct(POWER_MANA)",
                       "GetMoney()", "GetZoneId()", "sAreaTableStore", "IsAlive()"):
            self.assertIn(needle, stats)
        self.assertNotIn("QueueOrder", stats)
        self.assertNotIn("stats chat", stats)
        # Level, class, health %, mana %, money in g/s/c, zone id and name, alive/dead, movement strategy (D-9).
        for token in ('"% hp"', '"% mana"', '"g "', '"s "', '"c"', '"zone "', '"alive"', '"dead"'):
            self.assertIn(token, stats)
        self.assertIn("MoveWord(", stats)
        self.assertIn("Decide(caller, facts, Tier::Tactical)", stats)

    def test_summon_cooldown_key_is_the_legacy_one(self):
        # D-13: AiPlayerbot.OwnedBotSummonCooldown is the wired legacy key, read once at config load with the legacy read's own
        # default (300) and cap (3600); the other OwnedBotSummon* keys stay unread.
        cmd = read(PLAYER / "TbPlayerCmd.cpp")
        cfg = function_body(cmd, r"void LoadConfig\(\)\s*\{")
        self.assertIn('GetIntDefault("AiPlayerbot.OwnedBotSummonCooldown", 300)', cfg)
        self.assertIn("3600", cfg)
        self.assertNotIn("OwnedBotSummonCooldown", function_body(cmd, self.SUMMON))   # the handler reads the value, not the key
        for key in ("OwnedBotSummonAllowInCombat", "OwnedBotSummonAllowMasterDead", "OwnedBotSummonAllowBotDead",
                    "OwnedBotSummonRevive", "OwnedBotSummonRepair", "OwnedBotSummonWhenGroup"):
            self.assertNotIn(key, cmd)
        self.assertIn("AiPlayerbot.OwnedBotSummonCooldown", self.conf)
        self.assertIn("not read", self.conf)

    def test_docs_list_the_r3_verbs(self):
        doc = read(ROOT / "forever" / "docs" / "TB-COMMANDS.md")
        self.assertIn("## 1a. `.bot` (players)", doc)
        section = doc.split("## 1a. `.bot` (players)", 1)[1].split("\n## ", 1)[0]
        for needle in (".bot summon", ".bot stats", "on-cooldown", "not-master", "in-flight", "OwnedBotSummonCooldown"):
            self.assertIn(needle, section)


MUTANTS = [
    ("gm-branch-dropped",
     "    if (caller.gm)\n        return Verdict::Allowed;\n", ""),
    ("account-zero-allowed",
     "caller.account != 0 && bot.account != 0 && caller.account == bot.account", "caller.account == bot.account"),
    ("logout-tactical",
     '    if (word == "add" || word == "logout" || word == "remove" || word == "summon")\n        return Tier::Owner;',
     '    if (word == "logout")\n        return Tier::Tactical;\n    if (word == "add" || word == "remove" || word == "summon")\n        return Tier::Owner;'),
    ("leader-bot-counted",
     " && !bot.leaderIsBot", ""),
    ("claim-prefix",
     "if (word.size() > 3 && !std::isspace(static_cast<unsigned char>(word[3])))", "if (false)"),
    ("in-run-after-gm",
     "    if (bot.inRun)\n        return Verdict::InRun;", "    if (bot.inRun && !caller.gm)\n        return Verdict::InRun;"),
    ("login-cap-dropped",
     "if (ownedOnline >= maxBots)", "if (false)"),
    ("cooldown-gm-not-exempt",
     "if (gm || cooldownS == 0 || lastMs == 0)", "if (cooldownS == 0 || lastMs == 0)"),
]


def run_unit(header_text):
    """Compile the real unit test against `header_text`; True when every check passes."""
    with tempfile.TemporaryDirectory() as tmp:
        holder = Path(tmp) / "host" / "player"
        holder.mkdir(parents=True)
        (holder / "TbPlayerAuthPure.h").write_text(header_text)
        exe = Path(tmp) / "test_player_auth"
        built = subprocess.run(["g++", "-std=c++17", "-O0", "-I", tmp, str(TEST), "-o", str(exe)], capture_output=True, text=True)
        if built.returncode != 0:
            return False, built.stderr
        ran = subprocess.run([str(exe)], capture_output=True, text=True)
        return ran.returncode == 0, ran.stdout


if __name__ == "__main__":
    if os.environ.get("PLAYER_CMD_MUTATE") == "1":
        header = read(PLAYER / "TbPlayerAuthPure.h")
        ok, out = run_unit(header)
        if not ok:
            print(f"ERROR: the unmutated header fails its own test:\n{out}", file=sys.stderr)
            sys.exit(1)
        survivors = []
        for name, original, mutant in MUTANTS:
            if header.count(original) != 1:
                print(f"ERROR: {name}: anchor found {header.count(original)} times, expected 1", file=sys.stderr)
                sys.exit(1)
            passed, _ = run_unit(header.replace(original, mutant, 1))
            if passed:
                survivors.append(name)
            else:
                print(f"MUTATION CAUGHT: {name}")
        if survivors:
            print(f"ERROR: mutations survived: {survivors}", file=sys.stderr)
            sys.exit(1)
        sys.exit(0)
    unittest.main()

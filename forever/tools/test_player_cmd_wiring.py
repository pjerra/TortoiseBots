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
        scope = function_body(self.cpp, r"OwnerScope ScopeOf\(Player\* player\)\s*\{")
        self.assertIn("GetBattlenetAccountId()", scope)
        self.assertIn('"SELECT id FROM account WHERE battlenet_account = {}", scope.bnet', scope)
        # A live unlink of the caller's own game account leaves the session's old Battle.net id: the scope holds only while
        # the caller's game account is still in the set read now.
        self.assertIn("scope.gameAccounts.count(session->GetAccountId())", scope)
        self.assertLess(scope.index("PQuery"), scope.index("scope.gameAccounts.count(session->GetAccountId())"))
        self.assertIn("caller.account = scope.bnet", dispatch)
        facts = function_body(self.cpp, r"BotFacts FactsOf\(Player\* requester, Player\* bot, OwnerScope const& scope\)\s*\{")
        self.assertIn("sCharacterCache->GetCharacterAccountIdByGuid(guid)", facts)
        self.assertIn("scope.gameAccounts.count(gameAccount)", facts)
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
        self.assertNotIn("GetIdByGameAccount", code)
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
        for name in ("TierOf(", "Decide(", "CodeOf(", "ClaimsCommand(", "MayLoginOwned(", "SummonCooldownLeft("):
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

#!/usr/bin/env python3
"""Wiring test for the join-by-whisper guild request (TBF #30, round 1).
No build tree, no network, no core.

Run:
  python3 -m unittest forever/tools/test_guild_request_wiring.py
  python3 forever/tools/test_guild_request_wiring.py

The mutation list runs in the same suite (test_mutations_fail): each injected defect must be caught.

What it proves:
  * the hook sits in TbChatIn.cpp Resolve's whisper branch, before Consider, so a handled request never reaches the
    engine inbox (also for an owner), and it is compiled inside the slice guard;
  * the hook admits any real player that is not a bot (no owner gate: a stranger must reach it);
  * HandleJoinRequest is gated by TortoiseBots.World.GuildAcceptPlayers AND TortoiseBots.World.Guilds, decides with
    the pure policy and sends the invite through the existing InviteByName seam, answering in one whisper;
  * the bot-run guild test is IsPoolCharacter in TbHostGuild.h/.cpp;
  * the feature is opt-in: the config template and the code fallback are both off (0/false), and the kFeatures row
    carries the same default;
  * the new files make no direct database query;
  * the pure policy test compiles and passes.
"""
import os
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))


def read(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8", errors="replace") as f:
        return f.read()


def code_only(text):
    return re.sub(r"//[^\n]*", "", text)


CHAT_IN = read("forever/src/host/chat/TbChatIn.cpp")
BOTMGR = read("forever/src/TbBotMgr.cpp")
BOTMGR_H = read("forever/src/TbBotMgr.h")
JOIN = read("forever/src/host/TbHostGuildJoin.cpp")
JOIN_H = read("forever/src/host/TbHostGuildJoin.h")
HOST_GUILD = read("forever/src/host/TbHostGuild.cpp")
HOST_GUILD_H = read("forever/src/host/TbHostGuild.h")
REQUEST_PURE_H = read("forever/src/host/TbHostGuildRequestPure.h")
INVITE_PURE_H = read("forever/src/host/TbHostGuildInvitePure.h")
ADDON_INFO = read("forever/src/host/TbAddonInfo.cpp")
CONF = read("forever/conf/tortoisebots.conf.dist")
DOCS = read("forever/docs/TB-COMMANDS.md")

HOOK = ("if (line.kind == KindWhisper && line.lang != LangAddon && line.lang != LangAddonLogged\n"
        "            && RealPlayer(sender) && !BotMgr::Instance().IsBot(sender->GetGUID())\n"
        "            && tbhost::guildjoin::HandleJoinRequest(receiver, sender, line.text))")


def resolve_body(text):
    body = text.split("void Resolve(Arrival const& line)", 1)
    if len(body) != 2:
        raise AssertionError("TbChatIn.cpp lost Resolve")
    return body[1].split("\n// The PlayerScript the core's chat handlers call.", 1)[0]


def handle_body(text):
    body = code_only(text).split("bool HandleJoinRequest(Player* bot, Player* asker, std::string const& text)", 1)
    if len(body) != 2:
        raise AssertionError("TbHostGuildJoin.cpp lost HandleJoinRequest")
    return body[1].split("\n} // namespace guildjoin", 1)[0]


def pick_inviter_body(text):
    body = code_only(text).split("Player* PickInviter(Player* bot, uint64_t guildId, Guild* guild)", 1)
    if len(body) != 2:
        raise AssertionError("TbHostGuildJoin.cpp lost PickInviter")
    return body[1].split("\n// The one whisper a request is answered with", 1)[0]


def invite_failure_body(text):
    body = code_only(text).split("if (!tbhost::guild::InviteSucceeded(outcome))", 1)
    if len(body) != 2:
        raise AssertionError("HandleJoinRequest must branch on the invite outcome")
    return body[1].split("Tell(bot, asker, RequestAnswer(RequestVerdict::Invite", 1)[0]


def check(chat_in, botmgr, botmgr_h, join, join_h, host_guild, host_guild_h, request_pure_h, invite_pure_h, addon_info,
          conf, docs):
    code_join = code_only(join)
    code_join_h = code_only(join_h)
    code_host_guild = code_only(host_guild)
    code_host_guild_h = code_only(host_guild_h)

    # ---- the hook: in the whisper branch, before Consider, inside the slice guard ----
    body = resolve_body(chat_in)
    code_body = code_only(body)
    if HOOK not in code_body:
        raise AssertionError("TbChatIn.cpp Resolve must offer a real player's whisper to HandleJoinRequest")
    if code_body.index("HandleJoinRequest") > code_body.index("Consider(receiver, sender, line.kind, line.lang, line.text, false);"):
        raise AssertionError("the request hook must run before Consider, so its lines never join the inbox")
    if "BotMgr::Instance().IsBot(receiver->GetGUID())" not in code_body[:code_body.index("HandleJoinRequest")]:
        raise AssertionError("the hook must sit after the receiver-is-bot check")
    if "#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1\n        // TBF #30" not in body:
        raise AssertionError("the hook must be compiled inside the TORTOISEBOTS_SLICE1 guard")
    if "HandleJoinRequest(receiver, sender, line.text))\n            return;\n#endif" not in body:
        raise AssertionError("the hook's slice guard must close right after it")
    if '#include "host/TbHostGuildJoin.h"' not in code_only(chat_in):
        raise AssertionError("TbChatIn.cpp must include the request driver inside the slice guard")
    if "MasterOf" in handle_body(join):
        raise AssertionError("the request path must not be gated by the owner: a stranger's whisper is the point")

    # ---- the pure policy is used, and the driver fills its facts ----
    if "IsJoinRequest(text)" not in code_join:
        raise AssertionError("HandleJoinRequest must recognise the phrase with IsJoinRequest")
    if "DecideRequest(" not in code_join:
        raise AssertionError("HandleJoinRequest must decide with DecideRequest")
    if "RequestTimes().Admit(askerKey, nowMs)" not in code_join:
        raise AssertionError("HandleJoinRequest must rate limit through the pure RequestCooldowns")
    if "class RequestCooldowns" not in request_pure_h or "kRequestPruneMs" not in request_pure_h:
        raise AssertionError("the pure cooldown store must prune the per-asker map")
    if "RequestCode(" not in code_join:
        raise AssertionError("the log line must use RequestCode")
    if "tbhost::guild::InviteByName(" not in code_join:
        raise AssertionError("the invite must go through the InviteByName seam")

    # ---- the inviter: a pool bot, never a player's owned alt, by the pure lowest-rank pick ----
    inviter = pick_inviter_body(join)
    if "mgr.IsFreeBot(guid)" not in inviter:
        raise AssertionError("PickInviter must take a pool bot still playing free")
    if re.search(r"IsBot\(guid\)", inviter):
        raise AssertionError("PickInviter must not use BotMgr::IsBot: that also matches a player's owned alt")
    if "PickInviterIndex(candidates)" not in inviter:
        raise AssertionError("PickInviter must choose with the pure PickInviterIndex")

    # ---- a refused invite is answered for its reason, never as a missing officer ----
    refusal = invite_failure_body(join)
    if "InviteRefusalAnswer(outcome" not in refusal:
        raise AssertionError("an invite refusal must be answered with InviteRefusalAnswer")
    if "InviteOutcomeCode(outcome)" not in refusal:
        raise AssertionError("the refusal log must carry the seam's outcome code")
    if "NoOfficer" in refusal:
        raise AssertionError("an invite refusal must not be answered as 'no officer online'")
    if "I could not invite you right now." not in request_pure_h:
        raise AssertionError("the generic invite-refusal answer is missing")
    if "tbhost::guild::IsPoolCharacter(" not in code_join:
        raise AssertionError("the bot-run test must be IsPoolCharacter")
    if "IsPoolCharacter(" not in code_host_guild_h or "bool IsPoolCharacter(ObjectGuid const&" not in code_host_guild_h:
        raise AssertionError("TbHostGuild.h must declare IsPoolCharacter(ObjectGuid const&)")
    for pin in ("TortoiseBots::pool::AccountRegistered(accountId)", "TortoiseBots::world::IsPoolAccountName(accountName)",
                "sCharacterCache->GetCharacterAccountIdByGuid(guid)"):
        if pin not in code_host_guild:
            raise AssertionError("IsPoolCharacter must test the pool account (%s)" % pin)
    if "HandleJoinRequest(Player* bot, Player* asker, std::string const& text)" not in code_join_h:
        raise AssertionError("TbHostGuildJoin.h must declare HandleJoinRequest")
    # The one answer: every verdict but Silent tells the asker something.
    for text in ("Sent you an invite to", "I asked", "You are already in a guild.", "Our guild is full.",
                 "We only take", "is online, try later.", "I am not in a guild.", "does not take members this way."):
        if text not in join:
            raise AssertionError("the request answer is missing %r" % text)
    if "[W9] guild-request bot=" not in join or "result=" not in join:
        raise AssertionError("the request must log the [W9] guild-request line")

    # ---- the gates: the key, the guild key, the code fallback and the template ----
    hook_body = handle_body(join)
    if "GuildAcceptPlayersEnabled()" not in hook_body:
        raise AssertionError("HandleJoinRequest must be gated by TortoiseBots.World.GuildAcceptPlayers")
    if "GuildsEnabled()" not in hook_body:
        raise AssertionError("HandleJoinRequest must be gated by TortoiseBots.World.Guilds")
    if 'GetBoolDefault("TortoiseBots.World.GuildAcceptPlayers", false)' not in code_only(botmgr):
        raise AssertionError("BotMgr::LoadConfig must read TortoiseBots.World.GuildAcceptPlayers, defaulting to off (opt-in)")
    if "bool GuildAcceptPlayersEnabled() const { return _guildAcceptPlayers; }" not in code_only(botmgr_h):
        raise AssertionError("BotMgr must expose GuildAcceptPlayersEnabled()")
    if "bool _guildAcceptPlayers" not in code_only(botmgr_h):
        raise AssertionError("BotMgr must hold _guildAcceptPlayers")
    if re.search(r"^TortoiseBots\.World\.GuildAcceptPlayers = 0$", conf, re.M) is None:
        raise AssertionError("the template must ship TortoiseBots.World.GuildAcceptPlayers = 0 (opt-in, the code fallback)")
    if re.search(r"^TortoiseBots\.World\.Guilds = 1$", conf, re.M) is None:
        raise AssertionError("the template must keep TortoiseBots.World.Guilds = 1")
    if '{ "guild-accept-players", "TortoiseBots.World.GuildAcceptPlayers", true, 0, "Join bot guilds by whisper" }' not in addon_info:
        raise AssertionError("TbAddonInfo.cpp kFeatures must carry the guild-accept-players row, defaulting to off")

    # ---- the docs carry the new bullet ----
    if "GuildAcceptPlayers" not in docs or "ginv" not in docs:
        raise AssertionError("forever/docs/TB-COMMANDS.md must document the whisper request")

    # ---- no direct database query in the new code ----
    for name, text in (("TbHostGuildRequestPure.h", request_pure_h), ("TbHostGuildInvitePure.h", invite_pure_h),
                       ("TbHostGuildJoin.cpp", join)):
        for forbidden in ("Database", "Query(", "LoginDatabase", "CharacterDatabase", "WorldDatabase"):
            if forbidden in text:
                raise AssertionError("%s must make no direct database query (%s)" % (name, forbidden))
    # Core-free: only the sibling pure header may be included by path.
    for name, text in (("TbHostGuildRequestPure.h", request_pure_h), ("TbHostGuildInvitePure.h", invite_pure_h)):
        for inc in re.findall(r'#include\s+([<"])([^>"]+)', text):
            if inc[0] == '"' and not inc[1].endswith("Pure.h"):
                raise AssertionError("%s must stay core-free (it includes %s)" % (name, inc[1]))


class GuildRequestWiring(unittest.TestCase):
    def test_wiring_intact(self):
        check(CHAT_IN, BOTMGR, BOTMGR_H, JOIN, JOIN_H, HOST_GUILD, HOST_GUILD_H, REQUEST_PURE_H, INVITE_PURE_H,
              ADDON_INFO, CONF, DOCS)

    def test_the_policy_test_passes(self):
        gxx = shutil.which("g++")
        if not gxx:
            self.skipTest("no g++")
        with tempfile.TemporaryDirectory(prefix="guild-request-") as tmp:
            exe = os.path.join(tmp, "test_guild_request_pure")
            built = subprocess.run([gxx, "-std=c++17", "-O1", "-Wall", "-Wextra", "-I", ROOT, "-I",
                                    os.path.join(ROOT, "forever", "src"),
                                    os.path.join(ROOT, "forever", "tools", "test_guild_request_pure.cpp"),
                                    "-o", exe], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            out = subprocess.run([exe], capture_output=True, text=True)
        self.assertEqual(out.returncode, 0, out.stdout + out.stderr)

    def test_mutations_fail(self):
        base = [CHAT_IN, BOTMGR, BOTMGR_H, JOIN, JOIN_H, HOST_GUILD, HOST_GUILD_H, REQUEST_PURE_H, INVITE_PURE_H,
                ADDON_INFO, CONF, DOCS]
        mutations = [
            # no hook at all: a stranger's request never reaches the feature
            (0, CHAT_IN.replace(HOOK, "false")),
            # the hook after Consider (the request joins the inbox and the engine answers it)
            (0, CHAT_IN.replace(HOOK + "\n            return;\n", "").replace(
                "        Consider(receiver, sender, line.kind, line.lang, line.text, false);\n",
                "        Consider(receiver, sender, line.kind, line.lang, line.text, false);\n" + HOOK + "\n        return;\n")),
            # the hook outside the slice guard (the turtle build would need the host file)
            (0, CHAT_IN.replace("#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1\n        // TBF #30", "// TBF #30")
                  .replace("HandleJoinRequest(receiver, sender, line.text))\n            return;\n#endif",
                           "HandleJoinRequest(receiver, sender, line.text))\n            return;")),
            # the config key not read
            (1, BOTMGR.replace('GetBoolDefault("TortoiseBots.World.GuildAcceptPlayers", false)',
                               'GetBoolDefault("TortoiseBots.World.GuildAcceptPlayersX", false)')),
            # the code fallback turned on, so an absent line enables the feature
            (1, BOTMGR.replace('GetBoolDefault("TortoiseBots.World.GuildAcceptPlayers", false)',
                               'GetBoolDefault("TortoiseBots.World.GuildAcceptPlayers", true)')),
            # the accessor gone
            (2, BOTMGR_H.replace("bool GuildAcceptPlayersEnabled() const { return _guildAcceptPlayers; }", "")),
            # the driver loses one of its gates
            (3, JOIN.replace("GuildAcceptPlayersEnabled()", "true")),
            (3, JOIN.replace("mgr.GuildsEnabled()", "true")),
            # a request that never decides
            (3, JOIN.replace("RequestVerdict verdict = DecideRequest(facts);", "RequestVerdict verdict = RequestVerdict::Invite;")),
            # the invite no longer goes through the seam
            (3, JOIN.replace("tbhost::guild::InviteByName(", "no_invite_seam(")),
            # a player's guild counts as bot-run
            (3, JOIN.replace("tbhost::guild::IsPoolCharacter(guild->GetLeaderGUID())", "true")),
            # the rate limit gone
            (3, JOIN.replace("RequestTimes().Admit(askerKey, nowMs)", "false")),
            # a player's owned alt can be the inviter again
            (3, JOIN.replace("candidate.bot = mgr.IsFreeBot(guid);", "candidate.bot = mgr.IsBot(guid);")),
            # the inviter no longer goes through the pure lowest-rank pick
            (3, JOIN.replace("PickInviterIndex(candidates)", "0")),
            # a refusal is answered as "no officer online" again
            (3, JOIN.replace("InviteRefusalAnswer(outcome, guildName, side, inviter->GetName())",
                             "RequestAnswer(RequestVerdict::NoOfficer, guildName, side, nullptr, bot)")),
            # the outcome code is dropped from the refusal log
            (3, JOIN.replace("tbhost::guild::InviteOutcomeCode(outcome)", "why")),
            # the declarations gone
            (4, JOIN_H.replace("bool HandleJoinRequest(", "bool HandleJoinRequestGone(")),
            (6, HOST_GUILD_H.replace("bool IsPoolCharacter(", "bool IsPoolCharacterGone(")),
            # the bot character test forgets the pool's own name shape
            (5, HOST_GUILD.replace("TortoiseBots::world::IsPoolAccountName(accountName)", "false")),
            # the template default out of step with the code fallback
            (10, CONF.replace("TortoiseBots.World.GuildAcceptPlayers = 0", "TortoiseBots.World.GuildAcceptPlayers = 1")),
            # the Info-window row gone
            (9, ADDON_INFO.replace('    { "guild-accept-players", "TortoiseBots.World.GuildAcceptPlayers", true, 0, "Join bot guilds by whisper" },\n', "")),
            # the docs bullet gone
            (11, DOCS.replace("GuildAcceptPlayers", "GuildX")),
            # a direct database query in the new code
            (3, JOIN.replace("#include <string>", "#include <string>\n// WorldDatabase.Query(\"SELECT 1\")")),
        ]
        for i, (slot, mutation) in enumerate(mutations):
            args = list(base)
            if mutation == args[slot]:
                raise AssertionError("mutation %d changed nothing" % i)
            args[slot] = mutation
            with self.assertRaises(AssertionError, msg="mutation %d was not caught" % i):
                check(*args)


if __name__ == "__main__":
    unittest.main()

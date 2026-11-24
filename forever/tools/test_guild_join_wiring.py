#!/usr/bin/env python3
"""Wiring test for guild participation (TBF #156): accept/stay/found, management whispers, guild chat.
No build, no network, no core.

Run:
  python3 -m unittest forever/tools/test_guild_join_wiring.py
  python3 forever/tools/test_guild_join_wiring.py

The mutation list runs in the same suite (test_mutations_fail): each injected defect must be caught.

What it proves:
  * BotMgr::Update ticks the host guild driver for free bots behind TortoiseBots.World.Guilds, after the engine step,
    and the driver's timers go with a bot that leaves the world (both logout paths);
  * the accept path calls the existing AcceptInvite seam, whose rank-on-join hook is the one caller it lacked, and a
    refused invite is declined instead of being re-read forever;
  * the stay path leaves a guild that reached the member limit and nothing else leaves a guild on its own;
  * founding is the policy + the host seams (charter price paid by the bot, signatures from nearby free bots), never a
    free charter and never a guild bank withdrawal;
  * DeliverChat answers the owner's guild management whisper through HandleWhisper before the legacy chat commands;
  * guild chat: SayAction is on the build list and the generated context carries its action and trigger; the guild
    speak right is real in TbCompat_P.h, not the "unported" stub;
  * the config template's two keys match the code fallbacks.
"""
import os
import re
import unittest

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))


def read(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8", errors="replace") as f:
        return f.read()


def code_only(text):
    return re.sub(r"//[^\n]*", "", text)


BOTMGR = read("forever/src/TbBotMgr.cpp")
BOTMGR_H = read("forever/src/TbBotMgr.h")
BRAIN = read("forever/src/host/TbEngineBrain.cpp")
CONTEXT = read("forever/src/slice/SliceAiObjectContext.cpp")
GENERATOR = read("forever/tools/slice_gen_context.py")
SLICE = read("forever/SLICE-W4.txt")
HOST_GUILD = read("forever/src/host/TbHostGuild.cpp")
HOST_GUILD_H = read("forever/src/host/TbHostGuild.h")
HOST_JOIN = read("forever/src/host/TbHostGuildJoin.cpp")
HOST_JOIN_H = read("forever/src/host/TbHostGuildJoin.h")
PURE_H = read("forever/src/host/TbHostGuildJoinPure.h")
COMPAT_P = read("forever/src/compat/TbCompat_P.h")
COMPAT_UNPORTED = read("forever/src/compat/TbCompat_Unported.h")
CONF = read("forever/conf/tortoisebots.conf.dist")


def check(bot_mgr, bot_mgr_h, brain, context, generator, slice_list, host_guild, host_guild_h, host_join,
          host_join_h, pure_h, compat_p, compat_unported, conf):
    code_mgr = code_only(bot_mgr)
    code_brain = code_only(brain)
    code_host_guild = code_only(host_guild)
    code_host_join = code_only(host_join)
    code_compat_p = code_only(compat_p)
    code_compat_unported = code_only(compat_unported)

    # ---- config: the flags and their fallbacks ----
    if 'GetBoolDefault("TortoiseBots.World.Guilds", true)' not in code_mgr:
        raise AssertionError("BotMgr::LoadConfig must read TortoiseBots.World.Guilds, defaulting to on")
    if 'GetBoolDefault("TortoiseBots.World.GuildForming", false)' not in code_mgr:
        raise AssertionError("BotMgr::LoadConfig must read TortoiseBots.World.GuildForming, defaulting to off")
    if "bool GuildsEnabled() const { return _guildsEnabled; }" not in code_only(bot_mgr_h):
        raise AssertionError("BotMgr must expose GuildsEnabled()")
    if "bool GuildFormingEnabled() const { return _guildFormingEnabled; }" not in code_only(bot_mgr_h):
        raise AssertionError("BotMgr must expose GuildFormingEnabled()")
    if re.search(r"^TortoiseBots\.World\.Guilds = 1$", conf, re.M) is None:
        raise AssertionError("the template must set TortoiseBots.World.Guilds = 1 (the code fallback)")
    if re.search(r"^TortoiseBots\.World\.GuildForming = 0$", conf, re.M) is None:
        raise AssertionError("the template must set TortoiseBots.World.GuildForming = 0 (the code fallback)")

    # ---- the tick: free bots only, after the engine step ----
    if "tbhost::guildjoin::TickFreeBotGuild(me, _guildFormingEnabled);" not in code_mgr:
        raise AssertionError("BotMgr::Update must tick the guild driver")
    if "if (_guildsEnabled && bot.free)" not in code_mgr:
        raise AssertionError("the guild tick must be gated by the key and by the free flag")
    update = code_mgr.split("void BotMgr::Update(uint32 diff)", 1)
    if len(update) != 2:
        raise AssertionError("TbBotMgr.cpp lost BotMgr::Update")
    update_body = update[1].split("\nvoid ", 1)[0]
    if "TickFreeBotGuild" not in update_body:
        raise AssertionError("the guild tick must run from BotMgr::Update")
    if update_body.index("engine::Tick(bot.brain.get(), aiElapsed);") > update_body.index("TickFreeBotGuild"):
        raise AssertionError("the guild tick runs before the engine step (an accept must not race the AI's own turn)")
    if update_body.count("tbhost::guildjoin::ForgetBot(") != 1:
        raise AssertionError("the finished-session path must forget the bot's guild timers")
    logout = code_mgr.split("void BotMgr::OnPlayerLogout(Player* player)", 1)
    if len(logout) != 2 or "tbhost::guildjoin::ForgetBot(leaving);" not in logout[1].split("\nvoid ", 1)[0]:
        raise AssertionError("the logout path must forget the bot's guild timers")

    # ---- the driver: accept, stay, found ----
    if "InviteAcceptable(" not in code_host_join:
        raise AssertionError("the accept leg must decide with InviteAcceptable")
    if "guild::AcceptInvite(bot, invited, &why)" not in code_host_join:
        raise AssertionError("the accept leg must call the AcceptInvite seam")
    if "guild::DeclineInvite(" not in code_host_join:
        raise AssertionError("a refused invite must be declined, not re-read every tick")
    if "ShouldLeaveOversized(" not in code_host_join or "guild::LeaveGuild(" not in code_host_join:
        raise AssertionError("the stay leg must leave a guild that reached the member limit")
    if "FoundingEligible(" not in code_host_join or "CharterComplete(" not in code_host_join:
        raise AssertionError("the founding legs must decide with the pure policy")
    for seam in ("guild::BuyPetition(", "guild::SignPetition(", "guild::TurnInPetition(", "guild::RequiredPetitionSigns(",
                 "guild::CharterCost()"):
        if seam not in code_host_join:
            raise AssertionError("the founding legs must use " + seam)
    if "kPetitionerRangeYd" not in code_host_join or "UNIT_NPC_FLAG_PETITIONER" not in code_host_join:
        raise AssertionError("the founding legs must require a petitioner NPC in range")
    if "IsFreeBot(" not in code_host_join:
        raise AssertionError("only free bots may sign a charter")
    # The owner rule: nothing ever withdraws from the guild bank and nothing grants the charter price.
    for forbidden in ("Withdraw", "ModifyMoney(", "GiveMoney"):
        if forbidden in code_host_join or forbidden in code_host_guild:
            raise AssertionError("guild participation must not move money itself (%s)" % forbidden)
    if "TC_LOG_INFO(\"tortoisebots\", \"[W9]" not in host_join:
        raise AssertionError("the driver must log its decisions under the W9 tag")

    # ---- the rank-on-join hook now has its caller ----
    if "ApplyBotRankOnJoin(invitee, static_cast<uint32_t>(guildId));" not in code_host_guild:
        raise AssertionError("AcceptInvite must still apply the bot rank on join")
    for seam in ("bool DeclineInvite(", "bool LeaveGuild(", "bool PromoteMember(", "bool DemoteMember(",
                 "bool RemoveMember(", "bool SetLeader(", "bool SayToGuild(", "uint32_t CharterCost()"):
        if seam not in code_only(host_guild_h):
            raise AssertionError("TbHostGuild.h lacks " + seam)
    for handler in ("HandleGuildDeclineInvitation(", "HandleGuildLeave(", "HandleGuildPromoteMember(",
                    "HandleGuildDemoteMember(", "HandleGuildOfficerRemoveMember(", "HandleGuildSetGuildMaster("):
        if handler not in code_host_guild:
            raise AssertionError("TbHostGuild.cpp must reach the core's " + handler)
    if "GR_RIGHT_GCHATSPEAK" not in code_host_guild or "BroadcastToGuild(" not in code_host_guild:
        raise AssertionError("SayToGuild must gate on the rank's speak right and broadcast through the guild")

    # ---- the management whispers: owner/GM, before the legacy chat commands ----
    if "tbhost::guildjoin::HandleWhisper(brain->bot, sender, line.text)" not in code_brain:
        raise AssertionError("DeliverChat must offer the line to the guild whisper handler")
    deliver = code_brain.split("void DeliverChat(Brain* brain)", 1)
    if len(deliver) != 2:
        raise AssertionError("TbEngineBrain.cpp lost DeliverChat")
    deliver_body = deliver[1].split("\n}\n", 1)[0]
    if deliver_body.index("HandleWhisper") > deliver_body.index("brain->ai->HandleCommand(line.kind, line.text, *sender, line.lang);"):
        raise AssertionError("the guild whisper handler must run before the engine's chat commands")
    if "BotMgr::Instance().GuildsEnabled()" not in code_host_join:
        raise AssertionError("the whisper handler must be gated by the guild key")
    if "WhisperAuthorized(owner, gm)" not in code_host_join:
        raise AssertionError("only the owner or a GM may manage the bot's guild")
    for verb in ("Invite", "Join", "Leave", "Promote", "Demote", "Remove", "Leader"):
        if "Command::%s" % verb not in code_host_join:
            raise AssertionError("the whisper handler lacks " + verb)

    # ---- the pure header is core-free, the test drives it ----
    for inc in re.findall(r'#include\s+([<"])([^>"]+)', pure_h):
        if inc[0] == '"':
            raise AssertionError("TbHostGuildJoinPure.h must stay core-free (it includes %s)" % inc[1])
    pure_test = read("forever/tools/test_guild_join_pure.cpp")
    for used in ("InviteAcceptable(", "ShouldLeaveOversized(", "FoundingEligible(", "CharterComplete(", "ParseGuildWhisper(",
                 "WhisperAuthorized("):
        if used not in pure_test:
            raise AssertionError("the pure test does not exercise " + used)

    # ---- guild chat: SayAction built, registered, and the speak right real ----
    if "ai/playerbot/strategy/actions/SayAction.cpp" not in slice_list:
        raise AssertionError("SLICE-W4.txt must build SayAction.cpp (a registered key without its file links nowhere)")
    if '#include "playerbot/strategy/actions/SayAction.h"' not in context:
        raise AssertionError("the generated context must include SayAction.h")
    if 'creators["speak"] = [](PlayerbotAI* ai) { return new SpeakAction(ai); };' not in context:
        raise AssertionError("the generated context lacks the speak action (rerun slice_gen_context.py)")
    if 'creators["speak"] = [](PlayerbotAI* ai) { return new ChatCommandTrigger(ai, "speak"); };' not in context:
        raise AssertionError("the generated context lacks the speak trigger (rerun slice_gen_context.py)")
    if '"action": "speak"' not in generator or '"trigger": "speak"' not in generator:
        raise AssertionError("slice_gen_context.py GUILD_KEYS must name the speak action and trigger")
    if 'supported.push_back("speak");' not in read("ai/playerbot/strategy/generic/ChatCommandHandlerStrategy.cpp"):
        raise AssertionError("the chat strategy has no speak node (a registered key alone never fires)")
    if "GuildSayCompat" not in code_compat_p:
        raise AssertionError("TbCompat_P.h must define GuildSayCompat")
    if "HasAnyRankRight(member->GetRankId(), GR_RIGHT_GCHATSPEAK)" not in code_compat_p:
        raise AssertionError("GuildSayCompat must check the real speak right")
    if "guild->BroadcastToGuild(bot->GetSession(), false, msg, LANG_UNIVERSAL);" not in code_compat_p:
        raise AssertionError("GuildSayCompat must broadcast through the core's guild")
    if "inline bool GuildSayCompat(" in code_compat_unported:
        raise AssertionError("the unported GuildSayCompat stub must be gone (one definition only)")


class GuildJoinWiring(unittest.TestCase):
    def test_wiring_intact(self):
        check(BOTMGR, BOTMGR_H, BRAIN, CONTEXT, GENERATOR, SLICE, HOST_GUILD, HOST_GUILD_H, HOST_JOIN, HOST_JOIN_H,
              PURE_H, COMPAT_P, COMPAT_UNPORTED, CONF)

    def test_mutations_fail(self):
        base = (BOTMGR, BOTMGR_H, BRAIN, CONTEXT, GENERATOR, SLICE, HOST_GUILD, HOST_GUILD_H, HOST_JOIN, HOST_JOIN_H,
                PURE_H, COMPAT_P, COMPAT_UNPORTED, CONF)
        mutations = [
            # no tick at all: an invite is never answered
            (0, BOTMGR.replace("        if (_guildsEnabled && bot.free)\n            tbhost::guildjoin::TickFreeBotGuild(me, _guildFormingEnabled);\n", "")),
            # the config key not read
            (0, BOTMGR.replace('GetBoolDefault("TortoiseBots.World.Guilds", true)', 'GetBoolDefault("TortoiseBots.World.GuildsX", true)')),
            # the template default out of step with the code fallback
            (13, CONF.replace("TortoiseBots.World.GuildForming = 0", "TortoiseBots.World.GuildForming = 1")),
            # the whisper handler bypassed
            (2, BRAIN.replace("tbhost::guildjoin::HandleWhisper(brain->bot, sender, line.text)", "false")),
            # the accept leg loses the seam
            (8, HOST_JOIN.replace("guild::AcceptInvite(bot, invited, &why)", "false")),
            # a refused invite is never declined (re-read every tick)
            (8, HOST_JOIN.replace("tbhost::guild::DeclineInvite(bot, &why);", ";")),
            # the stay rule gone: bots squat on a full guild
            (8, HOST_JOIN.replace("ShouldLeaveOversized(guild->GetMembersCount())", "false")),
            # founding no longer pays the charter price gate
            (8, HOST_JOIN.replace("tbhost::guild::CharterCost()", "0")),
            # the rank-on-join hook loses its caller's callee
            (6, HOST_GUILD.replace("guild_order_access::ApplyBotRankOnJoin(invitee, static_cast<uint32_t>(guildId));", "")),
            # the speak action out of the build list
            (5, SLICE.replace("ai/playerbot/strategy/actions/SayAction.cpp\n", "")),
            # the speak key gone from the generated context
            (3, CONTEXT.replace('            creators["speak"] = [](PlayerbotAI* ai) { return new ChatCommandTrigger(ai, "speak"); };\n', "")),
            # the unported stub back (two definitions, or no guild chat)
            (12, COMPAT_UNPORTED.replace("// CHAT (PlayerbotAIChat.cpp)",
                                         "inline bool GuildSayCompat(Player* /*bot*/, Guild* /*guild*/, std::string const& /*msg*/) { return false; }\n// CHAT (PlayerbotAIChat.cpp)")),
            # the speak right not checked
            (11, COMPAT_P.replace("!guild->HasAnyRankRight(member->GetRankId(), GR_RIGHT_GCHATSPEAK)", "false")),
            # the logout timer cleanup gone
            (0, BOTMGR.replace("    tbhost::guildjoin::ForgetBot(leaving);\n", "")),
            # a non-owner may manage the guild
            (8, HOST_JOIN.replace("WhisperAuthorized(owner, gm)", "true")),
            # the founding legs lose the signature collection
            (8, HOST_JOIN.replace("guild::SignPetition(signer, petition->PetitionGuid, &why)", "false")),
        ]
        for i, (slot, mutation) in enumerate(mutations):
            args = list(base)
            self.assertNotEqual(mutation, args[slot], "mutation %d changed nothing" % i)
            args[slot] = mutation
            with self.assertRaises(AssertionError, msg="mutation %d was not caught" % i):
                check(*args)


if __name__ == "__main__":
    unittest.main()

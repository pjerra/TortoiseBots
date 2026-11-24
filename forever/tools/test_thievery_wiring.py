#!/usr/bin/env python3
"""Wiring pins for host thievery (TBF #153): pickpocket, bag lockboxes and
the lockbox-by-mail service.

Run:
  python3 forever/tools/test_thievery_wiring.py
  python3 forever/tools/test_thievery_wiring.py --mutate   # must FAIL
"""
import os
import re
import sys
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MUTATE = False

HOST = "forever/src/host/TbHostThievery.cpp"
HOST_H = "forever/src/host/TbHostThievery.h"
POLICY = "ai/playerbot/policy/profession/ThieveryPolicy.h"
BOT_MGR = "forever/src/TbBotMgr.cpp"
MAIL_ACTION = "ai/playerbot/strategy/actions/MailAction.cpp"
CONF = "forever/conf/tortoisebots.conf.dist"
DOCS = "docs/server/configuration.md"
ADDON_INFO = "forever/src/host/TbAddonInfo.cpp"


def read(rel):
    with open(os.path.join(ROOT, rel), "r", encoding="utf-8") as f:
        return f.read()


def host_text():
    text = read(HOST)
    if MUTATE:
        text = text.replace("HandleOpenItemOpcode", "SkipOpenItem")
    return text


def bot_mgr_text():
    text = read(BOT_MGR)
    if MUTATE:
        text = text.replace("tbhost::thievery::TickThievery(me, diff);", "")
    return text


class TestThieveryWiring(unittest.TestCase):
    def test_host_files_are_globbed(self):
        for name in ("TbHostThievery.cpp", "TbHostThievery.h"):
            self.assertTrue(os.path.exists(os.path.join(ROOT, "forever/src/host", name)), name)
        cmake = read("CMakeLists.txt")
        self.assertIn("host/TbHost*.cpp", cmake)

    def test_policy_stays_core_free(self):
        policy = read(POLICY)
        self.assertNotIn('#include "', policy, "the policy must not include core headers")
        for symbol in (
            "EvaluateLockbox",
            "EvaluatePickpocketBot",
            "EvaluatePickpocketTarget",
            "PickLockboxRogue",
            "LockboxReturnCopper",
            "MailPostageCopper",
            "IsLockboxServiceSubject",
        ):
            self.assertIn(symbol, policy, symbol)

    def test_host_drives_the_core_handlers(self):
        service = host_text()
        for symbol in (
            "HandleCastSpellOpcode",
            "HandleOpenItemOpcode",
            "HandleAutostoreLootItemOpcode",
            "HandleLootMoneyOpcode",
            "DoLootRelease",
            "tbhost::guild::SendMail",
            "IsLockboxServiceSubject",
            "CanGeneratePickPocketLoot",
            "PickPocketLootID",
            "CanHaveLoot",
            "HasStealthAura",
            "GetSkillValue(SKILL_LOCKPICKING)",
            "IsNonMeleeSpellCast",
        ):
            self.assertIn(symbol, service, f"missing {symbol} in {HOST}")
        # The spell ids come from the pure policy, not from magic numbers here.
        code = "\n".join(line.split("//", 1)[0] for line in service.splitlines())
        for magic in ("1784", "921", "1804"):
            self.assertNotRegex(code, rf"\b{magic}\b", f"{magic} must come from the policy")
        self.assertIn("TARGET_FLAG_ITEM", service)
        self.assertIn("TARGET_FLAG_UNIT", service)
        self.assertIn("CMSG_OPEN_ITEM", service)
        self.assertIn("mail->stationery == MAIL_STATIONERY_AUCTION", service)
        self.assertIn("mail->COD != 0", service)
        self.assertNotRegex(service, r"\b(CharacterDatabase|WorldDatabase)\s*[.>-]", "the service is memory only")
        self.assertNotIn("MailDraft", service)

    def test_bot_mgr_loads_ticks_and_forgets(self):
        mgr = bot_mgr_text()
        self.assertIn('#include "host/TbHostThievery.h"', mgr)
        self.assertIn("tbhost::thievery::LoadConfig();", mgr)
        self.assertIn("tbhost::thievery::TickThievery(me, diff);", mgr)
        self.assertIn("tbhost::thievery::HasActiveErrand(me)", mgr)
        self.assertGreaterEqual(mgr.count("tbhost::thievery::ForgetBot("), 2,
                                "logout and finished-session cleanup both forget the state")
        # Free bots only: the tick sits behind bot.free like the other world services.
        self.assertRegex(mgr, r"if \(bot\.free\)\s*\n\s*tbhost::thievery::TickThievery")

    def test_generic_mail_take_leaves_the_service_mail(self):
        mail_action = read(MAIL_ACTION)
        if MUTATE:
            mail_action = mail_action.replace("IsLockboxServiceMail", "NotTheService")
        self.assertIn('#include "host/TbHostThievery.h"', mail_action)
        self.assertGreaterEqual(mail_action.count("IsLockboxServiceMail(bot, mail)"), 2,
                                "the broad take and the order take both leave service mail alone")

    def test_config_and_docs_carry_the_three_keys(self):
        conf = read(CONF)
        docs = read(DOCS)
        addon = read(ADDON_INFO)
        for key in ("TortoiseBots.World.Pickpocket", "TortoiseBots.World.Lockboxes", "TortoiseBots.World.LockboxMail"):
            self.assertIn(f"{key} = 1", conf, key)
            self.assertIn(f"`{key}` | `1`", docs, key)
            self.assertIn(f'"{key}", true, 1', addon, key)
        # The code fallback equals the template value.
        host = read(HOST)
        for key in ("Pickpocket", "Lockboxes", "LockboxMail"):
            self.assertIn(f'GetBoolDefault("TortoiseBots.World.{key}", true)', host, key)

    def test_guild_rogue_list_keeps_online_and_offline_rogues(self):
        host = host_text()
        body = host.split("std::vector<LockboxRogueCandidate> GuildRogueCandidates(Player* bot)", 1)[1]
        body = body.split("\n}\n", 1)[0]
        if MUTATE:
            body = body.replace("out.push_back(candidate);", "")
        self.assertEqual(body.count("out.push_back(candidate);"), 2,
                         "an online and an offline rogue both reach the pick")

    def test_pure_test_exists_and_runs_the_gates(self):
        test = read("forever/tools/test_thievery_policy.cpp")
        for symbol in ("EvaluateLockbox", "EvaluatePickpocketBot", "EvaluatePickpocketTarget",
                       "PickLockboxRogue", "LockboxReturnCopper"):
            self.assertIn(symbol, test, symbol)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        MUTATE = True
        sys.argv.remove("--mutate")
    unittest.main()

#!/usr/bin/env python3
"""Wiring pins for the mount purchase errand (forever/src/host/TbMountShop.* and its hooks).

Run:
    python3 forever/tools/test_mount_shop_wiring.py
"""
import re
import unittest
from pathlib import Path

FOREVER = Path(__file__).resolve().parent.parent
ROOT = FOREVER.parent


def read(rel):
    return (ROOT / rel).read_text(encoding="utf-8")


class TestMountShopWiring(unittest.TestCase):
    def test_policy_has_mount_kind_and_need(self):
        policy = read("ai/playerbot/policy/world/CapitalErrandPolicy.h")
        self.assertIn("Mount = 5", policy)
        self.assertIn("bool needsMountPurchase = false;", policy)
        self.assertIn("uint64_t mountCooldownUntilMs = 0;", policy)
        self.assertIn("kMountCooldownMs", policy)
        # The mount errand is picked after mail but when not in a capital.
        pick = policy[policy.index("inline CapitalErrandKind PickCapitalErrand"):]
        mount = pick.index("CapitalErrandKind::Mount")
        self.assertLess(pick.index("CapitalErrandKind::Mailbox"), mount)
        self.assertLess(mount, pick.index("if (!in.inCapital)"))

    def test_capital_errands_run_the_plan(self):
        host = read("forever/src/host/TbHostCapitalErrands.cpp")
        self.assertIn('#include "host/TbMountShop.h"', host)
        self.assertIn("mountshop::FindPlan(bot, kMountSearchDistYd)", host)
        self.assertIn("mountshop::ExecutePlan(bot, mountPlan", host)
        self.assertIn("mountshop::Step::UseItem", host)
        self.assertIn("FinishMountErrand(st, nowMs, ok, plan.step)", host)
        self.assertIn("case CapitalErrandKind::Mount:", host)
        # The old capital-only early return must not swallow the mount errand.
        self.assertIn("if (!inCapital && !needsMount)", host)

    def test_command_is_registered_and_delegates(self):
        scripts = read("forever/src/TbScripts.cpp")
        self.assertIn('#include "host/TbMountShop.h"', scripts)
        self.assertIn('{ "mount",  HandleMount,  rbac::RBAC_PERM_COMMAND_GM, Console::Yes }', scripts)
        start = scripts.index("static bool HandleMount(")
        end = scripts.index("\n    static bool HandleHurt(", start)
        body = scripts[start:end]
        self.assertIn("tbhost::mountshop::FindPlan(bot, range)", body)
        self.assertIn("tbhost::mountshop::ExecutePlan(bot, plan, &spent, &refusal)", body)
        self.assertIn("mount buy: learn riding first", body)

    def test_module_reuses_town_buy_and_trainer_and_policy_list(self):
        module = read("forever/src/host/TbMountShop.cpp")
        self.assertIn('#include "host/TbHostTown.h"', module)
        self.assertIn('#include "ai/playerbot/policy/travel/MountShopPolicy.h"', module)
        self.assertIn("tbhost::town::BuyItemFromVendorSlot", module)
        self.assertIn("tbhost::town::TeachTrainerSpell", module)
        self.assertIn("MountShopPolicy::GetRacialMounts", module)
        # The owner rule: the bot pays the trainer and the vendor; the module
        # never grants the riding skill.
        self.assertNotIn("SetSkill(SKILL_RIDING", module)
        self.assertIn("bot->GetMoney()", module)

    def test_off_build_has_a_refusal_stub(self):
        module = read("forever/src/host/TbMountShop.cpp")
        off = module[module.index("#else"):]
        self.assertIn('plan.reason = "engine-not-built";', off)
        self.assertIn("return Need::None;", off)
        self.assertIn("return false;", off)

    def test_mount_errand_is_documented(self):
        conf = read("forever/conf/tortoisebots.conf.dist")
        start = conf.index("TortoiseBots.World.CapitalErrands")
        block = conf[conf.rindex("#", 0, start):conf.index("TortoiseBots.World.CapitalErrands = 1")]
        self.assertIn("riding trainer", block)
        self.assertIn("mount vendor", block)
        self.assertIn("nothing is granted", block)

        commands = read("forever/docs/TB-COMMANDS.md")
        self.assertIn(".tb mount <bot> [status\\|train\\|buy]", commands)

    def test_policy_uses_the_core_mount_levels(self):
        pure = read("forever/src/host/TbMountShopPure.h")
        self.assertIn("kMountLevel = 40", pure)
        self.assertIn("kEpicMountLevel = 60", pure)
        self.assertIn("kApprenticeRiding = 75", pure)
        self.assertIn("kJourneymanRiding = 150", pure)


if __name__ == "__main__":
    unittest.main()

"""Wiring pins for the world-life slice W1 (grind and live): the free-bot host path, the free kit, the dead kit and the one teleport.

The decisions themselves are exercised by standalone tests (forever/tools/test_ghost_rescue_policy.cpp); these pins read the
sources and prove the ORDER and the CUTS stay where FOREVER-W1-W6A-SPEC 1.2-1.7 and the notes put them: a free bot is a host flag
and has no master, its engine is built without one, the free kit is exactly the class spec plus the world kit, the corpse run and
the spirit-healer leg never teleport (the host's five-minute rescue is the only one), revives go through the core's handlers, and
`.tb state` carries the W1 keys.

Run: python3 -m unittest forever/tools/test_w1_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
MGR_CPP = (ROOT / "forever/src/TbBotMgr.cpp").read_text()
MGR_H = (ROOT / "forever/src/TbBotMgr.h").read_text()
SCRIPTS = (ROOT / "forever/src/TbScripts.cpp").read_text()
ENGINE = (ROOT / "forever/src/host/TbEngineBrain.cpp").read_text()
ENGINE_H = (ROOT / "forever/src/host/TbEngineBrain.h").read_text()
FACTORY = (ROOT / "forever/src/slice/SliceAiFactory.cpp").read_text()
STATE = (ROOT / "forever/src/host/TbPartyState.cpp").read_text()
REVIVE = (ROOT / "ai/playerbot/strategy/actions/ReviveFromCorpseAction.cpp").read_text()
DEAD = (ROOT / "ai/playerbot/strategy/values/DeadValues.cpp").read_text()
GRIND = (ROOT / "ai/playerbot/strategy/values/GrindTargetValue.cpp").read_text()
COMPAT_F = (ROOT / "forever/src/compat/TbCompat_W1.h").read_text()
COMPAT_T = (ROOT / "ai/cmangos-compat-w1.h").read_text()
HOSTBOTS = (ROOT / "forever/src/host/TbHostBotManager.cpp").read_text()
GEN = (ROOT / "forever/tools/slice_gen_context.py").read_text()
CTX = (ROOT / "forever/src/slice/SliceAiObjectContext.cpp").read_text()
LIST = (ROOT / "forever/SLICE-W4.txt").read_text()      # W0-A: the newest list (the build reads it; it holds every W1 path)
W1_LIST = (ROOT / "forever/SLICE-W1.txt").read_text()
CMAKE = (ROOT / "CMakeLists.txt").read_text()
POLICY = (ROOT / "ai/playerbot/policy/death/GhostRescuePolicy.h").read_text()


def func(text, signature):
    """The text of the function whose definition line contains `signature` (up to its closing brace)."""
    i = text.index(signature)
    j = text.index("{", i)
    depth = 0
    for k in range(j, len(text)):
        if text[k] == "{":
            depth += 1
        elif text[k] == "}":
            depth -= 1
            if depth == 0:
                return text[i:k + 1]
    raise AssertionError(signature)


class FreeHost(unittest.TestCase):
    def test_a_free_bot_is_a_host_flag_with_no_master(self):
        login = func(MGR_CPP, "std::string BotMgr::Login(")
        self.assertIn("bool free", login)
        self.assertLess(login.index("free && !master.IsEmpty()"), login.index("Bot bot;"))
        self.assertIn("bot.free = free;", login)
        # PR #129 (player commands R2) added owned bots to the same line; a free bot still gets an engine.
        self.assertIn("bot.useEngine = _engineByDefault || free || owned || engine;", login)
        self.assertIn("bool IsFreeBot(ObjectGuid", MGR_H)
        self.assertIn("bool free = false;", func(MGR_H, "struct Bot"))

    def test_the_engine_of_a_free_bot_is_built_without_a_master_and_moves_free(self):
        free_think = func(MGR_CPP, "void BotMgr::FreeThink(")
        self.assertIn("engine::Create(me, nullptr, error, true)", free_think)
        create = func(ENGINE, "Brain* Create(Player* bot, Player* master, std::string& error, bool free)")
        self.assertIn("if (!master && !free)", create)
        self.assertIn('SetMovementStrategy(free && !master ? "free" : "follow")', create)

    def test_free_is_refused_for_a_bot_with_a_master_or_a_party(self):
        body = func(MGR_CPP, "std::string BotMgr::SetFree(")
        self.assertIn('"the bot has a master', body)
        self.assertIn('"the bot is in a party', body)
        self.assertLess(body.index("!bot.master.IsEmpty()"), body.index("bot.free = true;"))
        self.assertLess(body.index("me->GetGroup()"), body.index("bot.free = true;"))

    def test_the_console_commands_lead_to_the_host(self):
        self.assertIn('{ "free",   HandleFree,', SCRIPTS)
        free = func(SCRIPTS, "static bool HandleFree(")
        self.assertIn("ObjectGuid const guid = GuidByName(name);", free)
        self.assertIn("RequireOwnedBot(handler, guid)", free)
        self.assertIn("std::string const error = BotMgr::Instance().SetFree(guid, word == \"on\");", free)
        self.assertLess(free.index("RequireOwnedBot(handler, guid)"), free.index("BotMgr::Instance().SetFree(guid"))
        self.assertIn("BotMgr::Instance().Login(guid, master, free)", func(SCRIPTS, "static bool HandleLogin("))
        self.assertIn("BotMgr::Instance().IsFreeBot(ObjectGuid::Create<HighGuid::Player>(guid))", HOSTBOTS)

    def test_the_free_kit_is_the_class_spec_plus_the_world(self):
        body = func(FACTORY, "void AiFactory::AddDefaultNonCombatStrategies(")
        kit = func(body, "if (player && sRandomBotFacade.IsFreeBot(player)")
        self.assertIn("TortoiseBots::world::FreeSpecStrategy(player->GetClass())", kit)
        self.assertIn("TortoiseBots::world::FreeWorldKit()", kit)
        self.assertIn("for (char const* const* kit = TortoiseBots::world::FreeWorldKit(); *kit; ++kit)", kit)
        policy = (ROOT / "ai/playerbot/policy/world/FreeKitPolicy.h").read_text()
        self.assertIn('{ "grind", "travel", "food", "loot", "roll", "default", "buff", "emote", nullptr }', policy)
        for absent in ('"follow"', '"wander"', '"rpg"', '"maintenance"', '"guild"'):
            self.assertNotIn(absent, policy, absent)
        self.assertIn("return;", kit)

    def test_every_free_death_and_revive_has_one_log_line(self):
        track = func(MGR_CPP, "void BotMgr::TrackLife(")
        self.assertIn('"[W1] death bot={} level={}"', track)
        self.assertIn('"[W1] revive bot={} how={} secs={} yd={:.1f}"', track)
        self.assertIn("++bot.deaths;", track)
        self.assertIn("++bot.teleports;", track)     # the core's release teleport


class OneTeleport(unittest.TestCase):
    def test_the_rescue_is_the_only_teleport_of_the_corpse_run(self):
        # Every TeleportTo of the corpse run and the spirit-healer leg sits behind the W1 gate (false on ForeverCore); the one
        # teleport left is the host rescue, through the core's own repop.
        self.assertGreaterEqual(REVIVE.count("CorpseRunTeleportsEnabledCompat()"), REVIVE.count("->TeleportTo("))
        self.assertIn("CorpseRunCrossMapEnabledCompat()", REVIVE)
        self.assertEqual(REVIVE.count("CorpseRunCrossMapEnabledCompat()"), 2)   # the scan cut and the cross-map return
        self.assertIn("return false;", COMPAT_F.split("inline bool CorpseRunTeleportsEnabledCompat()")[1])

    def test_the_spirit_healer_revives_through_the_core_handler(self):
        self.assertEqual(COMPAT_F.count("HandleSpiritHealerActivate("), 1)
        self.assertEqual(COMPAT_F.count("HandleReclaimCorpse("), 1)
        self.assertEqual(REVIVE.count("HandleReclaimCorpseOpcode("), 0)
        self.assertNotIn("HandleSpiritHealerActivate(", REVIVE)
        self.assertIn("ActivateSpiritHealerCompat(bot, ", REVIVE)
        # F splits before the Turtle direct revive: the legacy block stays for the Turtle core only.
        self.assertIn("if (SpiritHealerCoreHandlerCompat())", REVIVE)
        self.assertIn("return false;", COMPAT_T.split("inline bool SpiritHealerCoreHandlerCompat()")[1][:80])

    def test_the_rescue_waits_for_the_policy_and_fires_once_per_death(self):
        rescue = func(MGR_CPP, "void BotMgr::GhostRescue(")
        self.assertLess(rescue.index("bot.ghostRescue.Observe("), rescue.index("bot.ghostRescue.Due()"))
        self.assertLess(rescue.index("!bot.ghostRescue.Due() || bot.rescuedThisDeath"), rescue.index("RepopAtGraveyardCompat(me)"))
        self.assertIn("++bot.rescues;", rescue)
        self.assertIn("++bot.teleports;", rescue)
        self.assertIn('"[W1] rescue bot={} reason=ghost-stuck', rescue)
        self.assertIn("bot.rescuedThisDeath = false;", func(MGR_CPP, "void BotMgr::TrackLife("))

    def test_the_policy_is_five_minutes_and_five_yards(self):
        self.assertIn("5 * 60 * 1000", POLICY)
        self.assertIn("5.0f", POLICY)
        self.assertIn("corpseYd < _bestCorpse - GHOST_RESCUE_PROGRESS_YD", POLICY)
        self.assertIn("healerYd < _bestHealer - GHOST_RESCUE_PROGRESS_YD", POLICY)


class GrindAndState(unittest.TestCase):
    def test_grind_reads_are_behind_the_compat_pair(self):
        self.assertIn('#include "playerbot/TbCompatW1.h"', GRIND)
        self.assertNotIn("->rank", GRIND)
        self.assertNotIn("GetReputationOnKillEntry", GRIND)
        self.assertNotIn("IsHonorOrXPTarget(unit)", GRIND)
        self.assertIn("VisitUnfriendlyUnitsCompat(", GRIND)
        self.assertIn("CreatureRankCompat(creature)", GRIND)
        self.assertIn("inline bool IsHonorOrXPTargetCompat(Player* bot, Unit* unit)", COMPAT_F)
        self.assertIn("inline bool IsHonorOrXPTargetCompat(Player* bot, Unit* unit)", COMPAT_T)

    def test_dead_values_read_the_graveyard_through_the_pair(self):
        self.assertNotIn("sMapStorage", DEAD)
        self.assertNotIn("GetClosestGraveYard", DEAD)
        self.assertIn("ClosestGraveyardCompat(", DEAD)
        self.assertIn("MapIsDungeonCompat(", DEAD)

    def test_state_carries_the_w1_keys(self):
        for key in ('"free"', '"xp"', '"xpnext"', '"kills"', '"deaths"', '"grind"', '"corpserun"', '"ghostyd"',
                    '"rescue"', '"sick"', '"dur"', '"bags"', '"tele"'):
            self.assertIn(key, STATE)
        self.assertIn('"release"', STATE)
        self.assertIn('"reclaim"', STATE)
        self.assertIn('"healer"', STATE)
        self.assertIn('"rescue"', STATE)


class Registry(unittest.TestCase):
    def test_the_w1_keys_are_generated(self):
        block = GEN[GEN.index("W1_KEYS = {"):GEN.index("def _join(*ds):")]
        for key in ("grind", "grind target", "can fight elite", "dangerous elite", "attack anything", "avoid elite",
                    "idle wander", "find corpse", "revive from corpse", "spirit healer", "no target",
                    "dangerous elite near", "corpse near", "val", "graveyard", "best graveyard", "should spirit healer", "can move around"):
            self.assertIn(key, block)
        self.assertIn("SLICE9_KEYS, W1_KEYS", GEN)
        self.assertIn('creators["grind"]', CTX)
        self.assertIn('creators["grind target"]', CTX)
        self.assertIn('creators["should spirit healer"]', CTX)
        self.assertIn('creators["can move around"]', CTX)

    def test_the_census_list_carries_the_w1_files_and_the_build_reads_it(self):
        listed = [l.strip() for l in LIST.splitlines() if l.strip() and not l.startswith("#")]
        for path in ("ai/playerbot/strategy/generic/GrindingStrategy.cpp", "ai/playerbot/strategy/values/GrindTargetValue.cpp",
                     "ai/playerbot/strategy/actions/ReviveFromCorpseAction.cpp", "ai/playerbot/strategy/values/DeadValues.cpp",
                     "ai/playerbot/FleeManager.cpp"):
            self.assertIn(path, listed)
        # The newest list (now the travel slice W4's) is what the build reads; the W1 list stays its first part.
        self.assertIn("forever/SLICE-W4.txt", CMAKE)
        self.assertNotIn("forever/SLICE9.txt", CMAKE)
        # The W1 list stays the record of the W1 slice, and the list the build reads holds every one of its paths.
        for line in W1_LIST.splitlines():
            line = line.strip()
            if line and not line.startswith("#"):
                self.assertIn(line, listed)


class FreeAndCommanded(unittest.TestCase):
    """W1 (review fix 1): a bot has one identity. Binding a master ends free mode at that moment and keeps it off until `.tb free on`
    says otherwise, in every path that binds one: `.tb group` (GroupWith), the commander binding, a run and the engine's adoption all
    reach SetMaster (host bindings through BotMgr::SetMaster), and a login with a master never starts free."""

    def test_every_master_binding_goes_through_one_place_that_ends_free(self):
        bind = func(MGR_CPP, "void BotMgr::BindMaster(")
        self.assertIn("!master.IsEmpty() && bot.free", bind)
        self.assertIn("bot.free = false;", bind)
        self.assertIn("bot.ghostRescue.Reset();", bind)
        self.assertIn("bot.ghostYd = -1.0f;", bind)
        self.assertIn("bot.rescuedThisDeath = false;", bind)
        self.assertLess(bind.index("bot.free = false;"), bind.index("engine::BindMaster("))

        group_with = func(MGR_CPP, "std::string BotMgr::GroupWith(")
        self.assertIn("BindMaster(itr->second, masterGuid);", group_with)
        self.assertLess(group_with.index("JoinParty(bot, master)"), group_with.index("BindMaster("))   # a refused group changes nothing
        set_master = func(MGR_CPP, "bool BotMgr::SetMaster(")
        self.assertIn("BindMaster(itr->second, master);", set_master)
        login = func(MGR_CPP, "std::string BotMgr::Login(")
        self.assertLess(login.index("free && !master.IsEmpty()"), login.index("bot.free = free;"))
        self.assertIn("bot.free = free;", login)

    def test_the_host_and_run_bindings_go_through_setmaster(self):
        bind = func(HOSTBOTS, "bool BotManager::BindBotMaster(")
        self.assertIn("BotMgr::Instance().SetMaster(botGuid, masterGuid)", bind)

    def test_the_engine_takes_the_master_and_the_commanded_kit(self):
        bind = func(ENGINE, "void BindMaster(Brain* brain, Player* master)")
        self.assertIn("SetMaster(master)", bind)
        self.assertIn("Reset(true)", bind)                    # the travel/grind targets and the current path go
        self.assertIn("ReapplyStrategies(brain, false)", bind)  # ... and the kit is the commanded one, not the free one
        self.assertIn("void BindMaster(Brain* brain, Player* master)", ENGINE_H)

    def test_the_factory_and_the_host_agree_on_free(self):
        # The mode is read at the moment the kit is rebuilt, so the flag has to be clear before ReapplyStrategies runs.
        bind = func(MGR_CPP, "void BotMgr::BindMaster(")
        self.assertLess(bind.index("bot.free = false;"), bind.index("engine::BindMaster("))
        factory = func(FACTORY, "void AiFactory::AddDefaultNonCombatStrategies(")
        self.assertIn("sRandomBotFacade.IsFreeBot(player)", factory)


class FreeGrindTargets(unittest.TestCase):
    """W1 (review fix 2): a free bot only starts fights with hostile or neutral NPCs that are none of the protected kinds, and never
    starts one with a tapped mob. Self-defence is the attackers loop, which runs before the possible targets are read."""

    def test_the_rule_is_one_pure_decision(self):
        policy = (ROOT / "ai/playerbot/policy/world/FreeGrindPolicy.h").read_text()
        for fact in ("player", "friendly", "playerControlled", "serviceNpc", "guard", "civilian", "killLowersReputation",
                     "tappedByAnotherPlayer"):
            self.assertIn(f"bool {fact} = false;", policy)
        self.assertIn("constexpr bool FreeBotMayStartFight(", policy)
        self.assertIn("return FreeGrindRefusalOf(facts) == FreeGrindRefusal::None;", policy)

    def test_both_new_pull_paths_ask_the_rule(self):
        grind = func(GRIND, "Unit* GrindTargetValue::FindTargetForGrinding(")
        fallback = func(GRIND, "Unit* GrindTargetValue::FindIdleFallbackTarget(")
        for body in (grind, fallback):
            self.assertIn("FreeBotMayStartFight(", body)
            self.assertIn("FreeGrindFactsOf(", body)
        # The self-defence loop comes first and is not filtered by the free rule.
        self.assertLess(grind.index("possible attack targets"), grind.index("FreeBotMayStartFight("))

    def test_the_facts_come_from_the_protected_kinds(self):
        facts = func(GRIND, "FreeGrindFacts FreeGrindFactsOf(")
        for read in ("UNIT_NPC_FLAG_QUESTGIVER", "UNIT_NPC_FLAG_VENDOR", "UNIT_NPC_FLAG_TRAINER", "UNIT_NPC_FLAG_FLIGHTMASTER",
                     "UNIT_NPC_FLAG_INNKEEPER", "CreatureIsGuardFlagCompat(", "CreatureIsCivilianCompat(",
                     "KillLowersReputationCompat(", "CreatureHasLootRecipientCompat(", "CreatureIsTappedByCompat(",
                     "GetCharmerOrOwnerPlayerOrPlayerItself()"):
            self.assertIn(read, facts)
        self.assertEqual(facts.count("UnitHasNpcFlagW1Compat("), 5)   # the two cores spell the field differently
        self.assertIn("inline bool UnitHasNpcFlagW1Compat(Unit* unit, uint32 flag)", COMPAT_F)
        self.assertIn("inline bool UnitHasNpcFlagW1Compat(Unit* unit, uint32 flag)", COMPAT_T)
        self.assertIn("unit->HasNpcFlag(static_cast<NPCFlags>(flag))", COMPAT_F)
        self.assertIn("unit->HasFlag(UNIT_NPC_FLAGS, flag)", COMPAT_T)
        # And only for a free bot: a commanded bot keeps its player's own rules. ForeverCore only - the gate itself (the Turtle side
        # answers false) is pinned in tools/test_free_grind_target_wiring.py.
        grind = func(GRIND, "Unit* GrindTargetValue::FindTargetForGrinding(")
        self.assertIn("FreeBotStartFightRuleCompat()", grind)


if __name__ == "__main__":
    unittest.main()

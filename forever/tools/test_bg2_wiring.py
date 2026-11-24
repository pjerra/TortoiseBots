"""Wiring pins for round BG2 (BATTLEGROUNDS-SPEC.md D9): the PvP fight kit of a bot inside a battleground.

The engine cannot run without a server, so these pin the wiring in the source; each assertion fails on the tree before BG2.

  1 the five BG2 files are in SLICE-W4.txt (the list the build reads), once each, and exist
  2 the BG pair: ForeverCore's TbCompat_BG.h and Turtle's cmangos-compat-bg.h define the same helper names, the Turtle side keeps the old
    behaviour (target policy off, battlemaster path on), and ai/playerbot/TbCompatBg.h finds either
  3 the legacy files hold no cmangos battleground internals (BattleGroundWS, the picker getters, m_duel): they went into the pair
  4 EnemyPlayerValue.cpp ranks the enemy players through the pure policy inside a battleground, behind the pair's switch
  5 the pure policy includes no core header and the target ranges are named constants of BgLimits.h
  6 the carrier aura ids in BgIds.h are in the pinned core script (skipped when no core checkout can be read)
  7 the slice registers the PvP keys (BG2_KEYS) and slice_gen_context.py --check is clean
  8 the battleground kit is added after the class kit by a scope guard in both kit functions, adds "pvp" (and "battleground" to the
    non-combat engine), never adds "duel", and the class kits are untouched (no removed line in SliceAiFactory.cpp)
  9 PlayerbotAI's per-tick strategy reset for a bot inside a battleground is answered by the "battleground" strategy of the kit
 10 the old stub of GetMaxAttackDistance is gone (the real one is compiled) and the one WorldPvpSeekChecks member the value calls is stubbed
 11 no file under the core's src/ is in the diff (needs the base ref; skipped when git cannot answer)
 12 the carrier auras are read through the pure side mapping (BgIds.h) at both detection sites, never typed there (live row 4)
 13 `.tb master` asks the plain N5 rule first and, only where it refuses, one battleground instance and one team (live row 5)
 14 a bind made inside one match gives THAT bot the bg kind (per bound bot, with the commander's instance): a bg bot is never in the record or
    stored, is checked on its own facts and released alone (its leave, the commander's, a logout, the BG ending), never runs the release party
    rule (the raid's leader stands), and the commander's normal members keep the record, its row and its rules

BG_MUTATE=1 runs the same checks against mutated text and expects each to fail.
"""
import os
import pathlib
import re
import subprocess
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import test_bg1_wiring as bg1  # noqa: E402  (the pinned-core reader and the core-diff check)

FILES = {
    "enemy": "ai/playerbot/strategy/values/EnemyPlayerValue.cpp",
    "pvpvalues": "ai/playerbot/strategy/values/PvpValues.cpp",
    "pvptriggers": "ai/playerbot/strategy/triggers/PvpTriggers.cpp",
    "pvpstrategy": "ai/playerbot/strategy/generic/AttackEnemyPlayersStrategy.cpp",
    "bgstrategy": "ai/playerbot/strategy/generic/BattlegroundStrategy.cpp",
    "enemyh": "ai/playerbot/strategy/values/EnemyPlayerValue.h",
    "forever_pair": "forever/src/compat/TbCompat_BG.h",
    "turtle_pair": "ai/cmangos-compat-bg.h",
    "wrapper": "ai/playerbot/TbCompatBg.h",
    "policy": "ai/playerbot/policy/bg/BgTargetPolicy.h",
    "limits": "ai/playerbot/policy/bg/BgLimits.h",
    "ids": "ai/playerbot/policy/bg/BgIds.h",
    "factory": "forever/src/slice/SliceAiFactory.cpp",
    "stubs": "forever/src/slice/SliceLinkStubs.cpp",
    "gen": "forever/tools/slice_gen_context.py",
    "context": "forever/src/slice/SliceAiObjectContext.cpp",
    "w4": "forever/SLICE-W4.txt",
    "ai": "ai/playerbot/PlayerbotAI.cpp",
    "cmake": "CMakeLists.txt",
    "orders": "forever/src/host/TbOrders.cpp",
    "bindpure": "forever/src/host/cmd/TbMasterBindPure.h",
    "binding": "forever/src/host/cmd/TbCmdBinding.cpp",
    "life": "forever/src/host/cmd/TbCmdBindingLifePure.h",
}

BG2_LIST = (FILES["enemy"], FILES["pvpvalues"], FILES["pvptriggers"], FILES["pvpstrategy"], FILES["bgstrategy"])
PAIR_NAMES = ("BgFlagCarrierCompat", "BgBotCarriesFlagCompat", "BgBotMayQueueCompat",
              "BgDistanceToCompat", "kBgTargetPolicyCompat", "TB_BG_BATTLEMASTER_PATH")


def sources(overrides=None):
    overrides = overrides or {}
    out = {}
    for key, rel in FILES.items():
        out[key] = overrides.get(key, (ROOT / rel).read_text())
    return out


def check_slice_list(src):
    lines = [l.strip() for l in src["w4"].splitlines() if l.strip() and not l.startswith("#")]
    for rel in BG2_LIST:
        assert lines.count(rel) == 1, f"{rel} must be in SLICE-W4.txt exactly once"
        assert (ROOT / rel).exists(), f"{rel} does not exist"
    assert "forever/SLICE-W4.txt" in src["cmake"], "the build must read the list the BG2 files are in"


def check_pair(src):
    forever, turtle, wrapper = src["forever_pair"], src["turtle_pair"], src["wrapper"]
    for name in PAIR_NAMES:
        assert name in forever, f"TbCompat_BG.h lacks {name}"
        assert name in turtle, f"cmangos-compat-bg.h lacks {name}"
    assert "inline constexpr bool kBgTargetPolicyCompat = true;" in forever, "ForeverCore ranks targets through the policy"
    assert "inline constexpr bool kBgTargetPolicyCompat = false;" in turtle, "the Turtle build keeps its own pick"
    assert "#define TB_BG_BATTLEMASTER_PATH 0" in forever and "#define TB_BG_BATTLEMASTER_PATH 1" in turtle
    assert '__has_include("TbCompat_BG.h")' in wrapper and '#include "cmangos-compat-bg.h"' in wrapper
    assert "BattleGroundWS" in turtle and "BattleGroundWS" not in forever, "the cmangos battleground class stays on its side of the pair"
    assert "kWsgCarrierAuraWarsong" in forever and "GetBgMap()->GetPlayer" in turtle
    # The enemy-player sight range is the open-world PvP seek port's helper on both cores (TbCompatPvP.h / cmangos-compat-pvp.h): one rule,
    # not a second BgMaxAttackDistanceCompat beside it.
    assert "BgMaxAttackDistanceCompat" not in forever and "BgMaxAttackDistanceCompat" not in turtle


def check_legacy_files_clean(src):
    for key in ("enemy", "pvptriggers"):
        # three old triggers sit in `#ifdef MANGOS` blocks, which no build of this tree defines: they are not compiled on either core
        text = re.sub(r"#ifdef MANGOS.*?#endif", "", src[key], flags=re.S)
        for forbidden in ("BattleGroundWS", "FlagPickerGuid", "m_duel", "GetFlagState"):
            assert forbidden not in text, f"{FILES[key]} still reads {forbidden} (it belongs to the Turtle side of the pair)"
    values = src["pvpvalues"]
    assert "BattleGroundWS" not in values and "FlagPickerGuid" not in values, "FlagCarrierValue reads the pair"
    assert "BgFlagCarrierCompat(bot, sameTeam, sPlayerbotAIConfig.sightDistance, ignoreRange)" in values
    # the battlemaster values only exist where the legacy queue path does
    assert values.count("#if TB_BG_BATTLEMASTER_PATH") == 2 and "#endif // TB_BG_BATTLEMASTER_PATH" in values
    for key in ("enemy", "pvptriggers", "pvpvalues"):
        assert '#include "playerbot/TbCompatBg.h"' in src[key], f"{FILES[key]} must include the pair"


def check_policy_wired(src):
    text, header = src["enemy"], src["enemyh"]
    assert '#include "playerbot/policy/bg/BgTargetPolicy.h"' in text and '#include "playerbot/policy/bg/BgIds.h"' in text, "the Turtle side gets the aura ids only through this include"
    assert "if (kBgTargetPolicyCompat && bot->InBattleGround())" in text and "return PickInBattleground(enemyPlayers);" in text, \
        "the policy ranks the enemies inside a battleground, behind the pair's switch"
    assert "Unit* PickInBattleground(" in header
    body = text[text.index("Unit* EnemyPlayerValue::PickInBattleground("):]
    assert "TortoiseBots::bg::BgPickTarget(candidates, policyContext)" in body
    for fact in ("isFlagCarrier", "hitsOurCarrier", "hitsOurHealer", "isHealer", "isMyAttacker", "isMasterTarget", "objectiveDistanceYd"):
        assert f"candidate.{fact}" in body, f"the host does not fill {fact}"
    assert 'policyContext.bound = ai->HasRealPlayerMaster();' in body, "a bound bot is one with a human master"
    assert 'AI_VALUE(Unit*, "master target")' not in body, '"master target" is the master himself, not the unit he is fighting'
    assert "master->GetVictim()" in body and "master->GetSelectionGuid()" in body, "the human's target is his victim, else his selection"
    assert '["bg objective"]' in body, "the leash reads the objective position the objective strategy sets"
    assert "GetMaxAttackDistance(Player* bot)" in text and "MaxEnemyPlayerAttackDistanceCompat(bot)" in text, \
        "the sight range is TbCompatPvP.h's helper (60 yd in the world, 40 in a battleground)"
    assert "BgMaxAttackDistanceCompat" not in text, "no second sight helper on the BG2 side"


def check_policy_pure(src):
    policy = src["policy"]
    for line in policy.splitlines():
        if line.startswith("#include"):
            assert re.match(r'#include (<[a-z_]+>|"BgLimits\.h")', line), f"the pure policy includes a core header: {line}"
    limits = src["limits"]
    for name, value in (("kBgFlagCarrierYd", "40.0f"), ("kBgDefendYd", "40.0f"), ("kBgNearYd", "30.0f"), ("kBgLeashYd", "40.0f")):
        assert f"inline constexpr float {name} = {value};" in limits, f"{name} must be {value} (spec D9)"
        assert name in policy, f"the policy does not use {name}"


def check_ids(src):
    ids = src["ids"]
    wanted = {"kWsgCarrierAuraWarsong": "23333", "kWsgCarrierAuraSilverwing": "23335"}
    script = None
    for name, number in wanted.items():
        match = re.search(rf"{name}\s*=\s*(\d+);", ids)
        assert match and match.group(1) == number, f"{name} must be {number}"
        if script is None:
            script = bg1.core_text("scripts/Battlegrounds/WarsongGulch/battleground_warsong_gulch.cpp")
        if script is None:
            raise unittest.SkipTest(f"no core checkout at the pin {bg1.PIN[:9]} (set FOREVER_CORE)")
        assert re.search(rf"SPELL_(WARSONG|SILVERWING)_FLAG\s*=\s*{number}", script), f"{number} is not a carrier spell at the pin"


def check_carrier_sites(src):
    text, pair = src["enemy"], src["forever_pair"]
    # BG2 live row 4: the carrier is the AURA on the player, and both sites read it through the pure side mapping (BgIds.h) so the two
    # spell ids are not typed a third time. The ids themselves are pinned against the pinned core by check_ids.
    assert "candidate.isFlagCarrier = enemy->HasAura(enemyCarrierAura);" in text, "the enemy carrier must be read from the aura on the enemy"
    assert "victim->HasAura(ownCarrierAura)" in text, "an enemy hitting OUR carrier must be found by the aura on the victim"
    body = text[text.index("Unit* EnemyPlayerValue::PickInBattleground("):]
    assert "bg::WsgOwnCarrierAura(AiSide(bot))" in body and "bg::WsgEnemyCarrierAura(AiSide(bot))" in body, \
        "own and enemy carrier auras must come from the pure mapping (BgIds.h)"
    calc = text[text.index("EnemyPlayerValue::Calculate()"):text.index("float EnemyPlayerValue::GetMaxAttackDistance(")]
    assert "bg::WsgEnemyCarrierAura(AiSide(bot))" in calc, "the pre-policy flag pick must read the same aura"
    assert "kWsgCarrierAuraWarsong" not in text and "kWsgCarrierAuraSilverwing" not in text, \
        "the ids belong to BgIds.h: no site types them"
    assert "bg::WsgCarrierAuraOf(side == ALLIANCE ? TortoiseBots::bg::BgSide::Alliance : TortoiseBots::bg::BgSide::Horde)" in pair, \
        "BgFlagCarrierCompat must read the clock-race-free pure mapping too"
    assert "static TortoiseBots::bg::BgSide AiSide(Player* bot)" in text, "the bot's side must be one helper"


def check_master_bind(src):
    header, orders = src["bindpure"], src["orders"]
    for name in ("struct Facts", "enum class Verdict", "inline Verdict Judge(Facts const& facts)", "inline char const* CodeOf(Verdict verdict)",
                 "inline std::string DetailOf(Verdict verdict"):
        assert name in header, f"TbMasterBindPure.h lacks {name}"
    assert header.count('"not-allowed"') == 1 and '"other-instance"' in header and '"other-team"' in header
    assert "is neither a host bot nor your own character: it cannot be made the commander of bots" in header, \
        "the plain rule's refusal text lives in the header"
    body = orders[orders.index("bool HandleMaster("):]
    body = body[:body.index("// ---- `.tb spec")]
    assert '#include "host/cmd/TbMasterBindPure.h"' in orders, "HandleMaster must use the pure BG bind rule"
    assert "if (!release && !MayLeadBots(handler, commander->GetGUID()))" in body, "the plain N5 rule is still asked first"
    assert "cmd::master_bind::Judge(facts)" in body, "the verdict must come from the pure header"
    assert "facts.botInstance = bot->GetBattlegroundId();" in body and "facts.commanderInstance = commander->GetBattlegroundId();" in body, \
        "the facts are the core's own instance ids"
    assert "facts.sameTeam = bot->GetBGTeam() == commander->GetBGTeam();" in body, "the facts are the core's own fighting teams"
    assert "cmd::master_bind::CodeOf(verdict)" in body and "cmd::master_bind::DetailOf(verdict, name)" in body, \
        "the refusal uses the verdict's own code and text"
    assert "neither a host bot nor your own character: it cannot be made the commander of bots" not in body, \
        "the refusal text must not be copied beside the header's"
    assert "if (!bindable)" in body or "bindable" in body, "a command where nothing may bind refuses before anything changes"
    assert "verdicts.empty()" in body, "the per-bot refusal must only run when the plain rule refused"


def check_bg_bind_life(src):
    """A bind made inside a battleground is its own short-lived kind (BG2 live row 5), and the kind is PER BOUND BOT (the cold review's F2/F3).
    The handler reads the kind for each bot from the core's match facts (IsBgBind) and hands one kind per bound bot plus the COMMANDER's instance
    to the binding check. A bg bot goes into the in-memory book, never into the commander's record: it is never stored, no normal life rule or
    group/login event reaches it, it is checked once a second on its own facts (each member alone; the commander leaving releases all of his),
    its release never reaches into the raid (the raid's leader stands), and the normal members keep the record, its row and its rules."""
    header, life, orders, binding = src["bindpure"], src["life"], src["orders"], src["binding"]
    # The kind's rule is pure and reads the core's three facts.
    assert "inline bool IsBgBind(Facts const& facts)" in header, "the BG bind kind must be a pure rule"
    assert "facts.botInstance != 0 && facts.botInstance == facts.commanderInstance && facts.sameTeam" in header
    # The handler reads the kind per bot at the check and hands it, per bound bot, with the commander's instance.
    body = orders[orders.index("bool HandleMaster("):]
    body = body[:body.index("// ---- `.tb spec")]
    assert "uint32 const commanderInstance = (!release && commander) ? commander->GetBattlegroundId() : 0;" in body
    assert "kind = cmd::master_bind::IsBgBind(bgFacts) ? cmd::life::Kind::Bg : cmd::life::Kind::Normal;" in body, \
        "the bot's kind is not read from its match facts"
    assert "boundBots.push_back(bot);\n        boundKinds.push_back(kind);" in body, "the kind is not kept beside each bound bot"
    assert "cmd::NoteMaster(boundBots, boundKinds, who.leader, commander, commanderInstance);" in body, \
        "the per-bot kind (or the commander's instance) never reaches the binding check"
    assert "bool bgBind" not in body, "a single command-wide flag is back"
    # The record has no kind of its own: the bg members are a per-bot book beside it.
    meta = re.search(r"struct Meta\s*\{.*?\};", binding, re.S).group(0)
    assert "bgBind" not in meta and "bgInstance" not in meta, "the commander record carries a kind again (F2)"
    assert "L::BgBook bg;" in binding
    note = binding[binding.index("void NoteMaster(std::vector<Player*> const& bots, std::vector<L::Kind> const& kinds"):]
    note = note[:note.index("void NoteOrder(")]
    assert "result.kind = i < kinds.size() ? kinds[i] : L::Kind::Normal;" in note, "the binding check does not read each bot's own kind"
    assert "if (!L::ApplyBind(s.bg, commanderGuid.GetCounter(), results, bgInstance))\n        return;" in note, \
        "a command that bound nothing changes the state, or the instance is not the handler's"
    assert "switch (L::NormalRecordUpdate(results, recordMembers))" in note
    assert "std::vector<ObjectGuid> const bound = NormalBoundTo(commander->GetGUID());" in note, "the record is rebuilt from bg members too"
    assert "GetBattlegroundId" not in note, "the instance is read from a bot of the record (F2)"
    # The record's members, followers and release are the normal bots only.
    for site in ("std::vector<ObjectGuid> const bound = NormalBoundTo(commander);",        # SyncFollowers
                 "for (ObjectGuid bot : NormalBoundTo(commander))\n    {\n        // A released bot"):  # ReleaseCommander
        assert site in binding, f"a normal-record path reads the bg members: {site[:50]}"
    # The bg members' own check: once a second and at a logout, each on its own facts, released alone.
    assert "    RunBgCheck();\n    if (s.metas.empty())" in binding, "the 1 Hz tick does not check the bg members (or only with a record)"
    assert "RunBgCheck(player->GetGUID());" in binding, "a logout does not end the bg binds"
    assert "for (L::BgVerdict const& verdict : L::CheckBg(s.bg, live, BotMgr::Instance().Enabled()))" in binding
    assert "facts.botInstance = bot ? bot->GetBattlegroundId() : 0;" in binding
    assert "facts.commanderInstance = commanderPlayer ? commanderPlayer->GetBattlegroundId() : 0;" in binding
    release = binding[binding.index("void ReleaseBgMember("):binding.index("void RunBgCheck(")]
    for touch in ("ApplyPartyAction", "ChangeLeader", "SaveRow", "store::"):
        assert touch not in release, f"a bg release reaches {touch}"
    assert "if (L::ReleaseReachesParty(L::Kind::Normal, m.flags))" in binding, "the normal release's party rule is not the pure one"
    assert "m.bgBind" not in binding, "a record-wide battleground flag is back"
    # The pure rule and its verdicts.
    for name in ("struct BgFacts", "enum class BgEnd", "inline BgEnd BgBindEnd(", "inline End BgEndToEnd(", "enum class Kind",
                 "inline bool ApplyBind(", "inline std::vector<uint64_t> NormalMembers(", "inline NormalUpdate NormalRecordUpdate(",
                 "inline std::vector<BgVerdict> CheckBg(", "inline bool ReleaseReachesParty(",
                 "case BgEnd::BotLeft: return End::BgBotLeft;", "case BgEnd::MatchEnded: return End::BgMatchEnded;"):
        assert name in life, f"TbCmdBindingLifePure.h lacks {name}"
    assert "facts.bgBind" not in life, "Decide reads a record-wide battleground flag again"
    for code in ('"bg-bot-left"', '"bg-commander-left"', '"bg-match-ended"'):
        assert code in life, f"the release log lacks the code {code}"


def check_keys(src):
    gen, context = src["gen"], src["context"]
    assert "BG2_KEYS" in gen and "WPVP_KEYS, BG2_KEYS)" in gen, "the BG2 keys must be part of KEYS"
    # "pvp", "attack enemy player", "enemy player near" and the enemy player values came with the open-world PvP seek port (WPVP_KEYS);
    # BG2 adds "battleground" and the two flag carrier values.
    assert '"strategy": "battleground"' in gen and '"value": "enemy flag carrier, team flag carrier"' in gen
    for needle in ('creators["pvp"]', 'creators["battleground"]', 'creators["attack enemy player"]', 'creators["enemy player near"]',
                   'creators["enemy player target"]', 'creators["enemy player targets"]', 'creators["has enemy player targets"]',
                   'creators["enemy flag carrier"]', 'creators["team flag carrier"]'):
        assert needle in context, f"{needle} is not registered by the slice"


def check_slice_gen_clean():
    run = subprocess.run(["python3", str(ROOT / "forever/tools/slice_gen_context.py"), "--check"], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout + run.stderr


def check_kit(src):
    text = src["factory"]
    combat = text[text.index("void ApplyBattlegroundCombatKit("):text.index("void ApplyBattlegroundNonCombatKit(")]
    non_combat = text[text.index("void ApplyBattlegroundNonCombatKit("):text.index("} // namespace\n\nvoid AiFactory::AddDefaultCombatStrategies")]
    assert 'combatEngine->addStrategy("pvp");' in combat, "the combat kit adds pvp"
    assert 'addStrategy("duel")' not in combat and 'removeStrategy("duel")' in combat, "duel never belongs in a battleground"
    assert 'nonCombatEngine->addStrategies("pvp", "battleground", NULL);' in non_combat
    assert 'removeStrategy("grind")' in non_combat, "a free bot has no grind in a battleground"
    assert "player->InBattleground()" in combat and "player->InBattleground()" in non_combat
    assert "AtScopeExit const battlegroundKit([&] { ApplyBattlegroundCombatKit(player, facade, combatEngine); });" in text
    assert "AtScopeExit const battlegroundKit([&] { ApplyBattlegroundNonCombatKit(player, facade, nonCombatEngine); });" in text
    # the guard is the first statement of each kit function, so every early return of a class switch still runs it
    for fn, guard in (("void AiFactory::AddDefaultCombatStrategies(", "ApplyBattlegroundCombatKit"),
                      ("void AiFactory::AddDefaultNonCombatStrategies(", "ApplyBattlegroundNonCombatKit")):
        body = text[text.index(fn):]
        first = body[body.index("{") + 1:].lstrip().splitlines()[0]
        assert guard in first, f"the scope guard must be the first statement of {fn}"


def check_class_kits_untouched():
    # The PR's base is tbf/dev: the branch's own diff must not remove a line of Sagiroth's class kits.
    run = subprocess.run(["git", "-C", str(ROOT), "diff", "--numstat", "tbf/dev...HEAD", "--", FILES["factory"]],
                         capture_output=True, text=True)
    if run.returncode != 0:
        raise unittest.SkipTest("git cannot name the base ref")
    for line in run.stdout.splitlines():
        added, removed, _ = line.split("\t")
        assert removed == "0", f"SliceAiFactory.cpp lost {removed} lines: the class kits are Sagiroth's and stay as they are"


def check_tick_reset(src):
    assert 'if (bot->InBattleGround() && !HasStrategy("battleground", BotState::BOT_STATE_NON_COMBAT))' in src["ai"], \
        "PlayerbotAI's reset condition changed: re-read the BG kit"
    assert '"pvp", "battleground"' in src["factory"], "the non-combat kit must carry the strategy the tick reset looks for"
    assert 'creators["battleground"]' in src["context"], "the strategy the kit adds must be registered"


def check_stubs(src):
    stubs, w4 = src["stubs"], src["w4"]
    assert "EnemyPlayerValue::GetMaxAttackDistance" not in stubs, "the stub would clash with the compiled value"
    # The open-world PvP seek port (dev) compiles WorldPvpSeekChecks.cpp: the member EnemyPlayerValue calls has a real body, so a stub here
    # would be a duplicate symbol.
    assert "WorldPvpSeekChecks::FindAssistTarget" not in stubs, "WorldPvpSeekChecks.cpp is compiled: no stub may define it"
    assert "ai/playerbot/strategy/values/WorldPvpSeekChecks.cpp" in w4


class Bg2Wiring(unittest.TestCase):
    def test_files_are_in_the_build_list(self):
        check_slice_list(sources())

    def test_compat_pair(self):
        check_pair(sources())

    def test_legacy_files_hold_no_cmangos_battleground(self):
        check_legacy_files_clean(sources())

    def test_policy_is_wired(self):
        check_policy_wired(sources())

    def test_policy_is_pure(self):
        check_policy_pure(sources())

    def test_ids_match_the_pinned_core(self):
        check_ids(sources())

    def test_carrier_aura_sites(self):
        check_carrier_sites(sources())

    def test_master_bind_in_a_battleground(self):
        check_master_bind(sources())

    def test_a_battleground_bind_has_its_own_life(self):
        check_bg_bind_life(sources())

    def test_keys_are_registered(self):
        check_keys(sources())

    def test_slice_context_is_generated(self):
        check_slice_gen_clean()

    def test_kit_is_added_after_the_class_kit(self):
        check_kit(sources())

    def test_class_kits_untouched(self):
        check_class_kits_untouched()

    def test_tick_reset_is_answered(self):
        check_tick_reset(sources())

    def test_stubs(self):
        check_stubs(sources())

    def test_no_core_file_touched(self):
        bg1.check_core_untouched()


@unittest.skipUnless(os.environ.get("BG_MUTATE") == "1", "mutations are run on purpose: BG_MUTATE=1")
class Bg2Mutations(unittest.TestCase):
    """Each check must fail on a broken tree: the mutation is the smallest change that removes the guarantee."""

    def assertFails(self, fn, *args):
        with self.assertRaises(AssertionError):
            fn(*args)

    def mutate(self, key, old, new):
        text = (ROOT / FILES[key]).read_text()
        assert old in text, f"mutation target missing in {FILES[key]}: {old}"
        return sources({key: text.replace(old, new)})

    def test_list_mutation(self):
        self.assertFails(check_slice_list, self.mutate("w4", "ai/playerbot/strategy/values/PvpValues.cpp\n", ""))

    def test_pair_mutation(self):
        self.assertFails(check_pair, self.mutate("turtle_pair", "inline constexpr bool kBgTargetPolicyCompat = false;",
                                                  "inline constexpr bool kBgTargetPolicyCompat = true;"))

    def test_legacy_internals_mutation(self):
        self.assertFails(check_legacy_files_clean, self.mutate("pvptriggers", "return BgBotCarriesFlagCompat(bot);",
                                                                "return bot->getObjectGuid() == ((BattleGroundWS*)bot->GetBattleGround())->GetAllianceFlagPickerGuid();"))

    def test_battlemaster_guard_mutation(self):
        self.assertFails(check_legacy_files_clean, self.mutate("pvpvalues", "#if TB_BG_BATTLEMASTER_PATH\n#include", "#include"))

    def test_policy_switch_mutation(self):
        self.assertFails(check_policy_wired, self.mutate("enemy", "return PickInBattleground(enemyPlayers);", "(void)0;"))

    def test_policy_fact_mutation(self):
        self.assertFails(check_policy_wired, self.mutate("enemy", "candidate.hitsOurCarrier = true;", ""))

    def test_master_target_mutation(self):
        self.assertFails(check_policy_wired, self.mutate("enemy", "masterTargetGuid = master->GetSelectionGuid();",
                                                         'masterTargetGuid = AI_VALUE(Unit*, "master target")->getObjectGuid();'))

    def test_policy_core_include_mutation(self):
        self.assertFails(check_policy_pure, self.mutate("policy", '#include <vector>', '#include "Player.h"'))

    def test_policy_range_mutation(self):
        self.assertFails(check_policy_pure, self.mutate("limits", "kBgNearYd = 30.0f;", "kBgNearYd = 35.0f;"))

    def test_aura_id_mutation(self):
        self.assertFails(check_ids, self.mutate("ids", "kWsgCarrierAuraWarsong = 23333;", "kWsgCarrierAuraWarsong = 23334;"))

    def test_carrier_site_mutation(self):
        self.assertFails(check_carrier_sites, self.mutate("enemy", "candidate.isFlagCarrier = enemy->HasAura(enemyCarrierAura);",
                                                          "candidate.isFlagCarrier = enemy->HasAura(23333);"))

    def test_master_bind_rule_mutation(self):
        self.assertFails(check_master_bind, self.mutate("orders", "cmd::master_bind::Judge(facts)", "true"))

    def test_master_bind_team_mutation(self):
        self.assertFails(check_master_bind, self.mutate("orders", "facts.sameTeam = bot->GetBGTeam() == commander->GetBGTeam();",
                                                        "facts.sameTeam = true;"))

    def test_bg_flag_mutation(self):
        # the bind is let through by the match rule but its bot is never given the bg kind: it joins the record again
        self.assertFails(check_bg_bind_life, self.mutate("orders",
            "kind = cmd::master_bind::IsBgBind(bgFacts) ? cmd::life::Kind::Bg : cmd::life::Kind::Normal;", "kind = cmd::life::Kind::Normal;"))

    def test_bg_handoff_mutation(self):
        # the kinds never reach the check
        self.assertFails(check_bg_bind_life, self.mutate("orders", "boundKinds.push_back(kind);", ""))

    def test_bg_per_bot_kind_mutation(self):
        # the cold review's F2: one kind for the whole command (the first bot's) instead of each bot's own
        self.assertFails(check_bg_bind_life, self.mutate("binding", "result.kind = i < kinds.size() ? kinds[i] : L::Kind::Normal;",
                                                         "result.kind = kinds.empty() ? L::Kind::Normal : kinds[0];"))

    def test_bg_instance_source_mutation(self):
        # the cold review's F2: the instance read from a bot of the record, not the handler's commander instance
        self.assertFails(check_bg_bind_life, self.mutate("orders",
            "cmd::NoteMaster(boundBots, boundKinds, who.leader, commander, commanderInstance);",
            "cmd::NoteMaster(boundBots, boundKinds, who.leader, commander, boundBots.empty() ? 0 : boundBots.front()->GetBattlegroundId());"))

    def test_bg_nothing_bound_mutation(self):
        # a command where nothing bound still touches the record
        self.assertFails(check_bg_bind_life, self.mutate("binding",
            "if (!L::ApplyBind(s.bg, commanderGuid.GetCounter(), results, bgInstance))\n        return;",
            "L::ApplyBind(s.bg, commanderGuid.GetCounter(), results, bgInstance);"))

    def test_bg_record_members_mutation(self):
        # the record is rebuilt from every bot h3 gives the commander, bg members included (they get stored and judged by the world rules)
        self.assertFails(check_bg_bind_life, self.mutate("binding", "std::vector<ObjectGuid> const bound = NormalBoundTo(commander->GetGUID());",
                                                         "std::vector<ObjectGuid> const bound = BoundTo(commander->GetGUID());"))

    def test_bg_release_with_record_mutation(self):
        # a normal release takes the bg members with it
        self.assertFails(check_bg_bind_life, self.mutate("binding",
            "for (ObjectGuid bot : NormalBoundTo(commander))\n    {\n        // A released bot",
            "for (ObjectGuid bot : BoundTo(commander))\n    {\n        // A released bot"))

    def test_bg_tick_mutation(self):
        # the bg members are only checked when some record stands (or not at all): a bot that left keeps its bind (F3)
        self.assertFails(check_bg_bind_life, self.mutate("binding", "    RunBgCheck();\n    if (s.metas.empty())", "    if (s.metas.empty())"))

    def test_bg_logout_mutation(self):
        self.assertFails(check_bg_bind_life, self.mutate("binding", "RunBgCheck(player->GetGUID());", ""))

    def test_bg_party_action_mutation(self):
        self.assertFails(check_bg_bind_life, self.mutate("binding",
            "    s.log.Retain(StandingKeys());\n    sLog.outString(\"TortoiseBots: the battleground bind",
            "    if (Group* raid = InWorld(botGuid) ? InWorld(botGuid)->GetGroup() : nullptr)\n        raid->ChangeLeader(commanderGuid);\n"
            "    s.log.Retain(StandingKeys());\n    sLog.outString(\"TortoiseBots: the battleground bind"))

    def test_key_mutation(self):
        self.assertFails(check_keys, self.mutate("context", 'creators["pvp"]', 'creators["pvp2"]'))

    def test_kit_duel_mutation(self):
        self.assertFails(check_kit, self.mutate("factory", 'combatEngine->removeStrategy("duel");', 'combatEngine->addStrategy("duel");'))

    def test_kit_guard_mutation(self):
        self.assertFails(check_kit, self.mutate("factory",
            "AtScopeExit const battlegroundKit([&] { ApplyBattlegroundCombatKit(player, facade, combatEngine); });\n    if (!player)\n        return;\n",
            "if (!player)\n        return;\n    AtScopeExit const battlegroundKit([&] { ApplyBattlegroundCombatKit(player, facade, combatEngine); });\n"))

    def test_kit_battleground_mutation(self):
        self.assertFails(check_tick_reset, self.mutate("factory", '"pvp", "battleground"', '"pvp"'))

    def test_stub_mutation(self):
        self.assertFails(check_stubs, self.mutate("stubs", "bool SummonAction::FindSpotBeside(",
            "Unit* WorldPvpSeekChecks::FindAssistTarget(PlayerbotAI*, Player*) { return nullptr; }\nbool SummonAction::FindSpotBeside("))

    def test_seek_slice_mutation(self):
        self.assertFails(check_stubs, self.mutate("w4", "ai/playerbot/strategy/values/WorldPvpSeekChecks.cpp\n", ""))


if __name__ == "__main__":
    unittest.main()

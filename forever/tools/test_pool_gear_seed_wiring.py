"""Wiring pins for the one-time pool-bot spawn kit (owner's spawn gear contract, Oct 2026).

The pure rules are runtime/GearSeedMixPolicy.h and runtime/PoolSpawnPolicy.h (tools/test_gear_seed_mix_policy.cpp and
tools/test_pool_spawn_policy.cpp); these pins prove where the live half (forever/src/host/pool/TbPoolSpawn.cpp and
TortoiseBots::gear::SeedGearForTarget) is reached and which caps it keeps:
  * login only: the start level is applied before the level-bound syncs, the kit after them, and the level hook never
    seeds (a bot that spawns below 5 keeps the starter outfit for good);
  * pool bots only, with no master, and once: played time 0 plus the `pool-spawn` row and an in-process mark;
  * the kit uses the quality mix, the effective required level, the pool item-level cap, the gear band, the source gate
    and the blacklist; the same band and gate cover the top-up, `.tb gear set` and `.bot autogear`.

Run:
  python3 -m unittest forever/tools/test_pool_gear_seed_wiring.py
  python3 forever/tools/test_pool_gear_seed_wiring.py --mutate   # must FAIL (exit non-zero)
"""
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPTS = (ROOT / "forever/src/TbScripts.cpp").read_text()
SPAWN = (ROOT / "forever/src/host/pool/TbPoolSpawn.cpp").read_text()
SPAWN_H = (ROOT / "forever/src/host/pool/TbPoolSpawn.h").read_text()
GEAR_GEN = (ROOT / "forever/src/host/gear/TbGearGen.cpp").read_text()
GEAR_GEN_H = (ROOT / "forever/src/host/gear/TbGearGen.h").read_text()
AUTOGEAR = (ROOT / "forever/src/host/TbAutogear.cpp").read_text()
POLICY = (ROOT / "ai/playerbot/policy/item/GearSetPolicy.h").read_text()
MIX = (ROOT / "runtime/GearSeedMixPolicy.h").read_text()
SPAWN_POLICY = (ROOT / "runtime/PoolSpawnPolicy.h").read_text()
CONF = (ROOT / "forever/conf/tortoisebots.conf.dist").read_text()
CONFIG = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
TEMPLATE = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


def check_seed_caps(seed_src):
    start = seed_src.index("GearSetOutcome SeedGearForTarget(")
    end = seed_src.index("\n}\n", start)
    fn = seed_src[start:end]
    for needle in (
        "TortoiseBots::SeedQualityPlan(level)",
        "TortoiseBots::SeedUpgradedSlotCount(plan, rollBelow)",
        "TortoiseBots::SeedChooseUpgradedSlots(upgradedEligible,",
        "TortoiseBots::SeedCandidatePickCount(scores)",
        "TortoiseBots::SeedFallbackQuality(plan.baseQuality)",
        "TortoiseBots::GearItemLevelCap(level,",
        "sPlayerbotAIConfig.randomGearMaxItemLevelAboveLevel",
        "sPlayerbotAIConfig.randomGearMaxLevel",
        "ai::FilterCandidatesForSlot(",
        "SourceGateAllows(c.itemId)",
        "GearBlacklistBlocks(sPlayerbotAIConfig.randomGearBlacklist, c.itemId)",
        "c.quality < plan.baseQuality || c.itemLevel > maxItemLevel",
        "if (slot == EQUIPMENT_SLOT_MAINHAND)\n                    mainHandIsTwoHanded = twoHander;",
        "target->CanEquipNewItem(",
        "target->EquipNewItem(",
        "target->DestroyItem(",
    ):
        if needle not in fn:
            raise AssertionError("SeedGearForTarget missing: %s" % needle)
    # The starter outfit is spent, not stashed; the kit is not a bag-transfer of the core's level-1 items.
    if "StoreNewItem(" in fn:
        raise AssertionError("SeedGearForTarget must not stash the replaced starter outfit")


def check_spawn_host(src):
    for needle in (
        "BotMgr::Instance().IsPoolBot(guid)",
        "BotMgr::Instance().MasterOf(guid)",
        "PoolSpawnSubject(",
        "bot->GetTotalPlayedTime() == 0 && NeedsPoolSpawn(0, StoredSpawn(key))",
        "MarkSpawned(key)",
        "Store().state[key] = SpawnState::No;",
        "NormalizeSpawnLevelRange(",
        "sPlayerbotAIConfig.randomBotStartLevelMin",
        "sPlayerbotAIConfig.randomBotStartLevelMax",
        "bot->GiveLevel(seedLevel)",
        "s_applyingStartLevel = true;",
        "bool ApplyingStartLevel()",
        "gear::SeedGearForTarget(bot, level)",
        "SpawnSeedReady(level)",
        "sConfigMgr->GetBoolDefault(kKeySeed, true)",
    ):
        if needle not in src:
            raise AssertionError("TbPoolSpawn.cpp missing: %s" % needle)


class SpawnGearWiring(unittest.TestCase):
    def test_start_level_is_applied_before_the_login_syncs(self):
        login = body(SCRIPTS, "void OnLogin(Player* player, bool /*firstLogin*/)", "void OnLevelChanged")
        self.assertIn("TortoiseBots::poolspawn::ApplyStartLevel(player);", login)
        self.assertIn("TortoiseBots::poolspawn::SeedSpawnKit(player);", login)
        self.assertLess(login.index("ApplyStartLevel"), login.index('tbhost::autotrain::Sync(player, "login")'))
        self.assertLess(login.index('tbhost::skillspells::Sync(player, "login")'), login.index("SeedSpawnKit"))

    def test_level_hook_never_spawns(self):
        level = body(SCRIPTS, "void OnLevelChanged(Player* player, uint8 /*oldLevel*/)", "// TBF-B1:")
        self.assertNotIn("SeedSpawnKit", level)
        self.assertNotIn("ApplyStartLevel", level)
        self.assertNotIn("SeedGearForTarget", level)
        # GiveLevel during the start level would run the login block's own syncs a second time.
        self.assertIn("TortoiseBots::poolspawn::ApplyingStartLevel()", level)

    def test_pool_only_and_once(self):
        check_spawn_host(SPAWN)
        self.assertIn("bool PoolSpawnSubject(bool isPoolBot, bool hasMaster)", SPAWN_POLICY)
        self.assertIn("return playedTimeSeconds == 0 && !spawnFlag;", SPAWN_POLICY)
        # The persisted row and the in-process set both feed the answer.
        self.assertIn("char const* const kFlag = \"pool-spawn\";", SPAWN)
        self.assertIn("StoredSpawn(key)", SPAWN)
        self.assertIn("s.done.insert(key)", SPAWN)

    def test_kit_caps(self):
        check_seed_caps(GEAR_GEN)
        self.assertIn("GearSetOutcome SeedGearForTarget(Player* target, uint32 levelArg);", GEAR_GEN_H)

    def test_quality_mix_bands(self):
        for line in ("CheckBand(5, 1, 0, 1);", "CheckBand(10, 1, 1, 2);", "CheckBand(15, 1, 2, 3);",
                     "CheckBand(20, 2, 0, 2);", "CheckBand(30, 2, 3, 5);", "CheckBand(50, 2, 6, 9);"):
            self.assertIn(line, (ROOT / "tools/test_gear_seed_mix_policy.cpp").read_text())
        self.assertIn("inline constexpr std::uint32_t kSpawnSeedMinLevel = 5;", MIX)
        self.assertIn("base + 1", MIX)
        # The upgraded slots come from the slots with a candidate, the count is rolled, and the pick is not always the
        # single best-scoring item.
        self.assertIn("SeedChooseUpgradedSlots", MIX)
        self.assertIn("SeedCandidatePickCount", MIX)
        self.assertIn("SeedFallbackQuality", MIX)

    def test_band_and_effective_level(self):
        self.assertIn("kForeverItemBandMinId = 200000", POLICY)
        self.assertIn("kForeverItemBandMaxId = 292000", POLICY)
        self.assertIn("if (!IsItemInGearBand(itemId))", POLICY)
        self.assertIn("TortoiseBots::GearEffectiveRequiredLevel(c.requiredLevel, c.itemLevel)", POLICY)
        self.assertIn("GearEffectiveRequiredLevel(c.requiredLevel, c.itemLevel) <= bracket.maxRequiredLevel",
                      GEAR_GEN)

    def test_same_band_and_gate_on_the_other_generators(self):
        topup = body(GEAR_GEN, "GearSetOutcome TopUpGearForTarget(", "\nGearSetOutcome SeedGearForTarget(")
        self.assertIn("SourceGateAllows(c.itemId)", topup)
        # A 1H replacing a 2H clears the main-hand flag, so the off hand is offered again.
        self.assertIn("if (slot == EQUIPMENT_SLOT_MAINHAND)\n                    mainHandIsTwoHanded = twoHander;",
                      topup)
        self.assertIn("SourceGateAllows(c.itemId)",
                      body(GEAR_GEN, "GearSetOutcome SetGearForTarget(", "\n}\n"))
        self.assertIn("gear::SourceGateAllows(c.itemId)", AUTOGEAR)

    def test_start_level_defaults(self):
        # Forever's code fallback is the guarded constant; the generated config ships 1.
        self.assertRegex(CONFIG, r"kRandomBotStartLevelMinDefault = 1;")
        self.assertRegex(CONFIG, r'"AiPlayerbot\.RandomBotStartLevelMax",\s*60\)')
        self.assertRegex(TEMPLATE, r"(?m)^AiPlayerbot\.RandomBotStartLevelMin = 5$")
        self.assertRegex(TEMPLATE, r"(?m)^AiPlayerbot\.RandomBotStartLevelMax = 60$")
        self.assertIn('"AiPlayerbot.RandomBotStartLevelMin": "1",',
                      (ROOT / "forever/tools/gen_aiplayerbot_conf.py").read_text())
        self.assertRegex((ROOT / "forever/conf/aiplayerbot.conf.dist").read_text(),
                         r"(?m)^AiPlayerbot\.RandomBotStartLevelMin = 1$")

    def test_spawn_key_is_shipped_and_read(self):
        self.assertIn("TortoiseBots.Pool.GearOnCreate = 1", CONF)
        self.assertIn('char const* const kKeySeed = "TortoiseBots.Pool.GearOnCreate";', SPAWN)
        self.assertIn("TortoiseBots::poolspawn::LoadConfig();", SCRIPTS)
        self.assertIn("TortoiseBots::poolspawn::LoadAtStartup();", SCRIPTS)
        self.assertIn("char const* const kCreate =", SPAWN)
        self.assertIn("tortoise_bots_player_flag", SPAWN)

    def test_pure_tests_compile_and_pass(self):
        gxx = shutil.which("g++")
        if not gxx:
            self.skipTest("no g++")
        for name in ("test_gear_seed_mix_policy.cpp", "test_pool_spawn_policy.cpp"):
            exe = pathlib.Path(tempfile.mkdtemp()) / name.replace(".cpp", "")
            build = subprocess.run(
                [gxx, "-std=c++17", "-Wall", "-Wextra", "-I", str(ROOT), str(ROOT / "tools" / name), "-o", str(exe)],
                capture_output=True, text=True, timeout=300)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=120)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("PASS:", run.stdout)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        check_seed_caps(GEAR_GEN.replace("SourceGateAllows(c.itemId)", "true"))
        sys.exit(0)
    unittest.main()

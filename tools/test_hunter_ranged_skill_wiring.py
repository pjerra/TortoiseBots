"""Wiring pins for the hunter ranged weapon skill seed (TBF live finding, 2026-11-25).

Every Undead (5) and Gnome (7) hunter on the test realm had none of Bows (45),
Guns (46) or Crossbows (226), while every other race has one from its class-3
CharacterLoadout row and SkillRaceClassInfo availability-1 row (both missing for
those new race/class pairs in the 1.60 client). Without the skill the spawn kit's
ranged candidate is refused and Auto Shot never starts.

The skill seed lives in the skill-rewarded spell pass, which the login hook runs
before SeedSpawnKit: the kit can only equip a ranged weapon the bot is proficient
with, so the order is the fix.

Run:
  python3 tools/test_hunter_ranged_skill_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

PURE = (ROOT / "forever/src/host/TbAutoSkillSpellsPure.h").read_text()
SKILLS = (ROOT / "forever/src/host/TbAutoSkillSpells.cpp").read_text()
SCRIPTS = (ROOT / "forever/src/TbScripts.cpp").read_text()


def function_body(text, signature):
    start = text.index(signature)
    open_brace = text.index("{", start)
    depth = 0
    for i in range(open_brace, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[open_brace:i + 1]
    raise AssertionError("unbalanced body of " + signature)


def check_pure(pure):
    if "inline bool HunterRangedSkillOwed(" not in pure:
        raise AssertionError("TbAutoSkillSpellsPure.h misses HunterRangedSkillOwed")
    if "inline std::uint32_t HunterRangedSkill(" not in pure:
        raise AssertionError("TbAutoSkillSpellsPure.h misses HunterRangedSkill")
    body = function_body(pure, "inline std::uint32_t HunterRangedSkill(std::uint32_t rangedWeaponSkill)")
    # 45 Bows is the fallback (the starter Worn Shortbow); gun and crossbow keep their own line.
    for needle in ("case 46:  return 46;", "case 226: return 226;", "default:  return 45;"):
        if needle not in body:
            raise AssertionError("HunterRangedSkill misses " + needle)
    owed = function_body(pure, "inline bool HunterRangedSkillOwed(")
    if "PlayerClass::Hunter" not in owed:
        raise AssertionError("HunterRangedSkillOwed does not gate on the hunter class")
    for needle in ("!bowsSkill", "!gunsSkill", "!crossbowsSkill"):
        if needle not in owed:
            raise AssertionError("HunterRangedSkillOwed does not require the other ranged skills to be missing")


def check_seed(skills):
    seed = function_body(skills, "void EnsureHunterRangedSkill(Player* bot)")
    if "GetPureSkillValue(SKILL_BOWS)" not in seed or "GetPureSkillValue(SKILL_GUNS)" not in seed \
            or "GetPureSkillValue(SKILL_CROSSBOWS)" not in seed:
        raise AssertionError("EnsureHunterRangedSkill does not read the three ranged weapon skills")
    if "HunterRangedSkillOwed(" not in seed:
        raise AssertionError("EnsureHunterRangedSkill does not ask the pure rule")
    if "GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED)" not in seed:
        raise AssertionError("EnsureHunterRangedSkill does not read the ranged slot")
    if "GetSkill()" not in seed:
        raise AssertionError("EnsureHunterRangedSkill does not read the equipped weapon's skill")
    if "bot->SetSkill(skill, 0, 1, maxValue);" not in seed:
        raise AssertionError("EnsureHunterRangedSkill does not seed the skill at the core's create value (1/cap)")

    sync = function_body(skills, "std::uint32_t Sync(Player* bot, char const* why)")
    if "EnsureHunterRangedSkill(bot);" not in sync:
        raise AssertionError("Sync does not seed the hunter ranged skill")
    if sync.index("EnsureHunterRangedSkill(bot);") > sync.index("for (SkillLineAbilityEntry const* ability : RewardedRows())"):
        raise AssertionError("the ranged skill must be seeded before the skill-rewarded spell loop")


def check_login_order(scripts):
    login = function_body(scripts, "void OnLogin(Player* player, bool /*firstLogin*/) override")
    sync_at = login.index('tbhost::skillspells::Sync(player, "login");')
    kit_at = login.index("TortoiseBots::poolspawn::SeedSpawnKit(player);")
    if sync_at > kit_at:
        raise AssertionError("the spawn kit runs before the hunter ranged skill is seeded")
    # The starter bow needs the skill too: the post-kit ammo pass asks for it again.
    if "TortoiseBots::p11::EnsureHunterStarterAmmo(player);" not in login[kit_at:]:
        raise AssertionError("the post-kit ammo pass must stay after SeedSpawnKit")


class Wiring(unittest.TestCase):
    def test_pure_rule(self):
        check_pure(PURE)

    def test_seed_wiring(self):
        check_seed(SKILLS)

    def test_login_order(self):
        check_login_order(SCRIPTS)

    def test_seed_after_the_loop_is_caught(self):
        moved = SKILLS.replace("    EnsureHunterRangedSkill(bot);\n\n    for (SkillLineAbilityEntry const* ability",
                               "    for (SkillLineAbilityEntry const* ability")
        moved = moved.replace("    if (taught || kept)", "    EnsureHunterRangedSkill(bot);\n    if (taught || kept)")
        with self.assertRaises(AssertionError):
            check_seed(moved)


if __name__ == "__main__":
    unittest.main()

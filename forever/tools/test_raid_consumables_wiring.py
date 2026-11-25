#!/usr/bin/env python3
"""Wiring pins for #22 round 1: the raid-consumable table, the mode gate and the `.tb state` read-outs.

The Codex reviews of PR #291 found the holes this test keeps shut, and the pure rules are exercised by
forever/tools/test_raid_consumable_policy.cpp, which is built and run here (nothing else compiles it):
  * a config value outside 0, 1 and 2 is parsed by the policy and logged as an error: it stays off, so a typo never turns
    on the item-free casts (the old host formula mapped every value above 1 to free);
  * a free cast takes a category, not a generated row: the host asks the checked index for the row of the bot's own role
    and category, so a row the startup check dropped can never be cast or hand out the wrong buff;
  * a free cast refuses a category that is not due, and the out-of-combat gate is a pure policy function: a flask is never
    cast over a held elixir, an elixir never over a held flask, and the combat guard cannot be dropped without a failure;
  * a slot a role has no item for is declared in the table and printed as `consnotcovered=`, so an incomplete role is never
    read as ready; the elixir slot is a flask row, or both elixir rows, or a declared leg (the generator's matrix test);
  * the weapon slot means the table's own temporary enchantment (a rogue poison or a shaman imbue does not count), and a
    stone or an oil only goes on the weapon family the table names.

Run: python3 -m unittest forever/tools/test_raid_consumables_wiring.py
     python3 forever/tools/test_raid_consumables_wiring.py --mutate   # must pass: every mutation is caught
"""
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
POLICY = "ai/playerbot/policy/raid/RaidConsumablePolicy.h"
POLICY_TEST = "forever/tools/test_raid_consumable_policy.cpp"
HOST = "forever/src/host/TbRaidConsumables.cpp"
HOST_H = "forever/src/host/TbRaidConsumables.h"
STATE = "forever/src/host/TbPartyState.cpp"
COMPAT_F = "forever/src/compat/TbCompat_RaidCons.h"
COMPAT_T = "ai/cmangos-compat-raidcons.h"
CSV = "forever/data/raid/consumables.csv"
CONF = "forever/conf/tortoisebots.conf.dist"

SOURCES = (POLICY, POLICY_TEST, HOST, HOST_H, STATE, COMPAT_F, COMPAT_T, CSV, CONF)


def read(rel):
    return (ROOT / rel).read_text()


def sources():
    return {rel: read(rel) for rel in SOURCES}


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
    raise AssertionError("unbalanced braces after %r" % signature)


def pure_test_builds_and_passes(policy_text=None):
    """Compile and run the pure policy test; with `policy_text`, build against that header instead of the tree's."""
    gxx = shutil.which("g++")
    if not gxx:
        raise unittest.SkipTest("no g++")
    tmp = pathlib.Path(tempfile.mkdtemp())
    include = str(ROOT)
    if policy_text is not None:
        header = tmp / POLICY
        header.parent.mkdir(parents=True)
        header.write_text(policy_text)
        include = str(tmp)
    exe = tmp / "test_raid_consumable_policy"
    build = subprocess.run(
        [gxx, "-std=c++17", "-O1", "-Wall", "-Wextra", "-I", include, str(ROOT / POLICY_TEST), "-o", str(exe)],
        capture_output=True, text=True, timeout=300)
    if build.returncode != 0:
        return "build failed:\n" + build.stdout + build.stderr
    run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=120)
    if run.returncode != 0:
        return "case failed:\n" + run.stdout + run.stderr
    return ""


def check_pure_test(src):
    assert pure_test_builds_and_passes() == "", "the pure policy test does not pass"


def check_policy_owns_the_gates(src):
    text = src[POLICY]
    assert "inline bool ModeFromInt(std::int32_t value, Mode& mode)" in text
    assert "mode = Mode::Off;\n    return false;" in text
    assert "inline bool FreeCastStateOk(Mode mode, bool mapAllowed, bool alive, bool inCombat)" in text
    assert "return mode == Mode::Free && mapAllowed && alive && !inCombat;" in text
    assert "inline RowView const* DueFreeCastRow(Role role, Category category, std::uint32_t level," in text
    assert "if (!due)\n        return nullptr;" in text
    assert "inline bool WeaponFits(WeaponFamily family, std::uint32_t subClass)" in text
    assert "inline bool WeaponEnchantHeld(std::uint32_t mainHandEnchant" in text
    assert "inline std::vector<std::uint8_t> NotCoveredCategories(Role role, NotCovered const* declared" in text


def check_config_value_parsed_by_policy(src):
    body = func(src[HOST], "void LoadConfig()")
    assert "ai::raidcons::ModeFromInt(mode, config.mode)" in body
    assert "LOG_LEVEL_ERROR" in body
    assert "TortoiseBots.RaidConsumables = {} is not a mode" in body
    assert "? Mode::Free" not in body


def check_free_cast_takes_a_category_and_an_index_row(src):
    assert "bool ApplyFree(Player* bot, ai::raidcons::Category category);" in src[HOST_H]
    body = func(src[HOST], "bool ApplyFree(Player* bot, ai::raidcons::Category category)")
    assert "ai::raidcons::DueFreeCastRow(role, category, bot->GetLevel()" in body
    assert "indexViews.data()" in body
    # The generated table's cells never reach the casting boundary: a dropped row cannot be cast.
    assert "tbdata::raidcons::ConsumableRow" not in body
    assert "CategoryFromWord" not in body


def check_combat_guard_is_the_pure_gate(src):
    body = func(src[HOST], "bool ApplyFree(Player* bot, ai::raidcons::Category category)")
    assert "ai::raidcons::FreeCastStateOk(" in body
    assert "bot->IsAlive(), bot->IsInCombat()" in body
    # The category must be due before the row is chosen: a held flask keeps a battle elixir away and the reverse.
    assert "ai::raidcons::DueFreeCastRow(" in body


def check_index_views_are_built_with_the_index(src):
    body = func(src[HOST], "void BuildIndex()")
    assert "indexViews.clear();" in body
    assert "indexViews.push_back(view);" in body
    assert "view.weapon = row.weapon;" in body
    assert "indexViews.data()" in func(src[HOST], "std::vector<std::string> MissingCategories(Player* bot)")


def check_weapon_slot_is_the_tables_own_enchant(src):
    body = func(src[HOST], "bool Held(Player* bot, ai::raidcons::Category category, std::uint32_t& auraId)")
    assert "TableWeaponEnchant(bot) != 0" in body
    table = func(src[HOST], "std::uint32_t TableWeaponEnchant(Player* bot)")
    assert "TEMP_ENCHANTMENT_SLOT" in table
    assert "ai::raidcons::WeaponEnchantHeld(enchant, weaponEnchants.data(), weaponEnchants.size())" in table
    build = func(src[HOST], "void BuildIndex()")
    assert "TemporaryEnchantId(" in build
    assert "weaponEnchants.clear();" in build
    assert "weaponEnchants.push_back(enchant);" in build
    assert "EffectMiscValue" in func(src[HOST], "std::uint32_t TemporaryEnchantId(SpellEntry const& spell)")


def check_weapon_family_gates_both_modes(src):
    bag = func(src[HOST], "std::uint32_t BagItemFor(Player* bot, ai::raidcons::Category category)")
    assert "MainHandFits(bot, row.weapon)" in bag
    fits = func(src[HOST], "bool MainHandFits(Player* bot, ai::raidcons::WeaponFamily family)")
    assert "ai::raidcons::WeaponFits(family, proto->GetSubClass())" in fits
    apply = func(src[HOST], "bool ApplyFree(Player* bot, ai::raidcons::Category category)")
    assert "ai::raidcons::MainHand mainHand;" in apply
    assert "mainHand.subClass = proto ? proto->GetSubClass() : 0;" in apply
    policy = src[POLICY]
    assert "if (category == Category::Weapon && (!mainHand.weapon || !WeaponFits(row.weapon, mainHand.subClass)))" in policy


def check_state_read_out_names_the_uncovered_slots(src):
    state = src[STATE]
    assert '"consnotcovered"' in state
    assert "TortoiseBots::raidcons::NotCoveredCategories(bot)" in state
    assert "std::vector<std::string> NotCoveredCategories(Player* bot);" in src[HOST_H]


def check_compat_pair_takes_a_category(src):
    forever = src[COMPAT_F]
    turtle = src[COMPAT_T]
    assert "inline bool RaidConsApplyFreeCompat(Player* bot, int category)" in forever
    assert "inline bool RaidConsApplyFreeCompat(Player*, int)" in turtle
    assert "return false;" in func(turtle, "inline bool RaidConsApplyFreeCompat(Player*, int)")
    assert "RaidConsRowCompat" not in forever + turtle


def check_table_resolves_every_role_slot(src):
    text = src[CSV]
    rows = [line for line in text.splitlines()
            if line.strip() and not line.startswith("#") and line.startswith(tuple(
                "tank,melee,ranged,caster,healer".split(",")))]
    pairs = {tuple(line.split(",")[:2]) for line in rows if line.split(",")[1] != "reason"}
    declared = {tuple(line.split(",")[:2]) for line in rows if line.split(",")[1] == "reason" and len(line.split(",")) == 3}
    for role in ("tank", "melee", "ranged", "caster", "healer"):
        # The documented loadout: a flask, or a battle and a guardian elixir; a food buff; a weapon stone or oil. Each is a
        # row or a declared gap, and the elixir slot is a flask row or both elixir rows or the missing leg declared.
        assert (role, "flask") in pairs or ((role, "battle") in pairs and (role, "guardian") in pairs) or \
            any((role, leg) in declared for leg in ("battle", "guardian")), role
        for slot in ("food", "weapon"):
            assert (role, slot) in pairs or (role, slot) in declared, "%s/%s" % (role, slot)
    # The R1 fix: a melee or hunter bot holding only its battle elixir must see the guardian slot again.
    for role in ("melee", "ranged"):
        assert (role, "guardian") in pairs, "%s/guardian has no row" % role
        assert (role, "battle") in pairs, "%s/battle has no row" % role


def check_table_names_the_weapon_family(src):
    text = src[CSV]
    header = [line for line in text.splitlines() if line.startswith("role,category")]
    assert header and header[0] == "role,category,item_id,spell_id,aura_id,min_level,weapon,note"
    assert "melee,weapon,18262,22756,0,50,edged," in text
    assert "melee,weapon,12404,16138,0,35,edged," in text
    assert "caster,weapon,20749,25122,0,45,any," in text
    assert "healer,weapon,20748,25123,0,45,any," in text


def check_conf_documents_the_modes_and_the_read_out(src):
    conf = src[CONF]
    # The description is wrapped, so compare its words, not its lines.
    words = " ".join(conf.replace("#", " ").split())
    assert "TortoiseBots.RaidConsumables = 0" in conf
    assert "A value outside 0, 1 and 2 is a config error" in words
    assert "consnotcovered=" in conf


CHECKS = {
    "pure-test": check_pure_test,
    "policy-owns-the-gates": check_policy_owns_the_gates,
    "config-value": check_config_value_parsed_by_policy,
    "free-cast-index": check_free_cast_takes_a_category_and_an_index_row,
    "combat-guard": check_combat_guard_is_the_pure_gate,
    "index-views": check_index_views_are_built_with_the_index,
    "held-weapon-enchant": check_weapon_slot_is_the_tables_own_enchant,
    "weapon-family": check_weapon_family_gates_both_modes,
    "state-read-out": check_state_read_out_names_the_uncovered_slots,
    "compat-pair": check_compat_pair_takes_a_category,
    "table-slots": check_table_resolves_every_role_slot,
    "table-weapon-family": check_table_names_the_weapon_family,
    "conf": check_conf_documents_the_modes_and_the_read_out,
}


class RaidConsumablesWiring(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.src = sources()

    def test_pure_test_compiles_and_passes(self):
        check_pure_test(self.src)

    def test_the_policy_owns_the_gates(self):
        check_policy_owns_the_gates(self.src)

    def test_the_config_value_is_parsed_by_the_policy_and_logged(self):
        check_config_value_parsed_by_policy(self.src)

    def test_the_free_cast_takes_a_category_and_casts_an_index_row(self):
        check_free_cast_takes_a_category_and_an_index_row(self.src)

    def test_the_combat_guard_is_the_pure_gate(self):
        check_combat_guard_is_the_pure_gate(self.src)

    def test_the_index_views_are_built_with_the_index(self):
        check_index_views_are_built_with_the_index(self.src)

    def test_the_weapon_slot_is_the_tables_own_enchant(self):
        check_weapon_slot_is_the_tables_own_enchant(self.src)

    def test_the_weapon_family_gates_both_modes(self):
        check_weapon_family_gates_both_modes(self.src)

    def test_the_state_read_out_names_the_uncovered_slots(self):
        check_state_read_out_names_the_uncovered_slots(self.src)

    def test_the_compat_pair_takes_a_category(self):
        check_compat_pair_takes_a_category(self.src)

    def test_the_table_resolves_every_role_slot(self):
        check_table_resolves_every_role_slot(self.src)

    def test_the_table_names_the_weapon_family(self):
        check_table_names_the_weapon_family(self.src)

    def test_the_conf_documents_the_modes_and_the_read_out(self):
        check_conf_documents_the_modes_and_the_read_out(self.src)


# (name, check, source) -> a mutation of one source that the check must reject. The combat guard is also removed from the
# pure policy function, and the pure test must then fail to build or fail a case.
MUTATIONS = (
    ("host-no-combat", "combat-guard", HOST,
     "ai::raidcons::FreeCastStateOk(ModeFor(bot), MapAllows(bot), bot->IsAlive(), bot->IsInCombat())",
     "ai::raidcons::FreeCastStateOk(ModeFor(bot), MapAllows(bot), true, false)"),
    ("host-held-any-enchant", "held-weapon-enchant", HOST,
     "return TableWeaponEnchant(bot) != 0;",
     "return MainHandOf(bot) && MainHandOf(bot)->GetEnchantmentId(TEMP_ENCHANTMENT_SLOT) != 0;"),
    ("host-bag-ignores-family", "weapon-family", HOST,
     "if (category == Category::Weapon && !MainHandFits(bot, row.weapon))",
     "if (false)"),
    ("policy-no-family", "weapon-family", POLICY,
     "if (category == Category::Weapon && (!mainHand.weapon || !WeaponFits(row.weapon, mainHand.subClass)))",
     "if (category == Category::Weapon && !mainHand.weapon)"),
    ("policy-combat", "pure-test", POLICY,
     "return mode == Mode::Free && mapAllowed && alive && !inCombat;",
     "return mode == Mode::Free && mapAllowed && alive;"),
    ("policy-due", "pure-test", POLICY,
     "    if (!due)\n        return nullptr;\n",
     ""),
)


def run_mutations():
    base = sources()
    failed = []
    for name, check, rel, old, new in MUTATIONS:
        if old not in base[rel]:
            failed.append("%s: target text not found in %s" % (name, rel))
            continue
        mutated = dict(base)
        mutated[rel] = base[rel].replace(old, new, 1)
        try:
            if check == "pure-test":
                problem = pure_test_builds_and_passes(policy_text=mutated[POLICY])
                if not problem:
                    failed.append("%s: mutation was NOT caught (the pure test still passes)" % name)
                continue
            CHECKS[check](mutated)
        except AssertionError:
            continue
        failed.append("%s: mutation was NOT caught" % name)
    if failed:
        print("MUTATE FAIL:\n  " + "\n  ".join(failed))
        return 1
    print("MUTATE OK: %d/%d mutations caught" % (len(MUTATIONS), len(MUTATIONS)))
    return 0


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        sys.exit(run_mutations())
    unittest.main()

#!/usr/bin/env python3
"""Checks for forever/tools/gen_data_tables.py (issues #48 round 1 and #22 round 1: the school and consumable tables).

    python3 -m unittest forever/tools/test_gen_data_tables.py
    python3 forever/tools/test_gen_data_tables.py --mutate   # must FAIL (exit non-zero)

Pins:
  * the shipped headers are what the shipped CSVs generate (the `--check` gate);
  * the shipped spell rows are the five caster kits of the spec, first row per spec = its main school, non-main rows in
    school order (the policy's tie-break reads that order);
  * a bad row is REFUSED with its line number, never silently dropped: an unknown school, an unknown class, a spec with no
    row order 0, a gap in the orders, a duplicate order, a duplicate spell in one spec, a creature row that can never fire
    (both masks 0), a mask bit outside 0..6, and a spell, name or source carrying a double quote or a backslash (the header
    writes those cells into C++ string literals, which cannot hold them unescaped);
  * the shipped consumable rows are the fifteen seeds of the spec: three flasks, five elixirs, three food buffs and four
    weapon stones/oils, with the item, spell and aura ids read from the pin's stores; every weapon row carries aura 0 (its
    effect is a temporary enchantment) and every other row a real aura; an unknown role or category, an item or spell id
    that is not a number, a level outside 1..60, a duplicate role and item, a missing or quoted note, a short row and a
    table with no rows are all refused with their line number.
  * the not-covered section: every role resolves a food and a weapon slot with a row or a declaration (a tank's food and
    weapon, a hunter's weapon and a healer's food are declared today), a pair is one or the other, never both, and a
    declaration needs a known role and category and a reason, one line per pair.
"""
import pathlib
import subprocess
import sys
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE))

import gen_data_tables as gen  # noqa: E402

NUKES = ROOT / "forever/data/combat/school-nukes.csv"
CREATURES = ROOT / "forever/data/combat/creature-schools.csv"
OUT = ROOT / "forever/src/host/data/TbSchoolData.gen.h"

CONSUMABLES = ROOT / "forever/data/raid/consumables.csv"
CONSUMABLES_OUT = ROOT / "forever/src/host/data/TbConsumableData.gen.h"

HEADER = "class,spec,school,spell_name,order\n"
CONSUMABLE_HEADER = "role,category,item_id,spell_id,aura_id,min_level,weapon,note\n"
NOT_COVERED_HEADER = "role,category,reason\n"

# The twenty seeds: role, category, item, spell, aura, level, weapon family.
SEEDS = [
    ("tank", "flask", 13510, 17626, 17626, 50, ""),
    ("melee", "flask", 13510, 17626, 17626, 50, ""),
    ("ranged", "flask", 13510, 17626, 17626, 50, ""),
    ("caster", "flask", 13512, 17628, 17628, 50, ""),
    ("healer", "flask", 13511, 17627, 17627, 50, ""),
    ("melee", "battle", 13452, 17538, 17538, 46, ""),
    ("ranged", "battle", 13452, 17538, 17538, 46, ""),
    ("melee", "battle", 9206, 11405, 11405, 48, ""),
    ("caster", "battle", 13454, 17539, 17539, 47, ""),
    ("tank", "guardian", 13445, 11348, 11348, 43, ""),
    ("melee", "guardian", 13445, 11348, 11348, 43, ""),
    ("ranged", "guardian", 13445, 11348, 11348, 43, ""),
    ("healer", "guardian", 20007, 24363, 24363, 40, ""),
    ("melee", "food", 20452, 1248401, 1248422, 45, ""),
    ("ranged", "food", 18254, 1248396, 1248421, 35, ""),
    ("caster", "food", 13931, 1249513, 1249520, 35, ""),
    ("melee", "weapon", 18262, 22756, 0, 50, "edged"),
    ("melee", "weapon", 12404, 16138, 0, 35, "edged"),
    ("caster", "weapon", 20749, 25122, 0, 45, "any"),
    ("healer", "weapon", 20748, 25123, 0, 45, "any"),
]


def nukes(text):
    return gen.parse_nukes(text)


def creatures(text):
    return gen.parse_creatures(text)


def consumables(text):
    return gen.parse_consumables(text)


def not_covered(text):
    return gen.parse_not_covered(text)


def consumable_row(**cells):
    """One consumable CSV row with the cells named, the rest taken from a valid melee battle row."""
    base = {"role": "melee", "category": "battle", "item_id": "13452", "spell_id": "17538", "aura_id": "17538",
            "min_level": "46", "weapon": "", "note": "Elixir of the Mongoose"}
    base.update({key: str(value) for key, value in cells.items()})
    return ",".join(base[column] for column in gen.CONSUMABLE_COLUMNS) + "\n"


class ShippedData(unittest.TestCase):
    def test_check_is_clean(self):
        result = subprocess.run([sys.executable, str(HERE / "gen_data_tables.py"), "--check"],
                                cwd=str(ROOT), capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_specs_are_the_five_caster_kits(self):
        specs = {(row.classId, row.spec) for row in nukes(NUKES.read_text())}
        self.assertEqual(specs, {
            (gen.CLASS_MAGE, "fire"), (gen.CLASS_MAGE, "frost"), (gen.CLASS_MAGE, "arcane"),
            (gen.CLASS_WARLOCK, "affliction"), (gen.CLASS_WARLOCK, "demonology"), (gen.CLASS_WARLOCK, "destruction"),
            (gen.CLASS_PRIEST, "shadow"), (gen.CLASS_DRUID, "balance"), (gen.CLASS_SHAMAN, "elemental"),
        })

    def test_main_row_is_the_specs_school(self):
        # The spec's data line: a fire mage's main is fire, a shadow priest's is shadow, an elemental shaman's is nature.
        main = {}
        for row in nukes(NUKES.read_text()):
            if row.order == 0:
                main.setdefault((row.classId, row.spec), row.school)
        self.assertEqual(main[(gen.CLASS_MAGE, "fire")], "fire")
        self.assertEqual(main[(gen.CLASS_MAGE, "frost")], "frost")
        self.assertEqual(main[(gen.CLASS_MAGE, "arcane")], "arcane")
        self.assertEqual(main[(gen.CLASS_WARLOCK, "destruction")], "shadow")
        self.assertEqual(main[(gen.CLASS_PRIEST, "shadow")], "shadow")
        self.assertEqual(main[(gen.CLASS_DRUID, "balance")], "nature")
        self.assertEqual(main[(gen.CLASS_SHAMAN, "elemental")], "nature")

    def test_non_main_rows_are_in_school_order(self):
        # The policy breaks a tie on the lower school index, so the CSV must list the fallbacks in that order or the two
        # rules disagree.
        by_spec = {}
        for row in nukes(NUKES.read_text()):
            by_spec.setdefault((row.classId, row.spec), []).append(row)
        for key, rows in by_spec.items():
            fallbacks = [gen.SCHOOLS.index(row.school) for row in rows if row.order > 0]
            self.assertEqual(fallbacks, sorted(fallbacks), key)

    def test_spell_names_are_the_spec_named_ones(self):
        spells = {row.spell for row in nukes(NUKES.read_text())}
        self.assertEqual(spells, {"Fireball", "Frostbolt", "Arcane Missiles", "Shadow Bolt", "Searing Pain",
                                  "Mind Blast", "Smite", "Wrath", "Starfire", "Lightning Bolt", "Flame Shock",
                                  "Frost Shock"})

    def test_creature_table_ships_header_only(self):
        self.assertEqual(creatures(CREATURES.read_text()), [])

    def test_header_carries_the_rows(self):
        text = OUT.read_text()
        self.assertIn("Generated by forever/tools/gen_data_tables.py", text)
        self.assertIn('"fire", ' + str(gen.SCHOOLS.index("fire")) + ', "Fireball"', text)
        self.assertIn("kCreatureCount = 0", text)


class ShippedConsumables(unittest.TestCase):
    def test_the_twenty_seeds(self):
        got = [(row.role, row.category, row.item, row.spell, row.aura, row.minLevel, row.weapon) for row in consumables(
            CONSUMABLES.read_text())]
        self.assertEqual(got, SEEDS)

    def test_weapon_rows_have_no_aura_and_a_family(self):
        for row in consumables(CONSUMABLES.read_text()):
            if row.category == "weapon":
                self.assertEqual(row.aura, 0, row.note)
                self.assertIn(row.weapon, gen.WEAPON_FAMILIES, row.note)
            else:
                self.assertNotEqual(row.aura, 0, row.note)
                self.assertEqual(row.weapon, "", row.note)

    def test_the_stone_is_edged_and_the_oils_go_on_any_weapon(self):
        families = {(row.role, row.category, row.item): row.weapon for row in consumables(CONSUMABLES.read_text())}
        self.assertEqual(families[("melee", "weapon", 18262)], "edged")
        self.assertEqual(families[("melee", "weapon", 12404)], "edged")
        self.assertEqual(families[("caster", "weapon", 20749)], "any")
        self.assertEqual(families[("healer", "weapon", 20748)], "any")

    def test_every_role_and_category_it_uses_is_known(self):
        for row in consumables(CONSUMABLES.read_text()):
            self.assertIn(row.role, gen.ROLES)
            self.assertIn(row.category, gen.CATEGORIES)

    def test_header_carries_the_rows(self):
        text = CONSUMABLES_OUT.read_text()
        self.assertIn("GENERATED FILE: do not edit", text)
        self.assertIn("Written by forever/tools/gen_data_tables.py", text)
        self.assertIn('{ "tank", "flask", 13510u, 17626u, 17626u, 50, "", "Flask of the Titans" }', text)
        self.assertIn('{ "melee", "weapon", 18262u, 22756u, 0u, 50, "edged", ', text)
        self.assertIn("kConsumableCount = sizeof(kConsumables) / sizeof(kConsumables[0])", text)


class ShippedCoverage(unittest.TestCase):
    """The per-role matrix: food, weapon and the elixir slot resolved by a row or declared uncovered, never silently ready."""

    def test_every_role_resolves_food_and_weapon(self):
        text = CONSUMABLES.read_text()
        rows = {(row.role, row.category) for row in consumables(text)}
        declared = {(row.role, row.category) for row in not_covered(text)}
        for role in gen.ROLES:
            for slot in gen.PROMISED_SLOTS:
                self.assertTrue((role, slot) in rows or (role, slot) in declared,
                                "%s has no %s row and no not-covered line" % (role, slot))
                self.assertFalse((role, slot) in rows and (role, slot) in declared,
                                 "%s/%s is both a row and a not-covered line" % (role, slot))

    def test_every_role_resolves_the_elixir_slot(self):
        # A flask row fills both elixir slots; without one the role needs both elixir rows, and a leg the table cannot
        # fill is declared. A role holding only its battle elixir must never read ready.
        text = CONSUMABLES.read_text()
        rows = {(row.role, row.category) for row in consumables(text)}
        declared = {(row.role, row.category) for row in not_covered(text)}
        for role in gen.ROLES:
            if (role, "flask") in rows:
                continue
            missing = [elixir for elixir in gen.ELIXIR_CATEGORIES if (role, elixir) not in rows]
            for elixir in missing:
                self.assertIn((role, elixir), declared,
                              "%s has neither a flask row nor a %s row nor a not-covered line" % (role, elixir))

    def test_the_elixir_pair_the_table_ships(self):
        rows = {(row.role, row.category) for row in consumables(CONSUMABLES.read_text())}
        for role in gen.ROLES:
            self.assertIn((role, "flask"), rows, role)
        for role in ("melee", "ranged"):
            self.assertIn((role, "battle"), rows, role)
            self.assertIn((role, "guardian"), rows, role)

    def test_the_declarations_are_the_gaps_the_table_cannot_fill(self):
        declared = {(row.role, row.category): row.reason for row in not_covered(CONSUMABLES.read_text())}
        self.assertEqual(set(declared), {("tank", "battle"), ("tank", "food"), ("tank", "weapon"),
                                         ("caster", "guardian"), ("ranged", "weapon"),
                                         ("healer", "battle"), ("healer", "food")})
        for reason in declared.values():
            self.assertTrue(reason.strip())

    def test_the_generator_refuses_the_shipped_table_without_its_declarations(self):
        text = CONSUMABLES.read_text()
        rows = consumables(text)
        with self.assertRaises(gen.RowError) as caught:
            gen.check_coverage(rows, [], str(CONSUMABLES))
        self.assertIn("tank has no food row and no not-covered line", str(caught.exception))

    def test_the_generator_refuses_an_elixir_slot_without_a_flask_or_the_pair(self):
        # Only the battle row and no declaration: a bot holding it would read ready while the guardian elixir is missing.
        text = CONSUMABLES.read_text()
        rows = [row for row in consumables(text)
                if (row.role, row.category) not in {("melee", "flask"), ("melee", "guardian")}]
        declared = [row for row in not_covered(text) if (row.role, row.category) != ("melee", "guardian")]
        with self.assertRaises(gen.RowError) as caught:
            gen.check_coverage(rows, declared, str(CONSUMABLES))
        self.assertIn("melee has no flask row, no guardian row and no not-covered line", str(caught.exception))

    def test_a_declared_elixir_leg_resolves_the_slot(self):
        # The table cannot fill the guardian leg: declaring it is the honest resolution (`.tb state` names it), and the
        # declaration is enough for the matrix; only a role with neither shape and no line is refused.
        text = CONSUMABLES.read_text()
        rows = [row for row in consumables(text)
                if (row.role, row.category) not in {("ranged", "flask"), ("ranged", "guardian")}]
        declared = not_covered(text)
        declared.append(gen.NotCoveredRow("ranged", "guardian", "the table seeds no ranged guardian elixir", 1))
        gen.check_coverage(rows, declared, str(CONSUMABLES))

    def test_header_carries_the_declarations(self):
        text = CONSUMABLES_OUT.read_text()
        self.assertIn("std::array<NotCoveredRow, 7> kNotCovered", text)
        self.assertIn('{ "ranged", "weapon", "a bow or gun takes no temporary weapon enchantment" }', text)
        self.assertIn("kNotCoveredCount = kNotCovered.size()", text)


class BadConsumableRows(unittest.TestCase):
    def assertRefused(self, text, needle, line=2):
        with self.assertRaises(gen.RowError) as caught:
            consumables(text)
        self.assertIn(needle, str(caught.exception))
        self.assertIn("line %d" % line, str(caught.exception))

    def test_unknown_role(self):
        self.assertRefused(CONSUMABLE_HEADER + consumable_row(role="rogue"), "unknown role 'rogue'")

    def test_unknown_category(self):
        self.assertRefused(CONSUMABLE_HEADER + consumable_row(category="potion"), "unknown category 'potion'")

    def test_item_id_not_a_number(self):
        self.assertRefused(CONSUMABLE_HEADER + consumable_row(item_id="elixir"), "item_id 'elixir' is not a number")

    def test_zero_item_id_refused(self):
        self.assertRefused(CONSUMABLE_HEADER + consumable_row(item_id="0"), "item_id 0 is outside 1..4294967295")

    def test_zero_spell_id_refused(self):
        self.assertRefused(CONSUMABLE_HEADER + consumable_row(spell_id="0"), "spell_id 0 is outside 1..4294967295")

    def test_level_outside_the_rows(self):
        self.assertRefused(CONSUMABLE_HEADER + consumable_row(min_level="0"), "min_level 0 is outside 1..60")
        self.assertRefused(CONSUMABLE_HEADER + consumable_row(min_level="61"), "min_level 61 is outside 1..60")

    def test_duplicate_role_and_item(self):
        self.assertRefused(CONSUMABLE_HEADER + consumable_row() + consumable_row(), "duplicate row for melee", line=3)

    def test_note_with_a_quote_is_refused(self):
        self.assertRefused(CONSUMABLE_HEADER + consumable_row(note='Elixir of the "Mongoose"'),
                           "contains a double quote or a backslash")

    def test_empty_note_is_refused(self):
        self.assertRefused(CONSUMABLE_HEADER + consumable_row(note=""), "empty note")

    def test_short_row(self):
        self.assertRefused(CONSUMABLE_HEADER + "melee,battle,13452,17538,17538,46\n", "6 columns, expected 8")

    def test_unknown_weapon_family(self):
        self.assertRefused(CONSUMABLE_HEADER + consumable_row(category="weapon", aura_id="0", weapon="sharp"),
                           "unknown weapon family 'sharp'")

    def test_weapon_row_without_a_family(self):
        self.assertRefused(CONSUMABLE_HEADER + consumable_row(category="weapon", aura_id="0", weapon=""),
                           "unknown weapon family ''")

    def test_weapon_family_on_a_non_weapon_row(self):
        self.assertRefused(CONSUMABLE_HEADER + consumable_row(weapon="edged"),
                           "weapon family 'edged' on a battle row")

    def test_wrong_header(self):
        with self.assertRaises(gen.RowError) as caught:
            consumables("role,category,item\n")
        self.assertIn("no header row", str(caught.exception))

    def test_table_without_rows(self):
        with self.assertRaises(gen.RowError) as caught:
            consumables(CONSUMABLE_HEADER)
        self.assertIn("no rows", str(caught.exception))

    def test_same_item_for_two_roles_is_allowed(self):
        rows = consumables(CONSUMABLE_HEADER + consumable_row() + consumable_row(role="ranged"))
        self.assertEqual([row.role for row in rows], ["melee", "ranged"])


class BadNotCoveredLines(unittest.TestCase):
    """The not-covered section validates like the rows: known words, a reason, one line per pair."""

    def assertRefused(self, text, needle, line=4):
        with self.assertRaises(gen.RowError) as caught:
            not_covered(text)
        self.assertIn(needle, str(caught.exception))
        self.assertIn("line %d" % line, str(caught.exception))

    def table(self, line):
        return CONSUMABLE_HEADER + consumable_row() + NOT_COVERED_HEADER + line

    def test_unknown_role(self):
        self.assertRefused(self.table("rogue,food,no rogue food\n"), "unknown role 'rogue'")

    def test_unknown_category(self):
        self.assertRefused(self.table("tank,potion,no potion\n"), "unknown category 'potion'")

    def test_empty_reason(self):
        self.assertRefused(self.table("tank,food,\n"), "empty reason")

    def test_reason_with_a_quote_is_refused(self):
        self.assertRefused(self.table('tank,food,the "food" gap\n'),
                           "contains a double quote or a backslash")

    def test_short_line(self):
        self.assertRefused(self.table("tank,food\n"), "2 columns, expected 3")

    def test_duplicate_line(self):
        self.assertRefused(self.table("tank,food,no tank food\ntank,food,no tank food\n"),
                           "duplicate not-covered line for tank/food", line=5)

    def test_repeated_header(self):
        self.assertRefused(self.table("") + NOT_COVERED_HEADER, "appears twice", line=4)


class CoverageRefusals(unittest.TestCase):
    """The matrix: a slot is a row or a declaration, never neither and never both."""

    def assertRefused(self, text, needle):
        with self.assertRaises(gen.RowError) as caught:
            gen.check_coverage(gen.parse_consumables(text), gen.parse_not_covered(text))
        self.assertIn(needle, str(caught.exception))

    def test_a_declaration_contradicts_a_row(self):
        text = (CONSUMABLE_HEADER + consumable_row(category="food") + NOT_COVERED_HEADER + "melee,food,no melee food\n")
        self.assertRefused(text, "melee/food is declared not covered but the table has a row for it")

    def test_a_role_without_a_row_or_a_declaration(self):
        text = CONSUMABLES.read_text().replace("tank,food,the table's food buffs are strength/agility/spell power; none is a tank buff\n", "")
        self.assertRefused(text, "tank has no food row and no not-covered line")

    def test_the_generator_fails_instead_of_writing(self):
        with tempfile.TemporaryDirectory() as tmp:
            table = pathlib.Path(tmp) / "consumables.csv"
            table.write_text(CONSUMABLE_HEADER + consumable_row(category="food") + NOT_COVERED_HEADER)
            result = subprocess.run([sys.executable, str(HERE / "gen_data_tables.py"),
                                     "--consumables", str(table),
                                     "--consumables-out", str(pathlib.Path(tmp) / "TbConsumableData.gen.h")],
                                    cwd=str(ROOT), capture_output=True, text=True)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertIn("tank has no food row and no not-covered line", result.stderr)



class BadRows(unittest.TestCase):
    def assertRefused(self, parse, text, needle, line):
        with self.assertRaises(gen.RowError) as caught:
            parse(text)
        self.assertIn(needle, str(caught.exception))
        self.assertIn("line %d" % line, str(caught.exception))

    def test_unknown_school(self):
        self.assertRefused(nukes, HEADER + "mage,fire,fyre,Fireball,0\n", "unknown school 'fyre'", 2)

    def test_unknown_class(self):
        self.assertRefused(nukes, HEADER + "deathknight,unholy,shadow,Death Coil,0\n", "unknown class 'deathknight'", 2)

    def test_spec_without_order_zero(self):
        self.assertRefused(nukes, HEADER + "mage,fire,frost,Frostbolt,1\n", "no row with order 0", 2)

    def test_gap_in_orders(self):
        self.assertRefused(nukes, HEADER + "mage,fire,fire,Fireball,0\nmage,fire,arcane,Arcane Missiles,2\n",
                           "order 2 does not follow order 0", 3)

    def test_duplicate_order(self):
        self.assertRefused(nukes, HEADER + "mage,fire,fire,Fireball,0\nmage,fire,frost,Frostbolt,0\n",
                           "duplicate order 0", 3)

    def test_duplicate_spell_in_a_spec(self):
        self.assertRefused(nukes, HEADER + "mage,fire,fire,Fireball,0\nmage,fire,frost,Fireball,1\n",
                           "duplicate spell 'Fireball'", 3)

    def test_missing_spell_name(self):
        self.assertRefused(nukes, HEADER + "mage,fire,frost,,1\nmage,fire,fire,Fireball,0\n", "empty spell_name", 2)

    def test_wrong_column_count(self):
        self.assertRefused(nukes, HEADER + "mage,fire,fire,Fireball\n", "4 columns, expected 5", 2)

    def test_creature_row_that_can_never_fire(self):
        self.assertRefused(creatures, "entry,name,immune_mask,vuln_mask,source\n"
                           "1234,Some Slime,0,0,scripted hook\n", "both masks are 0", 2)

    def test_creature_mask_bit_outside_a_school(self):
        self.assertRefused(creatures, "entry,name,immune_mask,vuln_mask,source\n"
                           "1234,Some Slime,0x80,0,scripted hook\n", "mask bit outside schools 0..6", 2)

    def test_creature_entry_not_a_number(self):
        self.assertRefused(creatures, "entry,name,immune_mask,vuln_mask,source\n"
                           "slime-1,Some Slime,4,0,scripted hook\n", "entry is not a number", 2)

    def test_creature_row_without_a_source(self):
        self.assertRefused(creatures, "entry,name,immune_mask,vuln_mask,source\n"
                           "1234,Some Slime,4,0,\n", "empty source", 2)

    def test_creature_name_with_a_quote_is_refused(self):
        self.assertRefused(creatures, "entry,name,immune_mask,vuln_mask,source\n"
                           '1234,Some "Slime",4,0,scripted hook\n', "contains a double quote or a backslash", 2)

    def test_creature_source_with_a_backslash_is_refused(self):
        self.assertRefused(creatures, "entry,name,immune_mask,vuln_mask,source\n"
                           "1234,Some Slime,4,0,scripted\\hook\n", "contains a double quote or a backslash", 2)

    def test_spell_name_with_a_quote_is_refused(self):
        self.assertRefused(nukes, HEADER + 'mage,fire,fire,Fire"ball,0\n',
                           "contains a double quote or a backslash", 2)


class CheckGate(unittest.TestCase):
    def test_check_reports_a_stale_header(self):
        with tempfile.TemporaryDirectory() as tmp:
            stale = pathlib.Path(tmp) / "TbSchoolData.gen.h"
            stale.write_text("// hand-edited\n")
            result = subprocess.run([sys.executable, str(HERE / "gen_data_tables.py"), "--check", "--out", str(stale)],
                                    cwd=str(ROOT), capture_output=True, text=True)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertIn("stale", result.stderr)

    def test_write_then_check_passes(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = pathlib.Path(tmp) / "TbSchoolData.gen.h"
            raid = pathlib.Path(tmp) / "TbConsumableData.gen.h"
            subprocess.run([sys.executable, str(HERE / "gen_data_tables.py"), "--out", str(out),
                            "--consumables-out", str(raid)], cwd=str(ROOT), check=True, capture_output=True)
            result = subprocess.run([sys.executable, str(HERE / "gen_data_tables.py"), "--check", "--out", str(out),
                                     "--consumables-out", str(raid)], cwd=str(ROOT), capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


def mutation_proof():
    """A hand edit of either CSV must be caught by the pins above.

    Two edits: a spec's main spell swapped for another, and the mongoose's aura swapped for another buff. Each is written,
    the test file is run, and the CSV is restored; both must fail the run.
    """
    for path, before, after in (
        (NUKES, "mage,frost,frost,Frostbolt,0", "mage,frost,frost,Fireball,0"),
        (CONSUMABLES, "melee,battle,13452,17538,17538,46", "melee,battle,13452,17538,17539,46"),
    ):
        original = path.read_text()
        mutated = original.replace(before, after)
        if mutated == original:
            print("mutation did not change %s" % path.name)
            return 1
        path.write_text(mutated)
        try:
            result = subprocess.run([sys.executable, str(HERE / "test_gen_data_tables.py")], cwd=str(ROOT),
                                    capture_output=True, text=True)
        finally:
            path.write_text(original)
        print(result.stdout[-2000:] + result.stderr[-2000:])
        if result.returncode == 0:
            print("MUTATION NOT CAUGHT: %s" % path.name)
            return 1
        print("mutation caught: %s" % path.name)
    return 0


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        sys.exit(mutation_proof())
    unittest.main()

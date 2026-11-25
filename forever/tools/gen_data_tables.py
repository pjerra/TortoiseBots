#!/usr/bin/env python3
"""Generate the module's compiled-in data headers from the CSVs under forever/data.

The two combat tables (issue #48 round 1) become forever/src/host/data/TbSchoolData.gen.h, the raid-consumable table
(issue #22 round 1) TbConsumableData.gen.h beside it and the raid boss table (issue #20 round 1) TbResistData.gen.h.

    python3 forever/tools/gen_data_tables.py            # write the headers
    python3 forever/tools/gen_data_tables.py --check    # exit 1 with a diff when a header is stale

Inputs (owned by the data collaborator, never this script):
  forever/data/combat/school-nukes.csv      class,spec,school,spell_name,order   the caster specs' nukes
  forever/data/combat/creature-schools.csv  entry,name,immune_mask,vuln_mask,source  the extra statements, header-only
  forever/data/raid/consumables.csv         role,category,item_id,spell_id,aura_id,min_level,weapon,note  one raid
                                            consumable, then a role,category,reason section for the slots the table leaves
                                            uncovered
  forever/data/raid/boss-resist.csv         entry,name,school,evidence_spell,note  a raid boss's damage school

Every row is validated, and a bad one stops the run with `file:line N: <why>` instead of being dropped: a silent drop would
turn a typo into "this spec never switches" (or a boss that never gets its resistance set). The checks are the rules the
host relies on — a known class and school, one main row (order 0) and contiguous orders per spec, no duplicate order or
spell inside a spec, masks that stay inside the seven schools and say something (a 0/0 creature row could never fire), and
for the boss table a known school, a positive evidence spell and one row per boss entry. A cell written into the generated
header is refused when it carries a double quote or a backslash, which a C++ string literal cannot hold.

The sets and the names are C++ constants the host compares against its own: the class ids are the core's CLASS_* values and
the school order is SpellEntry::School's, both spelled the same as forever/src/host/TbSchool.cpp reads them. The consumable
roles and categories are the two enums of ai/playerbot/policy/raid/RaidConsumablePolicy.h, spelled as the CSV spells them; a
`role,item_id` pair may appear once (a row is one item in one role's category) and a weapon row carries aura 0 because its
effect is the main hand's temporary enchantment, not a buff, plus the weapon family the enchantment goes on (`weapon`: edged,
blunt or any; empty on every other category). The consumable table's second section declares the role/category slots it has
no item for; every role must resolve a food slot, a weapon slot and the elixir slot with a row or a declaration, so the
`.tb state` read-out can name an uncovered slot instead of calling the role ready. The elixir slot is a flask row, or both
elixir rows (a held flask fills both slots), and a leg a role has no row for must be declared: a role with a single battle
elixir row and no declaration would read ready while the documented loadout is not met. The boss table's school is the same
client id (1 holy, 2 fire, 3 nature, 4 frost, 5 shadow, 6 arcane).
"""
import argparse
import difflib
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent

NUKES = ROOT / "forever/data/combat/school-nukes.csv"
CREATURES = ROOT / "forever/data/combat/creature-schools.csv"
OUTPUT = ROOT / "forever/src/host/data/TbSchoolData.gen.h"
RESIST = ROOT / "forever/data/raid/boss-resist.csv"
RESIST_OUTPUT = ROOT / "forever/src/host/data/TbResistData.gen.h"

# The core's class ids (SharedDefines.h: CLASS_*), the ones a class switch and the CSV's `class` column share.
CLASSES = {
    "warrior": 1, "paladin": 2, "hunter": 3, "rogue": 4, "priest": 5,
    "shaman": 7, "mage": 8, "warlock": 9, "druid": 11,
}
CLASS_MAGE = CLASSES["mage"]
CLASS_WARLOCK = CLASSES["warlock"]
CLASS_PRIEST = CLASSES["priest"]
CLASS_DRUID = CLASSES["druid"]
CLASS_SHAMAN = CLASSES["shaman"]

# The core's SpellEntry::School order (normal 0 .. arcane 6): the index the host's takenPct array and the masks use.
SCHOOLS = ("normal", "holy", "fire", "nature", "frost", "shadow", "arcane")

NUKES_COLUMNS = ("class", "spec", "school", "spell_name", "order")
CREATURE_COLUMNS = ("entry", "name", "immune_mask", "vuln_mask", "source")
SCHOOL_MASK = (1 << len(SCHOOLS)) - 1

CONSUMABLES_PATH = ROOT / "forever/data/raid/consumables.csv"
CONSUMABLES_OUTPUT = ROOT / "forever/src/host/data/TbConsumableData.gen.h"

# The bot's consumable roles and the exclusivity groups (the two enums of RaidConsumablePolicy.h, spelled as the CSV does).
ROLES = ("tank", "melee", "ranged", "caster", "healer")
CATEGORIES = ("flask", "battle", "guardian", "food", "weapon", "scroll")

CONSUMABLE_COLUMNS = ("role", "category", "item_id", "spell_id", "aura_id", "min_level", "weapon", "note")
NOT_COVERED_COLUMNS = ("role", "category", "reason")

# A weapon row's main-hand family: a sharpening stone goes on a blade, a weightstone on a blunt weapon, an oil on any weapon.
# The other categories carry no family (an empty cell), because their effect is an aura on the bot, not an item enchantment.
WEAPON_FAMILIES = ("edged", "blunt", "any")

# The loadout the policy promises per role: a food buff and a weapon stone or oil, on top of the elixir slot (a flask, or a
# battle and a guardian elixir). The table must resolve each one for every role, or declare it uncovered, so the read-out
# never calls a materially incomplete loadout ready.
PROMISED_SLOTS = ("food", "weapon")
ELIXIR_CATEGORIES = ("battle", "guardian")


class RowError(Exception):
    """A CSV row that breaks a rule; the message carries the file and line."""


class NukeRow:
    __slots__ = ("classId", "spec", "school", "spell", "order", "line")

    def __init__(self, classId, spec, school, spell, order, line):
        self.classId, self.spec, self.school, self.spell, self.order, self.line = \
            classId, spec, school, spell, order, line


class CreatureRow:
    __slots__ = ("entry", "name", "immuneMask", "vulnMask", "source", "line")

    def __init__(self, entry, name, immuneMask, vulnMask, source, line):
        self.entry, self.name, self.immuneMask, self.vulnMask, self.source, self.line = \
            entry, name, immuneMask, vulnMask, source, line


class ConsumableRow:
    __slots__ = ("role", "category", "item", "spell", "aura", "minLevel", "weapon", "note", "line")

    def __init__(self, role, category, item, spell, aura, minLevel, weapon, note, line):
        self.role, self.category, self.item, self.spell = role, category, item, spell
        self.aura, self.minLevel, self.weapon, self.note, self.line = aura, minLevel, weapon, note, line


class NotCoveredRow:
    __slots__ = ("role", "category", "reason", "line")

    def __init__(self, role, category, reason, line):
        self.role, self.category, self.reason, self.line = role, category, reason, line


def rows(text, path, columns):
    """The data rows of a CSV with the comment and blank lines skipped, as (line number, cells)."""
    found = []
    header_seen = False
    for number, raw in enumerate(text.splitlines(), start=1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        cells = [cell.strip() for cell in line.split(",")]
        if not header_seen:
            if tuple(cells) != columns:
                raise RowError("%s:line %d: the header is %s, expected %s" %
                               (path, number, ",".join(cells), ",".join(columns)))
            header_seen = True
            continue
        found.append((number, cells))
    if not header_seen:
        raise RowError("%s:line 1: no header row (%s)" % (path, ",".join(columns)))
    return found


def parse_nukes(text, path="school-nukes.csv"):
    parsed = []
    for number, cells in rows(text, path, NUKES_COLUMNS):
        if len(cells) != len(NUKES_COLUMNS):
            raise RowError("%s:line %d: %d columns, expected %d (%s)" %
                           (path, number, len(cells), len(NUKES_COLUMNS), ",".join(NUKES_COLUMNS)))
        className, spec, school, spell, order = cells
        if className not in CLASSES:
            raise RowError("%s:line %d: unknown class '%s' (one of %s)" %
                           (path, number, className, ", ".join(sorted(CLASSES))))
        if not spec.isalpha() or not spec.islower():
            raise RowError("%s:line %d: spec '%s' is not a lower-case name (fire, shadow, ...)" % (path, number, spec))
        if school not in SCHOOLS:
            raise RowError("%s:line %d: unknown school '%s' (one of %s)" %
                           (path, number, school, ", ".join(SCHOOLS)))
        if not spell:
            raise RowError("%s:line %d: empty spell_name" % (path, number))
        literal_cell(path, number, "spell_name", spell)
        if not order.isdigit():
            raise RowError("%s:line %d: order '%s' is not a number" % (path, number, order))
        parsed.append(NukeRow(CLASSES[className], spec, school, spell, int(order), number))

    by_spec = {}
    for row in parsed:
        by_spec.setdefault((row.classId, row.spec), []).append(row)
    for (classId, spec), spec_rows in by_spec.items():
        spec_rows.sort(key=lambda row: row.order)
        if spec_rows[0].order != 0:
            raise RowError("%s:line %d: spec %s/%s has no row with order 0 (its main school)" %
                           (path, spec_rows[0].line, className_of(classId), spec))
        seen_spells = set()
        for expected, row in enumerate(spec_rows):
            if row.order != expected:
                if row.order < expected:
                    raise RowError("%s:line %d: duplicate order %d of %s/%s" %
                                   (path, row.line, row.order, className_of(classId), spec))
                raise RowError("%s:line %d: order %d does not follow order %d of %s/%s" %
                               (path, row.line, row.order, expected - 1, className_of(classId), spec))
            if row.spell in seen_spells:
                raise RowError("%s:line %d: duplicate spell '%s' in spec %s/%s" %
                               (path, row.line, row.spell, className_of(classId), spec))
            seen_spells.add(row.spell)
    return parsed


def className_of(classId):
    for name, value in CLASSES.items():
        if value == classId:
            return name
    return str(classId)


def parse_mask(path, number, column, text):
    try:
        mask = int(text, 0)
    except ValueError:
        raise RowError("%s:line %d: %s '%s' is not a number" % (path, number, column, text))
    if mask < 0 or mask & ~SCHOOL_MASK:
        raise RowError("%s:line %d: %s %#x has a mask bit outside schools 0..6" % (path, number, column, mask))
    return mask


def literal_cell(path, number, column, text):
    """A cell render() writes into a C++ string literal: refuse what the literal cannot carry unescaped."""
    if '"' in text or "\\" in text:
        raise RowError("%s:line %d: %s '%s' contains a double quote or a backslash; the generated C++ string literal cannot hold it"
                       % (path, number, column, text))
    return text


def parse_creatures(text, path="creature-schools.csv"):
    parsed = []
    for number, cells in rows(text, path, CREATURE_COLUMNS):
        if len(cells) != len(CREATURE_COLUMNS):
            raise RowError("%s:line %d: %d columns, expected %d (%s)" %
                           (path, number, len(cells), len(CREATURE_COLUMNS), ",".join(CREATURE_COLUMNS)))
        entry, name, immune, vuln, source = cells
        if not entry.isdigit() or int(entry) <= 0:
            raise RowError("%s:line %d: entry is not a number '%s'" % (path, number, entry))
        if not name:
            raise RowError("%s:line %d: empty name" % (path, number))
        literal_cell(path, number, "name", name)
        immuneMask = parse_mask(path, number, "immune_mask", immune)
        vulnMask = parse_mask(path, number, "vuln_mask", vuln)
        if immuneMask == 0 and vulnMask == 0:
            raise RowError("%s:line %d: both masks are 0 (the row could never fire)" % (path, number))
        if not source:
            raise RowError("%s:line %d: empty source" % (path, number))
        literal_cell(path, number, "source", source)
        parsed.append(CreatureRow(int(entry), name, immuneMask, vulnMask, source, number))
    seen_entries = {}
    for row in parsed:
        if row.entry in seen_entries:
            raise RowError("%s:line %d: duplicate entry %d (first on line %d)" %
                           (path, row.line, row.entry, seen_entries[row.entry]))
        seen_entries[row.entry] = row.line
    return parsed


def render(nuke_rows, creature_rows):
    """The C++ header text these rows generate, byte for byte."""
    school_index = {name: index for index, name in enumerate(SCHOOLS)}
    out = []
    out.append("// Generated by forever/tools/gen_data_tables.py from forever/data/combat/school-nukes.csv and")
    out.append("// forever/data/combat/creature-schools.csv; do not edit. `--check` fails on a hand edit.")
    out.append("#pragma once")
    out.append("")
    out.append("#include <array>")
    out.append("#include <cstddef>")
    out.append("#include <cstdint>")
    out.append("")
    out.append("namespace TortoiseBots")
    out.append("{")
    out.append("namespace school")
    out.append("{")
    out.append("namespace data")
    out.append("{")
    out.append("// One nuke: the class id, the kit spec name, the school, the spell, and 0 for the spec's main school.")
    out.append("struct NukeRow")
    out.append("{")
    out.append("    std::uint8_t classId;")
    out.append("    char const* spec;")
    out.append("    std::uint8_t school;")
    out.append("    char const* spell;")
    out.append("    std::uint8_t order;")
    out.append("};")
    out.append("")
    out.append("inline constexpr std::array<NukeRow, %d> kNukes = {{" % len(nuke_rows))
    for row in nuke_rows:
        out.append('    { %d, "%s", %d, "%s", %d },' %
                   (row.classId, row.spec, school_index[row.school], row.spell, row.order))
    out.append("}};")
    out.append("inline constexpr std::size_t kNukeCount = %d;" % len(nuke_rows))
    out.append("")
    out.append("// One creature statement: the schools it is immune to and the schools it takes extra damage from, 1 << school.")
    out.append("struct CreatureRow")
    out.append("{")
    out.append("    std::uint32_t entry;")
    out.append("    char const* name;")
    out.append("    std::uint8_t immuneMask;")
    out.append("    std::uint8_t vulnMask;")
    out.append("    char const* source;")
    out.append("};")
    out.append("")
    out.append("inline constexpr std::array<CreatureRow, %d> kCreatures = {{" % len(creature_rows))
    for row in creature_rows:
        out.append('    { %d, "%s", %d, %d, "%s" },' %
                   (row.entry, row.name, row.immuneMask, row.vulnMask, row.source))
    out.append("}};")
    out.append("inline constexpr std::size_t kCreatureCount = %d;" % len(creature_rows))
    out.append("}")
    out.append("}")
    out.append("}")
    out.append("")
    return "\n".join(out)


def sections(text, path, headers):
    """The data rows of a CSV that holds several header-led sections, keyed by the header tuple.

    Every section header must appear once; a line before the first header, or a repeated header, is an error. A section the
    file does not carry maps to an empty list. Each row keeps its line number.
    """
    found = {header: [] for header in headers}
    seen = set()
    current = None
    for number, raw in enumerate(text.splitlines(), start=1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        cells = [cell.strip() for cell in line.split(",")]
        header = tuple(cells)
        if header in found:
            if header in seen:
                raise RowError("%s:line %d: the header %s appears twice" % (path, number, ",".join(header)))
            seen.add(header)
            current = header
            continue
        if current is None:
            raise RowError("%s:line %d: no header row (%s)" %
                           (path, number, " / ".join(",".join(header) for header in headers)))
        found[current].append((number, cells))
    if current is None:
        raise RowError("%s:line 1: no header row (%s)" %
                       (path, " / ".join(",".join(header) for header in headers)))
    return found


def parse_consumables(text, path="consumables.csv"):
    """The raid-consumable rows, validated: ids, roles, categories, levels, the weapon family and one row per role and item."""
    parsed = []
    for number, cells in sections(text, path, (CONSUMABLE_COLUMNS, NOT_COVERED_COLUMNS))[CONSUMABLE_COLUMNS]:
        if len(cells) != len(CONSUMABLE_COLUMNS):
            raise RowError("%s:line %d: %d columns, expected %d (%s)" %
                           (path, number, len(cells), len(CONSUMABLE_COLUMNS), ",".join(CONSUMABLE_COLUMNS)))
        role, category, item, spell, aura, level, weapon, note = cells
        if role not in ROLES:
            raise RowError("%s:line %d: unknown role '%s' (one of %s)" % (path, number, role, ", ".join(ROLES)))
        if category not in CATEGORIES:
            raise RowError("%s:line %d: unknown category '%s' (one of %s)" %
                           (path, number, category, ", ".join(CATEGORIES)))
        item = number_cell(path, number, "item_id", item, low=1)
        spell = number_cell(path, number, "spell_id", spell, low=1)
        aura = number_cell(path, number, "aura_id", aura, low=0)
        level = number_cell(path, number, "min_level", level, low=1, high=60)
        if category == "weapon":
            if weapon not in WEAPON_FAMILIES:
                raise RowError("%s:line %d: unknown weapon family '%s' on a weapon row (one of %s)" %
                               (path, number, weapon, ", ".join(WEAPON_FAMILIES)))
        elif weapon:
            raise RowError("%s:line %d: weapon family '%s' on a %s row (only a weapon row carries one)" %
                           (path, number, weapon, category))
        if not note:
            raise RowError("%s:line %d: empty note" % (path, number))
        literal_cell(path, number, "note", note)
        parsed.append(ConsumableRow(role, category, item, spell, aura, level, weapon, note, number))

    if not parsed:
        raise RowError("%s: no rows (the table would never fire)" % path)
    seen = {}
    for row in parsed:
        key = (row.role, row.item)
        if key in seen:
            raise RowError("%s:line %d: duplicate row for %s and item %d (first on line %d)" %
                           (path, row.line, row.role, row.item, seen[key]))
        seen[key] = row.line
    return parsed


def parse_not_covered(text, path="consumables.csv"):
    """The table's not-covered declarations, validated: known role and category, a reason, one line per pair."""
    declared = []
    seen = {}
    for number, cells in sections(text, path, (CONSUMABLE_COLUMNS, NOT_COVERED_COLUMNS))[NOT_COVERED_COLUMNS]:
        if len(cells) != len(NOT_COVERED_COLUMNS):
            raise RowError("%s:line %d: %d columns, expected %d (%s)" %
                           (path, number, len(cells), len(NOT_COVERED_COLUMNS), ",".join(NOT_COVERED_COLUMNS)))
        role, category, reason = cells
        if role not in ROLES:
            raise RowError("%s:line %d: unknown role '%s' (one of %s)" % (path, number, role, ", ".join(ROLES)))
        if category not in CATEGORIES:
            raise RowError("%s:line %d: unknown category '%s' (one of %s)" %
                           (path, number, category, ", ".join(CATEGORIES)))
        if not reason:
            raise RowError("%s:line %d: empty reason" % (path, number))
        literal_cell(path, number, "reason", reason)
        if (role, category) in seen:
            raise RowError("%s:line %d: duplicate not-covered line for %s/%s (first on line %d)" %
                           (path, number, role, category, seen[(role, category)]))
        seen[(role, category)] = number
        declared.append(NotCoveredRow(role, category, reason, number))
    return declared


def check_coverage(consumable_rows, not_covered_rows, path="consumables.csv"):
    """The per-role matrix: every role promises a food buff, a weapon stone or oil and the elixir slot, each resolved by a
    row or declared uncovered.

    The elixir slot is one promise with two shapes: a flask row fills both elixir slots, so a role with a flask row needs no
    battle or guardian row; without one, the table must give the role both elixir rows, and a leg it has no row for must be
    declared. A role with neither shape and no declaration would read as ready while missing half its elixir slot.

    A pair is one or the other, never both, so the read-out cannot call a slot covered and uncovered at once.
    """
    covered = {(row.role, row.category) for row in consumable_rows}
    declared = {(row.role, row.category) for row in not_covered_rows}
    for row in not_covered_rows:
        if (row.role, row.category) in covered:
            raise RowError("%s:line %d: %s/%s is declared not covered but the table has a row for it" %
                           (path, row.line, row.role, row.category))
    for role in ROLES:
        for slot in PROMISED_SLOTS:
            if (role, slot) not in covered and (role, slot) not in declared:
                raise RowError("%s: %s has no %s row and no not-covered line; the read-out would call the slot ready" %
                               (path, role, slot))
        if (role, "flask") in covered:
            continue
        if all((role, elixir) in covered for elixir in ELIXIR_CATEGORIES):
            continue
        for elixir in ELIXIR_CATEGORIES:
            if (role, elixir) not in covered and (role, elixir) not in declared:
                raise RowError("%s: %s has no flask row, no %s row and no not-covered line; a bot with only the other "
                               "elixir would read ready" % (path, role, elixir))


def number_cell(path, number, column, text, low, high=0xFFFFFFFF):
    """A numeric cell inside its range: a bad id is the difference between "this row fires" and "it never does"."""
    if not text.isdigit():
        raise RowError("%s:line %d: %s '%s' is not a number" % (path, number, column, text))
    value = int(text)
    if not low <= value <= high:
        raise RowError("%s:line %d: %s %d is outside %d..%d" % (path, number, column, value, low, high))
    return value


def render_consumables(consumable_rows, not_covered_rows):
    """The C++ header text the consumable rows and their not-covered declarations generate, byte for byte."""
    out = []
    out.append("// GENERATED FILE: do not edit. Written by forever/tools/gen_data_tables.py from")
    out.append("// forever/data/raid/consumables.csv; --check fails on a hand edit.")
    out.append("#pragma once")
    out.append("")
    out.append("#include <array>")
    out.append("#include <cstddef>")
    out.append("#include <cstdint>")
    out.append("")
    out.append("namespace tbdata")
    out.append("{")
    out.append("// A raid consumable of one role and category, with the item, spell and aura that apply it (issue #22).")
    out.append("// `aura` is the buff the spell leaves on the bot (the food rows' 15-minute Well Fed); 0 on a weapon row,")
    out.append("// whose effect is the main hand's temporary enchantment and whose `weapon` names the family it goes on.")
    out.append("// The host keeps a row only when the item exists,")
    out.append("// it lists that spell and the spell leaves that aura (host/TbRaidConsumables.cpp: BuildIndex).")
    out.append("namespace raidcons")
    out.append("{")
    out.append("struct ConsumableRow")
    out.append("{")
    out.append("    char const* role;        // role")
    out.append("    char const* category;    // category")
    out.append("    std::uint32_t item;      // item_id")
    out.append("    std::uint32_t spell;     // spell_id")
    out.append("    std::uint32_t aura;      // aura_id")
    out.append("    std::uint8_t minLevel;   // min_level")
    out.append("    char const* weapon;      // weapon: a weapon row's main-hand family (edged/blunt/any), else empty")
    out.append("    char const* note;        // note")
    out.append("};")
    out.append("")
    out.append("inline constexpr ConsumableRow kConsumables[] = {")
    for row in consumable_rows:
        out.append('    { "%s", "%s", %du, %du, %du, %d, "%s", "%s" },' %
                   (row.role, row.category, row.item, row.spell, row.aura, row.minLevel, row.weapon, row.note))
    out.append("};")
    out.append("inline constexpr std::size_t kConsumableCount = sizeof(kConsumables) / sizeof(kConsumables[0]);")
    out.append("")
    out.append("// A role/category pair the table deliberately has no consumable for: the `.tb state` read-out names the slot")
    out.append("// (`consnotcovered=`) instead of leaving it silently ready.")
    out.append("struct NotCoveredRow")
    out.append("{")
    out.append("    char const* role;        // role")
    out.append("    char const* category;    // category")
    out.append("    char const* reason;      // why the table has no consumable for it")
    out.append("};")
    out.append("")
    out.append("inline constexpr std::array<NotCoveredRow, %d> kNotCovered = {{" % len(not_covered_rows))
    for row in not_covered_rows:
        out.append('    { "%s", "%s", "%s" },' % (row.role, row.category, row.reason))
    out.append("}};")
    out.append("inline constexpr std::size_t kNotCoveredCount = kNotCovered.size();")
    out.append("}")
    out.append("}")
    out.append("")
    return "\n".join(out)


def build(nukes_path, creatures_path):
    return render(parse_nukes(pathlib.Path(nukes_path).read_text(), str(nukes_path)),
                  parse_creatures(pathlib.Path(creatures_path).read_text(), str(creatures_path)))


def build_consumables(consumables_path=None):
    consumables_path = CONSUMABLES_PATH if consumables_path is None else consumables_path
    text = pathlib.Path(consumables_path).read_text()
    rows_ = parse_consumables(text, str(consumables_path))
    declared = parse_not_covered(text, str(consumables_path))
    check_coverage(rows_, declared, str(consumables_path))
    return render_consumables(rows_, declared)


# ---- the raid boss table (issue #20 round 1) ----

# The client's SpellSchools ids, the one word the boss table's `school` column uses.
SCHOOL_IDS = {"holy": 1, "fire": 2, "nature": 3, "frost": 4, "shadow": 5, "arcane": 6}
RESIST_COLUMNS = ("entry", "name", "school", "evidence_spell", "note")


class ResistRow:
    __slots__ = ("entry", "name", "school", "evidence", "note", "line")

    def __init__(self, entry, name, school, evidence, note, line):
        self.entry, self.name, self.school, self.evidence, self.note, self.line = \
            entry, name, school, evidence, note, line


def parse_resist(text, path="boss-resist.csv"):
    parsed = []
    for number, cells in rows(text, path, RESIST_COLUMNS):
        if len(cells) != len(RESIST_COLUMNS):
            raise RowError("%s:line %d: %d columns, expected %d (%s)" %
                           (path, number, len(cells), len(RESIST_COLUMNS), ",".join(RESIST_COLUMNS)))
        entry, name, school, evidence, note = cells
        if not entry.isdigit() or int(entry) <= 0:
            raise RowError("%s:line %d: entry is not a number '%s'" % (path, number, entry))
        if not name:
            raise RowError("%s:line %d: empty name" % (path, number))
        literal_cell(path, number, "name", name)
        if school not in SCHOOL_IDS:
            raise RowError("%s:line %d: unknown school '%s' (one of %s)" %
                           (path, number, school, ", ".join(SCHOOL_IDS)))
        if not evidence.isdigit() or int(evidence) <= 0:
            raise RowError("%s:line %d: evidence_spell is not a spell id '%s'" % (path, number, evidence))
        if not note:
            raise RowError("%s:line %d: empty note (say where the evidence spell comes from)" % (path, number))
        literal_cell(path, number, "note", note)
        parsed.append(ResistRow(int(entry), name, school, int(evidence), note, number))
    seen_entries = {}
    for row in parsed:
        if row.entry in seen_entries:
            raise RowError("%s:line %d: duplicate entry %d (first on line %d)" %
                           (path, row.line, row.entry, seen_entries[row.entry]))
        seen_entries[row.entry] = row.line
    return parsed


def render_resist(resist_rows):
    """The C++ header text the boss rows generate, byte for byte."""
    out = []
    out.append("// GENERATED FILE: do not edit. Written by forever/tools/gen_data_tables.py from forever/data/raid/boss-resist.csv.")
    out.append("// Regenerate after a CSV change: python3 forever/tools/gen_data_tables.py (--check fails while this is stale).")
    out.append("#pragma once")
    out.append("")
    out.append("#include <cstddef>")
    out.append("#include <cstdint>")
    out.append("")
    out.append("namespace tbdata")
    out.append("{")
    out.append("// A raid boss and the school its damage is mostly one of (issue #20: resistance gear before the pull).")
    out.append("// `school` is the client's SpellSchools id (1 holy, 2 fire, 3 nature, 4 frost, 5 shadow, 6 arcane);")
    out.append("// `evidenceSpell` is a spell the boss casts at the core pin (its script, or EventAI where it has none),")
    out.append("// and the module drops the row at boot when the spell mirror says that spell's school is another one.")
    out.append("namespace resist")
    out.append("{")
    out.append("struct BossResistRow")
    out.append("{")
    out.append("    std::uint32_t entry;           // entry")
    out.append("    char const* name;              // name")
    out.append("    std::uint8_t school;           // school")
    out.append("    std::uint32_t evidenceSpell;   // evidence_spell")
    out.append("    char const* note;              // note")
    out.append("};")
    out.append("")
    out.append("inline constexpr BossResistRow kBossResist[] = {")
    for row in resist_rows:
        out.append('    { %du, "%s", %d, %du, "%s" },' %
                   (row.entry, row.name, SCHOOL_IDS[row.school], row.evidence, row.note))
    out.append("};")
    out.append("inline constexpr std::size_t kResistRowCount = sizeof(kBossResist) / sizeof(kBossResist[0]);")
    out.append("}")
    out.append("}")
    out.append("")
    return "\n".join(out)


def build_resist(resist_path):
    return render_resist(parse_resist(pathlib.Path(resist_path).read_text(), str(resist_path)))


def write_or_check(out, text, check):
    """Write one generated header, or (with --check) fail when it is missing or stale. Returns 0, or 1 when stale."""
    if check:
        if not out.exists():
            print("stale: %s does not exist; run python3 forever/tools/gen_data_tables.py" % out, file=sys.stderr)
            return 1
        old = out.read_text()
        if old != text:
            print("stale: %s differs; run python3 forever/tools/gen_data_tables.py" % out, file=sys.stderr)
            diff = difflib.unified_diff(old.splitlines(), text.splitlines(), fromfile=str(out), tofile="generated",
                                        lineterm="", n=2)
            for line in list(diff)[:80]:
                print(line, file=sys.stderr)
            return 1
        return 0

    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(text)
    print("wrote %s" % out)
    return 0


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="fail (exit 1) when a header differs instead of writing it")
    parser.add_argument("--nukes", default=str(NUKES), help="the nuke table (default %(default)s)")
    parser.add_argument("--creatures", default=str(CREATURES), help="the creature table (default %(default)s)")
    parser.add_argument("--consumables", default=str(CONSUMABLES_PATH), help="the consumable table (default %(default)s)")
    parser.add_argument("--out", default=str(OUTPUT), help="the school header to write (default %(default)s)")
    parser.add_argument("--consumables-out", default=str(CONSUMABLES_OUTPUT),
                        help="the consumable header to write (default %(default)s)")
    parser.add_argument("--resist", default=str(RESIST), help="the boss table (default %(default)s)")
    parser.add_argument("--resist-out", default=str(RESIST_OUTPUT), help="the boss header to write (default %(default)s)")
    args = parser.parse_args(argv)

    try:
        school_text = build(args.nukes, args.creatures)
        consumables_text = build_consumables(args.consumables)
        resist_text = build_resist(args.resist)
    except RowError as error:
        print(str(error), file=sys.stderr)
        return 2
    except OSError as error:
        print(str(error), file=sys.stderr)
        return 2

    stale = write_or_check(pathlib.Path(args.out), school_text, args.check)
    stale += write_or_check(pathlib.Path(args.consumables_out), consumables_text, args.check)
    stale += write_or_check(pathlib.Path(args.resist_out), resist_text, args.check)
    return 1 if stale else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

#!/usr/bin/env python3
"""Generate forever/src/host/data/TbSchoolData.gen.h from the two combat tables (issue #48 round 1).

    python3 forever/tools/gen_data_tables.py            # write the header
    python3 forever/tools/gen_data_tables.py --check    # exit 1 with a diff when the header is stale

Inputs (owned by the data collaborator, never this script):
  forever/data/combat/school-nukes.csv      class,spec,school,spell_name,order   the caster specs' nukes
  forever/data/combat/creature-schools.csv  entry,name,immune_mask,vuln_mask,source  the extra statements, header-only

Every row is validated, and a bad one stops the run with `file:line N: <why>` instead of being dropped: a silent drop would
turn a typo into "this spec never switches". The checks are the rules the host relies on — a known class and school, one
main row (order 0) and contiguous orders per spec, no duplicate order or spell inside a spec, masks that stay inside the
seven schools and say something (a 0/0 creature row could never fire). A spell, creature name or source written into the
generated header is refused when it carries a double quote or a backslash, which a C++ string literal cannot hold.

The sets and the names are C++ constants the host compares against its own: the class ids are the core's CLASS_* values and
the school order is SpellEntry::School's, both spelled the same as forever/src/host/TbSchool.cpp reads them.
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


def build(nukes_path, creatures_path):
    return render(parse_nukes(pathlib.Path(nukes_path).read_text(), str(nukes_path)),
                  parse_creatures(pathlib.Path(creatures_path).read_text(), str(creatures_path)))


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="fail (exit 1) when the header differs instead of writing it")
    parser.add_argument("--nukes", default=str(NUKES), help="the nuke table (default %(default)s)")
    parser.add_argument("--creatures", default=str(CREATURES), help="the creature table (default %(default)s)")
    parser.add_argument("--out", default=str(OUTPUT), help="the header to write (default %(default)s)")
    args = parser.parse_args(argv)

    try:
        text = build(args.nukes, args.creatures)
    except RowError as error:
        print(str(error), file=sys.stderr)
        return 2
    except OSError as error:
        print(str(error), file=sys.stderr)
        return 2

    out = pathlib.Path(args.out)
    if args.check:
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


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

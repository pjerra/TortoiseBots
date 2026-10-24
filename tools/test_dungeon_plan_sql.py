"""Guards the dungeon plan tables (ai_dungeon_plan, ai_dungeon_leg, ai_dungeon_step).

The loader (ai/playerbot/DungeonPlanMgr.cpp) names its columns in SELECTs; a column that a
migration does not create would make the query fail at startup and leave every plan out. This
checks that each column the loader reads is created by a migration, and that any step rows a
later (exporter) migration inserts use a kind the loader knows.
"""
import glob
import math
import os
import pathlib
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KINDS = {"move", "move_direct", "move_hop_exact", "pull", "boss", "loot", "open", "open_proof", "use_item", "wait_object",
         "wait_clear", "rest", "ritual", "hold", "gossip", "use_on"}
# The opt-in Gahz'rilla plan for test runs (never applied automatically: not under data/sql/world).
OPT_IN = pathlib.Path(ROOT) / "tools/optional-plans/zulfarrak-gahzrilla.sql"
TABLES = ("ai_dungeon_plan", "ai_dungeon_leg", "ai_dungeon_step", "ai_dungeon_boss")
# The plans whose bosses ai_dungeon_boss lists, and their maps.
BOSS_PLANS = {"ragefire_chasm": 389, "wailing_caverns": 43, "uldaman": 70, "zulfarrak": 209, "deadmines": 36,
              "stormwind_stockade": 34,
              "razorfen_kraul": 47,
              "gnomeregan": 90,
              "scarlet_library": 189,
              "scarlet_armory": 189, "scarlet_cathedral": 189, "scarlet_graveyard": 189,
              "blackfathom_deeps": 48,
              "sunken_temple": 109}
# Every ai_dungeon_boss row, by (map, entry): the creature_template name and rank in the core's data (Tortoise V2
# sql/base tw_world_creature_template.sql, read 2026-10-21). All rank 1 (elite) and no instance bind, hence the table.
SEEDED_BOSSES = {
    (389, 11517): "Oggleflint", (389, 11520): "Taragaman the Hungerer", (389, 11518): "Jergosh the Invoker",
    (389, 11519): "Bazzalan",
    (43, 3653): "Kresh", (43, 3671): "Lady Anacondra", (43, 3669): "Lord Cobrahn", (43, 3670): "Lord Pythas",
    (43, 3674): "Skum", (43, 3673): "Lord Serpentis", (43, 5775): "Verdan the Everliving",
    (43, 3654): "Mutanus the Devourer", (43, 61965): "Vangros", (43, 61968): "Zandara Windhoof",
    (70, 6910): "Revelosh", (70, 6906): "Baelog", (70, 7228): "Ironaya", (70, 7023): "Obsidian Sentinel",
    (70, 7206): "Ancient Stone Keeper", (70, 7291): "Galgann Firehammer", (70, 4854): "Grimlok", (70, 2748): "Archaedas",
    (209, 8127): "Antu'sul", (209, 7272): "Theka the Martyr", (209, 7795): "Hydromancer Velratha",
    (209, 7271): "Witch Doctor Zum'rah", (209, 7274): "Sandfury Executioner", (209, 7275): "Shadowpriest Sezz'ziz",
    (209, 7267): "Chief Ukorz Sandscalp", (209, 7273): "Gahz'rilla",
    (36, 644): "Rhahk'Zor", (36, 642): "Sneed's Shredder", (36, 643): "Sneed", (36, 1763): "Gilnid",
    (36, 61963): "Masterpiece Harvester", (36, 646): "Mr. Smite", (36, 647): "Captain Greenskin",
    (36, 639): "Edwin VanCleef", (36, 645): "Cookie",
    (34, 1696): "Targorr the Dread", (34, 1663): "Dextren Ward", (34, 1666): "Kam Deepfury", (34, 1717): "Hamhock",
    (34, 1716): "Bazil Thredd",
    (47, 6168): "Roogug", (47, 4424): "Aggem Thorncurse", (47, 4428): "Death Speaker Jargba",
    (47, 4420): "Overlord Ramtusk", (47, 62503): "Rotthorn", (47, 4422): "Agathelos the Raging",
    (47, 4421): "Charlga Razorflank",
    (90, 7079): "Viscous Fallout", (90, 6235): "Electrocutioner 6000", (90, 6229): "Crowd Pummeler 9-60",
    (90, 6228): "Dark Iron Ambassador", (90, 7800): "Mekgineer Thermaplugg",
    (189, 3974): "Houndmaster Loksey", (189, 61983): "Brother Wystan", (189, 6487): "Arcanist Doan",
    (48, 4887): "Ghamoo-ra", (48, 4831): "Lady Sarevess", (48, 6243): "Gelihast",
    (48, 62530): "Velthelaxx the Defiler", (48, 4830): "Old Serra'kis", (48, 4832): "Twilight Lord Kelris",
    (48, 4829): "Aku'mai",
    (189, 61982): "Armory Quartermaster Daghelm", (189, 3975): "Herod",
    (189, 4542): "High Inquisitor Fairbanks", (189, 3976): "Scarlet Commander Mograine", (189, 3977): "High Inquisitor Whitemane",
    (189, 3983): "Interrogator Vishas", (189, 14693): "Scorn", (189, 4543): "Bloodmage Thalnos", (189, 61972): "Duke Dreadmoore",
    (109, 8580): "Atal'alarion", (109, 5720): "Weaver", (109, 5721): "Dreamscythe",
    (109, 5710): "Jammal'an the Prophet", (109, 5719): "Morphaz", (109, 5709): "Shade of Eranikus",
}
# Not bosses here: adds of a boss step (Eric 6907, Olaf 6908, Nekrum 7796, Ruuzlu 7797), the friendly Naralex 3679 and his
# Disciple 3678, the Sergeant Bly event 7604, Miner Johnson 3586 (a rare in the original game), rares (Deviate Faerie Dragon 5912, Zerillis 10082, Sandarr Dunereaver 10080,
# Bruegal Ironknuckle 1720: rank 2, Stormwind Stockade).
# Razorfen Kraul: Spearhide 4438, Earthcaller Halmgar 4842, Blind Hunter 4425 — rank 2 in the world data).
# Grubbis 7361: no spawn row in the map 90 export and no boss step names him, Gnomeregan).
# the BFD neutrals Argent Guard 4787 and Woji the Toad 62531 (both rank 0).
NOT_BOSSES = {6907, 6908, 7796, 7797, 3679, 3678, 7604, 5912, 10082, 10080, 3586, 1720, 4438, 4842, 4425, 7361, 4787, 62531}
# Zum'rah's 31 Shallow Graves (128403), in the order the leg opens them: the nearest-neighbour walk of the plan from 1859 1058
# (the exporter writes each as a stand move, a loot and a sweep pull, in this order). A reorder is a deliberate change.
ZUMRAH_GRAVES = (
    (1858.70, 1053.75), (1862.12, 1050.76), (1871.08, 1046.76), (1875.85, 1044.26),
    (1881.46, 1041.43), (1881.99, 1034.68), (1888.36, 1037.00), (1867.74, 1041.84),
    (1845.96, 1047.25), (1841.08, 1045.54), (1843.19, 1041.16), (1839.18, 1036.88),
    (1832.72, 1032.39), (1841.82, 1027.76), (1839.42, 1023.49), (1845.54, 1024.12),
    (1852.41, 1024.78), (1861.12, 1019.39), (1865.03, 1012.49), (1866.24, 1008.40),
    (1871.30, 1008.32), (1871.86, 994.26), (1890.12, 989.76), (1894.39, 977.01),
    (1887.46, 967.14), (1882.70, 971.83), (1879.89, 974.65), (1876.44, 972.21),
    (1892.57, 1006.37), (1891.60, 1023.06), (1887.94, 1029.08),
)
# Frostmane Hollow (20261022210030): its map and its four boss steps, names from the core's creature_template (all rank 1). Added apart
# from the dicts above so that this change merges with the other plans' edits of them.
BOSS_PLANS["frostmane_hollow"] = 822
SEEDED_BOSSES.update({(822, 63131): "Battlemaster Ubukaz", (822, 63129): "Kan'za the Seer",
                      (822, 63133): "Tan'sha the Sleek", (822, 63130): "Hailar the Frigid"})

def migrations():
    for path in sorted(glob.glob(os.path.join(ROOT, "data/sql/world/*_world.sql"))):
        with open(path, encoding="utf-8") as handle:
            yield os.path.basename(path), handle.read()


def newest_zulfarrak_rows():
    """The step rows of the newest migration that inserts the zulfarrak steps (its name, its text, its zulfarrak rows)."""
    newest = None
    for name, text in migrations():
        if re.search(r"INSERT INTO `ai_dungeon_step`.*?\('zulfarrak',", text, re.S):
            newest = (name, text)
    assert newest is not None, "no migration inserts the zulfarrak steps"
    name, text = newest
    return name, text, [r for r in DungeonPlanSql.step_rows(None, text) if r["plan"] == "zulfarrak"]


def newest_plan_text(plan):
    """The name and text of the newest migration that inserts the plan's steps."""
    newest = None
    for name, text in migrations():
        if re.search(r"INSERT INTO `ai_dungeon_step`.*?\('%s'," % plan, text, re.S):
            newest = (name, text)
    assert newest is not None, "no migration inserts the %s steps" % plan
    return newest


def newest_uldaman_rows():
    """The step rows of the newest migration that inserts the uldaman steps (its name, its text, its uldaman rows)."""
    name, text = newest_plan_text("uldaman")
    return name, text, [r for r in DungeonPlanSql.step_rows(None, text) if r["plan"] == "uldaman"]


def leg_rows(text, plan):
    """The ai_dungeon_leg rows of a migration for the plan as {leg: (name, comment)}."""
    return {int(m.group(1)): (m.group(2), m.group(3).replace("''", "'")) for m in
            re.finditer(r"^\('%s', (\d+), '([\w-]+)', '((?:[^']|'')*)'\)[,;]$" % plan, text, re.M)}


def leg_steps(rows, leg):
    """The rows of one leg in run order: by seq, as the loader seals them, not by their place in the file."""
    return sorted((r for r in rows if r["leg"] == str(leg)), key=lambda r: int(r["seq"]))


def dist(row, x, y):
    return math.hypot(float(row["x"]) - x, float(row["y"]) - y)


def created_columns():
    columns = {}
    for _name, text in migrations():
        for table in TABLES:
            match = re.search(r"CREATE TABLE IF NOT EXISTS `%s` \((.*?)\n\) ENGINE" % table, text, re.S)
            if match:
                columns.setdefault(table, set()).update(re.findall(r"^\s+`(\w+)`", match.group(1), re.M))
            for alter in re.findall(r"ALTER TABLE `%s`(.*?);" % table, text, re.S):
                columns.setdefault(table, set()).update(re.findall(r"ADD COLUMN IF NOT EXISTS `(\w+)`", alter))
    return columns


def loader_columns():
    with open(os.path.join(ROOT, "ai/playerbot/DungeonPlanMgr.cpp"), encoding="utf-8") as handle:
        text = handle.read().replace('"\n        "', "").replace('" "', "")
    found = {}
    for select in re.findall(r'"SELECT (.*?) FROM `(\w+)`"', text, re.S):
        found[select[1]] = set(re.findall(r"`(\w+)`", select[0]))
    return found


class DungeonPlanSql(unittest.TestCase):
    def test_tables_exist(self):
        columns = created_columns()
        for table in TABLES:
            self.assertIn(table, columns, "no migration creates %s" % table)

    def test_loader_reads_created_columns(self):
        created = created_columns()
        loaded = loader_columns()
        self.assertEqual(set(loaded), set(TABLES))
        for table, cols in loaded.items():
            missing = cols - created.get(table, set())
            self.assertFalse(missing, "%s: the loader reads columns no migration creates: %s" % (table, missing))

    def test_zulfarrak_keeps_its_crowd_control(self):
        # The Zul'Farrak plan row after all migrations (its newest INSERT, then the UPDATEs after it)
        # carries hand-set cc_always and kill_first (pending pjerra/turtle-agent#190); a re-export from
        # an exporter that does not write them must not land silently.
        row, name = None, None
        for name_, text in migrations():
            # A statement ends with the ';' that ends its line: a comment value may hold one ('live-checked; cut before ...').
            for statement in re.findall(r"(INSERT INTO `ai_dungeon_plan`.*?;$|UPDATE `ai_dungeon_plan`.*?;$)", text, re.S | re.M):
                if statement.startswith("INSERT"):
                    cols, values = re.match(r"INSERT INTO `ai_dungeon_plan` \(([^)]*)\) VALUES\s*(.*);", statement, re.S).groups()
                    columns = re.findall(r"`(\w+)`", cols)
                    for found in re.findall(r"\(('zulfarrak'.*?)\)\s*(?:,|;|$)", values + ";", re.S):
                        fields = [f.strip().strip("'") for f in re.findall(r"'(?:[^']|'')*'|[^,\s][^,]*", found)]
                        row, name = dict(zip(columns, fields)), name_
                elif row is not None and re.search(r"WHERE\s+`plan`\s*=\s*'zulfarrak'", statement):
                    for column, value in re.findall(r"`(\w+)`\s*=\s*'((?:[^']|'')*)'", statement.split("WHERE")[0]):
                        row[column] = value
                    name = name_
        self.assertIsNotNone(row, "no migration inserts the zulfarrak plan row")
        for column in ("cc_always", "kill_first"):
            self.assertTrue(row.get(column), "%s: the zulfarrak plan row has no %s" % (name, column))
        self.assertEqual(row.get("cc_always"), "7246")
        # 20261021005900 (the plan through the stairs event): the exporter writes the lists now; kill_first is the owner's
        # 8179,5650,7247 with the Ward of Zum'rah 7785 first (his leg is in the rows), and cc_priority stays the owner's list
        # (hand-set there: the plan file dropped 7247).
        # 20261021174716 (Gahz'rilla and Bly in the rows): Sergeant Bly's crew follows in his boss.first order, Murta 7608, Oro 7606,
        # Raven 7605, because his units hold has no kill order of its own (the plan-wide kill_first is focused in every fight).
        self.assertEqual(row.get("kill_first"), "7785,8179,5650,7247,7608,7606,7605")
        self.assertEqual(row.get("cc_priority"), "7246,7247,5648")

    def test_uldaman_keeps_its_event_steps(self):
        # The newest migration that inserts Uldaman's steps carries the two scripted events as hold rows
        # (#984; set by hand in 20261016120000, exported since 20261016220000): a re-export from an
        # exporter that has no `hold` kind must not drop them silently.
        newest = None
        for name, text in migrations():
            if re.search(r"INSERT INTO `ai_dungeon_step`.*?\('uldaman',", text, re.S):
                newest = (name, text)
        self.assertIsNotNone(newest, "no migration inserts the uldaman steps")
        name, text = newest
        for data, units in (("1", "'4857'"), ("2", "'2748,7309,7077,7076,10120'")):
            self.assertRegex(text, r"\('uldaman', \d+, \d+, 'hold', %s, [^)]*, 3, 40, 600, %s," % (data, units),
                             "%s: the uldaman hold on instance data %s is missing" % (name, data))
        self.assertEqual(len(re.findall(r"\('uldaman', \d+, \d+, 'ritual', \d+, [^)]*?, 0, 0, 3, 0, 0,", text)), 2,
                         "%s: the uldaman rituals need param 3" % name)

    def test_ragefire_wailing_deadmines_are_whole(self):
        # 20261017130000 exports the three plans (turtle-agent plan-export): each is deleted, then inserted, with
        # its bosses in leg order; the Wailing Caverns gm_cross is the only move_direct (a straight walk down to the
        # Serpentis ramp), and the Deadmines doors, gunpowder and cannon are plain steps (#984: nothing dropped silently).
        text = dict(migrations()).get("20261017130000_world.sql")
        self.assertIsNotNone(text, "20261017130000_world.sql is missing")
        bosses = {"ragefire_chasm": ["11517", "11520", "11518", "11519"],
                  "wailing_caverns": ["3669", "3670", "3674", "3673", "5775"],
                  "deadmines": ["644", "642", "61963", "1763", "646", "647", "639"]}
        for plan, entries in bosses.items():
            for table in ("ai_dungeon_step", "ai_dungeon_leg", "ai_dungeon_plan"):
                self.assertIn("DELETE FROM `%s` WHERE `plan` = '%s';" % (table, plan), text)
            self.assertLess(text.index("DELETE FROM `ai_dungeon_step` WHERE `plan` = '%s'" % plan),
                            text.index("INSERT INTO `ai_dungeon_plan` (`plan`, `map`, `cc_priority`, `cc_always`, `kill_first`, `source`, `comment`) VALUES\n('%s'" % plan))
            self.assertEqual(re.findall(r"^\('%s', \d+, \d+, 'boss', (\d+)," % plan, text, re.M), entries)
        self.assertEqual(re.findall(r"^\('(\w+)', \d+, \d+, 'move_direct'", text, re.M), ["wailing_caverns"])
        for kind, count in (("open", 3), ("loot", 1), ("wait_object", 6), ("wait_clear", 2)):
            self.assertEqual(len(re.findall(r"^\('deadmines', \d+, \d+, '%s'," % kind, text, re.M)), count, kind)
        self.assertRegex(text, r"\('deadmines', 9, \d+, 'open', 16398, [^)]*, 0, 5397, 0, 0, 0,")
        self.assertIn("gm_cross", text)  # the header names every step the run leader cannot do

    def test_deadmines_pack_fixes(self):
        # 20261022200000 (turtle-agent 16efe83, Deadmines r12-19z29 / r14-19z32, 2026-10-22): the plan again with the Engineers and
        # Craftsmen in the cc lists, the Sneed patroller's group first, the Harvester room's golem split off, and the Gilnid ramp pulled from
        # the spiral and the floor ring camp by camp (its pairs behind move rows to stands). Every pin below fails when its rows are cut.
        name, text = newest_plan_text("deadmines")
        self.assertEqual(name, "20261022200000_world.sql")
        self.assertIn("('deadmines', 36, '622,1731,4418,1729,1732', '622', '636,622', 'turtle-agent 16efe83 dungeons/deadmines.json',", text)
        rows = [r for r in self.step_rows(text) if r["plan"] == "deadmines"]
        self.assertEqual(len(rows), 210)
        self.assertEqual({leg: len(leg_steps(rows, leg)) for leg in (3, 6, 7, 8)}, {3: 11, 6: 11, 7: 41, 8: 8})

        def pulls(leg):
            return [(r["entries"], int(r["radius"]), round(float(r["x"]), 1)) for r in leg_steps(rows, leg) if r["kind"] == "pull"]

        def stand_before(leg, x, y):
            # the move row right before the pull at (x, y): its approach stand
            steps = leg_steps(rows, leg)
            for i, r in enumerate(steps):
                if r["kind"] == "pull" and abs(float(r["x"]) - x) < 0.02 and abs(float(r["y"]) - y) < 0.02:
                    self.assertEqual(steps[i - 1]["kind"], "move", "the pull at %s %s has no move row before it" % (x, y))
                    self.assertTrue(steps[i - 1]["comment"].endswith("approach"), steps[i - 1]["comment"])
                    return float(steps[i - 1]["x"]), float(steps[i - 1]["y"]), float(steps[i - 1]["z"])
            self.fail("no pull at %s %s in leg %d" % (x, y, leg))

        # Sneed: the group of the Woodcarver 79207's beat (radius 19) is the first pull.
        self.assertEqual(pulls(3)[0], ("641", 19, -277.3))
        self.assertEqual(pulls(3)[1], ("641", 12, -288.3))
        # Harvester room: the Taskmaster over the west half of its beat only (radius 12: the disc stops 17 yd from the Harvester); the Craftsman,
        # the Engineer and the golem that assists them are ONE pull of three from the west stand, 23 yd from the ledge Overseer's beat; the first
        # pull of the room counts the patrolling golem 2582923.
        hv = pulls(6)
        self.assertEqual(hv[0], ("622,61962", 11, -150.3))
        self.assertIn(("4417", 12, -95.9), hv)
        self.assertIn(("1731,622,61962", 9, -87.5), hv)
        self.assertNotIn(("61962", 6, -81.7), hv)
        self.assertEqual(stand_before(6, -87.49, -580.41), (-112.0, -573.0, 52.5))
        # Gilnid ramp: the two terrace Engineers FIRST, from the spiral (z 36.5, over the open pit), then the Craftsman under the spiral from
        # the floor (a spiral stand has no sight of him), then the floor ring.
        gl = pulls(7)
        self.assertEqual(gl[3:], [("622", 10, -202.7), ("1731", 8, -198.9), ("1731,622", 9, -233.8), ("1731,622", 5, -226.1),
                                  ("1731", 12, -209.6), ("1731,622", 8, -208.6), ("622,1731", 11, -187.1), ("1731", 6, -178.4)])
        self.assertEqual(stand_before(7, -202.74, -584.35), (-195.0, -596.0, 36.5))
        self.assertEqual(stand_before(7, -198.93, -603.78), (-215.0, -590.0, 21.1))
        self.assertEqual(stand_before(7, -233.75, -572.8), (-221.0, -596.0, 21.8))
        self.assertEqual(stand_before(7, -208.61, -552.0), (-217.0, -576.0, 21.1))
        self.assertEqual(stand_before(7, -187.05, -551.48), (-207.0, -568.0, 21.1))
        # The boss row is where it was (no boss approach moved) and the pair 79196/79201 is pulled in leg 7 only.
        self.assertEqual([(r["entry"], r["entries"], r["x"], r["y"]) for r in leg_steps(rows, 7) if r["kind"] == "boss"],
                         [("1763", "1731", "-177.362", "-574.458")])
        self.assertNotIn(("622,1731", 11, -187.1), pulls(8))
        self.assertEqual([r["entry"] for r in rows if r["kind"] == "boss"], ["644", "642", "61963", "1763", "646", "647", "639"])

    def test_scarlet_library_is_the_plan_with_the_pack_fixes(self):
        # 20261022210600 (turtle-agent prep/scarlet_library, library census 2026-10-22): the draft with the unit lists
        # (fleers 4287 4288 4291 4296 4299, cc 4299 4296, cc_always 4299, kill_first 4291), Loksey's three hounds and
        # Wystan's two camp Monks folded into their boss steps (boss.first), the two patrols on their pulls and the two
        # Doan-hall packs at radius 9. Every pin below fails when its rows are cut.
        name, text = newest_plan_text("scarlet_library")
        self.assertEqual(name, "20261022210600_world.sql")
        self.assertIn("('scarlet_library', 189, '4299,4296', '4299', '4291', 'turtle-agent c670d6e dungeons/scarlet_library.json',", text)
        rows = [r for r in self.step_rows(text) if r["plan"] == "scarlet_library"]
        self.assertEqual(len(rows), 88)
        self.assertEqual({int(leg): len(leg_steps(rows, leg)) for leg in (0, 1, 2)}, {0: 44, 1: 40, 2: 4})
        pulls = {leg: [(r["entries"], r["radius"], r["x"], r["y"]) for r in leg_steps(rows, leg) if r["kind"] == "pull"] for leg in (0, 1, 2)}
        self.assertEqual([len(pulls[0]), len(pulls[1]), len(pulls[2])], [17, 23, 2])
        # Loksey's cage: the hounds are not a pull any more; they are the boss's first.
        self.assertEqual([(r["entry"], r["entries"], r["x"], r["y"]) for r in leg_steps(rows, 0) if r["kind"] == "boss"],
                         [("3974", "4304", "119.938", "-260.869")])
        # Wystan's camp: the two Monks of 2583275/2583276 are the boss's first (and Fireball is a tactic).
        self.assertEqual([(r["entry"], r["entries"], r["x"], r["y"]) for r in leg_steps(rows, 1) if r["kind"] == "boss"],
                         [("61983", "4540", "209.491", "-401.61")])
        # The patrols of 2583271 (the Monk that walks 220.8 -386.7 to 220.9 -338.5 and back) and 40098 (the Chaplain's
        # 8-point beat to x 221.1 -329.2) stand on their pulls in the plan: pull rows of those exact spots still exist.
        self.assertIn(("4540", "10", "220.82", "-386.67"), pulls[1])
        self.assertIn(("4299", "10", "265.03", "-379.05"), pulls[1])
        # The Doan hall: both packs at radius 9, the boss 25.7 yd from the first stand.
        self.assertEqual([(r["entries"], r["radius"], r["x"], r["y"]) for r in leg_steps(rows, 2) if r["kind"] == "pull"],
                         [("4540", "9", "173.97", "-429.18"), ("4291,4540,4299", "9", "181.14", "-443.63")])
        self.assertEqual([r["entry"] for r in rows if r["kind"] == "boss"], ["3974", "61983", "6487"])
        # The boss table seeds the map's Library bosses (the three later wings add theirs with their own files).
        self.assertEqual([(m, e, n) for m, e, n in self.boss_rows() if m == 189 and e in (3974, 61983, 6487)],
                         [(189, 3974, "Houndmaster Loksey"), (189, 61983, "Brother Wystan"), (189, 6487, "Arcanist Doan")])


    def test_scarlet_armory_is_the_wing_with_the_census_fixes(self):
        # 20261023030000 (turtle-agent prep/sm-wings, armory census 2026-10-23): 2 legs, 42 pulls, 2 bosses; the six fleers are
        # plan-only, kill_first is the healer 4292 then the casters 4290/4297, and four radii are cut to 10 so a row does not take
        # an unplanned unit of its own entry (the ten patrol pulls have no row form).
        name, text = newest_plan_text("scarlet_armory")
        self.assertEqual(name, "20261023030000_world.sql")
        self.assertIn("('scarlet_armory', 189, '', '', '4292,4290,4297', 'turtle-agent ", text)
        rows = [r for r in self.step_rows(text) if r["plan"] == "scarlet_armory"]
        self.assertEqual(len(rows), 110)
        self.assertEqual({leg: len(leg_steps(rows, leg)) for leg in (0, 1)}, {0: 54, 1: 56})
        pulls = {leg: [(r["entries"], r["radius"], r["x"], r["y"]) for r in leg_steps(rows, leg) if r["kind"] == "pull"] for leg in (0, 1)}
        self.assertEqual([len(pulls[0]), len(pulls[1])], [31, 11])
        self.assertIn(("4286", "10", "1709.52", "-380.76"), pulls[0])   # group 7
        self.assertIn(("4298", "10", "1850.37", "-352.24"), pulls[0])   # group 22
        self.assertIn(("4295,4298", "10", "1878.32", "-384.53"), pulls[0])  # group 27
        self.assertIn(("4298", "10", "1894.39", "-380.89"), pulls[1])   # group 0
        self.assertEqual([(r["entry"], r["entries"]) for r in rows if r["kind"] == "boss"],
                         [("61982", ""), ("3975", "")])

    def test_scarlet_cathedral_is_the_wing_with_the_healer_fix(self):
        # 20261023090000 (turtle-agent cath-fix2, after r23 slot 5): 2 legs, 78 steps, 3 bosses; the pack fixes of
        # 20261023030100 stay (fleers, kill_first the three healers, no cc_always, Fairbanks' Monk as boss.first, the 0.4
        # double pull gone, five radii 9-14). The Mograine/Whitemane event broke in the run (Whitemane at 1 HP, the pair on
        # two legs, the boss corpse found 10 min late): the pair is now one boss hold (instance data 1, script value 4, flag 8
        # alone, radius 35, 600 s, entries 3976 3977) and the separate Whitemane leg is gone; the four entrance-hall packs
        # moved into the Fairbanks leg, so the run does not walk back to the door before Mograine; the chapel packs are
        # pulled from stands more than 30 yd from him (detection 20 yd, his aggro calls every Scarlet within 82 yd).
        name, text = newest_plan_text("scarlet_cathedral")
        self.assertEqual(name, "20261023090000_world.sql")
        self.assertIn("('scarlet_cathedral', 189, '', '', '4303,4300,4299', 'turtle-agent ", text)
        rows = [r for r in self.step_rows(text) if r["plan"] == "scarlet_cathedral"]
        self.assertEqual(len(rows), 78)
        self.assertEqual(leg_rows(text, "scarlet_cathedral"),
                         {0: ("high_inquisitor_fairbanks", ""), 1: ("scarlet_commander_mograine", "")})
        self.assertEqual({leg: len(leg_steps(rows, leg)) for leg in (0, 1)}, {0: 25, 1: 53})
        pulls = {leg: [(r["entries"], r["radius"], r["x"], r["y"]) for r in leg_steps(rows, leg) if r["kind"] == "pull"] for leg in (0, 1)}
        self.assertEqual([len(pulls[0]), len(pulls[1])], [9, 44])
        self.assertIn(("4300,4301,4302", "10", "1140.18", "1340.5"), pulls[0])   # the 0.5 pack, no duplicate 0.4 row
        # The four entrance-hall packs (x 865-890) are Fairbanks-leg pulls now; they were leg 1's last pulls, 250 yd back.
        for hall in (("4298", "15.7", "870.88", "1359.36"), ("4298,4295", "12.5", "870.85", "1406.03"),
                     ("4294,4295", "11.9", "889.97", "1376.13"), ("4298", "10", "890.28", "1360.67")):
            self.assertIn(hall, pulls[0])
            self.assertNotIn(hall, pulls[1])
        self.assertIn(("4303,4540", "9", "1131.07", "1406.46"), pulls[1])         # group 1
        self.assertIn(("4540,4302,4303", "10", "1114", "1400.55"), pulls[1])      # group 2
        self.assertIn(("4303,4540", "10", "1093.91", "1406.32"), pulls[1])        # group 3
        self.assertIn(("4301,4302", "10", "1101.94", "1422.63"), pulls[1])        # group 37
        self.assertIn(("4300,4301,4302", "14", "1112.35", "1376.42"), pulls[1])   # group 43
        self.assertFalse([r for r in pulls[0] if r[2] == "1134.68" and r[3] == "1338.78"])
        # The pair is one hold and it ends the plan: entry 1 is the instance data, param 4 its script value (0 not started,
        # 1 in progress, 2 Mograine died once, 3 revived, 4 both dead: not an encounter state, so flag 8 alone, never 40), and
        # both entries are the event's mobs.
        holds = [r for r in rows if r["kind"] == "hold"]
        self.assertEqual(len(holds), 1)
        h = holds[0]
        self.assertEqual((h["leg"], h["seq"], h["entry"], h["param"], h["flags"], h["radius"], h["timeout_s"], h["entries"]),
                         ("1", "52", "1", "4", "8", "35", "600", "3976,3977"))
        self.assertEqual((h["x"], h["y"], h["z"]), ("1140", "1398", "31.0314"))
        self.assertEqual(leg_steps(rows, 1)[-1]["kind"], "hold")
        self.assertAlmostEqual(dist(h, 1153.87, 1398.39), 13.9, places=1)  # 14 yd west of Mograine, 62 yd off Whitemane
        # No boss row for Whitemane and no row on her spawn (the pair is fought where Mograine stands).
        self.assertEqual([(r["entry"], r["entries"], r["x"], r["y"]) for r in rows if r["kind"] == "boss"],
                         [("4542", "4540", "1158.86", "1354.27")])
        self.assertFalse([r for r in rows if r["x"] == "1202.13" and r["y"] == "1399.07"])
        # Every chapel stand in leg 1 (an approach move) keeps more than 30 yd from Mograine.
        stands = [r for r in leg_steps(rows, 1) if r["kind"] == "move" and r["comment"].endswith("approach")]
        self.assertEqual(len(stands), 7)
        for r in stands:
            self.assertGreater(dist(r, 1153.87, 1398.39), 30.0, r["comment"])
        # The boss table's DELETE is scoped to this wing's three entries, never all of map 189 (four plans share the map).
        self.assertIn("DELETE FROM `ai_dungeon_boss` WHERE `map` IN (189) AND `entry` IN (4542, 3976, 3977);", text)

    def test_scarlet_graveyard_is_the_wing_with_the_census_fixes(self):
        # 20261023060000 (graveyard census 2026-10-23, then r23 slot 6): 5 legs, 47 pulls, 4 bosses; 6547 (faction 68 Horde
        # civilian) and 4075 (rat) are out of every group and four pure groups are gone; Vishas' Scryer and Scorn's guard are
        # boss.first; the hall leg's trash stays on the way to Thalnos and the Duke leg is third, its 36-spawn crypt pull
        # (radius 56.8) the plan's one needs_walk point; Scorn is a boss-only last leg (his spawn is in the inactive game event
        # 81, r23 paused on him as step 51 of 133) and the two leg-0 fights that killed the lead and wiped the party are split
        # into pulls of at most 3-4 census units.
        name, text = newest_plan_text("scarlet_graveyard")
        self.assertEqual(name, "20261023060000_world.sql")
        self.assertIn("('scarlet_graveyard', 189, '', '', '6490', 'turtle-agent 38db284 dungeons/scarlet_graveyard.json',", text)
        self.assertEqual(leg_rows(text, "scarlet_graveyard"),
                         {0: ("interrogator_vishas", ""), 1: ("graveyard_hall", ""), 2: ("bloodmage_thalnos", ""),
                          3: ("duke_dreadmoore", ""), 4: ("scorn", "")})
        rows = [r for r in self.step_rows(text) if r["plan"] == "scarlet_graveyard"]
        self.assertEqual(len(rows), 154)
        self.assertEqual({leg: len(leg_steps(rows, leg)) for leg in (0, 1, 2, 3, 4)}, {0: 15, 1: 37, 2: 29, 3: 50, 4: 23})
        pulls = {leg: [(r["entries"], r["radius"], r["x"], r["y"]) for r in leg_steps(rows, leg) if r["kind"] == "pull"]
                 for leg in (0, 1, 2, 3, 4)}
        self.assertEqual([len(pulls[0]), len(pulls[1]), len(pulls[2]), len(pulls[3]), len(pulls[4])], [13, 19, 11, 6, 0])
        # The split pulls: the 2x4306 group at radius 5 (it reached the patrolling Scryer's spawn at 11.8), the 4-unit
        # 4306/4293 pack as two pairs at radius 2.5, the jailer patrol at radius 5 (it reached the other two jailers at 9.3
        # and 9.5 yd), the crypt cluster as a group of three at radius 9 and the two jailers at radius 6.
        self.assertIn(("4306", "5", "1752.71", "1124.83"), pulls[0])              # group 3, step 5 of the old rows
        self.assertIn(("4306,4293", "2.5", "1755.58", "1145.74"), pulls[0])       # group 4, split pair A
        self.assertIn(("4306,4293", "2.5", "1758.72", "1146.27"), pulls[0])       # group 5, split pair B
        self.assertIn(("61971", "5", "1824.24", "1120.95"), pulls[0])             # group 8, the Jailer's patrol
        self.assertIn(("4306,4293", "9", "1822.84", "1120.48"), pulls[0])         # group 9, three units
        self.assertIn(("61971", "6", "1833.29", "1121.23"), pulls[0])             # group 10, the two jailers
        self.assertIn(("4283", "10", "1841.84", "1136.73"), pulls[0])             # the Sentry's patrol pull
        self.assertIn(("4293,4306,61971", "56.8", "1871.88", "1110.15"), pulls[3])  # the crypt pack, Duke leg
        self.assertIn(("4306,4293", "14", "1859.23", "1158.89"), pulls[3])        # the crypt room of 1.4 after the census
        # Boss order: Vishas, Thalnos, Duke, and Scorn's boss alone as the plan's last step.
        self.assertEqual([r["leg"] for r in rows if r["kind"] == "boss"], ["0", "2", "3", "4"])
        self.assertEqual([(r["entry"], r["entries"]) for r in rows if r["kind"] == "boss"],
                         [("3983", "4293"), ("4543", ""), ("61972", ""), ("14693", "6426")])
        self.assertEqual((rows[-1]["leg"], rows[-1]["kind"], rows[-1]["entry"]), ("4", "boss", "14693"))
        for row in rows:
            if row["kind"] == "pull":
                self.assertNotIn("6547", row["entries"].split(","), "the friendly Suffering Victim is still a pull member")
                self.assertNotIn("4075", row["entries"].split(","), "a Rat is still a pull member")
        # The boss table's DELETE is scoped to this wing's four entries, never all of map 189 (four plans share the map).
        self.assertIn("DELETE FROM `ai_dungeon_boss` WHERE `map` IN (189) AND `entry` IN (3983, 14693, 4543, 61972);", text)

    def step_rows(self, text):
        # Each ai_dungeon_step row of a migration as a dict of its columns, in file order.
        for block in re.findall(r"INSERT INTO `ai_dungeon_step` \(([^)]*)\) VALUES(.*?);\n", text, re.S):
            cols = [c.strip(" `") for c in block[0].split(",")]
            for row in re.findall(r"^\((.*)\)[,]?$", block[1], re.M):
                # A quoted value may hold commas (`entries`); the space after each comma is skipped before the match.
                values = [v.strip() for v in re.findall(r"\s*('(?:[^']|'')*'|[^,]+)", row)]
                yield dict(zip(cols, [v.strip("'") for v in values]))

    def test_stormwind_stockade_is_the_plan_with_the_pack_fixes(self):
        # 20261022210100 (turtle-agent 1bb93ed, 2026-10-22): the Stormwind Stockade draft with the pack fixes of PREP-STOCKADE-2026-10-22.md, no row
        # hand-set, with the boss rows of map 34. 5 legs, 95 steps (14 + 8 + 9 + 3 + 0 pulls), bosses in leg order, no door or event step. The plan row
        # has the unit lists; the packs that stand with a boss are that boss row's entries, not pulls; Bruegal 1720 is in a pull, not skipped.
        name, text = newest_plan_text("stormwind_stockade")
        self.assertEqual(name, "20261022210100_world.sql")
        self.assertIn("('stormwind_stockade', 34, '1711,1715,1706', '1711', '1717', 'turtle-agent 1bb93ed dungeons/stormwind_stockade.json', 'plan v1, draft (not mapped, not live-checked)');", text)
        self.assertEqual(len(leg_rows(text, "stormwind_stockade")), 5)
        rows = [r for r in self.step_rows(text) if r["plan"] == "stormwind_stockade"]
        self.assertEqual(len(rows), 95)
        self.assertEqual([len([r for r in leg_steps(rows, leg) if r["kind"] == "pull"]) for leg in range(5)], [14, 8, 9, 3, 0])
        self.assertEqual([r["entry"] for r in rows if r["kind"] == "boss"], ["1696", "1663", "1666", "1717", "1716"])
        self.assertEqual({r["kind"] for r in rows}, {"move", "pull", "boss"})
        # The last step of every leg is its boss; the boss rows' entries are the packs that stand with them (Hamhock's guards are a creature_group
        # and come with him: his row names none, since the 30 yd of an entries list would hold the Insurgent of Bazil's pair).
        for leg in range(5):
            self.assertEqual(leg_steps(rows, leg)[-1]["kind"], "boss", leg)
        self.assertEqual([r["entries"] for r in rows if r["kind"] == "boss"], ["1706,1707", "1706,1708", "1715,1708", "", "1715,1711"])
        pulls = [r for r in rows if r["kind"] == "pull"]
        self.assertEqual([r["entries"] for r in pulls if "1720" in r["entries"].split(",")], ["1708,1706,1720"])
        # No pull of leg 4 (the boss row is the fight), and the Inmates in front of Dextren are the last pull of leg 1 with a radius that stops short of him.
        self.assertEqual(leg_steps(rows, 4)[0]["kind"], "move")
        last = [r for r in leg_steps(rows, 1) if r["kind"] == "pull"][-1]
        self.assertEqual((last["entries"], float(last["radius"])), ("1708", 7.0))
        self.assertEqual(re.findall(r"(?m)^DELETE FROM `ai_dungeon_step` WHERE `plan` = '(\w+)';", text), ["stormwind_stockade"])
        self.assertIn("DELETE FROM `ai_dungeon_boss` WHERE `map` IN (34);", text)

    def test_sunken_temple_statues_are_proved_and_the_boss_is_a_units_hold(self):
        # 20261024140000 (turtle-agent st-proof 067f8ed, 2026-10-24): 20261024060100 again, with leg 0
        # (atal'alarion)'s six statue steps as open_proof rows (done by the respawned light 148883) and the
        # Atal'alarion hold done by its units instead of instance data 4. Run 2 (r28 slot6) paused six times with
        # "failed: The object showed no change after the bot used it." (one per statue: a plain open never sees the
        # light) and twice on the data-4 hold (SetData(TYPE_ATALARION, IN_PROGRESS) ends by writing IN_PROGRESS (1)
        # over the DONE (3) the sixth statue set, instance_sunken_temple.cpp:351, so data 4 never reads 3 with the
        # boss dead). The six Atal'ai statues 148830-148835 (guids 27898, 27899, 28111-28114) are used in that exact
        # entry order (a wrong statue triggers a trap and does not advance the counter); the right one sets
        # GO_FLAG_NO_INTERACT on itself and respawns the green light 148883 within INTERACTION_DISTANCE of it.
        # Census unit lists, radii, pulls and boss rows are unchanged. 6 legs, 2397 steps
        # (16 + 37 + 25 + 8 + 2 + 1 pulls), bosses in leg order; 6 open_proof rows and 1 hold row in leg 0.
        name, text = newest_plan_text("sunken_temple")
        self.assertEqual(name, "20261024140000_world.sql")
        self.assertIn("('sunken_temple', 109, '5259,5273,5269,5226', '5259', '5271,5280,5291', 'turtle-agent 067f8ed dungeons/sunken_temple.json', 'plan v1, draft (not mapped, not live-checked)');", text)
        self.assertEqual(len(re.findall(r"(?m)^\('sunken_temple', \d+, '(?:[^']|'')*', '(?:[^']|'')*'\)[,;]$", text)), 6)
        self.assertEqual([n.replace("''", "'") for n in re.findall(r"(?m)^\('sunken_temple', \d+, '((?:[^']|'')*)', '(?:[^']|'')*'\)[,;]$", text)],
                         ["atal'alarion", "weaver", "dreamscythe", "jammal'an_the_prophet", "morphaz", "shade_of_eranikus"])
        rows = [r for r in self.step_rows(text) if r["plan"] == "sunken_temple"]
        self.assertEqual(len(rows), 2397)
        self.assertEqual([len([r for r in leg_steps(rows, leg) if r["kind"] == "pull"]) for leg in range(6)], [16, 37, 25, 8, 2, 1])
        self.assertEqual([r["entry"] for r in rows if r["kind"] == "boss"], ["5720", "5721", "5710", "5719", "5709"])
        self.assertEqual({r["kind"] for r in rows}, {"move", "pull", "boss", "open_proof", "hold"})
        self.assertEqual(leg_steps(rows, 0)[-1]["kind"], "hold")
        for leg in range(1, 6):
            self.assertEqual(leg_steps(rows, leg)[-1]["kind"], "boss", leg)
        # The six statue rows are open_proof at the same points as before, radius 10, no item/param/flags/timeout,
        # entries 148883, in the same order.
        leg0 = leg_steps(rows, 0)
        proofs = [r for r in leg0 if r["kind"] == "open_proof"]
        self.assertEqual([(r["entry"], r["x"], r["y"], r["z"]) for r in proofs],
                         [("148830", "-515.553", "95.2582", "-148.74"),
                          ("148831", "-419.849", "94.4837", "-148.74"),
                          ("148832", "-491.4", "135.97", "-148.74"),
                          ("148833", "-491.491", "53.4818", "-148.74"),
                          ("148834", "-443.855", "136.101", "-148.74"),
                          ("148835", "-443.417", "53.8312", "-148.74")])
        for r in proofs:
            self.assertEqual((r["radius"], r["item"], r["param"], r["flags"], r["timeout_s"], r["entries"]),
                             ("10", "0", "0", "0", "0", "148883"), r["seq"])
        # No hold row between the last statue and the boss approach (the old entry-4 flags-40 row is gone);
        # leg 0's last row is the units hold.
        hold = [r for r in leg0 if r["kind"] == "hold"]
        self.assertEqual(len(hold), 1)
        self.assertEqual((hold[0]["entry"], hold[0]["x"], hold[0]["y"], hold[0]["z"]), ("0", "-460", "96.8", "-189.58"))
        self.assertEqual((hold[0]["radius"], hold[0]["param"], hold[0]["flags"], hold[0]["timeout_s"], hold[0]["entries"]),
                         ("30", "0", "0", "300", "8580"))
        self.assertEqual(leg0[-1]["seq"], hold[0]["seq"])
        self.assertIn("8580 counts while still friendly", hold[0]["comment"])
        # The boss approach is the three walkway points (the fourth, the hold spot, is dropped by hold());
        # the hold is the leg's last row, so nothing follows the approach but the hold.
        approach = [r for r in leg0 if r["comment"] == "boss approach"]
        self.assertEqual([(float(r["x"]), float(r["y"]), float(r["z"])) for r in approach],
                         [(-349.6, 99.4, -172.86), (-400.0, 98.2, -172.93), (-435.0, 97.4, -181.49)])
        self.assertLess(leg0.index(proofs[-1]), leg0.index(approach[0]))
        # THE Z FIX: the interpolated rows of old seq 764, 765, 767, 768 (z -177.55, -182.97, -182.09, -174.60 at
        # x -349.7, -399.4, -410.6, -361.2) are gone: nothing is cut between -300 100.5 and the first approach
        # point, or after the hold. No leg-0 move row with x in [-420, -345] stands below z -173.5 (the lessons
        # floor is -172.9 there).
        for r in leg0:
            if r["kind"] == "move" and -420 <= float(r["x"]) <= -345:
                self.assertGreaterEqual(float(r["z"]), -173.5, (r["seq"], r["x"], r["z"]))
        via = [r for r in leg_steps(rows, 1) if r["comment"] == "via"]
        self.assertEqual([(float(r["x"]), float(r["y"]), float(r["z"])) for r in via],
                         [(-460.0, 96.8, -189.58), (-435.0, 97.4, -181.49), (-400.0, 98.2, -172.93),
                          (-349.6, 99.4, -172.86), (-300.0, 100.5333, -172.13)])
        # Pull group 8's stand is unchanged: on the pack floor, 15-25 yd from the pack, same z band.
        pg8 = [r for r in leg0 if r["kind"] == "pull" and r["comment"] == "pull group 8"][0]
        stand8 = [r for r in leg0 if r["kind"] == "move" and r["comment"] == "pull group 8 approach"]
        self.assertEqual(len(stand8), 1)
        self.assertEqual([(float(r["x"]), float(r["y"]), float(r["z"])) for r in stand8], [(-455.0, 95.8, -189.55)])
        self.assertLessEqual(math.dist((float(stand8[0]["x"]), float(stand8[0]["y"])),
                                       (float(pg8["x"]), float(pg8["y"]))), 25.0)
        self.assertGreaterEqual(math.dist((float(stand8[0]["x"]), float(stand8[0]["y"])),
                                          (float(pg8["x"]), float(pg8["y"]))), 15.0)
        self.assertLessEqual(abs(float(stand8[0]["z"]) - float(pg8["z"])), 4.0)
        # Jammal'an's Ogom 5711 and Morphaz's Hazzas 5722 are the boss rows' `first` (their creature groups aggro together); the other
        # three have no linked add (no boss row of 8580: leg 0 ends in the units hold).
        self.assertEqual([r["entries"] for r in rows if r["kind"] == "boss"], ["", "", "5711", "5722", ""])
        pulls = [r for r in rows if r["kind"] == "pull"]
        # The three radii that reached the next pack (same entries 14.4-26.8 yd out) and the seven 5291 pulls with its 15 yd wander.
        self.assertEqual(float([r for r in leg_steps(rows, 0) if r["kind"] == "pull"][5]["radius"]), 17.5)
        self.assertEqual([float(r["radius"]) for r in leg_steps(rows, 3) if r["kind"] == "pull"][2:4], [14.3, 15.6])
        self.assertEqual([float(r["radius"]) for r in pulls if r["entries"] == "5291"], [17.0] * 7)
        # The 23 detour pulls have two approach moves each, plus the new single-stand pull group 8; the last stands on
        # the pack's own floor, 30 yd or less from the pull row (a cut path's move rows may lie between the two
        # approach points and the pull, and the statue walks put open_proof rows between, so find the last approach
        # move scanning back over those kinds).
        approaches = [r for r in rows if r["kind"] == "move" and r["comment"].startswith("pull group ") and r["comment"].endswith(" approach")]
        self.assertEqual(len(approaches), 47)
        self.assertEqual(len({(r["leg"], r["comment"].split()[2]) for r in approaches}), 24)
        for leg in range(6):
            steps = leg_steps(rows, leg)
            for i, pull in enumerate(steps):
                if pull["kind"] != "pull":
                    continue
                stand = None
                for j in range(i - 1, -1, -1):
                    if steps[j]["kind"] not in ("move", "stand"):
                        break
                    if steps[j]["kind"] == "move" and steps[j]["comment"].endswith(" approach"):
                        stand = steps[j]
                        break
                if stand is None:
                    continue
                d = math.dist((float(stand["x"]), float(stand["y"]), float(stand["z"])),
                              (float(pull["x"]), float(pull["y"]), float(pull["z"])))
                self.assertLessEqual(d, 30.0, (leg, pull["comment"]))
                self.assertLessEqual(abs(float(stand["z"]) - float(pull["z"])), 4.0, (leg, pull["comment"]))
        self.assertEqual(re.findall(r"(?m)^DELETE FROM `ai_dungeon_step` WHERE `plan` = '(\w+)';", text), ["sunken_temple"])
        # Entry-scoped on the shared map: only this plan's six bosses are deleted and replaced (the SM-wing rule on map 189).
    def test_razorfen_kraul_is_the_plan_with_the_pack_fixes(self):
        # 20261023120000 (turtle-agent rfk-fix5 32d3b491, 2026-10-23, after live run r24 slot8 stopped at step 15): the plan of
        # 20261023100000 again, with pull group 17's two approach moves reverted (r24: they led the lead bot onto the navmesh island
        # at 2076.5 1601.5 66.0), so leg 0 is 20261023040000's again and its hop from pull group 16 to 17 is the run-3 cut through
        # 2048.0 1578.7 64.6 (one resume there is accepted). The Ramtusk fix stays: leg overlord_ramtusk keeps the lead's floor,
        # pull group 13's approach 2129.3 1716 80.9, so the cut to it no longer walks the lower floor. No row hand-set, with the boss
        # rows of map 47 left in 20261022210300. 7 legs, 696 steps (25 moves fewer than 20261023040000's 591: the lower-floor approach
        # cut only), 32 + 20 + 8 + 14 + 17 + 10 + 22 pulls, bosses in leg order, no door or event step. The plan row has the unit
        # lists; the packs that stand with a boss are that boss row's entries, not pulls; Halmgar 4842 and the Blind Hunter 4425 are
        # pull members.
        name, text = newest_plan_text("razorfen_kraul")
        self.assertEqual(name, "20261023120000_world.sql")
        self.assertIn("('razorfen_kraul', 47, '4427,4515,4522,4440', '4427', '', 'turtle-agent 32d3b491 dungeons/razorfen_kraul.json', 'plan v1, draft (not mapped, not live-checked)');", text)
        self.assertEqual(len(leg_rows(text, "razorfen_kraul")), 7)
        rows = [r for r in self.step_rows(text) if r["plan"] == "razorfen_kraul"]
        self.assertEqual(len(rows), 696)
        # The opening stand is the lower-floor pocket 1994 1615 (the old 1987.53 1600.03 has the upper walkway overhead); the five
        # corridor groups all have radius 10 (group 0 was 25 and owned the next group's Quilguards; group 5's default was 19.3), and
        # seq 5 is the explicit corridor regroup move before pull group 4 (r19-19z37: the two ranged bots climbed the west ramp while
        # idle).
        first = leg_steps(rows, 0)
        self.assertEqual((first[0]["kind"], first[0]["x"], first[0]["y"], first[0]["z"]), ("move", "1994", "1615", "83.2"))
        self.assertEqual((first[1]["kind"], first[1]["entries"], first[1]["radius"]), ("pull", "4530", "10"))
        self.assertEqual([r["radius"] for r in first[2:5]], ["10", "10", "10"])
        self.assertEqual((first[5]["kind"], first[5]["x"], first[5]["y"], first[5]["z"], first[5]["comment"]),
                         ("move", "2000", "1635", "81", "pull group 4 approach"))
        # The two stands (r20-19z39: pulls 39 and 42 walked the long way; r22 slot5: the room's 2180 1604 was refused): 2110 1600 82.1
        # before the 4532 pull and the same 2110 1600 82.1 before the 4440 pull, whose radius is the explicit 10.
        stands = [(r["x"], r["y"], r["z"], r["comment"]) for r in leg_steps(rows, 0)
                  if r["kind"] == "move" and r["comment"] in ("pull group 21 approach", "pull group 24 approach")]
        self.assertEqual(stands, [("2110", "1600", "82.1", "pull group 21 approach"),
                                  ("2110", "1600", "82.1", "pull group 24 approach")])
        self.assertEqual([r["radius"] for r in leg_steps(rows, 0) if r["kind"] == "pull" and r["entries"] == "4440"], ["10"])
        # Step 20 (r23 slot8 paused once, "the lead bot is stuck 28.6 yd from 2048.0 1578.7 64.6"): the r24 slot8 run showed the
        # approach moves that replaced the corner led the party onto the navmesh island at 2076.5 1601.5 66.0, so the corner is back:
        # pull 17's hop from pull group 16 is the run-3 cut, 2048.0 1578.7 64.6 first (one resume there is accepted).
        self.assertEqual([(r["x"], r["y"], r["z"]) for r in leg_steps(rows, 0) if r["comment"] == "approach path"][:3],
                         [("2047.9999", "1578.6666", "64.60514"),
                          ("2050.1333", "1581.8666", "64.50514"),
                          ("2061.3333", "1623.2", "64.28699")])
        self.assertFalse([r for r in leg_steps(rows, 0) if r["comment"] == "pull group 17 approach"],
                         "pull 17's approach points are reverted")
        self.assertEqual([len([r for r in leg_steps(rows, leg) if r["kind"] == "pull"]) for leg in range(7)], [32, 20, 8, 14, 17, 10, 22])
        # Step 289 (r23 slot8: `cmd move 2140.0 1724.5 53.0 refused: unreachable`): the last lower-floor pull's hop to pull group 13
        # is anchored on the lead's floor, 2129.3 1716 80.9, and its cut walks the upper floor only.
        ramtusk = leg_steps(rows, 3)
        after12 = next(i for i, r in enumerate(ramtusk) if r["comment"] == "pull group 12") + 1
        self.assertEqual([(r["kind"], r["x"], r["y"], r["z"], r["comment"]) for r in ramtusk[after12:after12 + 2]],
                         [("move", "2129.3", "1716", "80.9", "pull group 13 approach"),
                          ("move", "2129.5999", "1689.0667", "82.18699", "approach path")])
        self.assertTrue(all(float(r["z"]) > 80 for r in ramtusk[after12:] if r["kind"] == "move"),
                        "the Ramtusk approach cut walks the lower floor again")
        self.assertEqual([r["entry"] for r in rows if r["kind"] == "boss"], ["6168", "4424", "4428", "4420", "62503", "4422", "4421"])
        self.assertEqual({r["kind"] for r in rows}, {"move", "pull", "boss"})
        # The last step of every leg is its boss; the boss rows' entries are the packs that stand with them (Ramtusk's Spearhides,
        # 5-6 yd from him; Aggem and the two animal legs hold none).
        for leg in range(7):
            self.assertEqual(leg_steps(rows, leg)[-1]["kind"], "boss", leg)
        self.assertEqual([r["entries"] for r in rows if r["kind"] == "boss"], ["4516,4442", "", "4515,4523", "4438", "", "", "4517"])
        # No pull still owns Halmgar as an uncounted extra: his pull row names him, and leg 3 has no skip left.
        pulls = [r for r in rows if r["kind"] == "pull"]
        self.assertEqual([r["entries"] for r in pulls if "4842" in r["entries"].split(",")], ["4525,4842"])
        self.assertEqual([r["entries"] for r in pulls if "4425" in r["entries"].split(",")], ["4539,4425"])
        # No pull of leg 6 holds a Death's Head Priest under the den (the boss is 2 yd 2-D from the old priest pull): the boss row is
        # the fight; the one priest pull left stands 40 yd from her, with the Warden (leg 6 old group 13).
        self.assertEqual([(r["entries"], round(float(r["x"])), round(float(r["y"]))) for r in pulls if r["leg"] == "6" and "4517" in r["entries"].split(",")], [("4517,4437", 2189, 1832)])
        self.assertEqual(re.findall(r"(?m)^DELETE FROM `ai_dungeon_step` WHERE `plan` = '(\w+)';", text), ["razorfen_kraul"])
        # The ai_dungeon_boss rows stay in 20261022210300 (the boss seed is per map and a repeat would trip test_boss_table_is_the_seed);
        # this file replaces the plan rows only.

    def test_gnomeregan_is_the_plan_with_the_pack_fixes(self):
        # 20261023110000 (turtle-agent c8082bc3, 2026-10-23): 20261023070000 (the step-181 stand fix, itself 20261023020000 with the four pull
        # groups of the unattackable event NPCs 7897 and 7849 removed) carried over unchanged except leg 1's two boss approach moves: the ramp
        # foot -524 582.7 -230.56 and its top -541.58 534.7 -216.68 before Electrocutioner 6000 (r23 slot 4b, stopped at step 375 of 622:
        # the exporter's 50 yd cut of the hop to the boss interpolated -541.3 535.5 -222.1, 5.4 yd below the mesh, and the run leader refused
        # it as unreachable). 5 legs, 621 steps (87 + 37 + 17 + 24 + 4 pulls), bosses in leg order, no door or event step. The plan row has the
        # unit lists (6223 is kill_first, not cc_priority); the Agent at the Ambassador's own spot is that boss row's entries, not a pull.
        # Leg 0's cave chain (groups 23-29: the merged 6223/6233 pairs and the 6223/6233 camps) is radius 8 and each has an approach move on its
        # own floor.
        name, text = newest_plan_text("gnomeregan")
        self.assertEqual(name, "20261023110000_world.sql")
        self.assertIn("('gnomeregan', 90, '6392,6329', '6392', '6220,6226,6227,7849,6223', 'turtle-agent c8082bc3 dungeons/gnomeregan.json', 'plan v1, draft (not mapped, not live-checked)');", text)
        self.assertEqual(len(leg_rows(text, "gnomeregan")), 5)
        rows = [r for r in self.step_rows(text) if r["plan"] == "gnomeregan"]
        self.assertEqual(len(rows), 621)
        self.assertEqual([len([r for r in leg_steps(rows, leg) if r["kind"] == "pull"]) for leg in range(5)], [87, 37, 17, 24, 4])
        # Step 375 (1-based) is now the Electrocutioner's boss row; the failed exporter cut is gone, and its two approach moves stand before it.
        self.assertEqual((rows[374]["leg"], rows[374]["kind"], rows[374]["entry"]), ("1", "boss", "6235"))
        leg1 = leg_steps(rows, 1)
        self.assertEqual([(r["kind"], r["comment"]) for r in leg1[-3:]],
                         [("move", "boss approach"), ("move", "boss approach"), ("boss", "boss")])
        self.assertEqual([(float(r["x"]), float(r["y"]), float(r["z"])) for r in leg1[-3:-1]],
                         [(-524.0, 582.7, -230.56), (-541.58, 534.7, -216.68)])
        self.assertFalse([r for r in leg1 if abs(float(r["x"]) + 541.31885) < 0.001 and abs(float(r["y"]) - 535.48035) < 0.001],
                         "the off-mesh exporter cut is back")
        # Step 181 (1-based) is the stand before pull group 66's pull, and it is not in the bomb's corridor (x -573..-557, y 74..126).
        steps = leg_steps(rows, 0)
        i = [k for k, r in enumerate(steps) if r["kind"] == "pull" and r["comment"] == "pull group 66"][0]
        stand = steps[i - 1]
        self.assertEqual((int(stand["seq"]), stand["kind"], stand["x"], stand["y"], stand["z"], stand["comment"]),
                         (180, "move", "-572", "58", "-198.75", "pull group 66 approach"))
        self.assertEqual((steps[i]["entries"], float(steps[i]["radius"])), ("6329,6211", 17.5))
        dx = max(0.0, -573 - float(stand["x"]), float(stand["x"]) + 557)
        dy = max(0.0, 74 - float(stand["y"]), float(stand["y"]) - 126)
        self.assertGreaterEqual(math.hypot(dx, dy), 15, "the stand is inside the bomb's corridor")
        self.assertEqual([(r["kind"], int(r["seq"])) for r in steps[i:i + 3]],
                         [("pull", 181), ("pull", 182), ("pull", 183)])
        # The east-end-first cave chain: 7 pulls, radius 8, each with its approach move (E-427 239 ... W-514 263).
        chain = [r for r in leg_steps(rows, 0) if r["kind"] == "pull" and 23 <= int(r["comment"].rsplit(" ", 1)[1]) <= 29]
        self.assertEqual([(float(r["radius"]), r["entries"]) for r in chain],
                         [(8.0, "6233,6223"), (8.0, "6223"), (8.0, "6233"), (8.0, "6233"), (8.0, "6233,6223"), (8.0, "6223"), (8.0, "6223,6233")])
        for n in range(23, 30):
            self.assertIn("'pull group %d approach'" % n, text)
        self.assertEqual([r["entry"] for r in rows if r["kind"] == "boss"], ["7079", "6235", "6229", "6228", "7800"])
        self.assertEqual({r["kind"] for r in rows}, {"move", "pull", "boss"})
        # The last step of every leg is its boss; the Ambassador's boss row carries his Agent, the others name none.
        for leg in range(5):
            self.assertEqual(leg_steps(rows, leg)[-1]["kind"], "boss", leg)
        self.assertEqual([r["entries"] for r in rows if r["kind"] == "boss"], ["", "", "", "6212", ""])
        # The Agent pulled in front of the Ambassador is the leg's last pull, 17.9 yd from him (the boss-approach fight).
        last = [r for r in leg_steps(rows, 3) if r["kind"] == "pull"][-1]
        self.assertEqual((last["entries"], float(last["radius"])), ("6212", 10.0))
        self.assertEqual(re.findall(r"(?m)^DELETE FROM `ai_dungeon_step` WHERE `plan` = '(\w+)';", text), ["gnomeregan"])
        # The map's ai_dungeon_boss seed stays in 20261022210500 (the five bosses are unchanged): this file must not seed it again (the
        # boss seed test asserts every (map, entry) is seeded once).
        self.assertNotRegex(text, r"(?m)^(?:INSERT|REPLACE) INTO `ai_dungeon_boss`")

    def test_blackfathom_deeps_is_the_plan_with_the_pack_fixes(self):
        # 20261023080000 (turtle-agent a87a3f5, 2026-10-23): the Blackfathom Deeps draft with the pack fixes of PREP-BFD-MUSE-2026-10-22.md,
        # the leg `to` points of Ghamoo-ra and Lady Sarevess moved out of the deep-water pools (BFD-STEP1-2026-10-22.md, "Water points") and
        # the run-3 pull stands and Lady Sarevess water-edge point, no row hand-set, with the boss rows of map 48. The group 43 approach now
        # climbs the real ramp instead of the old step-67 ledge point. 7 legs, 636 steps (44 + 8 + 47 + 11 + 17 + 28 + 3 pulls), bosses in
        # leg order, no door or event step. The plan row has the unit lists; the packs that stand with a boss are that boss row's entries; the
        # priestess-darkweaver block is geographic packs; radii are cut to the groups' members (the overlap test is clean) and 12 groups carry patrol blocks.
        name, text = newest_plan_text("blackfathom_deeps")
        self.assertEqual(name, "20261023080000_world.sql")
        self.assertIn("('blackfathom_deeps', 48, '4820,4809,4813,4811,4799', '4820,4809', '4809', 'turtle-agent a87a3f5 dungeons/blackfathom_deeps.json', 'plan v1, draft (not mapped, not live-checked)');", text)

        # leg_rows() matches [A-Za-z0-9_] leg names only; ghamoo-ra, old_serra'kis and aku'mai are not
        # words, so the legs are counted from the step rows (each leg's boss step proves its rows are there).
        rows = [r for r in self.step_rows(text) if r["plan"] == "blackfathom_deeps"]
        self.assertEqual(len({r["leg"] for r in rows}), 7)
        self.assertEqual(len(rows), 636)
        self.assertEqual([len([r for r in leg_steps(rows, leg) if r["kind"] == "pull"]) for leg in range(7)], [44, 8, 47, 11, 17, 28, 3])
        self.assertEqual([r["entry"] for r in rows if r["kind"] == "boss"], ["4887", "4831", "6243", "62530", "4830", "4832", "4829"])
        self.assertEqual({r["kind"] for r in rows}, {"move", "pull", "boss"})
        # The last step of every leg is its boss; the boss rows' first are the packs that stand with them (Sarevess' linked guard, Gelihast's adds).
        for leg in range(7):
            self.assertEqual(leg_steps(rows, leg)[-1]["kind"], "boss", leg)
        self.assertEqual([r["entries"] for r in rows if r["kind"] == "boss"], ["", "4807", "4819,4820", "", "", "", ""])
        self.assertEqual(re.findall(r"(?m)^DELETE FROM `ai_dungeon_step` WHERE `plan` = '(\w+)';", text), ["blackfathom_deeps"])
        # The map 48 ai_dungeon_boss rows stay in 20261022210800 (the seed test holds them to one seeding).

    def test_zulfarrak_and_uldaman_final_counts(self):
        # Each plan's newest INSERT follows the DELETE of its old steps and legs, so its rows are the plan after all migrations.
        name, text = newest_plan_text("zulfarrak")
        self.assertEqual(name, "20261022210000_world.sql")
        self.assertEqual(len(leg_rows(text, "zulfarrak")), 17)
        self.assertEqual(len([r for r in self.step_rows(text) if r["plan"] == "zulfarrak"]), 232)
        name, text = newest_plan_text("uldaman")
        self.assertEqual(name, "20261022170000_world.sql")
        self.assertEqual(len(leg_rows(text, "uldaman")), 14)
        self.assertEqual(len([r for r in self.step_rows(text) if r["plan"] == "uldaman"]), 139)

    def test_frostmane_hollow_plan_is_whole(self):
        # 20261022210030 (turtle-agent b8d3f62, 2026-10-22): the first rows of the draft plan, exported with --allow-draft --gm-cross-direct
        # --approach-hops, with the census fixes (the units the core links to a boss are the boss row's entries). The plan file has no event, door or gossip step, so its 4 legs are only moves, pulls and one boss each. Every pin
        # below fails when a leg, a pack or a boss row is cut.
        name, text = newest_plan_text("frostmane_hollow")
        self.assertEqual(name, "20261022210030_world.sql")
        self.assertIn("('frostmane_hollow', 822, '63137,63139', '', '63136,63132,36519,82', 'turtle-agent b8d3f62 dungeons/frostmane_hollow.json', "
                      "'plan v1, draft (not mapped, not live-checked)');", text)
        legs = re.findall(r"^\('frostmane_hollow', (\d+), '((?:[^']|'')+)', ''\)[,;]$", text, re.M)
        self.assertEqual([(int(n), leg.replace("''", "'")) for n, leg in legs],
                         [(0, "battlemaster_ubukaz"), (1, "kan'za_the_seer"), (2, "tan'sha_the_sleek"), (3, "hailar_the_frigid")])
        rows = [r for r in self.step_rows(text) if r["plan"] == "frostmane_hollow"]
        self.assertEqual(len(rows), 413)
        self.assertEqual({k: len([r for r in rows if r["kind"] == k]) for k in {r["kind"] for r in rows}}, {"move": 326, "pull": 83, "boss": 4})
        self.assertEqual({leg: len(leg_steps(rows, leg)) for leg in range(4)}, {0: 53, 1: 205, 2: 45, 3: 110})
        self.assertEqual({leg: len([r for r in rows if r["leg"] == str(leg) and r["kind"] == "pull"]) for leg in range(4)}, {0: 16, 1: 49, 2: 15, 3: 3})
        # One boss per leg, last in the leg, in the plan's order Ubukaz, Kan'za, Tan'sha, Hailar; Hailar's five Ritualists 36519 (each
        # linked to him in the core, channelling Frostmane Ritual on him) are no pull any more: the boss row lists 36519, as it lists Kan'za's two
        # Snowcallers 63137 and Handler Oboka 63132 (both linked to their boss); the lone linked units are not pulls.
        self.assertEqual([r["entry"] for r in rows if r["kind"] == "boss"], ["63131", "63129", "63133", "63130"])
        for leg in range(4):
            self.assertEqual(leg_steps(rows, leg)[-1]["kind"], "boss", leg)
            self.assertEqual(leg_steps(rows, leg)[0]["kind"], "move", leg)
            self.assertEqual(len([r for r in leg_steps(rows, leg) if r["comment"] == "to"]), 1, leg)
        self.assertEqual([r["entries"] for r in rows if r["kind"] == "boss"], ["", "63137", "63132", "36519"])
        self.assertEqual([r["entries"] for r in leg_steps(rows, 3) if r["kind"] == "pull"].count("36519"), 0)
        self.assertNotIn("63132", [r["entries"] for r in leg_steps(rows, 2) if r["kind"] == "pull"])
        for r in rows:
            self.assertEqual((r["item"], r["param"], r["timeout_s"]), ("0", "0", "0"), r)

    def test_frostmane_hollow_rows_pass_the_loader_checks(self):
        # Every step row goes through the loader's own code (DungeonPlanPolicy.h, compiled from tools/test_dungeon_plan_rows.cpp): a row
        # the loader refuses is skipped at startup and the run pauses before it.
        compiler = shutil.which("g++")
        if compiler is None:
            self.skipTest("g++ is not installed")
        _name, text = newest_plan_text("frostmane_hollow")
        rows = [r for r in self.step_rows(text) if r["plan"] == "frostmane_hollow"]
        lines = ["P\tfrostmane_hollow\t822"]
        for r in rows:
            lines.append("\t".join(["S", r["plan"], r["leg"], r["seq"], r["kind"], r["entry"], r["x"], r["y"], r["z"], r["radius"], r["item"],
                                    r["param"], r["flags"], r["timeout_s"], r["entries"]]))
        with tempfile.TemporaryDirectory() as tmp:
            binary = os.path.join(tmp, "rows")
            build = subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", os.path.join(ROOT, "tools/test_dungeon_plan_rows.cpp"), "-o", binary],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([binary], input="\n".join(lines) + "\n", capture_output=True, text=True)
        self.assertEqual(run.stdout.strip().splitlines()[-1], "rows %d refused 0" % len(rows), run.stdout)
        self.assertEqual(run.returncode, 0, run.stdout)

    def test_exact_flag_only_on_a_move_before_its_direct_step(self):
        # flags 64 (kPlanFlagExact, DungeonPlanPolicy.h): the loader refuses it on any kind but move, and it marks the launch
        # point of the move_direct right after it in the same leg (the run leader sends that hop only after the exact proof).
        for name, text in migrations():
            rows = list(self.step_rows(text))
            for i, row in enumerate(rows):
                if int(row["flags"]) & 64 == 0:
                    continue
                self.assertEqual(row["kind"], "move", "%s: the exact flag on a %s row" % (name, row["kind"]))
                self.assertLess(i + 1, len(rows), "%s: an exact move is the last row" % name)
                after = rows[i + 1]
                self.assertEqual((after["plan"], after["leg"], after["kind"]), (row["plan"], row["leg"], "move_hop_exact"),
                                 "%s: the exact move %s/%s/%s is not followed by its move_hop_exact" % (name, row["plan"], row["leg"], row["seq"]))

    def test_hop_exact_follows_its_exact_move(self):
        # move_hop_exact (DungeonPlanPolicy.h kPlanKindMoveHopExact) is the hop launched from the flag 64 move right before it in
        # its leg; a module without exact waypoints skips the unknown kind (no direct order for the hop).
        for name, text in migrations():
            rows = list(self.step_rows(text))
            for i, row in enumerate(rows):
                if row["kind"] != "move_hop_exact":
                    continue
                self.assertGreater(i, 0, "%s: a move_hop_exact is the first row" % name)
                before = rows[i - 1]
                self.assertEqual((before["plan"], before["leg"], before["kind"]), (row["plan"], row["leg"], "move"), name)
                self.assertTrue(int(before["flags"]) & 64, "%s: the row before the move_hop_exact %s/%s/%s is not an exact move" %
                                (name, row["plan"], row["leg"], row["seq"]))

    def test_inserted_kinds_fit_the_column(self):
        # ai_dungeon_step.kind is a varchar(16) (20261014150000): a longer kind fails the INSERT in strict mode, or is cut.
        for name, text in migrations():
            for row in self.step_rows(text):
                self.assertLessEqual(len(row["kind"]), 16, "%s: kind %r is longer than the column" % (name, row["kind"]))
        with open(os.path.join(ROOT, "ai/playerbot/DungeonPlanPolicy.h"), encoding="utf-8") as handle:
            policy = handle.read()
        for kind in re.findall(r'\{"(\w+)", PlanStepKind::', policy) + re.findall(r'kPlanKind\w+ = "(\w+)"', policy):
            self.assertLessEqual(len(kind), 16, "kind %r is longer than ai_dungeon_step.kind" % kind)

    def test_wailing_caverns_serpentis_foot_is_exact(self):
        # The newest migration that inserts the wailing_caverns steps carries the live-proven hop (tests/wc-hop-probe-2): the foot
        # -293.3 0.5 -58 as an exact move (flags 64), then the move_direct to the landing -287.5 5 -63.6 (pjerra/TortoiseBots
        # feat/exact-waypoint). A re-export from an exporter that writes no exact flag would launch the hop from 3-6 yd short.
        newest = None
        for name, text in migrations():
            if re.search(r"INSERT INTO `ai_dungeon_step`.*?\('wailing_caverns',", text, re.S):
                newest = (name, text)
        self.assertIsNotNone(newest)
        name, text = newest
        rows = [r for r in self.step_rows(text) if r["plan"] == "wailing_caverns"]
        exact = [i for i, r in enumerate(rows) if int(r["flags"]) & 64]
        self.assertEqual(len(exact), 1, "%s: the wailing_caverns rows need exactly one exact move" % name)
        foot, hop = rows[exact[0]], rows[exact[0] + 1]
        self.assertEqual((foot["kind"], foot["x"], foot["y"], foot["z"]), ("move", "-293.3", "0.5", "-58"), name)
        self.assertEqual((hop["kind"], hop["x"], hop["y"], hop["z"]), ("move_hop_exact", "-287.5", "5", "-63.6"), name)

    def test_wailing_caverns_kresh_pull(self):
        # 20261022210200 (turtle-agent e281a2b, 2026-10-22) re-exports the whole wailing_caverns plan with Kresh 3653 as a pull of his own
        # in leg 2 (cobrahn_a) at his spawn, radius 12, between the Ectoplasm-patrol pack and the Crocolisk pair; it replaces one approach
        # move (12 legs, 226 steps: 131 move + 1 move_hop_exact + 89 pull + 5 boss). Lady Anacondra stays a member of her four spot packs
        # (a boss row per spot would pause the run 'boss not found' three times in four) and the five boss rows are unchanged.
        name, text = newest_plan_text("wailing_caverns")
        self.assertEqual(name, "20261022210200_world.sql")
        self.assertIn("'turtle-agent e281a2b dungeons/wailing_caverns.json'", text)
        rows = [r for r in self.step_rows(text) if r["plan"] == "wailing_caverns"]
        kinds = {k: len([r for r in rows if r["kind"] == k]) for k in KINDS}
        self.assertEqual((len(rows), kinds["move"], kinds["move_hop_exact"], kinds["pull"], kinds["boss"]), (226, 131, 1, 89, 5), name)
        kresh = [r for r in rows if "3653" in r["entries"].split(",")]
        self.assertEqual(len(kresh), 1, name)
        self.assertEqual((kresh[0]["leg"], kresh[0]["kind"], kresh[0]["x"], kresh[0]["y"], kresh[0]["z"], kresh[0]["radius"], kresh[0]["entries"]),
                         ("2", "pull", "-64.4", "319.1", "-106.7", "12", "3653"), name)
        self.assertEqual(len([r for r in rows if r["kind"] == "pull" and "3671" in r["entries"].split(",")]), 4, name)
        self.assertEqual([r["entry"] for r in rows if r["kind"] == "boss"], ["3669", "3670", "3674", "3673", "5775"], name)
        self.assertEqual(len([r for r in rows if r["kind"] == "boss" and r["entry"] in ("3653", "3671")]), 0, name)

    def test_gossip_rows_fit_the_loader(self):
        # NormalizePlanStep (DungeonPlanPolicy.h): a gossip row needs its NPC entry, an option of 1-15 (the number `.bot cmd gossip` takes,
        # counted from 1: it refuses 0) and no flag but kPlanFlagDeadNpcIsDone (512; 20261022210000 sets it on Bly's row only, Weegli's
        # End Door needs him alive); the timeout is at most 600. A row the loader refuses is skipped with a log line and the
        # run then pauses at the step before it, so a migration must never carry one.
        seen = 0
        for name, text in migrations():
            for row in self.step_rows(text):
                if row["kind"] != "gossip":
                    continue
                seen += 1
                where = "%s: gossip %s/%s/%s" % (name, row["plan"], row["leg"], row["seq"])
                self.assertGreater(int(row["entry"]), 0, where + ": no NPC entry")
                self.assertTrue(1 <= int(row["param"]) <= 15, where + ": the option is outside 1-15 (counted from 1)")
                flags = int(row["flags"])
                self.assertIn(flags, (0, 512), where + ": a gossip carries no flag but 512 (dead NPC is done)")
                if flags == 512:
                    self.assertEqual(row["entry"], "7604", where + ": only Sergeant Bly's gossip counts a dead NPC as done")
                self.assertLessEqual(int(row["timeout_s"]), 600, where)
        self.assertGreaterEqual(seen, 1)

    def test_zulfarrak_end_door_is_a_hold_a_gossip_and_a_door_wait(self):
        # Weegli Blastfuse (7607) blows the End Door (146084) only once the pyramid phase (instance data 1) reads 8 (PYRAMID_KILLED_ALL_TROLLS,
        # tortoise-wow zulfarrak.h:40) and his menu then offers its one line (zulfarrak.cpp:497: index 0, so `cmd gossip` option 1).
        name, _text, zf = newest_zulfarrak_rows()
        rows = [r for r in zf if r["leg"] == "14"]
        self.assertEqual([r["kind"] for r in rows], ["move", "hold", "gossip", "wait_object"])
        hold, gossip, door = rows[1], rows[2], rows[3]
        # flag 8 alone: the phase is a script value, not an encounter state (no 32), and param 8 is above the encounter states 0-4
        self.assertEqual((hold["entry"], hold["param"], hold["flags"], hold["timeout_s"]), ("1", "8", "8", "600"))
        self.assertEqual((gossip["entry"], gossip["param"], gossip["flags"], gossip["timeout_s"]), ("7607", "1", "0", "0"))
        self.assertEqual((gossip["x"], gossip["y"], gossip["z"]), ("1877.52", "1199.63", "8.87"))
        # the door wait: object 146084 in state 2 (active_alt): Weegli's blast opens the door with the alternative state
        # (zulfarrak.cpp:423 DoUseDoorOrButton(EndDoorGUID, 0, true)), and wait_object compares exactly: the r8 run paused twice at
        # a state-1 wait on an open door. The order of the rows is the point.
        self.assertEqual((door["entry"], door["param"]), ("146084", "2"))
        # Sergeant Bly (leg 15): his gossip, then a hold done by its units (no done flag) with Bly first, so he counts while still
        # friendly; a boss row would leave the friendly Bly alone at once (DungeonRunPolicy.h FightStep).
        bly = [r for r in zf if r["leg"] == "15"]
        self.assertEqual([r["kind"] for r in bly], ["move", "gossip", "hold"], name)
        # flags 512 = kPlanFlagDeadNpcIsDone (20261022210000): his crew dies in the pyramid event's troll waves in some runs (r12, r15-19z33)
        # and a dead Bly then counts the gossip as done instead of pausing the run; Weegli's gossip (leg 14, above) keeps flags 0.
        self.assertEqual((bly[1]["entry"], bly[1]["param"], bly[1]["flags"]), ("7604", "1", "512"), name)
        hold = bly[2]
        self.assertEqual((hold["entry"], hold["param"], hold["flags"], hold["radius"], hold["timeout_s"], hold["entries"]),
                         ("0", "0", "0", "25", "300", "7604,7608,7606,7605"), name)
        self.assertEqual((hold["x"], hold["y"], hold["z"]), ("1884", "1212", "9"), name)
        # the Ukorz leg follows with the plain kinds, after Bly
        ukorz = [r for r in zf if r["leg"] == "16"]
        self.assertEqual(max(int(r["leg"]) for r in zf), 16, name + ": Ukorz is the last leg")
        self.assertEqual([r["kind"] for r in ukorz], ["move", "move", "pull", "pull", "pull", "pull", "pull", "boss"])
        self.assertEqual((ukorz[-1]["entry"], ukorz[-1]["entries"]), ("7267", "7797"))

    def test_zulfarrak_dead_bly_migration_changes_one_flag(self):
        # 20261022210000 is 20261021174716 with ONE row changed: Sergeant Bly's gossip (leg 15 seq 1) carries flags 512
        # (kPlanFlagDeadNpcIsDone). Every other step row, the legs and the plan row (but its source text) are equal.
        texts = dict(migrations())
        old_name, new_name = "20261021174716_world.sql", "20261022210000_world.sql"
        self.assertIn(old_name, texts)
        self.assertIn(new_name, texts)
        old = [r for r in self.step_rows(texts[old_name]) if r["plan"] == "zulfarrak"]
        new = [r for r in self.step_rows(texts[new_name]) if r["plan"] == "zulfarrak"]
        self.assertEqual(len(old), len(new))
        changed = [(o, n) for o, n in zip(old, new) if o != n]
        self.assertEqual(len(changed), 1)
        o, n = changed[0]
        self.assertEqual((n["leg"], n["seq"], n["kind"], n["entry"]), ("15", "1", "gossip", "7604"))
        self.assertEqual((o["flags"], n["flags"]), ("0", "512"))
        self.assertEqual(dict(o, flags="512"), n)
        self.assertEqual(leg_rows(texts[old_name], "zulfarrak"), leg_rows(texts[new_name], "zulfarrak"))

    def test_gossip_kind_is_in_the_policy_tables(self):
        # The kind text the exporter writes is the one the loader parses and the status line prints.
        with open(os.path.join(ROOT, "ai/playerbot/DungeonPlanPolicy.h"), encoding="utf-8") as handle:
            policy = handle.read()
        self.assertIn('{"gossip", PlanStepKind::Gossip}', policy)
        self.assertIn('case PlanStepKind::Gossip: return "gossip";', policy)

    def test_hold_rows_follow_the_loader_rules(self):
        # NormalizePlanStep (DungeonPlanPolicy.h) for a hold: at most one done flag (8 data, 16 object); the encounter flag 32 only
        # with 8 and then a state 0-4; by object a state 0-2 and an entry; with neither flag, entries. A plain script value (flag 8
        # without 32: Zul'Farrak's pyramid phase) may be any number.
        for name, text in migrations():
            for row in self.step_rows(text):
                if row["kind"] != "hold":
                    continue
                flags, param, where = int(row["flags"]), int(row["param"]), "%s: hold %s/%s/%s" % (name, row["plan"], row["leg"], row["seq"])
                self.assertFalse(flags & 8 and flags & 16, where + " has both done flags")
                if flags & 32:
                    self.assertTrue(flags & 8, where + ": the encounter flag without the data flag")
                    self.assertLessEqual(param, 4, where + ": no encounter state")
                if flags & 16:
                    self.assertNotEqual(row["entry"], "0", where)
                    self.assertLessEqual(param, 2, where)
                if not flags & 24:
                    self.assertTrue(row["entries"], where + ": done by its units without entries")

    def test_zulfarrak_runs_the_stairs_event(self):
        # The newest migration that inserts the zulfarrak steps (20261021174713, the graves on 20261021115557, 20261021095914 before; turtle-agent plan-export of the plan with the stairs
        # holds, Weegli's door and Ukorz) carries Zum'rah, the Executioner's key to the cage, the three holds on the pyramid phase
        # (instance data 1: 3 = WAVE_1, 7 = WAVE_3 on the stairs top, 8 = PYRAMID_KILLED_ALL_TROLLS at the foot, script values with
        # flag 8 alone), Nekrum before Sezz'ziz at the end of leg 13 and Ukorz as the last row; a re-export from an exporter without hold values
        # would cut the plan at the stairs again (or write them as encounter states, which the loader refuses above 4).
        name, text, rows = newest_zulfarrak_rows()
        self.assertEqual([r["entry"] for r in rows if r["kind"] == "boss"], ["8127", "7272", "7795", "7271", "7274", "7275", "7267"], name)
        cage = [i for i, r in enumerate(rows) if r["kind"] == "open"]
        self.assertEqual(len(cage), 1, name)
        i = cage[0]
        self.assertEqual((rows[i]["entry"], rows[i]["item"]), ("141073", "8444"), name)
        loot = [r for r in rows[:i] if r["kind"] == "loot"]
        self.assertEqual((loot[-1]["entry"], loot[-1]["item"]), ("7274", "8444"), name + ": the key is looted before the cage")
        holds = [r for r in rows if r["kind"] == "hold"]
        self.assertEqual([(r["entry"], r["param"], r["flags"], r["timeout_s"]) for r in holds],
                         [("1", "3", "8", "90"), ("1", "7", "8", "600"), ("1", "8", "8", "600"), ("0", "0", "0", "300")], name)
        self.assertEqual(rows[i + 1]["kind"], "hold", name + ": the first hold right after the cage (the event's start)")
        for r in holds[:2]:
            self.assertEqual((r["x"], r["y"], r["z"], r["entries"]), ("1887", "1274", "42", "7787,7788,7789,8876,8877"), name)
        self.assertEqual((holds[2]["leg"], holds[2]["x"], holds[2]["y"], holds[2]["z"], holds[2]["entries"]),
                         ("14", "1877.5", "1199.6", "8.9", "7787,7788,7789,8876,8877"), name)
        leg13 = [r for r in rows if r["leg"] == "13"]
        self.assertEqual(leg13[-2]["entries"], "7796", name + ": Nekrum before Sezz'ziz")
        self.assertEqual(leg13[-1]["kind"], "wait_clear", name)
        self.assertEqual((rows[-1]["kind"], rows[-1]["leg"]), ("boss", "16"), name + ": Ukorz ends the plan")
        self.assertIn("BotAreaTriggers = 962", text, name + ": the header names the trigger setting Zum'rah needs")

    def test_zulfarrak_antusul_pulls_the_pair_before_the_camp_from_stands(self):
        # r2-19z19 step 26: with the camp before the pair and no stand, the run leader stopped 30 yd short of the camp centre, 15.4 yd from the
        # pair's Shadowcaster, and the walk began the fight. The pair (5648,7247) is pulled first, from the stand 1620 880; the camp
        # (5648,5650) after it, from 1630 878; the north pack last. Each pull is right behind its stand move.
        name, _text, rows = newest_zulfarrak_rows()
        leg = [r for r in rows if r["leg"] == "3"]
        pulls = [(i, r) for i, r in enumerate(leg) if r["kind"] == "pull"]
        order = [r["entries"] for _i, r in pulls]
        self.assertEqual(order[-3:], ["5648,7247", "5648,5650", "7246,5648,5649"], name + ": the pair, the camp, the north pack")
        for (i, r), stand in ((pulls[-3], ("1620", "880")), (pulls[-2], ("1630", "878"))):
            move = leg[i - 1]
            self.assertEqual((move["kind"], move["x"], move["y"]), ("move", stand[0], stand[1]), name + ": a stand before " + r["entries"])
        # the stands keep 20 yd from every later pack and 15 yd from the patrol path (the plan check of turtle-agent measures it); here the
        # pair's stand is west of 1625 and the camp's stand west of 1635, so neither is near the Shadowcaster camp at 1660 901
        for stand, limit in ((leg[pulls[-3][0] - 1], 1625), (leg[pulls[-2][0] - 1], 1635)):
            self.assertLess(float(stand["x"]), limit, name)
        self.assertEqual(max(int(r["leg"]) for r in rows), 16, name)

    def test_shipped_zulfarrak_has_no_gahzrilla_leg(self):
        # The cold review of 2026-10-21: the shipped rows never carry the gong (no player or unattended run has the Mallet, so every
        # run would pause there); the leg is the opt-in file's only.
        name, text, rows = newest_zulfarrak_rows()
        self.assertNotIn("gahzrilla", [n for n, _c in leg_rows(text, "zulfarrak").values()], name)
        self.assertFalse([r for r in rows if r["kind"] == "use_on" or r["entry"] == "7273"], name)

    def test_zulfarrak_gahzrilla_opt_in_is_the_migration_plus_the_leg(self):
        # tools/optional-plans/zulfarrak-gahzrilla.sql (never auto-applied) is the newest zulfarrak migration with the gahzrilla leg
        # added: the same plan row, and the same leg and step rows once the gahzrilla leg is taken out and the legs after it are
        # numbered one lower. Nothing else may differ (a stale opt-in file would put an older plan on a test box).
        name, text, rows = newest_zulfarrak_rows()
        opt = OPT_IN.read_text()
        opt_rows = [r for r in self.step_rows(opt) if r["plan"] == "zulfarrak"]
        gl = [n for n, (leg, _c) in leg_rows(opt, "zulfarrak").items() if leg == "gahzrilla"]
        self.assertEqual(len(gl), 1)
        g = gl[0]
        def renumber(r):
            r = dict(r)
            r["leg"] = str(int(r["leg"]) - 1) if int(r["leg"]) > g else r["leg"]
            return r
        # The one difference the export makes on its own: the long-hop cuts ('path' moves, --mmaps) at the start of the leg after
        # gahzrilla are measured from where that leg begins, Gahz'rilla's spot instead of the leg before. Those rows, and that
        # leg's seq numbers, are left out of the comparison; every other column of every other row must match.
        def key(r, nxt):
            r = dict(r)
            if r["leg"] == str(nxt):
                r.pop("seq")
            return r
        mine = [key(renumber(r), g) for r in opt_rows if int(r["leg"]) != g and not (int(r["leg"]) == g + 1 and r["comment"] == "path")]
        theirs = [key(r, g) for r in rows if not (int(r["leg"]) == g and r["comment"] == "path")]
        self.assertEqual(mine, theirs, name + ": the opt-in rows besides the gahzrilla leg")
        legs = {(n - 1 if n > g else n): v for n, v in leg_rows(opt, "zulfarrak").items() if n != g}
        self.assertEqual(legs, leg_rows(text, "zulfarrak"))
        plan_row = lambda t: re.search(r"^\('zulfarrak', 209, .*\);$", t, re.M).group(0)
        self.assertEqual(plan_row(opt), plan_row(text))
        update = "UPDATE `ai_dungeon_plan` SET `cc_priority` = '7246,7247,5648' WHERE `plan` = 'zulfarrak';"
        self.assertTrue(text.rstrip().endswith(update) and opt.rstrip().endswith(update))
        self.assertIn("NOT A MIGRATION", opt.split("DELETE FROM")[0])
        self.assertIn("re-apply data/sql/world/" + name, opt.split("DELETE FROM")[0])

    def test_zulfarrak_gahzrilla_rings_the_gong_with_the_mallet(self):
        # 20261021174716: the optional gahzrilla leg is in the rows (turtle-agent plan-export --with-optional gahzrilla): the pool-ring
        # pulls, the stand 1652.84 1168.99 10.67, then ONE use_on of the gong 141832 at its point with the Mallet of Zul'Farrak 9240 (the
        # order `cmd use 9240 <carrier> entry:141832@1650.9,1171.9,10.9`; a plain open of the gong does nothing), then Gahz'rilla 7273
        # where his walk-out ends. The Mallet is GM-added to the lead bot before the run (no bot can get it); the header says so.
        name = OPT_IN.name
        text = OPT_IN.read_text()
        rows = [r for r in self.step_rows(text) if r["plan"] == "zulfarrak"]
        legs = leg_rows(text, "zulfarrak")
        self.assertEqual(legs[9][0], "gahzrilla", name)
        self.assertEqual(max(int(r["leg"]) for r in rows), 17, name + ": Ukorz is leg 17 with the gahzrilla leg")
        leg = [r for r in rows if r["leg"] == "9"]
        uses = [i for i, r in enumerate(leg) if r["kind"] == "use_on"]
        self.assertEqual(len(uses), 1, name + ": the gong is rung once (a second ring summons a new Gahz'rilla that dies at once, an invisible corpse at the pit)")
        i = uses[0]
        use = leg[i]
        self.assertEqual((use["entry"], use["item"], use["x"], use["y"], use["z"], use["param"], use["flags"]),
                         ("141832", "9240", "1650.9", "1171.9", "10.9", "0", "0"), name)
        self.assertEqual((leg[i - 1]["kind"], leg[i - 1]["x"], leg[i - 1]["y"]), ("move", "1652.84", "1168.99"), name + ": the stand first")
        self.assertEqual((leg[i + 1]["kind"], leg[i + 1]["entry"], leg[i + 1]["x"]), ("boss", "7273", "1702.58"), name)
        self.assertEqual(sum(1 for r in leg if r["kind"] == "pull"), 8, name + ": the pool ring is cleared first")
        self.assertIn(".additem 9240", text, name + ": the header names the Mallet's GM setup")
        self.assertIn("7604", text.split("DELETE FROM")[0], name + ": the header names Bly's BotGossipNpcs entry")

    def test_zulfarrak_graves_before_the_trigger(self):
        # Zum'rah's 31 Shallow Graves (128403) are opened one at a time before the trigger 962 (turtle-agent plan-export
        # --carrier-chests): each is a move to its stand (4 yd or less from the grave), the loot (gone-is-done flag 256, item 0)
        # and the sweep pull of 7286,7276 at the grave's point (radius 15). The walk keeps ZUMRAH_GRAVES' order. 128308 (the
        # script graves, not Zum'rah's) is never a row. The graves are read from the newest zulfarrak migration.
        name, _text, rows = newest_zulfarrak_rows()
        leg = next(r["leg"] for r in rows if r["kind"] == "boss" and r["entry"] == "7271")
        steps = [r for r in rows if r["leg"] == leg]
        self.assertEqual([int(r["seq"]) for r in steps], list(range(len(steps))), name + ": the zumrah seq is not 0..n-1")
        self.assertEqual([r for r in rows if r["entry"] == "128308"], [], name + ": a 128308 row (script graves, not Zum'rah's)")
        loots = [i for i, r in enumerate(steps) if r["kind"] == "loot"]
        self.assertEqual(len(loots), len(ZUMRAH_GRAVES), name + ": the number of grave loot rows")
        for i, (gx, gy) in zip(loots, ZUMRAH_GRAVES):
            where = "%s: grave at seq %s" % (name, steps[i]["seq"])
            self.assertEqual((steps[i]["entry"], steps[i]["flags"], steps[i]["item"]), ("128403", "256", "0"), where)
            self.assertLessEqual(math.hypot(float(steps[i]["x"]) - gx, float(steps[i]["y"]) - gy), 0.1, where + ": not the plan's point")
            move, pull = steps[i - 1], steps[i + 1]
            self.assertEqual((move["kind"], move["comment"]), ("move", "stand"), where + ": not preceded by its stand")
            self.assertLessEqual(math.hypot(float(move["x"]) - gx, float(move["y"]) - gy), 4, where + ": the stand is more than 4 yd off")
            self.assertEqual((pull["kind"], pull["entries"], pull["radius"]), ("pull", "7286,7276", "15"), where + ": not followed by the sweep")
            self.assertLessEqual(math.hypot(float(pull["x"]) - float(steps[i]["x"]), float(pull["y"]) - float(steps[i]["y"])), 0.1,
                                 where + ": the sweep is not at the grave")
        trigger = [i for i, r in enumerate(steps) if (r["kind"], r["x"], r["y"], r["z"]) == ("move", "1903", "1014", "11.8")]
        boss = [i for i, r in enumerate(steps) if r["kind"] == "boss"]
        self.assertEqual((len(trigger), len(boss)), (1, 1), name + ": the trigger 962 and the boss 7271 are each one row")
        self.assertLess(loots[-1] + 1, trigger[0], name + ": the last grave's sweep is not before the trigger")
        self.assertLess(trigger[0], boss[0], name + ": the trigger is not before the boss")
        self.assertEqual(boss[0], len(steps) - 1, name + ": the boss does not end the zumrah leg")

    def test_zulfarrak_velratha_approach_stays_off_the_island_shadowhunter(self):
        # turtle-agent#286: the lone Shadowhunter 81610 stands on a navmesh island at 1591 1047.7 (no path leaves it; a fight there
        # takes the tank onto the island, zf-r6-19z24). Pull group 4's approach points were 1584 1050 and 1603 1043, 7.4 and 12.9 yd
        # from it; they are 1594 1011, 1598 1028 and 1606 1040 now, and the leg's comment carries the plan's `leave` list as text
        # (the run leader does not read it yet).
        name, text, rows = newest_zulfarrak_rows()
        legs = leg_rows(text, "zulfarrak")
        leg = next(k for k, (leg_name, _c) in legs.items() if leg_name == "velratha")
        steps = leg_steps(rows, leg)
        approach = [(float(r["x"]), float(r["y"])) for r in steps if r["kind"] == "move" and r["comment"] == "pull group 4 approach"]
        self.assertEqual(approach, [(1594.0, 1011.0), (1598.0, 1028.0), (1606.0, 1040.0)], name)
        for old in ((1584.0, 1050.0), (1603.0, 1043.0)):
            self.assertEqual([r for r in steps if (float(r["x"]), float(r["y"])) == old], [], "%s: the old approach point %s is back" % (name, old))
        for r in steps:
            if r["kind"] == "move":
                self.assertGreaterEqual(dist(r, 1591.0, 1047.7), 15, "%s: velratha move %s is within 15 yd of the island Shadowhunter" % (name, r["seq"]))
        # the approach is right before its pull, which still fetches the Shadowhunters and the Witch Doctor of the group
        after = steps[[r["comment"] for r in steps].index("pull group 4")]
        self.assertEqual((after["kind"], after["x"], after["y"]), ("pull", "1583", "1086.76"), name)
        self.assertEqual(int(after["seq"]) - 1, int(next(r for r in steps if r["x"] == "1606")["seq"]), name + ": the last approach is not right before its pull")
        comment = legs[leg][1]
        self.assertIn("leave (not read by the run leader yet)", comment, name)
        self.assertIn("81610", comment, name)
        self.assertIn("1591 1047.7", comment, name)
        # the graves of 20261021115557 stay: 31 loots of 128403 with the gone-is-done flag, none of 128308
        graves = [r for r in rows if r["kind"] == "loot" and r["entry"] == "128403"]
        self.assertEqual(len(graves), 31, name)
        self.assertEqual({r["flags"] for r in graves}, {"256"}, name)

    def test_uldaman_pulls_the_keepers_neighbours_before_the_boss(self):
        # PLAN-ULDAMAN-GRIMLOK #2 (turtle-agent#287): the boss 7206 was pulled with the Stone Steward 4860 (a patrol that walks to 13.6 yd
        # from it), the Sculptors and the Rocksmasher of the galgann leg alive 20-35 yd north, and 5-6 mobs joined (r4 slot1, r6 slot5).
        # They are pulled first, each from an approach move, in the Keeper's own leg; nothing of them is left in the galgann leg.
        name, text, rows = newest_uldaman_rows()
        legs = leg_rows(text, "uldaman")
        keeper = next(k for k, (leg_name, _c) in legs.items() if leg_name == "ancient_stone_keeper")
        galgann = next(k for k, (leg_name, _c) in legs.items() if leg_name == "galgann")
        steps = leg_steps(rows, keeper)
        boss = [i for i, r in enumerate(steps) if r["kind"] == "boss" and r["entry"] == "7206"]
        self.assertEqual(boss, [len(steps) - 1], name + ": the Stone Keeper is the one boss row and ends its leg")
        for entries, x, y in (("4860", -39.6, 272.8), ("7012,7011", -43.03, 263.93), ("7011", -46.4, 202.9)):
            pulls = [i for i, r in enumerate(steps) if r["kind"] == "pull" and r["entries"] == entries and dist(r, x, y) < 0.1]
            self.assertEqual(len(pulls), 1, "%s: the pull of %s at %s %s is missing or doubled" % (name, entries, x, y))
            self.assertLess(pulls[0], boss[0], "%s: the pull of %s comes after the Stone Keeper" % (name, entries))
            self.assertEqual(steps[pulls[0] - 1]["kind"], "move", name + ": the pull of %s has no approach move" % entries)
            self.assertIn("approach", steps[pulls[0] - 1]["comment"], name)
        # they are the last three pulls of the leg, the Steward (the patrol's ground) first; the boss approach (-73.3 240.2) is the row before the boss
        self.assertEqual([r["entries"] for r in steps if r["kind"] == "pull"][-3:], ["4860", "7012,7011", "7011"], name)
        self.assertEqual((steps[-2]["kind"], steps[-2]["x"], steps[-2]["y"]), ("move", "-73.3", "240.2"), name)
        for r in rows:
            if r["leg"] == str(galgann) and r["kind"] == "pull":
                self.assertNotEqual(r["entries"], "4860", name + ": the Steward is pulled in the galgann leg again")
                self.assertGreater(dist(r, -43.03, 263.93), 1, name + ": the Sculptor pull is in the galgann leg again")

    def test_uldaman_scorpid_pull_is_approached_from_the_ramp_top(self):
        # r10 regression (PLAN-ULDAMAN-R10-REGRESSION.txt, F2): the approach of the ten scorpids (pull group 3) is the ramp top where r9 pulled
        # cleanly; from -17.5 292.5 the healer and the mage stood 27-30 yd behind the tank when the scorpids engaged at step 79.
        name, text, rows = newest_uldaman_rows()
        legs = leg_rows(text, "uldaman")
        galgann = leg_steps(rows, next(k for k, (leg_name, _c) in legs.items() if leg_name == "galgann"))
        pulls = [i for i, r in enumerate(galgann) if r["kind"] == "pull" and r["entries"] == "7405"]
        self.assertEqual(len(pulls), 1, name + ": the scorpid pull is missing or doubled")
        approach = galgann[pulls[0] - 1]
        self.assertEqual((approach["kind"], approach["comment"], approach["x"], approach["y"], approach["z"]),
                         ("move", "pull group 3 approach", "-20", "321", "-40"), name + ": the scorpid approach is not the ramp top")

    def test_uldaman_grimlok_corridor_is_pulled_one_group_at_a_time(self):
        # PLAN-ULDAMAN-GRIMLOK #2: Brawler 30105 (40.6 359.9) joined pull group 0 in 8 of 8 runs, so group 0 lists it (radius 16 at
        # 35.77 370.57: 11.7 yd to it, the Brawler at 55.4 379 stays out). The first groups of the keepers_door leg are pulled next
        # from stands at 28 360 and 33 354, the patrol and its trio are one group (radius 18), and the pair at 38-40 439-445 is pulled from 48 419.
        name, text, rows = newest_uldaman_rows()
        legs = leg_rows(text, "uldaman")
        by_name = {leg_name: k for k, (leg_name, _c) in legs.items()}
        grimlok = leg_steps(rows, by_name["grimlok"])
        pulls = [r for r in grimlok if r["kind"] == "pull"]
        first = pulls[0]
        self.assertEqual((first["comment"], first["radius"], first["entries"]), ("pull group 0", "16", "4853,4855"), name)
        self.assertLessEqual(dist(first, 40.6, 359.9), float(first["radius"]), name + ": the Brawler 30105 at 40.6 359.9 is outside group 0")
        self.assertGreater(dist(first, 55.4, 379.0), float(first["radius"]), name + ": the patrol-side Brawler at 55.4 379 is inside group 0")
        # the keepers_door packs now pulled from the grimlok leg, before the patrol and the east groups
        order = [(float(r["x"]), float(r["y"])) for r in pulls]
        def at(x, y):
            found = [i for i, (px, py) in enumerate(order) if math.hypot(px - x, py - y) < 0.1]
            self.assertEqual(len(found), 1, "%s: the grimlok leg has %d pulls at %s %s" % (name, len(found), x, y))
            return found[0]
        pair, brawlers, merged = at(49.1, 343.45), at(39.55, 327.5), at(58.63, 374.4)
        self.assertLess(0, pair, name)
        self.assertLess(pair, brawlers, name)
        self.assertLess(brawlers, merged, name + ": the keepers_door packs are not pulled before the patrol")
        for index, stand in ((pair, (28.0, 360.0)), (brawlers, (33.0, 354.0))):
            move = grimlok[grimlok.index(pulls[index]) - 1]
            self.assertEqual((move["kind"], float(move["x"]), float(move["y"])), ("move", stand[0], stand[1]), name)
        # the patrol and the trio are one group of radius 18 (pull groups 1 and 2 of the old order were never alone)
        self.assertEqual((pulls[merged]["radius"], pulls[merged]["entries"]), ("18", "4855,4853"), name)
        self.assertEqual([i for i, (px, py) in enumerate(order) if math.hypot(px - 57.9, py - 374.0) < 3 and i != merged], [], name + ": a second pull of the patrol")
        # the southern pair from 48 419, right before its pull, then Grimlok
        south = at(38.85, 442.0)
        move = grimlok[grimlok.index(pulls[south]) - 1]
        self.assertEqual((move["kind"], float(move["x"]), float(move["y"])), ("move", 48.0, 419.0), name)
        self.assertEqual(grimlok[-1]["kind"], "boss", name)
        self.assertEqual(grimlok[-1]["entry"], "4854", name)
        # none of the three packs is left to the keepers_door leg
        for r in rows:
            if r["leg"] == str(by_name["keepers_door"]) and r["kind"] == "pull":
                for x, y in ((49.1, 343.45), (40.6, 359.9), (39.55, 327.5)):
                    self.assertGreater(dist(r, x, y), 1, "%s: keepers_door pulls the pack at %s %s again" % (name, x, y))

    def test_uldaman_grimlok_patrol_is_pulled_from_the_lower_floor(self):
        # PLAN-ULDAMAN-GRIMLOK-ISLAND D1 (turtle-agent fix/uldaman-grimlok-pit): the tank shot pull group 3 from the crest above the 6x6
        # pit at 39.7-46.1 370.7-376.8 and fell in (r13 slot1, slot5). Two approach moves now come before the pull: 19.1 366.4 -40.6, then
        # 37.5 363.5 -42.5 on the lower floor, 21.4 yd from the nearest live Brawler (the 20 yd clearance rule of PR #281). The pull and the
        # step 96 approach (50.8 379.3 -38.6) after it keep their rows, one seq later than before the second move.
        name, text, rows = newest_uldaman_rows()
        legs = leg_rows(text, "uldaman")
        grimlok = leg_steps(rows, next(k for k, (leg_name, _c) in legs.items() if leg_name == "grimlok"))
        merged = next(i for i, r in enumerate(grimlok) if r["kind"] == "pull" and r["comment"] == "pull group 3")
        self.assertEqual([(r["kind"], float(r["x"]), float(r["y"]), float(r["z"]), r["comment"]) for r in grimlok[merged - 2:merged]],
                         [("move", 19.1, 366.4, -40.6, "pull group 3 approach"), ("move", 37.5, 363.5, -42.5, "pull group 3 approach")], name)
        self.assertEqual(int(grimlok[merged]["seq"]), int(grimlok[merged - 1]["seq"]) + 1, name)
        after = grimlok[merged + 1]
        self.assertEqual((after["kind"], float(after["x"]), float(after["y"]), float(after["z"]), after["comment"]),
                         ("move", 50.8, 379.3, -38.6, "pull group 4 approach"), name)
        self.assertEqual(int(grimlok[-1]["seq"]), len(grimlok) - 1 + int(grimlok[0]["seq"]), name + ": the leg's seq has a gap")

    def test_uldaman_descent_ends_out_of_the_door_squad(self):
        # r9-19z26: the descent leg ended at 141 290, 7 yd from the door squad; the tank engaged it on arrival, its fight moves took it
        # onto the raised rubble at 132-139 / 296-304 (mesh at z -23.8 no path leaves) and the run paused at step 127. The leg ends 20 yd
        # or more from every squad member now (the pull's radius 20.3 round its centre does not reach it) and 12 yd or more off the rubble.
        name, text, rows = newest_uldaman_rows()
        legs = leg_rows(text, "uldaman")
        descent = leg_steps(rows, next(k for k, (leg_name, _c) in legs.items() if leg_name == "descent"))
        end, squad = descent[1], descent[2]
        self.assertEqual((end["kind"], end["comment"], squad["kind"], squad["comment"]), ("move", "to", "pull", "pull group 0"), name)
        self.assertGreater(dist(end, float(squad["x"]), float(squad["y"])), float(squad["radius"]), name + ": the leg ends inside the squad's pull")
        for x, y in ((148.1, 287.9), (144.4, 299.0), (152.7, 288.4), (153.6, 298.3), (144.5, 306.5), (151.2, 303.8)):
            self.assertGreaterEqual(dist(end, x, y), 20, "%s: the leg ends %.1f yd from the squad member at %s %s" % (name, dist(end, x, y), x, y))
        ex, ey = float(end["x"]), float(end["y"])
        self.assertGreaterEqual(math.hypot(max(132 - ex, ex - 139, 0), max(296 - ey, ey - 304, 0)), 12, name + ": the leg ends by the rubble")

    def test_zulfarrak_and_uldaman_rows_pass_the_loader_checks(self):
        # Every step row of the newest zulfarrak and uldaman migrations goes through the loader's own code (DungeonPlanPolicy.h:
        # ParsePlanStepRowKind, ParsePlanEntries, DungeonPlanTable::AddStep with NormalizePlanStep and the (plan, leg, seq) key),
        # compiled from tools/test_dungeon_plan_rows.cpp: a row the loader refuses is skipped at startup and the run pauses before it.
        compiler = shutil.which("g++")
        if compiler is None:
            self.skipTest("g++ is not installed")
        lines, counts = [], {}
        # The opt-in Gahz'rilla file under its own plan name (the same key twice would be the loader's Duplicate).
        opt_rows = [dict(r, plan="zulfarrak_opt") for r in self.step_rows(OPT_IN.read_text()) if r["plan"] == "zulfarrak"]
        for plan, rows in (("zulfarrak", newest_zulfarrak_rows()[2]), ("uldaman", newest_uldaman_rows()[2]), ("zulfarrak_opt", opt_rows)):
            lines.append("P\t%s\t%d" % (plan, BOSS_PLANS.get(plan, 209)))
            counts[plan] = len(rows)
            for r in rows:
                lines.append("\t".join(["S", r["plan"], r["leg"], r["seq"], r["kind"], r["entry"], r["x"], r["y"], r["z"], r["radius"], r["item"],
                                        r["param"], r["flags"], r["timeout_s"], r["entries"]]))
        self.assertGreater(counts["zulfarrak"], 200)
        self.assertGreater(counts["uldaman"], 130)
        self.assertGreaterEqual(counts["zulfarrak_opt"] - counts["zulfarrak"], 12, "the opt-in file adds the gahzrilla leg's rows")
        with tempfile.TemporaryDirectory() as tmp:
            binary = os.path.join(tmp, "rows")
            build = subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", os.path.join(ROOT, "tools/test_dungeon_plan_rows.cpp"), "-o", binary],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([binary], input="\n".join(lines) + "\n", capture_output=True, text=True)
        self.assertEqual(run.stdout.strip().splitlines()[-1], "rows %d refused 0" % sum(counts.values()), run.stdout)
        self.assertEqual(run.returncode, 0, run.stdout)

    def test_scarlet_cathedral_rows_pass_the_loader_checks(self):
        # The row the whole fix rests on (the pair's hold, entry 1 param 4 flags 8) goes through the loader's own code with the
        # rest of the plan: NormalizePlanStep takes param 4 as a script value (flag 8 without 32), keeps radius 35 and 600 s,
        # and a refused row would leave the run pausing before Mograine and Whitemane.
        compiler = shutil.which("g++")
        if compiler is None:
            self.skipTest("g++ is not installed")
        _name, text = newest_plan_text("scarlet_cathedral")
        rows = [r for r in self.step_rows(text) if r["plan"] == "scarlet_cathedral"]
        lines = ["P\tscarlet_cathedral\t%d" % BOSS_PLANS["scarlet_cathedral"]]
        for r in rows:
            lines.append("\t".join(["S", r["plan"], r["leg"], r["seq"], r["kind"], r["entry"], r["x"], r["y"], r["z"], r["radius"], r["item"],
                                    r["param"], r["flags"], r["timeout_s"], r["entries"]]))
        with tempfile.TemporaryDirectory() as tmp:
            binary = os.path.join(tmp, "rows")
            build = subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", os.path.join(ROOT, "tools/test_dungeon_plan_rows.cpp"), "-o", binary],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([binary], input="\n".join(lines) + "\n", capture_output=True, text=True)
        self.assertEqual(run.stdout.strip().splitlines()[-1], "rows %d refused 0" % len(rows), run.stdout)

    def test_inserted_step_kinds_are_known(self):
        for name, text in migrations():
            for block in re.findall(r"INSERT INTO `ai_dungeon_step` \(([^)]*)\) VALUES(.*?);", text, re.S):
                cols = [c.strip(" `") for c in block[0].split(",")]
                kind_at = cols.index("kind")
                for row in re.findall(r"^\((.*)\)[,]?$", block[1], re.M):
                    values = [v.strip() for v in re.findall(r"'[^']*'|[^,]+", row)]
                    self.assertIn(values[kind_at].strip("'"), KINDS, "%s: unknown step kind in %s" % (name, row))

    def boss_rows(self):
        # Each ai_dungeon_boss row of every migration as (map, entry, name), in file order.
        for _name, text in migrations():
            for block in re.findall(r"(?:INSERT|REPLACE) INTO `ai_dungeon_boss` \(`map`, `entry`, `name`\) VALUES(.*?);\n", text, re.S):
                for row in re.findall(r"^\((\d+), (\d+), '((?:[^']|'')*)'\)[,]?$", block, re.M):
                    yield int(row[0]), int(row[1]), row[2].replace("''", "'")

    def test_boss_table_is_the_seed(self):
        # A later file may REPLACE a (map, entry) it seeds again with the same name (the Graveyard's 20261023060000 re-declares
        # its four: the wing's plan changed while its bosses did not); a second name for the same key is the failure.
        seeded = {}
        for m, e, n in self.boss_rows():
            if (m, e) in seeded:
                self.assertEqual(seeded[(m, e)], n, "(%d, %d) is seeded with two names" % (m, e))
            seeded[(m, e)] = n
        self.assertEqual(seeded, SEEDED_BOSSES)
        self.assertFalse({e for _m, e in seeded} & NOT_BOSSES, "an add, an event unit or a rare is seeded as a boss")

    def test_boss_table_lists_every_plan_boss(self):
        # The newest migration that inserts a plan's boss steps holds its bosses (later ones replace single legs).
        seeded = set(SEEDED_BOSSES)
        for plan, map_id in BOSS_PLANS.items():
            newest = None
            for name, text in migrations():
                if re.search(r"^\('%s', \d+, \d+, 'boss'," % plan, text, re.M):
                    newest = text
            self.assertIsNotNone(newest, plan)
            entries = [int(r["entry"]) for r in self.step_rows(newest) if r["plan"] == plan and r["kind"] == "boss"]
            self.assertTrue(entries, plan)
            for entry in entries:
                self.assertIn((map_id, entry), seeded, "%s: boss step entry %d is not in ai_dungeon_boss" % (plan, entry))

    def test_loader_adds_the_boss_rows_before_the_seal(self):
        # AddBoss is refused after the seal, so a row read after it would be dropped without a word.
        with open(os.path.join(ROOT, "ai/playerbot/DungeonPlanMgr.cpp"), encoding="utf-8") as handle:
            loader = handle.read()
        add = loader.find("table.AddBoss(fields[0].GetUInt32(), fields[1].GetUInt32())")
        self.assertNotEqual(add, -1, "the loader does not add the ai_dungeon_boss rows")
        self.assertLess(add, loader.index("table.Seal();"))

    def test_boss_table_is_created_and_seeded_idempotently(self):
        for name, text in migrations():
            if "`ai_dungeon_boss`" not in text:
                continue
            self.assertNotRegex(text, r"CREATE TABLE `ai_dungeon_boss`", name)  # only IF NOT EXISTS
            self.assertNotRegex(text, r"(?m)^INSERT INTO `ai_dungeon_boss`", name)  # REPLACE, so a re-run keeps one row
            # The DELETE before the rows is scoped to exactly the maps the file seeds: a seeded map it leaves out keeps a
            # row a later file drops; a map it names beyond them loses rows it does not put back. A map shared by several
            # plans (189, the four SM wings) scopes the DELETE to its own boss entries too.
            seeded = {int(m) for m in re.findall(r"(?m)^\((\d+), \d+, '", text)}
            seeded_entries = {(int(m), int(e)) for m, e in re.findall(r"(?m)^\((\d+), (\d+), '", text)}
            scoped = re.findall(r"DELETE FROM `ai_dungeon_boss` WHERE `map` IN \(([\d, ]+)\)(?: AND `entry` IN \(([\d, ]+)\))?;", text)
            self.assertEqual(len(scoped), 1, name)
            if scoped[0][1]:
                self.assertEqual({int(e) for e in scoped[0][1].split(",")}, {e for _m, e in seeded_entries}, name)
            self.assertEqual({int(m) for m in scoped[0][0].split(",")}, seeded, name)


if __name__ == "__main__":
    unittest.main()

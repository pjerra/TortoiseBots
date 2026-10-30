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
KINDS = {"move", "move_direct", "move_hop_exact", "pull", "boss", "loot", "open", "use_item", "wait_object",
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
              "dragonmaw_retreat": 816}
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
    (816, 62056): "Bogpaw Truthsay", (816, 62057): "Gowlfang", (816, 62072): "Searistrasz",
    (816, 62069): "Halgan Redbrand", (816, 62037): "Zuluhed the Whacked",
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

    def test_dragonmaw_retreat_is_the_plan_with_the_census_fixes(self):
        # 20261030120000 (2026-10-30, after the fix-8 live run on tortoise-test2: the pocket passed, but Searistrasz aggroed on the
        # walk at step 260 and took 24 min with all six Dragonmaw Enchanters alive): 20261025002000's rows with leg 3 walked round
        # the boss's patrol. Searistrasz (62072, guid 2584790) walks 42 waypoints up and down x -5980.2..-5981.7, y -4191.5..
        # -4358.9 (tortoise-wow 20260630194548_world.sql); the exporter's walk to the leg's `to` (-5978.8 -4137.4) ran along that
        # very line (steps 258-264), so any walk-in meets him wherever he is on it. The hall's two halves meet only south of the
        # patrol (y < -4385) and through the alcove past its north turn, the one way into the alcove. So leg 3 now pulls the south
        # corridor, the west half south to north, walks back through the south, the east half south to north, then the boss,
        # every pull from a stand 28+ yd off the patrol (the closest server path between two rows is 30.8 yd; navq over
        # turtle-agent internal/navmesh, sight by the core's vmap); the six enchanters (whose Enchanting Flames he keeps until the
        # sixth dies) all come before the boss row. The alcove's three packs are leg 4's first pulls, after the boss.
        # 20261025002000 (turtle-agent c19713b, 2026-10-25, after live run r41s2 slotA2 needed a skip and a GM move at searistrasz
        # steps 267-273): the pull of 62040 at -6028.7 -4176.9 (leg searistrasz group 2) stood beside a 22 x 19 yd hall room whose
        # 6 navmesh polygons are an island (bbox -6022.1 -4192.0 .. -6000.0 -4173.3, z 322.19, no link in or out; the WMO floor is
        # one dry plane at 322.10, so this is not the step-453 lava clamp). The band's firing stands had no sight ('firing stands
        # used up (2)'), so the tank walked straight at the unit, crossed the room's threshold and stopped on the island's west
        # edge (-6022.1 -4176.9, then -6022.1 -4192.0): every Detour query there answers NOPATH ('unreachable start poly'), the
        # packs of steps 269/270 were deferred ('no safe stand 17.8-25.0 yd from it the lead bot has a path to'), the 271 cut row
        # was refused ('no navmesh route reaches it') and 273's pack had 'no walkable path'; a GM move to -6060.2 -4231.4 freed
        # the run. Group 2 now walks round the room's east and south sides (-5986.9 -4160, -5986.9 -4178.1, -5998.9 -4193.1) and
        # stands at -6021 -4199 322.2: 23.4 yd from 62040, in sight, on the 4272-poly component, so the shot goes out from the
        # stand. Groups 4 and 5 (the deferred packs), group 6 (the refused 271 point -6067.2 -4230.7 is its first approach row,
        # then the stand -6046 -4240) and group 7 get stands 19-23 yd off, in sight of every member. Group 56 (the 62453 at
        # -6021.9 -4217.9) and group 57 (the pack east of the room) replace the exporter's two cut rows there with stands
        # -6003 -4212 and -5994 -4196. Every hop of every chain is a complete server path and every stand is reachable and
        # leavable (navq over turtle-agent internal/navmesh; the sight rays are the core's vmap LOS, specs/dmr-step453-tools).
        # 20261025001900 (turtle-agent 1116b84, 2026-10-25, after live run r41s2 slotA stopped at step 755 with 4 pauses): the
        # pull of 62073 at -6100.5 -3971.4 (leg zuluhed_floor group 10) had no approach, so the run leader walked the lead from
        # the group-9 pull (-6103 -4004.5) over the thin floor at -6122.6..-6129 -4000.5..-3988.7, where the core's
        # GetWaterOrGroundLevel answers the lava level 72.25 - 2 = 70.25; the lead sank into the pocket under the sheet (z 70.3,
        # dry but 3 yd under the floor) and its own stay walk failed ('stay walk resting after 3 failed walks'). Group 10 now
        # has three approach rows -6130 -4005, -6135 -3990 and the stand -6119 -3966 72.6: the stand is 19.2 yd from the pull
        # (inside the 24.5 yd shot rule), class S, the clamp keeps 72.6, it sees the pull, and every hop is a complete server
        # path with no sinking point (sinkgate.py/standpath.py over specs/dmr-step453-tools). The same scan found one sinking
        # point at -6275.9 -3967.4 on the group-16 -> group-17 walk (62035 at -6290.9 -4016.7); group 17's two approach rows
        # -6284 -3968, -6290 -3996 72.6 (20.7 yd, in sight) route round it. The 5 exporter cut rows those replace were on the
        # sinking line. sinkgate.py over all 10 legs: 0 rows sink below their floor.
        # 20261025001800 (turtle-agent 5779343, 2026-10-25, after r39s2/r41s2 lost 34 bots in the halgan hall with no mob in
        # combat, 33 of them on a thin floor over WMO magma): 20261025001600 again, with every stand of the hall on a solid column.
        # The core's GetWaterOrGroundLevel answers the lava level 72.25 - 2 = 70.25 for a point on the 72.6 floor (its liquid
        # lookup casts from ground_z and so finds the lava bed below the plane), and DispatchMovement keeps the 2.35 yd clamp, so
        # the plan's own stand -6328 -3861.5 sank the lead into the magma (rowcheck.py: class D, clamp 70.25, LAVA - the only such
        # row of the plan). (6) Group 16's last approach is -6322 -3880 72.6: on solid floor, the nearest non-solid cell 9.6 yd
        # off, 16.9 yd (at most ~22 with the wander) from the bridge 62035 and with sight, inside the 24.5 yd shot rule, and the
        # -6341.7 -3892.5 hop to it is a complete server path with no non-solid sample. (7) Group 17 got an approach round the
        # open pool: the turn -6326 -3912, then the stand -6281 -3915.5 72.6 (17.0 and 21.2 yd from its two members, sight to
        # both, nothing non-solid within 10 yd). (8) The leg-8 pack 62035/62036/62046 at -6260.6 -3936 (r41s2 fought it on
        # halgan_approach's own walk at steps 456-458) moved into leg 4 right after group 17 with the stand -6264 -3914 72.6
        # (15.2-22.2 yd off, sight to all three); leg 8 keeps its other 22 packs.
        # 20261025001600 (turtle-agent 529ab6d, 2026-10-25, after live run r39s2 slotA2 stopped at step 614): 20261025001000 again,
        # with (4) the ramp up to the z 290-303 ledge as the approach rows of the two ledge pulls, and (5) the hall_clear landing as
        # a plain direct step (no gm_cross, so plan-export takes the plan without --gm-cross-direct).
        # 20261025001000 (turtle-agent be41b1c, 2026-10-25, after live run r39s2 slotA stopped at step 453): 20261025000700 again,
        # with the corridor camp pulled BEFORE the walk crosses it and the halgan rest stand off the magma sheet.
        # (1) The corridor camp the leg-2 walk crossed (r27 slot5 step 388: 21 units in one fight, 2 wipes; r39s2 steps 226-243: 8
        # deaths, three tries on step 228 and four more at 236) is leg 2 now, hall_clear: its `to` is the corridor bottom
        # (-6314.6665 -4074.6665 215.089), the 12 waves climb the corridor in walk order (reversed from the descent, each group's
        # approach list reversed too, 1-6 units a wave), and searistrasz follows with the z 322 hall and the boss. The walk back
        # down the cleared corridor is halgan_approach's own route. (2) r39s2 step 453 (the rest/gather stand -6332.93 -3863.11):
        # no unit attacked the party (the addon view held 0 hostiles, every trace mob had Target=0 and cb=false, and the two
        # suspects - the 62035 at -6360.97 -3838.82 73.17 and the ledge 62036 at -6357.21 -3867.19 106.973 - never engaged it);
        # the hp fell 605-610 a second, the core's lava tick, while the party stood in the magma sheet under the bridge that the
        # navmesh calls ground. The stand moved to -6328.0 -3861.5 73.2 (dry in the trace), reached by a 4-point approach chain
        # along the south shore (-6400 -3880, -6370 -3890, -6341.7 -3892.5); the two packs that stand in the lava are left out; a
        # second avoid box covers the sheet. (3) The four walks that no single hop could carry keep their `via` chains of the
        # server path's own points (halgan_redbrand's climb starts on the east rim at -6387.4668 -3977.3333 72.689), the plan's
        # needs_walk pack (62046 at -6190.56 -3919.91) stays `leave`d, and plan-check is ok on all 591 points.
        # (4) r39s2 slotA2 step 614: the four packs on the z 290-303 ledge above the corridor's south end were deferred ('not
        # reachable from this floor') and the next row's point (-6283.5 -4392.5 302.7) had 'no walkable path' from the lead at
        # -6282.7 -4325.9 266.2 - the plan had no route up. The ramp the party walks down in leg 4 and climbs in leg 3 is now
        # pull group 19's approach (4 server-path points, ending on the shelf stand -6330 -4380 290.4) and the second ramp is
        # group 22's (3 points, ending on the terrace stand -6283.47 -4392.53 302.69); the three z 290 packs pull from the shelf
        # stand 25.8-29.6 yd off, the z 303 golem from the terrace stand 22.4 yd off, and no point of the way up is within 20 yd
        # of a pack not pulled yet (the nearest is 24.6 yd).
        # (5) The hall_clear landing (-6190.1 -3962.4 215.8) is a plain direct step with a check_skip now: 7.4 yd across and
        # 5.1 yd down over a 9.0 yd navmesh gap (ServerPath answers no route across it), inside the direct limits, and the run
        # walked the same move_direct live at step 200 (from -6191.8 -3954.9 221.6 to -6186.7 -3964.3 215.2, no death).
        # 10 legs, 846 steps (20261025002000's 835: leg 3 128 -> 132 rows, leg 4 116 -> 123), the same 83 pulls of legs 3 and 4
        # (28+6+12+57+26+17+25+10+22+3: the alcove's 3 moved from leg 3 to leg 4), bosses in leg order, no door or event step.
        name, text = newest_plan_text("dragonmaw_retreat")
        self.assertEqual(name, "20261030120000_world.sql")
        self.assertIn("('dragonmaw_retreat', 816, '62077,62036,62047,62046', '62077', '62053,62039,62071,62070,8853', 'turtle-agent c19713b dungeons/dragonmaw_retreat.json', 'plan v1, draft (not mapped, not live-checked)');", text)
        legs = leg_rows(text, "dragonmaw_retreat")
        self.assertEqual(sorted(legs), list(range(10)))
        self.assertEqual([legs[i][0] for i in range(10)],
                         ["bogpaw_truthsay", "gowlfang", "hall_clear", "searistrasz", "halgan_approach", "halgan_redbrand",
                          "zuluhed_approach", "zuluhed_mid", "zuluhed_floor", "zuluhed_the_whacked"])
        self.assertIn("leave (not read by the run leader yet): 62046 at -6190.56 -3919.91", legs[7][1])
        rows = [r for r in self.step_rows(text) if r["plan"] == "dragonmaw_retreat"]
        self.assertEqual(len(rows), 846)
        self.assertEqual([len([r for r in leg_steps(rows, leg) if r["kind"] == "pull"]) for leg in range(10)],
                         [28, 6, 12, 57, 26, 17, 25, 10, 22, 3])
        self.assertEqual([(int(r["leg"]), int(r["entry"])) for r in rows if r["kind"] == "boss"],
                         [(0, 62056), (1, 62057), (3, 62072), (5, 62069), (9, 62037)])
        # The landing of leg 2 is the one straight walk of the plan (the run leader has no GM move, and no gm_cross is left in
        # the plan: plan-export takes it without --gm-cross-direct), with its own comment;
        # and the server-path `via` of leg 3 (after the corridor camp is clear).
        self.assertEqual([(r["x"], r["y"], r["z"], r["comment"]) for r in rows if r["kind"] == "move_direct"],
                         [("-6190.1", "-3962.4", "215.8", "via")])
        # r39s2 slotA2 step 614: the ramp up to the z 290-303 ledge, as pull group 19's and group 22's approach rows, in walk
        # order, the last point of each on the floor its packs stand on.
        ledge = [(r["kind"], r["x"], r["y"], r["z"], r["comment"]) for r in leg_steps(rows, 6)
                 if r["comment"] in ("pull group 19 approach", "pull group 22 approach")]
        self.assertEqual(ledge, [
            ("move", "-6293.33", "-4349.07", "274.39", "pull group 19 approach"),
            ("move", "-6329.07", "-4349.33", "278.69", "pull group 19 approach"),
            ("move", "-6329.6", "-4369.6", "290.69", "pull group 19 approach"),
            ("move", "-6330", "-4380", "290.4", "pull group 19 approach"),
            ("move", "-6293.33", "-4380", "294.49", "pull group 22 approach"),
            ("move", "-6288.5", "-4386", "297.46", "pull group 22 approach"),
            ("move", "-6283.47", "-4392.53", "302.69", "pull group 22 approach"),
        ])
        # Each of the four ledge packs is pulled from a stand 21-30 yd away on its own floor (+/- 2 yd of z), and the approach
        # row right before it is that stand: the run leader pulls what it sees within 60 yd, so a stand on the ledge is what
        # makes these pulls reachable (on the corridor they were deferred as 'not reachable from this floor').
        import math as _math
        zuluhed = leg_steps(rows, 6)
        for pack, entries in ((("-6300.44", "-4380.81", "290.84"), "62078"), (("-6305.22", "-4372.68", "290.16"), "62039"),
                              (("-6304.99", "-4390.02", "290.16"), "62051"), (("-6266.44", "-4407.07", "303.03"), "62078")):
            i = next(j for j, r in enumerate(zuluhed)
                     if r["kind"] == "pull" and (r["x"], r["y"], r["z"]) == pack and r["entries"] == entries)
            stands = [r for r in zuluhed[:i] if r["comment"].startswith("pull group")
                      and r["comment"].endswith("approach")]
            self.assertTrue(stands, "the pull at %s has no approach row" % (pack,))
            stand = stands[-1]
            d = _math.dist((float(stand["x"]), float(stand["y"]), float(stand["z"])),
                           tuple(float(v) for v in pack))
            self.assertTrue(21.0 <= d <= 30.0, "the stand of the pull at %s is %.1f yd off" % (pack, d))
            self.assertLess(abs(float(stand["z"]) - float(pack[2])), 2.0, "the stand of the pull at %s is not on its floor" % (pack,))
        # The way up keeps 20 yd clear of every pack pulled after the pull it leads to (the pulls run in row order; a pull's
        # point is its pack): the nearest such distance on the ascent rows is 24.6 yd, so climbing aggros nothing new.
        pos = [(j, r) for j, r in enumerate(zuluhed) if r["comment"] in ("pull group 19 approach", "pull group 22 approach")]
        for j, r in pos:
            p = tuple(float(r[k]) for k in ("x", "y", "z"))
            target = next(j2 for j2 in range(j + 1, len(zuluhed)) if zuluhed[j2]["kind"] == "pull")
            later = [tuple(float(q[k]) for k in ("x", "y", "z")) for q in zuluhed[target + 1:] if q["kind"] == "pull"]
            near = min(_math.dist(p, q) for q in later)
            self.assertGreater(near, 20.0, "the ascent row %s passes %.1f yd from a pack still alive" % (p, near))
        leg3 = leg_steps(rows, 3)
        self.assertIn(("move", "-6293.33", "-4373.33", "294.49", "via"),
                      [(r["kind"], r["x"], r["y"], r["z"], r["comment"]) for r in leg3])
        # The hall camp: leg 2, walked up: 12 waves, each a pull with 1-4 approach rows ending in a stand 21-30 yd from the wave;
        # no boss; the first pull is the z 239.7 pack by the corridor's north ramp, the last the z 278 pack at the top.
        hall = leg_steps(rows, 2)
        self.assertEqual([r["kind"] for r in hall if r["kind"] not in ("move", "move_direct")], ["pull"] * 12)
        self.assertFalse([r for r in hall if r["kind"] == "boss"], "hall_clear must not carry a boss")
        waves = [r for r in hall if r["kind"] == "pull"]
        self.assertEqual((waves[0]["x"], waves[0]["y"], waves[0]["z"]), ("-6258.83", "-4069.58", "239.74"))
        self.assertEqual((waves[-1]["x"], waves[-1]["y"], waves[-1]["z"]), ("-6338.42", "-4336.78", "278.16"))
        self.assertEqual([float(w["radius"]) for w in waves][:3], [4.7, 9.7, 3.2])
        self.assertTrue(all(len(r["entries"].split(",")) <= 6 for r in waves), "a wave is over 6 units")
        # One stand per wave, on its north side (the walk's own direction): the party stops before each pack, never past it.
        approach = [r for r in hall if r["comment"].endswith("approach")]
        self.assertEqual(len(approach), 12, "the waves' approach rows")
        self.assertEqual([(r["x"], r["y"], r["z"]) for r in approach][:3],
                         [("-6258.138", "-4093.273", "247.18"), ("-6270.069", "-4096.311", "248.96"),
                          ("-6269.621", "-4114.897", "260.37")])
        self.assertEqual((approach[-1]["x"], approach[-1]["y"], approach[-1]["z"]), ("-6312.194", "-4330.296", "278.76"))
        # Each stand is 21-45 yd from its wave's pull row (the plan check holds the 21-30 yd to the nearest member; a row names
        # one member only). The exact stands are pinned above.
        import math as _math
        for i, a in enumerate(approach):
            p = waves[i]
            d = _math.dist((float(a["x"]), float(a["y"]), float(a["z"])),
                           (float(p["x"]), float(p["y"]), float(p["z"])))
            self.assertTrue(21.0 <= d <= 45.0, "wave %d stand is %.1f yd from the pull row's pack" % (i, d))
        # The walk round the magma pool (r35 slot6b step 475, 11 deaths at the old dip -6404.53 -3950.93 70.94): no row of the
        # plan stands in the avoid box, and halgan_redbrand's climb starts on the east rim at the fix2 line.
        self.assertFalse([r for r in rows
                          if -6416 <= float(r["x"]) <= -6378 and -3970 <= float(r["y"]) <= -3946
                          and 70.7 <= float(r["z"]) <= 72.6], "a row stands in the avoid box (the magma pool)")
        # The hall's thin floor over the WMO magma (specs/dmr-step453.md): a stand on it is clamped to the lava level - 2 (70.25,
        # the lava tick), so every stand is now on a solid column. The group-16 stand is -6322 -3880 72.6 (the nearest non-solid
        # cell 9.6 yd off), group 17's is -6281 -3915.5 72.6 and the moved leg-8 pack's is -6264 -3914 72.6; the old stands
        # -6328 -3861.5 (fix 4) and -6332.93 -3863.11 (fix 3) are gone for good: they are what killed the party.
        self.assertFalse([r for r in rows
                          if -6385 <= float(r["x"]) <= -6330.5 and -3870 <= float(r["y"]) <= -3830
                          and 69.9 <= float(r["z"]) <= 73.3], "a row stands in the avoid box (the hall lava sheet)")
        for old in ((-6328.0, -3861.5), (-6332.93, -3863.11)):
            self.assertFalse([r for r in rows
                              if abs(float(r["x"]) - old[0]) < 0.01 and abs(float(r["y"]) - old[1]) < 0.01],
                             "the stand %s over the magma is back" % (old,))
        halgan = leg_steps(rows, 4)
        stand = [r for r in halgan if r["kind"] == "pull" and abs(float(r["x"]) + 6308.27) < 0.01]
        self.assertEqual(len(stand), 1, "the group-16 62035 pull is missing")
        i = halgan.index(stand[0])
        self.assertEqual([(r["x"], r["y"], r["z"]) for r in halgan[i - 4:i]],
                         [("-6400", "-3880", "73.2"), ("-6370", "-3890", "73.2"), ("-6341.7", "-3892.5", "72.7"),
                          ("-6322", "-3880", "72.6")], "the south-shore approach chain or the solid stand is missing")
        # Group 17's approach round the open pool, then the leg-8 pack right after it: each stand is on solid floor and 15-22 yd
        # from its pack, inside the run leader's 24.5 yd shot rule (so it shoots from the stand, never walks onto the bridge).
        g17 = [r for r in halgan if r["kind"] == "pull" and abs(float(r["x"]) + 6273.79) < 0.01]
        self.assertEqual(len(g17), 1, "the group-17 pull is missing")
        j = halgan.index(g17[0])
        self.assertEqual([(r["x"], r["y"], r["z"], r["comment"]) for r in halgan[j - 2:j]],
                         [("-6326", "-3912", "72.6", "pull group 17 approach"),
                          ("-6281", "-3915.5", "72.6", "pull group 17 approach")], "the group-17 approach is missing")
        moved = [r for r in halgan if r["kind"] == "pull" and (r["x"], r["y"], r["z"]) == ("-6261.17", "-3932.36", "72.6")]
        self.assertEqual(len(moved), 1, "the moved 62035/62036/62046 pull is missing from leg 4")
        self.assertEqual((moved[0]["entries"], moved[0]["radius"]), ("62035,62036,62046", "13.7"))
        m = halgan.index(moved[0])
        self.assertEqual([(r["x"], r["y"], r["z"], r["comment"]) for r in halgan[m - 1:m]],
                         [("-6264", "-3914", "72.6", "pull group 18 approach")], "the moved pack's stand is missing")
        self.assertFalse([r for r in leg_steps(rows, 8)
                          if (r["x"], r["y"], r["z"]) == ("-6261.17", "-3932.36", "72.6")],
                         "the 62035/62036/62046 pack is still pulled in leg 8")
        # r41s2 slotA step 755: the group-10 pull of 62073 has its own approach now, and the group-17 pull's approach replaces
        # the exporter's 5 cut rows (they lay on the sinking line over the thin floor). Each stand is on solid floor, 19-21 yd
        # from the pack (inside the 24.5 yd shot rule), and every hop of both chains is a complete server path with no sinking
        # point (sinkgate.py, standpath.py). Remove a row and this fails: it pins the only stands that keep the lead out of
        # the pocket.
        floor = leg_steps(rows, 8)
        for pack, group, chain in (
                (("-6100.52", "-3971.36", "73.22"), "10",
                 [("-6130", "-4005", "72.6"), ("-6135", "-3990", "72.6"), ("-6119", "-3966", "72.6")]),
                (("-6290.9", "-4016.66", "73.19"), "17",
                 [("-6284", "-3968", "72.6"), ("-6290", "-3996", "72.6")])):
            i = next(j for j, r in enumerate(floor) if r["kind"] == "pull" and (r["x"], r["y"], r["z"]) == pack)
            self.assertEqual([(r["x"], r["y"], r["z"], r["comment"]) for r in floor[i - len(chain):i]],
                             [(x, y, z, "pull group %s approach" % group) for x, y, z in chain],
                             "the solid-floor approach of the pull at %s is missing" % (pack,))
        # r41s2 slotA2 steps 267-273, the isolated hall room: every pull of the pocket has its stand row, and no row stands in
        # the room itself. The room is the 6-poly navmesh island -6022.1 -4192.0 .. -6000.0 -4173.3 at z 322.19 (no link in or
        # out, the WMO floor a dry plane) right beside the group-2 pull; the lead that walked into it lost every path ('no
        # navmesh route reaches it', the 269/270 packs deferred, 271 refused, 273 no walkable path). A row inside it, or the
        # stand chains gone, brings that back: each stand is 18-24.5 yd from its pack and in sight (the pin's distances are the
        # plan rows', the sight and path checks are in specs/dmr-fix8-tools/).
        sear = leg_steps(rows, 3)
        self.assertFalse([r for r in rows if -6022.1 <= float(r["x"]) <= -6000.0 and -4192.0 <= float(r["y"]) <= -4173.3
                          and 321.5 <= float(r["z"]) <= 323.0], "a plan row stands in the isolated hall room")
        self.assertFalse([r for r in sear if r["comment"] == "approach path"
                          and (r["x"], r["y"], r["z"]) in (("-6067.2", "-4230.6665", "322.189"),
                                                           ("-5994.667", "-4224", "322.189"),
                                                           ("-6003.2", "-4212.2666", "322.189"))],
                         "an exporter cut row of the pocket walk is back")
        for pack, group, chain in (
                (("-6028.72", "-4176.9", "322.69"), "2", [("-6046.7", "-4190.9", "322.2")]),
                (("-6050.79", "-4199.42", "322.1"), "4", [("-6073.8", "-4208.4", "322.2")]),
                (("-6072.09", "-4224.85", "322.13"), "5", [("-6061.1", "-4242.8", "322.2")]),
                (("-6028.71", "-4254.44", "322.69"), "6", [("-6022.7", "-4276.4", "322.2")]),
                (("-6040.23", "-4274.5", "322.1"), "7", [("-6019.2", "-4287.5", "322.2")]),
                (("-6021.87", "-4217.88", "323.96"), "56", [("-6027.9", "-4195.9", "322.2")])):
            i = next(j for j, r in enumerate(sear) if r["kind"] == "pull" and (r["x"], r["y"], r["z"]) == pack)
            self.assertEqual([(r["x"], r["y"], r["z"], r["comment"]) for r in sear[i - len(chain):i]],
                             [(x, y, z, "pull group %s approach" % group) for x, y, z in chain],
                             "the stand chain of the pull at %s is missing" % (pack,))
            d = _math.dist((float(chain[-1][0]), float(chain[-1][1]), float(chain[-1][2])),
                           (float(pack[0]), float(pack[1]), float(pack[2])))
            self.assertTrue(18.0 <= d <= 26.0, "the stand of the pull at %s is %.1f yd off" % (pack, d))
        # Searistrasz's patrol (guid 2584790; every waypoint lies within 0.8 yd of this segment): no row of leg 3 before the
        # boss row stands within 28 yd of it (the fix-8 run's walk went along it, steps 258-264), the boss row is the leg's
        # last, the six Dragonmaw Enchanters (62453) are pulled before it, and the alcove past the patrol's north turn is the
        # head of leg 4, its stands as fix 8 had them, pulled once he is dead.
        pa, pb = (-5981.68, -4191.49), (-5980.23, -4358.90)

        def patrol_yd(r):
            x, y = float(r["x"]), float(r["y"])
            t = max(0.0, min(1.0, ((x - pa[0]) * (pb[0] - pa[0]) + (y - pa[1]) * (pb[1] - pa[1])) /
                             ((pb[0] - pa[0]) ** 2 + (pb[1] - pa[1]) ** 2)))
            return _math.hypot(x - pa[0] - t * (pb[0] - pa[0]), y - pa[1] - t * (pb[1] - pa[1]))
        self.assertEqual((sear[-1]["kind"], sear[-1]["entry"]), ("boss", "62072"))
        near = [(r["x"], r["y"], round(patrol_yd(r), 1)) for r in sear[:-1] if r["kind"] == "move" and patrol_yd(r) < 28.0]
        self.assertFalse(near, "leg 3 stands on Searistrasz's patrol before his boss row: %s" % (near,))
        self.assertEqual(len([r for r in sear if r["kind"] == "pull" and r["entries"] == "62453"]), 6)
        self.assertFalse([r for r in leg_steps(rows, 4) if "62453" in r["entries"].split(",")])
        self.assertEqual([(r["kind"], r["x"], r["y"], r["z"], r["comment"]) for r in leg_steps(rows, 4)[:7]], [
            ("move", "-5994", "-4196", "322.2", "searistrasz pull group 57 approach"),
            ("pull", "-5981.44", "-4179.05", "322.1", "searistrasz pull group 57"),
            ("move", "-5986.9", "-4178.1", "322.2", "searistrasz pull group 0 approach"),
            ("move", "-5986.9", "-4160", "322.2", "searistrasz pull group 0 approach"),
            ("pull", "-5980.78", "-4140.34", "322.1", "searistrasz pull group 0"),
            ("move", "-5989.4", "-4145.1", "322.2", "searistrasz pull group 1 approach"),
            ("pull", "-6011.39", "-4140.09", "322.1", "searistrasz pull group 1")])
        self.assertFalse([r for r in sear if (r["x"], r["y"]) in (("-5981.44", "-4179.05"), ("-5980.78", "-4140.34"),
                                                                  ("-6011.39", "-4140.09"), ("-5978.82", "-4137.36"))],
                         "an alcove pack or the old `to` on the far side of the patrol is back in leg 3")
        self.assertFalse([r for r in halgan if r["kind"] == "pull" and abs(float(r["x"]) + 6360.97) < 0.01],
                         "the 62035 that stands in the lava sheet is still a pull")
        self.assertFalse([r for r in halgan if r["kind"] == "pull" and abs(float(r["x"]) + 6357.21) < 0.01
                          and 106.9 < float(r["z"]) < 107.1], "the ledge 62036 with no dry stand is still a pull")
        self.assertFalse([r for r in rows if abs(float(r["x"]) + 6404.5337) < 0.01 and abs(float(r["y"]) + 3950.9333) < 0.01],
                         "the old step 475 dip point is still exported")
        self.assertIn(("move", "-6387.47", "-3977.33", "72.69"),
                      [(r["kind"], r["x"], r["y"], r["z"]) for r in leg_steps(rows, 5)],
                      "the east-rim row of the halgan climb is missing")
        # The long walks are `via` chains, one row per ~45 yd of the server's own path (the refused-hop fix).
        self.assertGreaterEqual(len([r for r in leg_steps(rows, 5) if r["comment"] == "via"]), 19)
        self.assertGreaterEqual(len([r for r in leg_steps(rows, 9) if r["comment"] == "via"]), 44)
        self.assertEqual([r["kind"] for r in leg_steps(rows, 9)][-1], "boss")
        pulls = [r for r in rows if r["kind"] == "pull"]
        self.assertFalse({e for r in pulls for e in r["entries"].split(",")} & {"4075", "14881"}, "a critter is still a pull member")
        self.assertEqual((pulls[0]["entries"], float(pulls[0]["radius"])), ("62052", 5.0))
        # The Bruiser that stood inside Bogpaw's and Gowlfang's 20 yd detection is not a pull row any more.
        self.assertFalse([r for r in pulls if abs(float(r["x"]) + 5985.32) < 0.5 and abs(float(r["y"]) + 3626.44) < 0.5])
        # The map's boss rows stay in the migration that introduced them (20261024151500): this one re-exports the same plan on
        # the same map, so it must not delete or re-seed ai_dungeon_boss (the exporter writes no boss rows).
        earlier = dict(migrations())["20261024151500_world.sql"]
        self.assertIn("DELETE FROM `ai_dungeon_boss` WHERE `map` IN (816) AND `entry` IN (62056,62057,62072,62069,62037);", earlier)
        self.assertIn("REPLACE INTO `ai_dungeon_boss` (`map`, `entry`, `name`) VALUES\n(816, 62056, 'Bogpaw Truthsay'),\n(816, 62057, 'Gowlfang'),\n(816, 62072, 'Searistrasz'),\n(816, 62069, 'Halgan Redbrand'),\n(816, 62037, 'Zuluhed the Whacked');", earlier)
        self.assertNotIn("ai_dungeon_boss", text, "the re-export must leave the map's boss rows alone")
        self.assertIn("DELETE FROM `ai_dungeon_step` WHERE `plan` = 'dragonmaw_retreat';", text)
        self.assertEqual(re.findall(r"(?m)^DELETE FROM `ai_dungeon_step` WHERE `plan` = '(\w+)';", text), ["dragonmaw_retreat"])

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

"""Guards the rows of data/sql/world/*_world.sql that fill ai_boss_stand.

The loader (ai/playerbot/BossStandMgr.cpp, LoadBossStands) skips a bad row with only a
count in the log, so a typo in a migration would silently turn a stand set off. This reads
every INSERT into ai_boss_stand and checks what BossStandTable::Add checks, plus the
primary key and, for each boss with rows, the geometry its seats rest on.

Run: python3 -m unittest tools/test_boss_stand_sql.py
"""
import collections
import glob
import math
import os
import re
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROLES = {"ranged", "healer", "melee"}
MAX_SLOTS = 8
SPACING = 9.0
ROW = re.compile(
    r"^\((\d+), (\d+), (\d+), (-?[\d.]+), (-?[\d.]+), ([\d.]+), '(\w+)', (\d+), (\d+), (\d+), (\d+), "
    r"(-?[\d.]+), (-?[\d.]+), (-?[\d.]+), (-?[\d.]+), (-?[\d.]+), (\d+), (\d+), (\d+), '(\w+)'(?:, (\d+))?, "
    r"'(?:[^']|'')*'\)[,;]$"
)
DELETE = re.compile(r"^DELETE FROM `ai_boss_stand` WHERE `map` = (\d+)(?: AND `boss_entry` = (\d+))?;?$")
FIELDS = ("map", "boss", "anchor", "ax", "ay", "ar", "role", "window", "aura", "side", "seq",
          "x", "y", "z", "dx", "dy", "slots", "hold", "release", "signal", "hp_max")

VENOXIS_MAP = 309
VENOXIS = 14507
# The boss's own fight spot in the walk-22 fights: while he fights there the frames put 14507 within 5.01 yd of
# this anchor (frames.jsonl, hp < 100 %: -12015.8 -1690.5 the kill, -12020.6 -1695.1 r6, -12019.1 -1696.7 test2).
# The radius is that drift plus 2 yd of margin; the spawn -12029.8 -1707.9, 17.5 yd out, is only ever at 100 %.
VENOXIS_ANCHOR = (-12018.0, -1695.0, 7.0)
# The stand is up while the boss is anywhere in that disk, so a seat has to keep a caster's range wherever he is
# inside it: the nearest he may come is 15 yd (Holy Nova 23858 reaches that far), the farthest 30 yd (a caster's
# range). With the 7 yd radius that is a seat distance of 22-23 yd from the anchor.
VENOXIS_SEAT_MIN_BOSS = 15.0
VENOXIS_SEAT_MAX_BOSS = 30.0
# Holy Wrath 23979 links the unit nearest its last one within 10 yd plus bounding radii: two bots 11 yd apart
# cannot be linked, so the chain cannot walk out of the melee clump.
VENOXIS_SEAT_GAP = 11.0
# The core evades Venoxis outside z 27-43 (boss_venoxis.cpp).
VENOXIS_SEAT_Z = (27.0, 43.0)
VENOXIS_SEATS = 12


def migration_rows():
    """The rows a boot ends up with, in file order: each file's DELETEs drop the live seats they name first."""
    live = {}
    order = []
    for path in sorted(glob.glob(os.path.join(ROOT, "data/sql/world/*_world.sql"))):
        name = os.path.basename(path)
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        if "`ai_boss_stand`" not in text:
            continue
        in_insert = False
        for line in text.splitlines():
            delete = DELETE.match(line.strip())
            if delete:
                map_id, boss = int(delete.group(1)), int(delete.group(2)) if delete.group(2) else None
                for key in [key for key, row in live.items()
                            if row["map"] == map_id and (boss is None or row["boss"] == boss)]:
                    del live[key]
                    order.remove(key)
                continue
            if line.startswith("INSERT INTO `ai_boss_stand`"):
                in_insert = True
                continue
            if not in_insert or line.startswith("--") or not line.strip():
                continue
            match = ROW.match(line)
            if not match:
                raise AssertionError("unparsable ai_boss_stand row in %s: %s" % (name, line))
            values = list(match.groups())
            # boss_hp_max arrived after the older rows: an INSERT without it loads the column's 100 default.
            if values[-1] is None:
                values[-1] = "100"
            row = {"file": name}
            for field, value in zip(FIELDS, values):
                row[field] = value if field in ("role", "signal") else float(value)
            key = (row["map"], row["boss"], row["anchor"], row["role"], row["window"], row["side"], row["seq"])
            if key in live:
                raise AssertionError("duplicate key %s in %s" % (key, name))
            live[key] = row
            order.append(key)
            if line.endswith(";"):
                in_insert = False
    return [live[key] for key in order]


def slot_positions(row):
    length = math.hypot(row["dx"], row["dy"])
    if length > 0.0001:
        ux, uy = row["dx"] / length, row["dy"] / length
    else:
        ux, uy = row["x"] - row["ax"], row["y"] - row["ay"]
        length = math.hypot(ux, uy)
        ux, uy = ux / length, uy / length
    return [(row["x"] + ux * SPACING * k, row["y"] + uy * SPACING * k) for k in range(int(row["slots"]))]


def venoxis_rows():
    return [r for r in migration_rows() if r["map"] == VENOXIS_MAP and r["boss"] == VENOXIS]


class BossStandSql(unittest.TestCase):
    def test_rows_exist(self):
        self.assertTrue(migration_rows(), "no ai_boss_stand rows found")

    def test_rows_are_loadable(self):
        for row in migration_rows():
            where = "%s boss %d anchor %d" % (row["file"], row["boss"], row["anchor"])
            self.assertIn(row["role"], ROLES, where)
            self.assertIn(row["signal"], ("cast", "stopped_no_target"), where)
            if row["signal"] == "stopped_no_target":
                # BossStandTable::Add: a signal that opens the window needs an aura and a window spell.
                self.assertGreater(row["aura"], 0, where)
                self.assertGreater(row["window"], 0, where)
            self.assertGreater(row["map"], 0, where)
            self.assertGreater(row["boss"], 0, where)
            self.assertGreater(row["ar"], 0.0, where)
            self.assertIn(int(row["side"]), (0, 1), where)
            self.assertGreaterEqual(row["slots"], 1, where)
            self.assertLessEqual(row["slots"], MAX_SLOTS, where)
            self.assertLess(row["seq"], 256, where)
            if row["release"]:
                self.assertNotEqual(row["release"], row["anchor"], where)
            # BossStandTable::Add bounds the hold: a longer one parks bots for as long as it lasts.
            self.assertGreaterEqual(row["hold"], 1000, where)
            self.assertLessEqual(row["hold"], 15000, where)
            # BossStandMgr rejects a boss_hp_max of 0 or above 100.
            self.assertGreaterEqual(row["hp_max"], 1, where)
            self.assertLessEqual(row["hp_max"], 100, where)

    def test_release_anchors_name_an_anchor_of_the_same_boss(self):
        # BossStandTable::Seal refuses a boss's whole stand set when one does not.
        anchors = {}
        for row in migration_rows():
            anchors.setdefault((row["map"], row["boss"]), set()).add(row["anchor"])
        for row in migration_rows():
            if row["release"]:
                self.assertIn(row["release"], anchors[(row["map"], row["boss"])],
                              "%s anchor %d names release anchor %d, which the boss does not hold" % (
                                  row["file"], row["anchor"], row["release"]))

    def test_primary_keys_are_unique(self):
        seen = set()
        for row in migration_rows():
            key = (row["map"], row["boss"], row["anchor"], row["role"], row["window"], row["side"], row["seq"])
            self.assertNotIn(key, seen, "duplicate key %s in %s" % (key, row["file"]))
            seen.add(key)

    def test_one_anchor_id_is_one_place(self):
        # BossStandTable::Add refuses a row whose anchor id disagrees with the first row of that id.
        anchors = {}
        for row in migration_rows():
            key = (row["map"], row["boss"], row["anchor"])
            place = (row["ax"], row["ay"], row["ar"])
            self.assertEqual(anchors.setdefault(key, place), place, "anchor %s names two places" % (key,))

    def test_the_table_migration_creates_the_table_only(self):
        # The table comes empty; each boss's rows come in that boss's own migration.
        path = os.path.join(ROOT, "data/sql/world/20261101075500_world.sql")
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        self.assertIn("CREATE TABLE IF NOT EXISTS `ai_boss_stand`", text)
        self.assertNotIn("INSERT", text)
        self.assertNotIn("DELETE", text)


class BossStandVenoxisSql(unittest.TestCase):
    def test_venoxis_seats_hold_their_range_anywhere_the_boss_may_stand(self):
        # walk 22: the raid stood 1-16 yd from the boss (r6: all 19 inside 16 yd, the healers 3.5-8.7 yd) and one
        # Holy Wrath 23979 chain killed 9 bots inside a second (hits 5.4-57 k, k11-k21). The seats hold the ranged
        # and the healers out of the melee clump and 11 yd apart, checked against the whole disk the boss may be in
        # while the stand is up (the anchor +- the radius), not the anchor point: over that disk the nearest the
        # boss may come to a seat is 15 yd and the farthest 30 yd.
        rows = venoxis_rows()
        self.assertTrue(rows, "no Venoxis stand rows")
        self.assertEqual(1, len(set((r["ax"], r["ay"], r["ar"]) for r in rows)),
                         "the seats name more than one anchor")
        for row in rows:
            self.assertAlmostEqual(VENOXIS_ANCHOR[0], row["ax"], delta=0.1)
            self.assertAlmostEqual(VENOXIS_ANCHOR[1], row["ay"], delta=0.1)
            self.assertAlmostEqual(VENOXIS_ANCHOR[2], row["ar"], delta=0.1)
        seats = []
        for row in rows:
            where = "%s anchor %d %s seq %d" % (row["file"], row["anchor"], row["role"], row["seq"])
            # One bot a row: kBossStandSpacing is 9 yd, under the chain's 10 yd jump.
            self.assertEqual(1.0, row["slots"], "%s seats %s bots" % (where, row["slots"]))
            self.assertIn(row["role"], ("ranged", "healer"), where)
            # No window and no aura: the seats are up for the whole P1 fight, not only around a cast.
            self.assertEqual(0.0, row["window"], where)
            self.assertEqual(0.0, row["aura"], where)
            self.assertEqual("cast", row["signal"], where)
            self.assertGreaterEqual(row["z"], VENOXIS_SEAT_Z[0], where)
            self.assertLessEqual(row["z"], VENOXIS_SEAT_Z[1], where)
            for x, y in slot_positions(row):
                distance = math.hypot(x - VENOXIS_ANCHOR[0], y - VENOXIS_ANCHOR[1])
                # Worst case over the whole disk: the boss at the near edge is distance - radius from the seat, at
                # the far edge distance + radius. Both use the row's own radius, so widening it turns this red.
                self.assertGreaterEqual(distance - row["ar"], VENOXIS_SEAT_MIN_BOSS - 0.05,
                                        "%s: seat (%.2f, %.2f) is only %.2f yd from the boss at the near edge" % (
                                            where, x, y, distance - row["ar"]))
                self.assertLessEqual(distance + row["ar"], VENOXIS_SEAT_MAX_BOSS + 0.05,
                                     "%s: seat (%.2f, %.2f) is %.2f yd from the boss at the far edge" % (
                                         where, x, y, distance + row["ar"]))
                seats.append((x, y, where))
        self.assertGreaterEqual(len(seats), VENOXIS_SEATS, "only %d seats" % len(seats))
        roles = collections.Counter(row["role"] for row in rows)
        self.assertGreaterEqual(roles["ranged"], 1, "no ranged seats: %s" % dict(roles))
        self.assertGreaterEqual(roles["healer"], 1, "no healer seats: %s" % dict(roles))
        for i in range(len(seats)):
            for j in range(i + 1, len(seats)):
                distance = math.hypot(seats[i][0] - seats[j][0], seats[i][1] - seats[j][1])
                self.assertGreaterEqual(distance, VENOXIS_SEAT_GAP - 0.05,
                                        "%s and %s are %.2f yd apart" % (seats[i][2], seats[j][2], distance))


class BossStandOnyxiaSql(unittest.TestCase):
    def test_onyxia_walk2f_melee_seats_near_t(self):
        # 20261111170300_world.sql: on T's side (the raid centre T = (8.0, -200.0)) for anchors 3, 4, 7 and 8 the
        # melee quota is seated first, nearest T, so a melee bot and the slot-1 tank walk the shortest way out of
        # the lane. Walk 2e b-2: the tank's seat was 30.9 yd from T and he died on the way, and seated rogues ran
        # back in. Anchor 3 has no usable cell nearer than 34.5 yd; 4, 7 and 8 are inside 25 yd.
        rows = [row for row in migration_rows()
                if row["map"] == 249 and row["boss"] == 10184 and row["anchor"] <= 8]
        self.assertEqual(601, len(rows), "the walk 2f migration must live with the 601 Deep Breath seats")
        for anchor in range(1, 9):
            seat_rows = [row for row in rows if row["anchor"] == anchor]
            self.assertEqual(len(seat_rows), len(set((row["x"], row["y"]) for row in seat_rows)),
                             "anchor %d seats a cell twice" % anchor)
        for row in rows:
            where = "%s anchor %d %s side %d seq %d" % (row["file"], row["anchor"], row["role"], row["side"],
                                                        row["seq"])
            self.assertEqual(12000, row["hold"], where)
            self.assertEqual(((int(row["anchor"]) + 3) % 8) + 1, row["release"], where)
        raid_t = (8.0, -200.0)
        for anchor, side in ((3, 1), (4, 0), (7, 0), (8, 1)):
            seats = [row for row in rows
                     if row["anchor"] == anchor and row["side"] == side and row["role"] == "melee"]
            self.assertEqual(13, len(seats), "anchor %d side %d has %d melee seats" % (anchor, side, len(seats)))
            nearest = min(math.hypot(row["x"] - raid_t[0], row["y"] - raid_t[1]) for row in seats)
            if anchor == 3:
                # No usable cell of this side lies nearer T; the generator's own floor check is the limit.
                self.assertLessEqual(nearest, 35.0,
                                     "anchor 3's nearest T-side melee seat is %.1f yd from T" % nearest)
            else:
                self.assertLessEqual(nearest, 25.0,
                                     "anchor %d's nearest T-side melee seat is %.1f yd from T" % (anchor, nearest))


class BossStandOnyxiaStackSql(unittest.TestCase):
    def test_onyxia_walk2n_healer_stack(self):
        # 20261116120000_world.sql: walk 2n's resting healer stack (anchor 9). Walk 2m: the healers had no point of
        # their own, took the Deep Breath seats and scattered the whelps that peel to them. No spot clears every breath
        # lane (the ticks reach 30 yd), so the stack is a resting spot gated by the Hover aura 17131 (P2 only) and a
        # breath's own anchor outranks it (BossStandPolicy's ActiveStandPoints takes the nearest anchor holding her).
        # Ten points 6 yd apart, one bot a point; z from the offline path-check from both walk STARTs.
        rows = [row for row in migration_rows()
                if row["map"] == 249 and row["boss"] == 10184 and row["anchor"] == 9]
        self.assertEqual(10, len(rows), "the walk 2n healer stack is not ten points")
        places = set((row["ax"], row["ay"], row["ar"]) for row in rows)
        self.assertEqual({(-22.0, -213.5, 60.0)}, places, "the stack names more than one anchor place")
        self.assertEqual(list(range(10)), sorted(int(row["seq"]) for row in rows), "the stack's seq is not 0-9")
        points = []
        for row in rows:
            where = "%s anchor 9 seq %d" % (row["file"], row["seq"])
            self.assertEqual("healer", row["role"], where)
            # A resting spot: no window spell, the Hover aura alone opens it, and no release anchor to wait for.
            self.assertEqual(0.0, row["window"], where)
            self.assertEqual(17131.0, row["aura"], where)
            self.assertEqual(0.0, row["release"], where)
            self.assertEqual("cast", row["signal"], where)
            self.assertEqual(0.0, row["side"], where)
            # One bot a point: kBossStandSpacing lines up the slots of one point (9 yd), and the ten bots are spread
            # over ten points 6 yd apart instead.
            self.assertEqual(1.0, row["slots"], where)
            self.assertEqual(8000.0, row["hold"], where)
            self.assertGreaterEqual(row["z"], -93.0, where)
            self.assertLessEqual(row["z"], -80.0, where)
            distance = math.hypot(row["x"] - row["ax"], row["y"] - row["ay"])
            self.assertLessEqual(distance, row["ar"], "%s: %.1f yd from the anchor" % (where, distance))
            points.append((row["x"], row["y"], where))
        for i in range(len(points)):
            for j in range(i + 1, len(points)):
                distance = math.hypot(points[i][0] - points[j][0], points[i][1] - points[j][1])
                self.assertGreaterEqual(distance, 5.0,
                                        "%s and %s are only %.2f yd apart" % (points[i][2], points[j][2], distance))


class BossStandOnyxiaP3StackSql(unittest.TestCase):
    """20261117210100_world.sql: the P3 healer stack (anchor 10) on her two flanks."""

    MT = (20.0, -218.0)        # spot E, the main tank's P3 spot
    HER = (0.5, -212.8)        # her P3 landing point / raid centre
    NORTH = (5.0, -188.0)
    SOUTH = (5.0, -246.0)
    # The east edge of the 95-row 176511 egg field (tw_world_gameobject.sql, map 249: the largest x any egg row
    # carries), with the two cave-mouth pairs nearest the east seats. Every seat here stands east of x 2, so the
    # 14 yd bound holds against every egg row, not only these.
    EGGS = ((-33.0413, -266.323), (-32.5829, -162.944), (-38.458, -262.433), (-37.5972, -167.346))

    def seats(self):
        return [row for row in migration_rows()
                if row["map"] == 249 and row["boss"] == 10184 and row["anchor"] == 10]

    def test_eight_healer_rows_behind_the_hp_gate(self):
        rows = self.seats()
        self.assertEqual(8, len(rows), "the P3 healer stack is not eight seats")
        self.assertEqual({(-22.0, -213.5, 60.0)}, set((row["ax"], row["ay"], row["ar"]) for row in rows),
                         "the stack names more than one anchor place")
        self.assertEqual(list(range(8)), sorted(int(row["seq"]) for row in rows), "the stack's seq is not 0-7")
        north = [row for row in rows if row["y"] > self.HER[1]]
        self.assertEqual(4, len(north), "the stack is not four north and four south seats")
        self.assertEqual(4, len(rows) - len(north))
        for row in rows:
            where = "%s anchor 10 seq %d" % (row["file"], row["seq"])
            self.assertEqual("healer", row["role"], where)
            # A resting spot: no window spell, no aura, no side and no release anchor; the hp gate opens it.
            self.assertEqual(0.0, row["window"], where)
            self.assertEqual(0.0, row["aura"], where)
            self.assertEqual(0.0, row["release"], where)
            self.assertEqual("cast", row["signal"], where)
            self.assertEqual(0.0, row["side"], where)
            self.assertEqual(1.0, row["slots"], where)
            self.assertEqual(8000.0, row["hold"], where)
            self.assertEqual(40.0, row["hp_max"], where)
            self.assertGreaterEqual(row["z"], -93.0, where)
            self.assertLessEqual(row["z"], -80.0, where)
            self.assertLessEqual(math.hypot(row["x"] - row["ax"], row["y"] - row["ay"]), row["ar"], where)
        # Alternating north/south by seq, so any healer count splits evenly.
        for seq, row in enumerate(sorted(rows, key=lambda r: r["seq"])):
            self.assertEqual(seq % 2 == 0, row["y"] > self.HER[1],
                             "seq %d is not on its alternating side" % seq)

    def test_the_seats_stand_clear_of_the_breath_and_the_egg_field(self):
        seats = [(row["x"], row["y"]) for row in self.seats()]
        self.assertEqual(len(seats), len(set(seats)), "a seat is named twice")
        for x, y in seats:
            # The spec's own centre bounds (the anchor is the stack, not one seat): 36 yd of the main tank's spot,
            # 25 yd off her landing point. The seats keep a slightly wider envelope: the farthest is 36.9 yd from
            # spot E and the nearest 22.4 yd from her landing point (the spec's per-seat numbers were the centres').
            self.assertLessEqual(math.hypot(x - self.MT[0], y - self.MT[1]), 37.0,
                                 "seat (%.1f, %.1f) is %.1f yd from spot E" % (
                                     x, y, math.hypot(x - self.MT[0], y - self.MT[1])))
            self.assertGreaterEqual(math.hypot(x - self.HER[0], y - self.HER[1]), 22.0,
                                    "seat (%.1f, %.1f) is %.1f yd from her landing point" % (
                                        x, y, math.hypot(x - self.HER[0], y - self.HER[1])))
            # The flank: 60-120 deg off the east axis seen from her landing point, out of the front cone and the tail.
            angle = abs(math.degrees(math.atan2(y - self.HER[1], x - self.HER[0])))
            angle = min(angle, 360.0 - angle)
            self.assertGreaterEqual(angle, 60.0, "seat (%.1f, %.1f) is %.0f deg off her east axis" % (x, y, angle))
            self.assertLessEqual(angle, 120.0, "seat (%.1f, %.1f) is %.0f deg off her east axis" % (x, y, angle))
            # East of the egg field, and clear of the cave-mouth eggs by 14 yd anyway.
            self.assertGreaterEqual(x, 2.0, "seat (%.1f, %.1f) is inside the egg field" % (x, y))
            for egg in self.EGGS:
                self.assertGreaterEqual(math.hypot(x - egg[0], y - egg[1]), 14.0,
                                        "seat (%.1f, %.1f) is %.1f yd from egg %s" % (
                                            x, y, math.hypot(x - egg[0], y - egg[1]), egg))
        for centre in (self.NORTH, self.SOUTH):
            self.assertLessEqual(math.hypot(centre[0] - self.MT[0], centre[1] - self.MT[1]), 36.0)
            self.assertGreaterEqual(math.hypot(centre[0] - self.HER[0], centre[1] - self.HER[1]), 25.0)


class BossTacticOnyxiaP3Sql(unittest.TestCase):
    """20261117210000_world.sql: Fix A, the Hatcher burn off the P3 damage crew."""

    STAMP = "20261117210000_world.sql"
    KILLED = ("`boss_entry` = 50144 AND `kind` IN ('kill_first', 'hold_kill')")

    def migration(self):
        path = os.path.join(ROOT, "data/sql/world", self.STAMP)
        self.assertTrue(os.path.exists(path), "the P3 Hatcher migration is missing")
        with open(path, encoding="utf-8") as handle:
            return handle.read()

    def later_files(self):
        return [path for path in sorted(glob.glob(os.path.join(ROOT, "data/sql/world/*_world.sql")))
                if os.path.basename(path) > self.STAMP]

    def test_the_hatcher_burn_rows_are_gone(self):
        text = self.migration()
        self.assertIn("DELETE FROM `ai_boss_tactic` WHERE `map` = 249 AND `boss_entry` = 10184 AND "
                      "`kind` = 'hold_kill' AND `target_entry` = 50144;", text)
        self.assertIn("DELETE FROM `ai_boss_tactic` WHERE `map` = 249 AND " + self.KILLED + ";", text)
        # Nothing re-inserts a kill_first on 50144 or a hold off her while a Hatcher fights.
        for path in [os.path.join(ROOT, "data/sql/world", self.STAMP)] + self.later_files():
            with open(path, encoding="utf-8") as handle:
                live = "\n".join(line for line in handle.read().splitlines()
                                 if line.startswith("(") or line.startswith("INSERT"))
            self.assertNotIn("(249, 50144, 'kill_first'", live,
                             "%s re-inserts a Hatcher kill_first" % os.path.basename(path))
            self.assertNotIn("(249, 10184, 'hold_kill', 50144", live,
                             "%s re-inserts the hold off Onyxia" % os.path.basename(path))
        # No later migration brings either back.
        for path in self.later_files():
            with open(path, encoding="utf-8") as handle:
                text_later = handle.read()
            self.assertNotIn("50144, 'kill_first'", text_later, os.path.basename(path))
            self.assertNotIn("10184, 'hold_kill', 50144", text_later, os.path.basename(path))

    def test_the_dps_leave_the_hatchers_to_their_posts(self):
        text = self.migration()
        self.assertIn(
            "(249, 50144, 'hold_kill', 10184, 0, 0, 0, '', 0, 100, 0, '', 0, 0, 0, 0, 0, 0, '", text)
        # The P3 landing row: band 0-40, until_tank, 5 s (a spell-less row holds from the band's edge).
        self.assertIn(
            "(249, 10184, 'dps_hold_after', 0, 0, 0, 5, '', 0, 40, 0, 'until_tank', 0, 0, 0, 0, 0, 0, '", text)
        self.assertIn("DELETE FROM `ai_boss_tactic` WHERE `map` = 249 AND `boss_entry` = 10184 AND "
                      "`kind` = 'dps_hold_after' AND `hp_max` = 40;", text)


if __name__ == "__main__":
    unittest.main()

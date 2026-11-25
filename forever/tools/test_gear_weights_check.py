#!/usr/bin/env python3
"""Checks for forever/tools/check_gear_weights.py (the offline weight/gear audit).

The audit reads four shipped files and must keep matching them: the seed SQL
(weights), the stat policy header (ItemModType map and rating divisors), the
aotc priority/gear CSVs and the TB id list. These tests pin the parse against
the shipped content and the arithmetic against the C++ scoring it mirrors.
"""
import contextlib
import io
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_gear_weights as check


class WeightsSql(unittest.TestCase):
    def test_shipped_scales_parse(self):
        scales = check.parse_weights()
        self.assertEqual(len(scales), 29)
        self.assertEqual(sum(len(s["stats"]) for s in scales.values()), 238)
        self.assertEqual((scales[2]["name"], scales[2]["class"]), ("fury", 1))
        self.assertEqual((scales[32]["name"], scales[32]["class"]), ("feraldps", 11))

    def test_forever_stat_mix(self):
        scales = check.parse_weights()
        stats = scales[4]["stats"]
        self.assertNotIn("splpwr", stats)         # healers score stat 45 at splheal
        self.assertEqual(stats["spi"], 3)
        self.assertEqual(stats["manargn"], 5)
        self.assertEqual(stats["splheal"], 10)    # +healing ~ twice intellect
        self.assertNotIn("splpwr", scales[14]["stats"])
        self.assertEqual(scales[22]["stats"]["spi"], 13)
        self.assertEqual(scales[31]["stats"]["splheal"], 21)
        self.assertEqual(scales[6]["stats"]["splpwr"], 1)
        self.assertEqual(scales[21]["stats"]["splpwr"], 1)
        self.assertEqual(scales[21]["stats"]["int"], 2)
        self.assertEqual(scales[7]["stats"]["int"], 1)

    def test_healers_value_healing_above_intellect(self):
        scales = check.parse_weights()
        for sid in (4, 13, 14, 22, 31):
            stats = scales[sid]["stats"]
            self.assertGreater(stats["splheal"], stats["int"], "scale %d" % sid)
            self.assertLessEqual(stats["manargn"], stats["int"], "scale %d" % sid)


class PolicyHeader(unittest.TestCase):
    def test_names_and_divisors(self):
        names, divisors = check.parse_policy()
        self.assertEqual(names[45], "splpwr")
        self.assertEqual(names[41], "splheal")
        self.assertEqual(names[42], "splpwr")
        self.assertEqual(names[32], "critstrkrtng")
        self.assertEqual(names[21], "spellcritstrkrtng")
        self.assertEqual(divisors[32], 14)
        self.assertEqual(divisors[21], 14)
        self.assertEqual(divisors[31], 10)
        self.assertEqual(divisors[18], 10)
        self.assertEqual(divisors[13], 12)
        self.assertEqual(divisors[14], 15)
        self.assertEqual(divisors[15], 5)
        self.assertEqual(divisors[37], 10)
        # Raw-value stats are not divided.
        for stat in (12, 38, 41, 42, 45, 47, 48, 50, 85):
            self.assertNotIn(stat, divisors)


class StatFamilies(unittest.TestCase):
    """The loot-priority family the weights imply; hybrids must stay physical."""

    def test_families(self):
        scales = check.parse_weights()
        want = {
            1: "Strength", 2: "Strength", 3: "Tank", 4: "Spell", 5: "Tank",
            6: "Strength", 7: "Agility", 8: "Agility", 9: "Agility",
            10: "Agility", 11: "Agility", 12: "Agility", 13: "Spell",
            14: "Spell", 15: "Spell", 16: "Tank", 20: "Spell",
            21: "Strength", 22: "Spell", 23: "Spell", 24: "Spell",
            25: "Spell", 26: "Spell", 27: "Spell", 28: "Spell",
            29: "Spell", 30: "Tank", 31: "Spell", 32: "Strength",
        }
        for sid, family in want.items():
            self.assertEqual(check.spec_family(scales[sid]["stats"]), family, "scale %d" % sid)

    def test_hybrids_keep_strength_with_margin(self):
        """A +1 to any spell stat must not flip retribution or enhancement to Spell."""
        scales = check.parse_weights()
        for sid in (6, 21):
            stats = scales[sid]["stats"]
            spell = sum(v for k, v in stats.items() if k in check.SPELL_STATS)
            self.assertLess(spell, stats["str"], "scale %d" % sid)


class Coverage(unittest.TestCase):
    def test_spec_map_matches_priority(self):
        priority = check.parse_priority()
        self.assertEqual(len(priority), 27)
        self.assertEqual(set(priority), set(check.SPEC_SCALE))
        scales = check.parse_weights()
        for key, sid in check.SPEC_SCALE.items():
            self.assertIn(sid, scales, key)

    def test_stat_names(self):
        self.assertEqual(check.weight_stat_name("mage", "fire", "Crit"), "spellcritstrkrtng")
        self.assertEqual(check.weight_stat_name("warrior", "fury", "Crit"), "critstrkrtng")
        self.assertEqual(check.weight_stat_name("paladin", "holy", "Crit"), "spellcritstrkrtng")
        self.assertEqual(check.weight_stat_name("druid", "feral", "Crit"), "critstrkrtng")
        self.assertEqual(check.weight_stat_name("shaman", "elemental", "Hit"), "spellhitrtng")
        self.assertEqual(check.weight_stat_name("shaman", "restoration", "Hit"), "spellhitrtng")
        self.assertEqual(check.weight_stat_name("hunter", "survival", "Hit"), "hitrtng")
        self.assertEqual(check.weight_stat_name("mage", "fire", "Hit"), "spellhitrtng")
        self.assertEqual(check.weight_stat_name("warlock", "affliction", "Hit"), "spellhitrtng")
        self.assertEqual(check.weight_stat_name("priest", "holy", "Spell Power"), "splpwr")
        self.assertEqual(check.weight_stat_name("druid", "restoration", "Mana per 5"), "manargn")


class Scoring(unittest.TestCase):
    def setUp(self):
        self.scales = check.parse_weights()
        self.names, self.divisors = check.parse_policy()

    def score(self, sid, stats=None, **kw):
        item = {"stats": stats or {}, "armor": 0, "dmgMin": 0, "dmgMax": 0,
                "delay": 0, "subclass": 0}
        item.update(kw)
        return check.score_item(self.scales[sid], item, self.names, self.divisors)

    def test_rating_stats_score_in_percent(self):
        # fury: str 4/point, crit 56/%, hit 56/%
        self.assertEqual(self.score(2, {4: 20, 32: 28, 31: 20}), 20 * 4 + 2 * 56 + 2 * 56)

    def test_raw_stats_unchanged(self):
        self.assertEqual(self.score(2, {38: 50}), 50 * 2)          # attack power
        self.assertEqual(self.score(2, {7: 10}), 10 * 1)            # stamina
        self.assertEqual(self.score(14, {41: 10}), 10 * 61)         # holy priest healing
        self.assertEqual(self.score(6, {45: 10}), 10 * 1)           # retribution spell power

    def test_spell_power_scores_the_better_weight(self):
        # Stat 45 (spell power) grants damage and healing; stat 42 (spell damage
        # done) grants damage only, so a pure healer values 45 and rejects 42.
        self.assertEqual(self.score(14, {45: 10}), 10 * 61)         # holy priest: splheal 61
        self.assertEqual(self.score(14, {42: 10}), 0)
        self.assertEqual(self.score(4, {45: 10}), 10 * 10)          # holy paladin: splheal 10
        self.assertEqual(self.score(4, {42: 10}), 0)
        # Damage casters and the specs that price both weights keep the better one.
        self.assertEqual(self.score(24, {45: 10}), 10 * 7)          # mage fire: splpwr 7 only
        self.assertEqual(self.score(31, {45: 10}), 10 * 21)         # resto druid: 21 over 10
        self.assertEqual(self.score(31, {42: 10}), 10 * 10)

    def test_rating_stats_round_to_nearest_and_keep_one_point(self):
        # hunter crit weight 29 per percent: 7 crit rating is 0.5% = 14.5 -> 15
        self.assertEqual(self.score(7, {32: 7}), 15)
        # prot paladin prices hit at 1 per percent: 4 hit rating is still worth 1
        self.assertEqual(self.score(5, {31: 4}), 1)
        self.assertEqual(self.score(5, {31: 0}), 0)                 # no stat, no point
        self.assertEqual(self.score(5, {37: 4}), 1)                 # expertise, same rule

    def test_hybrid_caster_jewelry_does_not_beat_strength(self):
        caster_ring = {5: 10, 45: 30}   # +10 intellect, +30 spell power
        melee_ring = {4: 15, 7: 15}     # +15 strength, +15 stamina
        for sid in (6, 21):
            self.assertLess(self.score(sid, caster_ring), self.score(sid, melee_ring),
                            "scale %d" % sid)

    def test_unknown_or_unweighted_stats_score_zero(self):
        self.assertEqual(self.score(2, {82: 100}), 0)               # unweighted ItemModType
        self.assertEqual(self.score(2, {5: 20}), 0)                 # intellect on fury
        self.assertEqual(self.score(2, {32: -28}), 0)               # negative stat

    def test_armor_and_weapon_dps(self):
        self.assertEqual(self.score(5, armor=500), 500)             # prot paladin armor 1
        self.assertEqual(self.score(2, dmgMin=50, dmgMax=100, delay=2500), 30 * 28)
        self.assertEqual(self.score(7, dmgMin=50, dmgMax=100, delay=2500, subclass=2), 30 * 14)

    def test_stats_field(self):
        self.assertEqual(check.parse_stats_field("7:9,3:3"), {7: 9, 3: 3})
        self.assertEqual(check.parse_stats_field("41:11,42:4,6:6"), {41: 11, 42: 4, 6: 6})
        self.assertEqual(check.parse_stats_field(""), {})
        self.assertEqual(check.parse_stats_field("7:x,3:3"), {3: 3})


class EndToEnd(unittest.TestCase):
    def run_tool(self, gear_rows, item_rows, top=2):
        with tempfile.TemporaryDirectory() as d:
            gear = os.path.join(d, "gear.csv")
            items = os.path.join(d, "items.csv")
            with open(gear, "w", encoding="utf-8") as f:
                f.write("class,spec,population,slot,name,wowhead_id,pct,chars,of_chars\n")
                for row in gear_rows:
                    f.write(",".join(str(x) for x in row) + "\n")
            with open(items, "w", encoding="utf-8") as f:
                f.write("id,name,stats,armor,dmgMin,dmgMax,delay,subclass\n")
                for row in item_rows:
                    f.write(",".join(str(x) for x in row) + "\n")
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                rc = check.main(["--gear", gear, "--items", items, "--top", str(top)])
            return rc, buf.getvalue()

    def test_flags_an_item_the_scoring_rates_badly(self):
        gear = [
            ("warrior", "fury", "best_geared", "Head", "Strength helm", "item=1", 50, 1, 1),
            ("warrior", "fury", "best_geared", "Head", "Caster helm", "item=2", 40, 1, 1),
            ("warrior", "fury", "best_geared", "Head", "No data helm", "item=3", 10, 1, 1),
            ("warrior", "fury", "best_geared", "Head", "Plain helm", "item=4", 5, 1, 1),
            ("warrior", "fury", "best_geared", "Head", "Plain helm 2", "item=5", 4, 1, 1),
        ]
        items = [
            (1, "Strength helm", '"4:20"', 0, 0, 0, 0, 0),
            (2, "Caster helm", '"5:20"', 0, 0, 0, 0, 0),
            (4, "Plain helm", '"4:10"', 0, 0, 0, 0, 0),
            (5, "Plain helm 2", '"4:10"', 0, 0, 0, 0, 0),
        ]
        rc, out = self.run_tool(gear, items)
        self.assertEqual(rc, 0)
        self.assertIn("Caster helm", out)
        self.assertIn("1 worn items have no stats row in the dump", out)

    def test_no_dump_still_reports_weights(self):
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = check.main([])
        self.assertEqual(rc, 0)
        text = buf.getvalue()
        self.assertIn("scales: 29, weights: 238", text)
        self.assertIn("No --items dump", text)


if __name__ == "__main__":
    unittest.main()

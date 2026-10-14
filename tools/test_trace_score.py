"""Tests for tools/trace_score.py: each oracle on a trace that trips it and on
one that does not, the merged-record expansion of the loop oracle, the summary
file update and the command line."""
import json
import os
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import trace_score as ts  # noqa: E402

TANK, HEAL, MAGE = "0x1", "0x2", "0x3"
MOB = "0xf1"


def rec(kind, t=0, **fields):
    out = {"v": 1, "k": kind, "run": "r1", "src": "server", "t": t}
    out.update(fields)
    return out


def party():
    return [
        rec("party", g=TANK, name="Tank", **{"class": "warrior"}, role="tank", level=40),
        rec("party", g=HEAL, name="Heal", **{"class": "priest"}, role="heal", level=40),
        rec("party", g=MAGE, name="Mage", **{"class": "mage"}, role="dps", level=40),
    ]


def bot(g, cb=True, al=True, pw=100, mpw=100, pt=0, cs=0, x=0.0, y=0.0, tg=None, fa=None, mv=None, fr=0):
    b = {"g": g, "al": al, "cb": cb, "st": "c" if cb else "n", "hp": 100, "mhp": 100,
         "pw": pw, "mpw": mpw, "pt": pt, "x": x, "y": y, "z": 0.0}
    if fa is not None:
        b["fa"] = fa
    if mv is not None:
        b["mv"] = mv
    if fr:
        b["fr"] = fr
    if cs:
        b["cs"] = cs
    if tg:
        b["tg"] = tg
    return b


def mob(tg=TANK, cb=True, **extra):
    m = {"g": MOB, "hp": 90, "x": 1.0, "y": 1.0, "z": 0.0, "cb": cb, "tg": tg}
    m.update(extra)
    return m


def frames(count, step=500, bots=None, mobs=None, start=0):
    out = []
    for i in range(count):
        t = start + i * step
        out.append(rec("frame", t=t, bots=bots(t) if callable(bots) else (bots or []),
                       mobs=mobs(t) if callable(mobs) else (mobs or [])))
    return out


CAPS = ["facing", "moving", "interrupts", "in_fire"]


def build(*parts, complete=True, caps=CAPS):
    header = rec("header", scope="party", scope_name="Tank", interval_ms=500, start_unix=1)
    if caps is not None:
        header["caps"] = caps
    records = [header]
    records += party()
    for part in parts:
        records += part
    if complete:
        records.append(rec("end", t=99999, reason="stop", counters={}))
    return records


def load(records):
    d = tempfile.mkdtemp()
    path = os.path.join(d, "r1.jsonl")
    with open(path, "w") as fh:
        for r in records:
            fh.write(json.dumps(r) + "\n")
    return path, ts.load_trace(path)


def one(records, oracle, **overrides):
    _, trace = load(records)
    cfg = dict(ts.DEFAULTS)
    cfg.update(overrides)
    return oracle(trace, cfg)


def action(g, name, res, t, n=1, tl=None):
    r = rec("action", t=t, g=g, name=name, res=res, rel=1.0)
    if n > 1:
        r["n"] = n
        r["tl"] = tl if tl is not None else t
    return r


class LoopOracle(unittest.TestCase):
    def test_nine_failures_in_ten_seconds_is_a_loop(self):
        fails = [action(MAGE, "summon spellstone", "FAILED", i * 500) for i in range(9)]
        r = one(build(fails), ts.oracle_action_loop)
        self.assertEqual(r["verdict"], "fail")
        self.assertEqual(r["count"], 1)
        self.assertEqual(r["first_ms"], 4000)

    def test_eight_is_not(self):
        fails = [action(MAGE, "stay", "IMPOSSIBLE", i * 500) for i in range(8)]
        self.assertEqual(one(build(fails), ts.oracle_action_loop)["verdict"], "pass")

    def test_slow_failures_never_add_up(self):
        fails = [action(MAGE, "melee", "FAILED", i * 2000) for i in range(30)]
        self.assertEqual(one(build(fails), ts.oracle_action_loop)["verdict"], "pass")

    def test_successes_and_other_outcomes_do_not_count(self):
        recs = [action(MAGE, "melee", "OK", i * 100) for i in range(50)]
        recs += [action(MAGE, "melee", "USELESS", i * 100) for i in range(50)]
        self.assertEqual(one(build(recs), ts.oracle_action_loop)["verdict"], "pass")

    def test_a_merged_record_counts_its_occurrences(self):
        merged = [action(MAGE, "shoot", "FAILED", 1000, n=40, tl=4000)]
        r = one(build(merged), ts.oracle_action_loop)
        self.assertEqual(r["verdict"], "fail")
        self.assertIn("Mage shoot FAILED", r["examples"][0])

    def test_per_bot_and_per_action(self):
        recs = []
        for i in range(6):
            recs.append(action(MAGE, "melee", "FAILED", i * 100))
            recs.append(action(TANK, "melee", "FAILED", i * 100))
            recs.append(action(MAGE, "shoot", "FAILED", i * 100))
        self.assertEqual(one(build(recs), ts.oracle_action_loop)["verdict"], "pass")

    def test_a_continuing_loop_is_reported_again_after_the_window(self):
        fails = [action(MAGE, "stay", "FAILED", i * 500) for i in range(100)]
        r = one(build(fails), ts.oracle_action_loop)
        self.assertGreaterEqual(r["count"], 4)

    def test_no_actions_is_na(self):
        self.assertEqual(one(build(), ts.oracle_action_loop)["verdict"], "na")


class ThreatOracle(unittest.TestCase):
    def fight(self, target, **mob_extra):
        return frames(10, bots=lambda t: [bot(TANK), bot(HEAL), bot(MAGE)],
                      mobs=lambda t: [mob(tg=target, **mob_extra)])

    def test_mob_on_the_mage_for_four_seconds_fails(self):
        r = one(build(self.fight(MAGE)), ts.oracle_threat_off_tank)
        self.assertEqual(r["verdict"], "fail")
        self.assertEqual(r["count"], 1)
        self.assertIn("on Mage (dps)", r["examples"][0])

    def test_mob_on_the_tank_passes(self):
        self.assertEqual(one(build(self.fight(TANK)), ts.oracle_threat_off_tank)["verdict"], "pass")

    def test_a_short_stretch_passes(self):
        short = frames(4, bots=lambda t: [bot(TANK), bot(MAGE)], mobs=lambda t: [mob(tg=MAGE)])
        self.assertEqual(one(build(short), ts.oracle_threat_off_tank)["verdict"], "pass")

    def test_crowd_controlled_or_evading_mobs_are_ignored(self):
        self.assertEqual(one(build(self.fight(MAGE, cc=True)), ts.oracle_threat_off_tank)["verdict"], "pass")
        self.assertEqual(one(build(self.fight(MAGE, ev=True)), ts.oracle_threat_off_tank)["verdict"], "pass")

    def test_a_dead_tank_excuses_it(self):
        fr = frames(10, bots=lambda t: [bot(TANK, al=False, cb=False), bot(MAGE)], mobs=lambda t: [mob(tg=MAGE)])
        self.assertEqual(one(build(fr), ts.oracle_threat_off_tank)["verdict"], "pass")

    def test_no_tank_is_na(self):
        records = build(self.fight(MAGE))
        records = [r for r in records if r.get("role") != "tank"]
        self.assertEqual(one(records, ts.oracle_threat_off_tank)["verdict"], "na")


class IdleOracle(unittest.TestCase):
    def combat(self, **kw):
        return frames(14, bots=lambda t: [bot(TANK), bot(MAGE, **kw)], mobs=lambda t: [mob()])

    def test_no_cast_and_no_action_is_idle(self):
        acts = [action(TANK, "melee", "OK", 0, n=14, tl=6500)]
        r = one(build(self.combat(), acts), ts.oracle_idle_caster)
        self.assertEqual(r["verdict"], "fail")
        self.assertIn("Mage idle", r["examples"][0])

    def test_casting_is_not_idle(self):
        acts = [action(TANK, "melee", "OK", 0)]
        r = one(build(self.combat(cs=133), acts), ts.oracle_idle_caster)
        self.assertEqual(r["verdict"], "pass")

    def test_an_executed_action_is_not_idle(self):
        acts = [action(MAGE, "shoot", "OK", 0, n=14, tl=6500)]
        self.assertEqual(one(build(self.combat(), acts), ts.oracle_idle_caster)["verdict"], "pass")

    def test_failing_actions_do_not_excuse_it(self):
        acts = [action(MAGE, "frostbolt", "FAILED", 0, n=14, tl=6500)]
        self.assertEqual(one(build(self.combat(), acts), ts.oracle_idle_caster)["verdict"], "fail")

    def test_out_of_mana_is_not_idle(self):
        acts = [action(TANK, "melee", "OK", 0)]
        r = one(build(self.combat(pw=3), acts), ts.oracle_idle_caster)
        self.assertEqual(r["verdict"], "pass")

    def test_out_of_combat_is_not_idle(self):
        fr = frames(14, bots=lambda t: [bot(MAGE, cb=False)], mobs=lambda t: [mob()])
        acts = [action(TANK, "melee", "OK", 0)]
        self.assertEqual(one(build(fr, acts), ts.oracle_idle_caster)["verdict"], "pass")


class DeathOracle(unittest.TestCase):
    def test_a_death_fails_and_names_the_killer(self):
        mobs = [rec("mobs", t=100, g=MOB, entry=1, name="Archaedas", level=47, rank=3, boss=True)]
        evs = [rec("event", t=8000, ev="death", a=MAGE, b=MOB)]
        r = one(build(frames(2, bots=[bot(MAGE)]), mobs, evs), ts.oracle_deaths)
        self.assertEqual(r["verdict"], "fail")
        self.assertEqual(r["first_ms"], 8000)
        self.assertIn("Mage died", r["examples"][0])
        self.assertIn("Archaedas", r["examples"][0])

    def test_no_death_passes(self):
        self.assertEqual(one(build(frames(2, bots=[bot(MAGE)])), ts.oracle_deaths)["verdict"], "pass")


class OomOracle(unittest.TestCase):
    def test_a_dry_healer_in_combat_fails(self):
        fr = frames(10, bots=lambda t: [bot(TANK), bot(HEAL, pw=2)])
        self.assertEqual(one(build(fr), ts.oracle_oom_healer)["verdict"], "fail")

    def test_a_healer_with_mana_passes(self):
        fr = frames(10, bots=lambda t: [bot(TANK), bot(HEAL, pw=60)])
        self.assertEqual(one(build(fr), ts.oracle_oom_healer)["verdict"], "pass")

    def test_dry_out_of_combat_is_not_this_oracle(self):
        fr = frames(10, bots=lambda t: [bot(TANK, cb=False), bot(HEAL, cb=False, pw=2)])
        self.assertEqual(one(build(fr), ts.oracle_oom_healer)["verdict"], "pass")

    def test_a_rage_user_is_not_mana(self):
        fr = frames(10, bots=lambda t: [bot(TANK), bot(HEAL, pw=0, pt=1)])
        self.assertEqual(one(build(fr), ts.oracle_oom_healer)["verdict"], "pass")


class RestOracle(unittest.TestCase):
    def test_drink_failing_in_a_loop_fails(self):
        acts = [action(MAGE, "drink", "FAILED", i * 1000) for i in range(6)]
        r = one(build(acts), ts.oracle_rest_failure)
        self.assertEqual(r["verdict"], "fail")
        self.assertIn("drink failed 6 times", r["examples"][0])

    def test_other_failing_actions_are_not_rest(self):
        acts = [action(MAGE, "frostbolt", "FAILED", i * 1000) for i in range(6)]
        self.assertEqual(one(build(acts), ts.oracle_rest_failure)["verdict"], "pass")

    def test_standing_at_low_mana_without_recovering_fails(self):
        fr = frames(60, bots=lambda t: [bot(MAGE, cb=False, pw=10)])
        r = one(build(fr), ts.oracle_rest_failure)
        self.assertEqual(r["verdict"], "fail")
        self.assertIn("without recovering", r["examples"][0])

    def test_recovering_mana_passes(self):
        fr = frames(60, bots=lambda t: [bot(MAGE, cb=False, pw=10 + t // 1000)])
        self.assertEqual(one(build(fr), ts.oracle_rest_failure)["verdict"], "pass")

    def test_moving_bot_is_not_a_stall(self):
        fr = frames(60, bots=lambda t: [bot(MAGE, cb=False, pw=10, x=t / 100.0)])
        self.assertEqual(one(build(fr), ts.oracle_rest_failure)["verdict"], "pass")


class FacingOracle(unittest.TestCase):
    def melee(self, fa, **kw):
        # 12 frames (5.5 s) next to the mob, 2 yd away.
        return frames(12, bots=lambda t: [bot(TANK, x=0.0, tg=MOB, fa=fa, **kw)],
                      mobs=lambda t: [mob(tg=TANK, x=2.0, y=0.0)])

    def test_back_to_the_mob_in_melee_fails(self):
        r = one(build(self.melee(150)), ts.oracle_facing)
        self.assertEqual(r["verdict"], "fail")
        self.assertIn("in melee 150 deg off", r["examples"][0])

    def test_facing_the_mob_passes(self):
        self.assertEqual(one(build(self.melee(20)), ts.oracle_facing)["verdict"], "pass")

    def test_casting_while_facing_away_fails(self):
        fr = frames(12, bots=lambda t: [bot(MAGE, tg=MOB, fa=120, cs=116)], mobs=lambda t: [mob(tg=TANK, x=30.0)])
        r = one(build(fr), ts.oracle_facing)
        self.assertEqual(r["verdict"], "fail")
        self.assertIn("casting", r["examples"][0])

    def test_far_and_not_casting_is_not_melee(self):
        fr = frames(12, bots=lambda t: [bot(TANK, tg=MOB, fa=150)], mobs=lambda t: [mob(tg=TANK, x=30.0)])
        self.assertEqual(one(build(fr), ts.oracle_facing)["verdict"], "pass")

    def test_turning_on_the_move_is_not_flagged(self):
        self.assertEqual(one(build(self.melee(150, mv=True)), ts.oracle_facing)["verdict"], "pass")

    def test_a_short_stretch_passes(self):
        fr = frames(3, bots=lambda t: [bot(TANK, tg=MOB, fa=150)], mobs=lambda t: [mob(tg=TANK, x=2.0)])
        self.assertEqual(one(build(fr), ts.oracle_facing)["verdict"], "pass")

    def test_na_when_the_producer_cannot_see_it(self):
        self.assertEqual(one(build(self.melee(150), caps=[]), ts.oracle_facing)["verdict"], "na")
        # An older trace without a caps list: judged by whether any sample carries the field.
        self.assertEqual(one(build(self.melee(150), caps=None), ts.oracle_facing)["verdict"], "fail")
        plain = frames(12, bots=lambda t: [bot(TANK, tg=MOB)], mobs=lambda t: [mob()])
        self.assertEqual(one(build(plain, caps=None), ts.oracle_facing)["verdict"], "na")


class SelfInterruptOracle(unittest.TestCase):
    def interrupts(self, times, why="moving"):
        return [rec("event", t=t, ev="cast_interrupted", a=MAGE, v=116, **({"s": why} if why else {}))
                for t in times]

    def test_repeated_cancels_while_moving_fail(self):
        r = one(build(self.interrupts([1000, 5000, 9000])), ts.oracle_self_interrupt)
        self.assertEqual(r["verdict"], "fail")
        self.assertIn("Mage: 3 casts", r["examples"][0])

    def test_one_or_two_pass(self):
        self.assertEqual(one(build(self.interrupts([1000, 5000])), ts.oracle_self_interrupt)["verdict"], "pass")

    def test_spread_out_passes(self):
        self.assertEqual(one(build(self.interrupts([0, 40000, 80000, 120000])), ts.oracle_self_interrupt)["verdict"], "pass")

    def test_interrupts_while_standing_do_not_count(self):
        self.assertEqual(one(build(self.interrupts([1000, 2000, 3000, 4000], why="")), ts.oracle_self_interrupt)["verdict"], "pass")

    def test_na_without_the_capability(self):
        self.assertEqual(one(build(self.interrupts([1, 2, 3]), caps=["facing"]), ts.oracle_self_interrupt)["verdict"], "na")
        self.assertEqual(one(build(self.interrupts([1, 2, 3]), caps=None), ts.oracle_self_interrupt)["verdict"], "na")


class StuckOracle(unittest.TestCase):
    def test_moving_in_place_fails(self):
        fr = frames(20, bots=lambda t: [bot(TANK, cb=False, mv=True, x=5.0, y=5.0)])
        r = one(build(fr), ts.oracle_stuck)
        self.assertEqual(r["verdict"], "fail")
        self.assertIn("no progress", r["examples"][0])

    def test_standing_still_is_not_stuck(self):
        fr = frames(20, bots=lambda t: [bot(TANK, cb=False, mv=False, x=5.0, y=5.0)])
        self.assertEqual(one(build(fr), ts.oracle_stuck)["verdict"], "pass")

    def test_walking_is_not_stuck(self):
        fr = frames(20, bots=lambda t: [bot(TANK, cb=False, mv=True, x=t / 100.0, y=0.0)])
        self.assertEqual(one(build(fr), ts.oracle_stuck)["verdict"], "pass")

    def test_oscillating_between_two_points_fails(self):
        def at(t):
            return 0.0 if (t // 500) % 2 == 0 else 6.0
        fr = frames(30, bots=lambda t: [bot(TANK, cb=False, mv=True, x=at(t), y=0.0)])
        r = one(build(fr), ts.oracle_stuck)
        self.assertEqual(r["verdict"], "fail")
        self.assertIn("oscillated", r["examples"][0])

    def test_oscillating_in_combat_is_left_to_other_oracles(self):
        def at(t):
            return 0.0 if (t // 500) % 2 == 0 else 6.0
        fr = frames(30, bots=lambda t: [bot(TANK, cb=True, mv=True, x=at(t), y=0.0)])
        self.assertEqual(one(build(fr), ts.oracle_stuck)["verdict"], "pass")

    def test_a_short_stall_passes(self):
        fr = frames(6, bots=lambda t: [bot(TANK, cb=False, mv=True, x=5.0)])
        self.assertEqual(one(build(fr), ts.oracle_stuck)["verdict"], "pass")

    def test_na_without_movement_data(self):
        fr = frames(20, bots=lambda t: [bot(TANK, cb=False, x=5.0)])
        self.assertEqual(one(build(fr, caps=None), ts.oracle_stuck)["verdict"], "na")


class FireOracle(unittest.TestCase):
    def test_standing_in_fire_fails(self):
        fr = frames(8, bots=lambda t: [bot(MAGE, fr=5740)])
        r = one(build(fr), ts.oracle_in_fire)
        self.assertEqual(r["verdict"], "fail")
        self.assertIn("spell 5740", r["examples"][0])

    def test_a_moment_in_fire_passes(self):
        fr = frames(3, bots=lambda t: [bot(MAGE, fr=5740)])
        self.assertEqual(one(build(fr), ts.oracle_in_fire)["verdict"], "pass")

    def test_no_fire_passes(self):
        fr = frames(8, bots=lambda t: [bot(MAGE)])
        self.assertEqual(one(build(fr), ts.oracle_in_fire)["verdict"], "pass")

    def test_na_without_the_capability(self):
        fr = frames(8, bots=lambda t: [bot(MAGE, fr=5740)])
        self.assertEqual(one(build(fr, caps=["facing", "moving"]), ts.oracle_in_fire)["verdict"], "na")


class Summary(unittest.TestCase):
    def test_score_writes_verdicts_and_keeps_the_rest(self):
        fails = [action(MAGE, "stay", "FAILED", i * 500) for i in range(9)]
        path, trace = load(build(fails))
        summary_path = ts.summary_path_for(path)
        with open(summary_path, "w") as fh:
            json.dump({"v": 1, "run": "r1", "status": "complete", "counters": {"frames": 3}, "oracles": []}, fh)
        results = ts.score(trace)
        summary = ts.update_summary(summary_path, trace, results, path)
        self.assertEqual(summary["verdict"], "fail")
        self.assertEqual(summary["counters"], {"frames": 3})
        self.assertEqual(summary["scorer"], ts.SCORER)
        by_id = {o["id"]: o for o in summary["oracles"]}
        self.assertEqual(by_id["action_loop"]["verdict"], "fail")
        self.assertEqual(sorted(by_id), sorted(["action_loop", "threat_off_tank", "idle_caster",
                                                "deaths", "oom_healer", "rest_failure", "facing",
                                                "self_interrupt", "stuck", "in_fire"]))
        with open(summary_path) as fh:
            self.assertEqual(json.load(fh)["verdict"], "fail")
        self.assertFalse(os.path.exists(summary_path + ".tmp"))

    def test_missing_summary_is_created_from_the_trace(self):
        path, trace = load(build(complete=False))
        summary_path = ts.summary_path_for(path)
        summary = ts.update_summary(summary_path, trace, ts.score(trace), path)
        self.assertEqual(summary["run"], "r1")
        self.assertFalse(summary["trace_complete"])
        self.assertEqual(summary["trace_file"], "r1.jsonl")
        self.assertTrue(os.path.exists(summary_path))

    def test_bad_lines_are_counted_not_fatal(self):
        path, _ = load(build())
        with open(path, "a") as fh:
            fh.write("{not json\n[1,2]\n\n")
        trace = ts.load_trace(path)
        self.assertEqual(trace.bad_lines, 2)
        self.assertTrue(trace.complete)

    def test_command_line(self):
        fails = [action(MAGE, "stay", "FAILED", i * 500) for i in range(9)]
        path, _ = load(build(fails))
        self.assertEqual(ts.main([path, "--json"]), 0)
        self.assertEqual(ts.main([path, "--no-write", "--fail-on-violation"]), 1)
        self.assertEqual(ts.main([path, "--no-write", "--set", "loop_failures=20", "--fail-on-violation"]), 0)
        self.assertEqual(ts.main([os.path.dirname(path), "--no-write"]), 0)
        with self.assertRaises(SystemExit):
            ts.main([path, "--set", "nonsense=1"])


if __name__ == "__main__":
    unittest.main()

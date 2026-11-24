#!/usr/bin/env python3
"""Wiring pins for #24 round 1: pool bots leave camps and quest mobs to real players (SPEC-TortoiseBotsForever-24).

The decisions are pure and exercised by forever/tools/test_pool_share_mobs_policy.cpp; these pins read the sources and prove the
places and the cuts stay where the spec puts them:
  * the policy header is core-free and carries every named decision;
  * the two compat sides exist, the Forever one asks the host, the Turtle one answers false (its pick is untouched);
  * the skip is asked from exactly two places in GrindTargetValue: the possible-targets loop and the idle fallback - and never
    from the attackers loop, so a mob fighting the bot or its group is still answered;
  * BotMgr reads and clamps the five keys, defaults the switch to off, and feeds the host tick beside TickQuestGroups (slice only);
  * the conf template documents the five keys with their defaults;
  * the host table holds no database call (the repo's own tools/test_forever_no_direct_queries.py is the authority);
  * slice_gen_context.py --check is still clean (no trigger/action/value/strategy was added, so nothing regenerates);
  * the pure rule test builds with its own g++ line and passes.

Run: python3 -m unittest forever/tools/test_pool_share_mobs_wiring.py
Mutations: SHARE_MOBS_MUTATE=1 python3 -m unittest forever/tools/test_pool_share_mobs_wiring.py   (every pin must then fail)
"""
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
POLICY = "ai/playerbot/policy/world/PoolShareMobsPolicy.h"
HOST = "forever/src/host/TbHostShareMobs.cpp"
HOST_H = "forever/src/host/TbHostShareMobs.h"
COMPAT_F = "forever/src/compat/TbCompat_W1.h"
COMPAT_T = "ai/cmangos-compat-w1.h"
GRIND = "ai/playerbot/strategy/values/GrindTargetValue.cpp"
MGR = "forever/src/TbBotMgr.cpp"
MGR_H = "forever/src/TbBotMgr.h"
CONF = "forever/conf/tortoisebots.conf.dist"
CMAKE = "CMakeLists.txt"

FILES = (POLICY, HOST, HOST_H, COMPAT_F, COMPAT_T, GRIND, MGR, MGR_H, CONF, CMAKE)

# The five keys and the defaults the template documents and LoadConfig falls back to.
KEYS = [
    ("TortoiseBots.World.PoolShareMobs", "0"),
    ("TortoiseBots.World.PoolShareMobs.NearYd", "30"),
    ("TortoiseBots.World.PoolShareMobs.FarYd", "60"),
    ("TortoiseBots.World.PoolShareMobs.QuestSpotCap", "3"),
    ("TortoiseBots.World.PoolShareMobs.GiveUpSeconds", "600"),
]


def sources(overrides=None):
    """Every file these pins read, with the mutation's text swapped in."""
    overrides = overrides or {}
    return {rel: overrides.get(rel, (ROOT / rel).read_text()) for rel in FILES}


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
    raise AssertionError(signature)


def strip_comments(text):
    import re
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


# --- the pins, as plain functions so a mutation can be run against mutated text -----------------------------------------
def check_policy_is_core_free(src):
    policy = src[POLICY]
    assert '#include "' not in policy, "the policy must not include a core header"
    for symbol in (
        "struct ShareMobsConfig", "ClampShareMobsConfig", "struct BotFacts", "ShareMobsApplies", "struct PlayerRow",
        "MobVerdict", "SpotWorkedByPlayer", "struct Worker", "PastQuestSpotCap", "class GiveUpBook", "void GiveUp(",
        "IsGivenUp(",
    ):
        assert symbol in policy, symbol
    # The row must answer its quest lists by binary search (sorted, unique is the host's promise).
    assert "std::binary_search" in policy


def check_compat_pair(src):
    forever = src[COMPAT_F]
    turtle = src[COMPAT_T]
    assert "inline bool PoolShareMobsSkipCompat(Player* bot, Unit* unit)" in forever
    assert "tbhost::sharemobs::Judge(bot, unit)" in forever
    body = func(turtle, "inline bool PoolShareMobsSkipCompat(")
    assert "return false;" in body, "the Turtle pick is untouched: the compat answers false there"


def grind_regions(src):
    body = func(src[GRIND], "Unit* GrindTargetValue::FindTargetForGrinding(")
    marker = 'std::list<ObjectGuid> targets = *context->GetValue<std::list<ObjectGuid> >("possible targets");'
    split = body.index(marker)
    return body[:split], body[split:]


def check_grind_calls(src):
    attackers, possible = grind_regions(src)
    assert "PoolShareMobsSkipCompat" not in attackers, "the attackers loop must never ask #24 (self-defence stays)"
    assert possible.count("PoolShareMobsSkipCompat(bot, unit)") == 1, "the possible-targets loop asks once"
    fallback = func(src[GRIND], "Unit* GrindTargetValue::FindIdleFallbackTarget(")
    assert fallback.count("PoolShareMobsSkipCompat(bot, unit)") == 1, "the idle fallback asks once"
    assert src[GRIND].count("PoolShareMobsSkipCompat(") == 2, "exactly the two NEW-pull places ask"
    assert "a real player works this area" in src[GRIND], "the grind log names the reason"


def check_bot_mgr(src):
    mgr = src[MGR]
    hdr = src[MGR_H]
    assert '#include "host/TbHostShareMobs.h"' in mgr
    assert "world::ShareMobsConfig GetShareMobsConfig() const" in hdr
    assert "bool IsOwnedBot(ObjectGuid characterGuid) const" in hdr
    load = func(mgr, "void BotMgr::LoadConfig(")
    assert "_shareMobs = world::ClampShareMobsConfig(shareMobs);" in load
    for key, default in KEYS:
        assert '"%s"' % key in load, key
    assert 'GetBoolDefault("TortoiseBots.World.PoolShareMobs", false)' in load, "the switch defaults to off"
    # The tick sits beside TickQuestGroups, in the slice-only block (the host cpp is compiled there only).
    update = func(mgr, "void BotMgr::Update(")
    i = update.index("TickQuestGroups(diff);")
    tail = update[i:]
    assert "tbhost::sharemobs::Tick(diff);" in tail, "the table is rebuilt from BotMgr::Update"
    assert tail.index("#if defined(TORTOISEBOTS_SLICE1)") < tail.index("tbhost::sharemobs::Tick(diff);"), "slice-guarded"
    assert '#include "host/TbHostShareMobs.h"' in src[MGR]


def check_quest_needs_skip_complete(src):
    # The row's lists only hold what the player's open quests still need: the core's own "objective complete" answer
    # reaches the rule, and the rule drops it.
    body = func(src[HOST], "void CollectQuestNeeds(")
    assert "player->IsQuestObjectiveComplete(questId, objective.ID)" in body, \
        "a finished objective must never enter the row"
    assert "AddQuestObjective(" in body, "the host fills the lists through the policy's rule"
    assert "player->IsQuestObjectiveComplete(questId, objective.ID));" in body, "the answer is passed as the rule's argument"
    add = func(src[POLICY], "inline void AddQuestObjective(")
    assert "|| complete ||" in add, "the rule reads the answer"
    assert "return;" in add, "and drops such an objective"


def check_facts_once_per_pass(src):
    # The bot's other facts cost a party walk, a guild-order lookup and an AI lookup: once, and only when a real player is
    # actually within the far ring of this mob.
    body = func(src[HOST], "MobVerdict Judge(")
    assert body.count("Facts(bot, config)") == 1, "read once per pass"
    assert body.index("if (distance > config.farYd)") < body.index("Facts(bot, config)"), \
        "not for a mob no real player is near"
    gate = body.find("if (!haveFacts)")
    assert gate != -1 and gate < body.index("Facts(bot, config)"), "not for every nearby row"


def check_conf(src):
    conf = src[CONF]
    for key, default in KEYS:
        documented = "#    %s\n" % key
        assert documented in conf, key
        assert "\n%s = %s\n" % (key, default) in conf, (key, default)
    assert "\nTortoiseBots.World.PoolShareMobs = 0\n" in conf, "the switch is documented OFF until the live rows prove it"


def check_host_has_no_query(src):
    host = strip_comments(src[HOST] + src[HOST_H])
    for word in ("Database.", "Database->", "Query(", "PQuery(", "Execute("):
        assert word not in host, word


def check_cmake_globs_a_slice_only_host(src):
    cmake = src[CMAKE]
    assert "host/TbHost[^/]*)\\\\.cpp$" in cmake or "host/TbHost[^/]*)\\.cpp$" in cmake, "the host glob the file rides on"
    assert "forever/src/host/TbHost*.cpp" in cmake


def check_generator_is_clean():
    proc = subprocess.run([sys.executable, str(ROOT / "forever/tools/slice_gen_context.py"), "--check"],
                          capture_output=True, text=True, timeout=180)
    assert proc.returncode == 0, proc.stdout + proc.stderr


class ShareMobsWiring(unittest.TestCase):
    def test_policy_is_core_free_and_complete(self):
        check_policy_is_core_free(sources())

    def test_the_compat_pair_hands_the_rule_to_the_host_and_leaves_turtle_alone(self):
        check_compat_pair(sources())

    def test_the_skip_is_asked_from_the_two_new_pull_places_only(self):
        check_grind_calls(sources())

    def test_bot_mgr_reads_clamps_and_ticks(self):
        check_bot_mgr(sources())

    def test_the_conf_documents_the_five_keys(self):
        check_conf(sources())

    def test_the_row_skips_objectives_the_player_already_finished(self):
        check_quest_needs_skip_complete(sources())

    def test_the_bot_facts_are_read_once_per_pass(self):
        check_facts_once_per_pass(sources())

    def test_the_host_makes_no_database_call(self):
        check_host_has_no_query(sources())

    def test_the_host_file_is_built_by_the_slice_only(self):
        check_cmake_globs_a_slice_only_host(sources())

    def test_the_generator_is_still_clean(self):
        check_generator_is_clean()

    def test_the_pure_rule_test_compiles_and_passes(self):
        # The rule itself is a standalone g++ test (as test_free_grind_policy.cpp is): build and run it here so the
        # tools runner covers it too.
        gxx = shutil.which("g++")
        if not gxx:
            self.skipTest("no g++")
        exe = pathlib.Path(tempfile.mkdtemp()) / "test_pool_share_mobs_policy"
        build = subprocess.run(
            [gxx, "-std=c++17", "-O1", "-Wall", "-Wextra", "-I", str(ROOT),
             str(ROOT / "forever/tools/test_pool_share_mobs_policy.cpp"), "-o", str(exe)],
            capture_output=True, text=True, timeout=300)
        self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
        run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=120)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertIn("RESULT: PASS", run.stdout)


@unittest.skipUnless(os.environ.get("SHARE_MOBS_MUTATE") == "1", "mutations are run on purpose: SHARE_MOBS_MUTATE=1")
class ShareMobsMutations(unittest.TestCase):
    """Each broken tree must fail the pin that guards it."""

    def assertFails(self, check, src):
        with self.assertRaises(AssertionError):
            check(src)

    def test_skip_in_attackers_loop(self):
        overrides = {GRIND: (ROOT / GRIND).read_text().replace(
            '    std::list<ObjectGuid> attackers = context->GetValue<std::list<ObjectGuid>>("possible attack targets")->Get();',
            '    std::list<ObjectGuid> attackers = context->GetValue<std::list<ObjectGuid>>("possible attack targets")->Get();\n'
            '    if (PoolShareMobsSkipCompat(bot, unit)) continue;')}
        self.assertFails(check_grind_calls, sources(overrides))

    def test_turtle_compat_true(self):
        overrides = {COMPAT_T: (ROOT / COMPAT_T).read_text().replace(
            "inline bool PoolShareMobsSkipCompat(Player* /*bot*/, Unit* /*unit*/)\n{\n    return false;",
            "inline bool PoolShareMobsSkipCompat(Player* /*bot*/, Unit* /*unit*/)\n{\n    return true;")}
        self.assertFails(check_compat_pair, sources(overrides))

    def test_completed_objective_kept(self):
        # The host stops asking the core and passes `false`: a finished objective stays in the row and bots step aside.
        overrides = {HOST: (ROOT / HOST).read_text().replace(
            "player->IsQuestObjectiveComplete(questId, objective.ID)", "false")}
        self.assertFails(check_quest_needs_skip_complete, sources(overrides))

    def test_policy_keeps_completed_objectives(self):
        overrides = {POLICY: (ROOT / POLICY).read_text().replace(
            "if (kind == QuestNeedKind::None || complete || amount <= 0 || objectId == 0)",
            "if (kind == QuestNeedKind::None || amount <= 0 || objectId == 0)")}
        self.assertFails(check_quest_needs_skip_complete, sources(overrides))

    def test_facts_per_nearby_row(self):
        # The gate dropped: every nearby player's row recomputes the bot's party walk and guild lookup.
        lazy = ("        if (!haveFacts)\n"
                "        {\n"
                "            facts = Facts(bot, config);\n"
                "            haveFacts = true;\n"
                "            if (!TortoiseBots::world::ShareMobsApplies(facts))\n"
                "                return MobVerdict::None;\n"
                "        }\n")
        eager = ("        facts = Facts(bot, config);\n"
                 "        if (!TortoiseBots::world::ShareMobsApplies(facts))\n"
                 "            return MobVerdict::None;\n")
        overrides = {HOST: (ROOT / HOST).read_text().replace(lazy, eager)}
        self.assertFails(check_facts_once_per_pass, sources(overrides))

    def test_default_on(self):
        overrides = {
            CONF: (ROOT / CONF).read_text().replace(
                "\nTortoiseBots.World.PoolShareMobs = 0\n", "\nTortoiseBots.World.PoolShareMobs = 1\n"),
            MGR: (ROOT / MGR).read_text().replace(
                'GetBoolDefault("TortoiseBots.World.PoolShareMobs", false)',
                'GetBoolDefault("TortoiseBots.World.PoolShareMobs", true)'),
        }
        self.assertFails(check_conf, sources(overrides))
        self.assertFails(check_bot_mgr, sources(overrides))


if __name__ == "__main__":
    unittest.main()

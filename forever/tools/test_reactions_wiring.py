#!/usr/bin/env python3
"""Wiring pins for issue #125 (shared reaction strategies: flee, racials, avoid aoe, kite, fear/stun/threat reactions).

Run:
    python3 forever/tools/test_reactions_wiring.py
    python3 forever/tools/test_reactions_wiring.py --mutate
"""
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]


def read(rel):
    return (ROOT / rel).read_text(encoding="utf-8", errors="replace")


def func(text, signature):
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


EXPECTED_SOURCES = [
    "ai/playerbot/strategy/generic/FleeStrategy.cpp",
    "ai/playerbot/strategy/generic/KiteStrategy.cpp",
    "ai/playerbot/strategy/generic/RunawayStrategy.cpp",
    "ai/playerbot/strategy/generic/ReactionStrategy.cpp",
    "ai/playerbot/strategy/generic/RacialsStrategy.cpp",
    "ai/playerbot/strategy/generic/MarkRtiStrategy.cpp",
    "ai/playerbot/strategy/generic/FocusStrategy.cpp",
    "ai/playerbot/strategy/actions/SpreadDistanceAction.cpp",
    "ai/playerbot/strategy/values/LeastHpTargetValue.cpp",
    "ai/playerbot/strategy/values/AttackerWithoutAuraTargetValue.cpp",
]

EXPECTED_CONTEXT_KEYS = [
    'creators["flee"] = [](PlayerbotAI* ai) { return new FleeStrategy(ai); }',
    'creators["kite"] = [](PlayerbotAI* ai) { return new KiteStrategy(ai); }',
    'creators["runaway"] = [](PlayerbotAI* ai) { return new RunawayStrategy(ai); }',
    'creators["react"] = [](PlayerbotAI* ai) { return new ReactionStrategy(ai); }',
    'creators["avoid aoe"] = [](PlayerbotAI* ai) { return new AvoidAoeStrategy(ai); }',
    'creators["racials"] = [](PlayerbotAI* ai) { return new RacialsStrategy(ai); }',
    'creators["mark rti"] = [](PlayerbotAI* ai) { return new MarkRtiStrategy(ai); }',
    'creators["focus"] = [](PlayerbotAI* ai) { return new FocusStrategy(ai); }',
    'creators["spread distance"] = [](PlayerbotAI* ai) { return new SpreadDistanceAction(ai); }',
    'creators["feared"] = [](PlayerbotAI* ai) { return new FearedTrigger(ai); }',
    'creators["stunned"] = [](PlayerbotAI* ai) { return new StunnedTrigger(ai); }',
    'creators["fear sleep sap"] = [](PlayerbotAI* ai) { return new FearSleepSapTrigger(ai); }',
    'creators["low tank threat"] = [](PlayerbotAI* ai) { return new LowTankThreatTrigger(ai); }',
    'creators["some threat"] = [](PlayerbotAI* ai) { return new SomeThreatTrigger(ai); }',
    'creators["no threat"] = [](PlayerbotAI* ai) { return new NoThreatTrigger(ai); }',
    'creators["least hp target"] = [](PlayerbotAI* ai) { return new LeastHpTargetValue(ai); }',
    'creators["attacker without aura"] = [](PlayerbotAI* ai) { return new AttackerWithoutAuraTargetValue(ai); }',
    'creators["spread distance"] = [](PlayerbotAI* ai) { return new SpreadDistanceValue(ai); }',
]


def check_all(factory_override=None):
    failures = []

    def check(cond, msg):
        if not cond:
            failures.append(msg)

    slice_list = [l.strip() for l in read("forever/SLICE-W4.txt").splitlines() if l.strip() and not l.startswith("#")]
    for src in EXPECTED_SOURCES:
        check(src in slice_list, "SLICE-W4.txt missing: " + src)
        check((ROOT / src).is_file(), "missing source file: " + src)

    ctx = read("forever/src/slice/SliceAiObjectContext.cpp")
    for key in EXPECTED_CONTEXT_KEYS:
        check(key in ctx, "SliceAiObjectContext.cpp missing: " + key)

    factory = factory_override if factory_override is not None else read("forever/src/slice/SliceAiFactory.cpp")
    combat = func(factory, "void AiFactory::AddDefaultCombatStrategies(")
    non_combat = func(factory, "void AiFactory::AddDefaultNonCombatStrategies(")
    reaction = func(factory, "void AiFactory::AddDefaultReactionStrategies(")

    check('"racials"' in combat, "AddDefaultCombatStrategies must add racials")
    check('"racials"' in non_combat, "AddDefaultNonCombatStrategies must add racials")
    check('"react"' in reaction and '"avoid aoe"' in reaction, "AddDefaultReactionStrategies must add react and avoid aoe")
    check('"flee"' in combat, "AddDefaultCombatStrategies must add flee for ranged/healers")

    for script in ("slice_gen_context.py --check", "slice_registry_check.py --quiet"):
        r = subprocess.run([sys.executable, *(ROOT / "forever/tools" / script.split()[0], *script.split()[1:])],
                           capture_output=True, text=True)
        check(r.returncode == 0, f"{script} failed: {r.stderr or r.stdout}")

    return failures


def main():
    if "--mutate" in sys.argv:
        factory = read("forever/src/slice/SliceAiFactory.cpp")
        mutated = factory.replace('"avoid aoe"', '"avoid aoe MUTATED"')
        failures = check_all(factory_override=mutated)
        if not failures:
            print("FAIL: mutation was not caught")
            sys.exit(1)
        print("OK: mutation caught:", failures[0])
        return

    failures = check_all()
    if failures:
        print("\n".join(failures))
        sys.exit(1)
    print("OK: reactions wiring pins passed")


if __name__ == "__main__":
    main()

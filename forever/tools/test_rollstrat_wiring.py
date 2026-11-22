#!/usr/bin/env python3
"""Wiring pin: every class in SliceAiFactory::AddDefaultNonCombatStrategies carries "loot" and "roll"."""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

CLASSES = (
    "CLASS_PRIEST",
    "CLASS_DRUID",
    "CLASS_PALADIN",
    "CLASS_SHAMAN",
    "CLASS_MAGE",
    "CLASS_ROGUE",
    "CLASS_HUNTER",
    "CLASS_WARLOCK",
    "CLASS_WARRIOR",
)


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def extract_func(text: str, sig: str) -> str:
    idx = text.find(sig)
    if idx < 0:
        raise AssertionError(f"missing function: {sig}")
    brace = text.find("{", idx)
    depth = 0
    for i in range(brace, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[idx : i + 1]
    raise AssertionError(f"unclosed function: {sig}")


def extract_case_blocks(non_combat: str) -> dict[str, str]:
    sw = non_combat.find("switch (player->GetClass())")
    if sw < 0:
        raise AssertionError("missing switch (player->GetClass())")
    body = non_combat[sw:]
    matches = list(re.finditer(r"\bcase\s+(CLASS_[A-Z]+)\s*:", body))
    blocks: dict[str, str] = {}
    for i, m in enumerate(matches):
        start = m.end()
        end = matches[i + 1].start() if i + 1 < len(matches) else len(body)
        blocks[m.group(1)] = body[start:end]
    return blocks


def check_all(factory_override: str | None = None) -> list[str]:
    failures: list[str] = []

    def check(cond: bool, msg: str) -> None:
        if not cond:
            failures.append("FAIL: " + msg)

    factory = factory_override if factory_override is not None else read("forever/src/slice/SliceAiFactory.cpp")
    non_combat = extract_func(factory, "void AiFactory::AddDefaultNonCombatStrategies(")

    # Free bots must also get loot + roll via FreeWorldKit().
    free_kit_policy = read("ai/playerbot/policy/world/FreeKitPolicy.h")
    check("FreeWorldKit()" in non_combat, "AddDefaultNonCombatStrategies must apply FreeWorldKit()")
    check('"loot"' in free_kit_policy and '"roll"' in free_kit_policy, "FreeKitPolicy.h must include loot and roll")

    blocks = extract_case_blocks(non_combat)
    for cls in CLASSES:
        if cls not in blocks:
            failures.append(f"FAIL: missing case {cls} in AddDefaultNonCombatStrategies")
            continue
        calls = re.findall(r"nonCombatEngine->addStrategies\s*\(([^;]+)\);", blocks[cls])
        check(len(calls) > 0, f"{cls} has no addStrategies(...) calls")
        for idx, call in enumerate(calls):
            check('"loot"' in call, f'{cls} addStrategies #{idx + 1} missing "loot"')
            check('"roll"' in call, f'{cls} addStrategies #{idx + 1} missing "roll"')

    ctx = read("forever/src/slice/SliceAiObjectContext.cpp")
    check('creators["loot"]' in ctx, 'SliceAiObjectContext.cpp missing strategy "loot"')
    check('creators["roll"]' in ctx, 'SliceAiObjectContext.cpp missing strategy "roll"')

    return failures


def main() -> None:
    if "--mutate" in sys.argv:
        factory = read("forever/src/slice/SliceAiFactory.cpp")
        # Mutate CLASS_MAGE by dropping "roll" from its non-combat strategies.
        target = 'case CLASS_MAGE:\n            nonCombatEngine->addStrategies(tab == 0 ? "arcane" : (tab == 2 ? "frost" : "fire"), "dps assist", "follow", "cure", "buff", "food", "loot", "roll", "default", "boost", NULL);'
        replacement = target.replace('"roll", ', "")
        assert target in factory, "mutation anchor not found in SliceAiFactory.cpp"
        failures = check_all(factory_override=factory.replace(target, replacement, 1))
        if not failures:
            print("FAIL: mutation was not caught")
            sys.exit(1)
        print("OK: mutation caught:", failures[0])
        return

    failures = check_all()
    if failures:
        print("\n".join(failures))
        sys.exit(1)
    print("OK: all 9 classes carry loot and roll in AddDefaultNonCombatStrategies")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Wiring pin for CastInnervateAction claim placement (#14).

Checks that CastInnervateAction claims the target in Execute(), not in GetTarget()
(which is called during isUseful() / isPossible() action evaluation).

Run with --mutate to verify each check fails when its key invariant is broken.
"""
import argparse
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]


def extract_innervate_class(text):
    start = text.index("class CastInnervateAction")
    end = text.index("class CastTranquilityAction", start)
    return text[start:end]


def check_all(overrides=None):
    overrides = overrides or {}
    rel = "ai/playerbot/strategy/druid/DruidActions.h"
    text = overrides[rel] if rel in overrides else (ROOT / rel).read_text(encoding="utf-8")

    block = extract_innervate_class(text)
    get_target_start = block.index("Unit* GetTarget() override")
    next_method_start = block.index("bool ", get_target_start)
    get_target_body = block[get_target_start:next_method_start]

    assert "BuffClaimRegistry::Claim" not in get_target_body, (
        "CastInnervateAction::GetTarget() must not claim target during evaluation"
    )
    assert "bool Execute(Event& event) override" in block, (
        "CastInnervateAction missing Execute(Event& event) override"
    )
    exec_start = block.index("bool Execute(Event& event) override")
    exec_end = block.index("bool IsTargetValid(Unit* target) override", exec_start)
    exec_body = block[exec_start:exec_end]
    assert "BuffClaimRegistry::Claim" in exec_body, (
        "CastInnervateAction::Execute() must claim target on cast"
    )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mutate", action="store_true")
    args = ap.parse_args()

    check_all()
    if args.mutate:
        rel = "ai/playerbot/strategy/druid/DruidActions.h"
        orig = (ROOT / rel).read_text(encoding="utf-8")
        mutations = [
            ("BuffClaimRegistry::Claim", "BROKEN_CLAIM"),
            ("bool Execute(Event& event) override", "bool BrokenExecute(Event& event) override"),
        ]
        for needle, repl in mutations:
            mutated = orig.replace(needle, repl, 1)
            # Ensure we mutated inside CastInnervateAction
            if extract_innervate_class(mutated) == extract_innervate_class(orig):
                idx = orig.index("class CastInnervateAction")
                mutated = orig[:idx] + orig[idx:].replace(needle, repl, 1)
            try:
                check_all({rel: mutated})
            except AssertionError:
                continue
            print(f"MUTATION SURVIVED: {rel} ({needle})", file=sys.stderr)
            return 1
        print("OK: test_dispel_mana_wiring (2/2 mutations caught)")
        return 0

    print("OK: test_dispel_mana_wiring")
    return 0


if __name__ == "__main__":
    sys.exit(main())

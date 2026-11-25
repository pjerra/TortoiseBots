#!/usr/bin/env python3
"""Wiring pin for polish features (#50, #51, #52).

Checks that:
1. ReleaseSpiritAction.h wires ShouldHoldDeathRelease / ResetDeathReleasePause for free bots only.
2. MovementActions.cpp wires ShouldOffsetWalkTarget / ComputeWalkTargetOffset in MoveTo2.
3. GrindTargetValue.cpp wires ShouldHuntRare and prioritizes rares over normal grind targets.
4. All four config keys exist in PlayerbotAIConfig.cpp and forever/conf/aiplayerbot.conf.dist.

Run with --mutate to verify each check fails when its key symbol is broken.
"""
import argparse
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]


def check_all(overrides=None):
    overrides = overrides or {}

    def read(rel):
        if rel in overrides:
            return overrides[rel]
        return (ROOT / rel).read_text(encoding="utf-8")

    rel_spirit = read("ai/playerbot/strategy/actions/ReleaseSpiritAction.h")
    assert "ShouldHoldDeathRelease" in rel_spirit, "ReleaseSpiritAction.h missing ShouldHoldDeathRelease"
    assert "ResetDeathReleasePause" in rel_spirit, "ReleaseSpiritAction.h missing ResetDeathReleasePause"
    assert "deathReleasePauseMinSec" in rel_spirit and "deathReleasePauseMaxSec" in rel_spirit

    move = read("ai/playerbot/strategy/actions/MovementActions.cpp")
    assert "ShouldOffsetWalkTarget" in move, "MovementActions.cpp missing ShouldOffsetWalkTarget"
    assert "ComputeWalkTargetOffset" in move, "MovementActions.cpp missing ComputeWalkTargetOffset"
    assert "walkTargetOffset" in move
    assert "bot->GetMap()->Instanceable()" not in move, "MovementActions.cpp still calls bot->GetMap()->Instanceable() without FindMap()"
    assert "bot->FindMap()" in move, "MovementActions.cpp missing bot->FindMap() guard"

    grind = read("ai/playerbot/strategy/values/GrindTargetValue.cpp")
    assert "ShouldHuntRare" in grind, "GrindTargetValue.cpp missing ShouldHuntRare"
    assert "resultIsRare" in grind, "GrindTargetValue.cpp missing resultIsRare priority"
    assert "huntRares" in grind

    cfg_h = read("ai/playerbot/PlayerbotAIConfig.h")
    cfg = read("ai/playerbot/PlayerbotAIConfig.cpp")
    conf_in = read("ai/playerbot/aiplayerbot.conf.dist.in")
    conf = read("forever/conf/aiplayerbot.conf.dist")
    doc = read("docs/server/settings/world-population.md")
    for key in (
        "AiPlayerbot.DeathReleasePauseMinSec",
        "AiPlayerbot.DeathReleasePauseMaxSec",
        "AiPlayerbot.WalkTargetOffset",
        "AiPlayerbot.HuntRares",
    ):
        assert key in cfg, f"PlayerbotAIConfig.cpp missing {key}"
        assert f"{key} =" in conf, f"aiplayerbot.conf.dist missing {key} ="

    assert "bool huntRares = true;" in cfg_h, "PlayerbotAIConfig.h huntRares default must be true"
    assert 'GetBoolDefault("AiPlayerbot.HuntRares", true)' in cfg, "PlayerbotAIConfig.cpp HuntRares default must be true"
    assert "AiPlayerbot.HuntRares = 1" in conf_in, "aiplayerbot.conf.dist.in HuntRares default must be 1"
    assert "AiPlayerbot.HuntRares = 1" in conf, "forever/conf/aiplayerbot.conf.dist HuntRares default must be 1"
    hunt_rares = doc.split("### `AiPlayerbot.HuntRares`", 1)
    assert len(hunt_rares) == 2, "world-population.md must document HuntRares"
    hunt_rares = hunt_rares[1].split("\n### ", 1)[0]
    assert "**Default:** `1`" in hunt_rares, "world-population.md HuntRares default must be 1"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mutate", action="store_true")
    args = ap.parse_args()

    check_all()
    if args.mutate:
        mutations = [
            ("ai/playerbot/strategy/actions/ReleaseSpiritAction.h", "ShouldHoldDeathRelease", "BROKEN"),
            ("ai/playerbot/strategy/actions/MovementActions.cpp", "ShouldOffsetWalkTarget", "BROKEN"),
            ("ai/playerbot/strategy/values/GrindTargetValue.cpp", "ShouldHuntRare", "BROKEN"),
            ("forever/conf/aiplayerbot.conf.dist", "AiPlayerbot.HuntRares =", "BROKEN ="),
        ]
        for rel, needle, repl in mutations:
            text = (ROOT / rel).read_text(encoding="utf-8").replace(needle, repl, 1)
            try:
                check_all({rel: text})
            except AssertionError:
                continue
            print(f"MUTATION SURVIVED: {rel} ({needle})", file=sys.stderr)
            return 1
        print("OK: test_polish_wiring (4/4 mutations caught)")
        return 0

    print("OK: test_polish_wiring")
    return 0


if __name__ == "__main__":
    sys.exit(main())

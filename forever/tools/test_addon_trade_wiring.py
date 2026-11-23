#!/usr/bin/env python3
"""Wiring pins for the `trade` addon verb (TbAddonTrade.{h,cpp}, TbAddonTradePure.h, TbScripts.cpp)."""

from __future__ import annotations

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
HOST = ROOT / "forever/src/host"
PURE = HOST / "TbAddonTradePure.h"
HDR = HOST / "TbAddonTrade.h"
CPP = HOST / "TbAddonTrade.cpp"
SCRIPTS = ROOT / "forever/src/TbScripts.cpp"


def check(mutate: bool = False) -> None:
    pure = PURE.read_text()
    hdr = HDR.read_text()
    cpp = CPP.read_text()
    scripts = SCRIPTS.read_text()

    if mutate:
        cpp = cpp.replace('addon::RegisterVerb("trade"', 'addon::RegisterVerb("disabled"')

    for token in ("ParseTradeArgs", "FindFreeTradeSlot", "DecideTradeTick"):
        assert token in pure, f"TbAddonTradePure.h missing {token}"

    for token in ("RegisterAddonVerbsTrade", "HandleItemTrade", "PlaceItemInOpenTrade"):
        assert token in hdr, f"TbAddonTrade.h missing {token}"

    for token in (
        'addon::RegisterVerb("trade"',
        "HandleInitiateTradeOpcode",
        "HandleBeginTradeOpcode",
        "HandleSetTradeItemOpcode",
        "HandleAcceptTradeOpcode",
        "HandleCancelTradeOpcode",
    ):
        assert token in cpp, f"TbAddonTrade.cpp missing {token}"

    assert '#include "host/TbAddonTrade.h"' in scripts, "TbScripts.cpp missing TbAddonTrade.h include"
    assert "TortoiseBots::trade::RegisterAddonVerbsTrade();" in scripts, "TbScripts.cpp missing RegisterAddonVerbsTrade()"
    print("OK test_addon_trade_wiring")


if __name__ == "__main__":
    mutate_mode = "--mutate" in sys.argv[1:]
    if mutate_mode:
        try:
            check(mutate=True)
        except AssertionError:
            print("OK mutation failed as expected")
            sys.exit(0)
        print("FAIL mutation did not fail", file=sys.stderr)
        sys.exit(1)
    check(mutate=False)

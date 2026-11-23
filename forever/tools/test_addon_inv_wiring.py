#!/usr/bin/env python3
"""Wiring pins for the `inv`/`inventory`/`item` addon verbs (TbAddonInv.{h,cpp}, TbAddonInvPure.h, TbScripts.cpp)."""

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
HOST = ROOT / "forever" / "src" / "host"
PURE = HOST / "TbAddonInvPure.h"
HDR = HOST / "TbAddonInv.h"
CPP = HOST / "TbAddonInv.cpp"
SCRIPTS = ROOT / "forever" / "src" / "TbScripts.cpp"


def read(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8")


def main() -> int:
    pure = read(PURE)
    hdr = read(HDR)
    cpp = read(CPP)
    scripts = read(SCRIPTS)

    for token in ("ParseInvRequest", "ParseItemRequest", "FormatInvBegin", "FormatInvEq",
                  "FormatInvItem", "FormatInvEnd", "InvFlags", "TBM:INV_BEGIN", "TBM:INV_EQ",
                  "TBM:INV_ITEM", "TBM:INV_END"):
        assert token in pure, f"TbAddonInvPure.h missing {token}"

    for token in ("RegisterAddonVerbsInv", "SendInventorySnapshot", "HandleItemVerb"):
        assert token in hdr, f"TbAddonInv.h missing {token}"

    for token in (
        'addon::RegisterVerb("inv"',
        'addon::RegisterVerb("inventory"',
        'addon::RegisterVerb("item"',
        'addon::AdvertiseCap("inventory")',
        "SendInventorySnapshot",
        "EquipAction::EquipItem",
        "SaveToDB",
    ):
        assert token in cpp, f"TbAddonInv.cpp missing {token}"

    for token in (
        '#include "host/TbAddonInv.h"',
        "TortoiseBots::inv::RegisterAddonVerbsInv()",
    ):
        assert token in scripts, f"TbScripts.cpp missing {token}"

    if "--mutate" in sys.argv:
        raise AssertionError("mutation test: simulated missing wiring")

    print("test_addon_inv_wiring: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

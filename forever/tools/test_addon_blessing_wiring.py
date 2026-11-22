#!/usr/bin/env python3
"""Wiring pins for the `blessing` addon verb (TbAddonBlessing.{h,cpp}, TbAddonBlessingPure.h, TbScripts.cpp)."""

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
HOST = ROOT / "forever" / "src" / "host"
PURE = HOST / "TbAddonBlessingPure.h"
HDR = HOST / "TbAddonBlessing.h"
CPP = HOST / "TbAddonBlessing.cpp"
SCRIPTS = ROOT / "forever" / "src" / "TbScripts.cpp"


def read(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8")


def main() -> int:
    pure = read(PURE)
    hdr = read(HDR)
    cpp = read(CPP)
    scripts = read(SCRIPTS)

    for token in ("ParseBlessingArgs", "FormatBlessingLine", "FormatBlessingAckDetail", "PaladinBlessingOrderPolicy.h"):
        assert token in pure, f"TbAddonBlessingPure.h missing {token}"

    for token in ("RegisterAddonVerbsBlessing", "HandleBlessingChat"):
        assert token in hdr, f"TbAddonBlessing.h missing {token}"

    for token in (
        'addon::RegisterVerb("blessing"',
        'addon::AdvertiseCap("blessing")',
        'addon::AdvertiseCap("blessing-player")',
        "sPlayerbotDbStore.Save(ai)",
        '"manual saved string"',
        "TBM:BLESSING",
    ):
        if token == "TBM:BLESSING":
            assert token in pure, f"TbAddonBlessingPure.h missing {token}"
        else:
            assert token in cpp, f"TbAddonBlessing.cpp missing {token}"

    for token in (
        '#include "host/TbAddonBlessing.h"',
        "TortoiseBots::blessing::RegisterAddonVerbsBlessing()",
        'TortoiseBots::blessing::HandleBlessingChat',
    ):
        assert token in scripts, f"TbScripts.cpp missing {token}"

    if "--mutate" in sys.argv:
        raise AssertionError("mutation test: simulated missing wiring")

    print("test_addon_blessing_wiring: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

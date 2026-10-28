"""Every client LFT dungeon has a range in the policy header.

Fixture: the 41 (code, min, max) rows of the Turtle client LFT list,
source refaim/Turtle-WoW-UI-Source 03bbc6b3f8
Interface/FrameXML/LFT/LFT_Data.lua (client 1.18.1 (7272)), sha256
9a28a5b3caefaf4d3e7d9646bf37be626c85e961fdcfab9da76a27a83310549d.
NO_RANGE lists client codes deliberately without a policy range (empty:
the queue offers raids no row for). Update both with a new client code.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
HEADER = ROOT / "runtime/policy/lfg/LftDungeonRangePolicy.h"

CLIENT_LFT_DATA = [
    ("rfc", 13, 19), ("fmh", 13, 20), ("wc", 16, 25), ("dm", 16, 24),
    ("sfk", 20, 28), ("stocks", 23, 32), ("bfd", 22, 31),
    ("windhorn", 25, 33), ("smgy", 30, 37), ("smlib", 32, 40),
    ("gnomer", 28, 37), ("rfk", 29, 36), ("tcg", 33, 39),
    ("smarmory", 34, 42), ("smcath", 35, 45), ("rfd", 35, 44),
    ("ulda", 41, 50), ("gilneas", 43, 52), ("zf", 42, 51),
    ("maraorange", 43, 51), ("marapurple", 45, 52),
    ("maraprincess", 46, 54), ("st", 49, 58), ("hfq", 51, 60),
    ("brd", 54, 60), ("brdarena", 50, 60), ("brdemp", 54, 60),
    ("lbrs", 55, 60), ("dme", 55, 60), ("dmn", 57, 60),
    ("dmt", 57, 60), ("dmw", 57, 60), ("scholo", 58, 60),
    ("stratud", 58, 60), ("stratlive", 58, 60), ("kc", 60, 60),
    ("cotbm", 60, 60), ("swv", 60, 60), ("swc", 32, 40),
    ("swd", 35, 44), ("dmr", 26, 35),
]

NO_RANGE = set()


def header_ranges():
    text = HEADER.read_text()
    return re.findall(r'\{"(\w+)", \{(\d+), (\d+)\}\}', text)


class LftClientCodesTest(unittest.TestCase):
    def test_every_client_code_has_a_range(self):
        found = {code: (int(lo), int(hi)) for code, lo, hi in header_ranges()}
        fixture = {code: (lo, hi) for code, lo, hi in CLIENT_LFT_DATA}
        for code in fixture:
            self.assertTrue(code in NO_RANGE or code in found,
                            "client code %s neither ranged nor waived" % code)
            self.assertFalse(code in NO_RANGE and code in found,
                             "client code %s both ranged and waived" % code)
        for code, (lo, hi) in fixture.items():
            if code in NO_RANGE:
                continue
            self.assertIn(code, found, code)
            self.assertEqual(found[code], (lo, hi), code)

    def test_every_header_key_matches_a_client_code(self):
        by_range = {}
        for code, lo, hi in CLIENT_LFT_DATA:
            by_range.setdefault((lo, hi), set()).add(code)
        for code, lo, hi in header_ranges():
            self.assertIn((int(lo), int(hi)), by_range,
                          "header key %s carries a range no client code has" % code)

    def test_no_duplicate_header_key(self):
        seen = set()
        for code, _lo, _hi in header_ranges():
            occurrences = len(re.findall(r'\{"%s", \{' % re.escape(code),
                                         HEADER.read_text()))
            self.assertEqual(occurrences, 1, code)
            seen.add(code)
        self.assertEqual(len(seen), len(header_ranges()))


if __name__ == "__main__":
    unittest.main()

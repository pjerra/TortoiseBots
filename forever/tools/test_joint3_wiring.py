#!/usr/bin/env python3
"""Wiring pin for the third joint live test (JOINT-LIVE-3) fixes in the module.

  * `.tb equip ... replace` destroys a replaced starter piece (poor or common, lower item level) instead of pushing it
    into the bags, reports it (`destroyed=`/`destroyed_item=`) and never touches a real player's character
    (host/cmd/TbReplacePure.h; forever/tools/test_equip_replace.cpp proves the rule);
  * `.tb run status` names the total member deaths (`member_deaths=`, the same total as the existing `deaths=`), so the
    done note's narrower wipe, near-wipe and lead-death counters cannot be read as the party's death total;
  * the run event ring holds 1024 events (a 63-minute run overflowed 200) and `events since=` reports `oldest=`, the oldest
    seq the ring still holds, so a reader can tell truncation from a gap.

These assertions fail on the commit before the fixes.
"""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SRC = ROOT / "forever" / "src"
DOCS = ROOT / "forever" / "docs"


def text(rel):
    return (SRC / rel).read_text()


class Joint3(unittest.TestCase):
    def test_a_starter_piece_is_destroyed_on_replace(self):
        kit = text("host/TbTestKit.cpp")
        self.assertIn("replace::IsStarterUpgrade(", kit)
        self.assertIn("replace::MayDestroyReplacedItem(", kit)
        self.assertIn("bot->DestroyItem(piece->GetBagSlot(), piece->GetSlot(), true);", kit)
        self.assertLess(kit.index("bot->SwapItem(fresh->GetPos(), swapDest);"),
                        kit.index("dropIfStarter(displaced);"))

    def test_the_equip_record_reports_destruction(self):
        kit = text("host/TbTestKit.cpp")
        self.assertIn('rec.N("destroyed", int64(destroyed->size())).L("destroyed_item", ids);', kit)
        self.assertIn("SEC_GAMEMASTER", kit)

    def test_the_replace_rule_is_pure(self):
        pure = (ROOT / "forever" / "src" / "host" / "cmd" / "TbReplacePure.h").read_text()
        self.assertIn("quality <= kCommonQuality", pure)
        self.assertIn("oldItemLevel < newItemLevel", pure)
        self.assertIn("targetIsHostBot || (senderIsGm && senderIsTarget)", pure)

    def test_status_names_member_deaths(self):
        run = text("host/run/TbRun.cpp")
        self.assertIn('.N("member_deaths", rec.deaths)', run)
        self.assertLess(run.index('.N("deaths", rec.deaths)'), run.index('.N("member_deaths", rec.deaths)'))

    def test_the_ring_is_1024_and_events_names_the_oldest(self):
        header = text("host/run/TbRun.h")
        self.assertIn("constexpr size_t kEventRing = 1024;", header)
        commands = text("host/run/TbRunCommands.cpp")
        self.assertIn('.N("oldest", rec->events.front().seq)', commands)
        self.assertLess(commands.index('end.N("oldest"'), commands.index('end.N("last"'))

    def test_the_docs_explain_every_counter_and_the_ring(self):
        doc = (DOCS / "TB-COMMANDS.md").read_text()
        self.assertIn("`deaths` and `member_deaths` are the same total", doc)
        self.assertIn("event ring of 1024", doc)
        self.assertIn("`oldest=`", doc)
        self.assertIn("destroyed_item", doc)


if __name__ == "__main__":
    unittest.main()

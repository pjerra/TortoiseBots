"""A hired mercenary accepts the LFT offer made to its hirer's party (#1284).

`LftBotFillService::AcceptPartyBotOffers` must route the random-pool skip
through `LftPartyBotMayAccept`: a random-pool bot that is not the human's is
still skipped, but a hire (a pool character that is its hirer's companion)
is accepted like an alt. The anchor (master or group leader, a real player
in an offer) must, for a hire, be its own hirer (HireLifecycle::GetMaster) in
the same offer (same queue block, see LftQueuedTogether), so a hire moved
into a stranger's group does not accept the stranger's offer. Fill-owned bots
still go through `AcceptPendingOffers`.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
CPP = (ROOT / "runtime/LftBotFillService.cpp").read_text()


def strip_comments(text):
    return "\n".join(line.split("//", 1)[0].rstrip() for line in text.splitlines())


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class LftPartyAcceptWiringTest(unittest.TestCase):
    def test_party_acceptor_delegates_to_policy(self):
        self.assertIn('#include "runtime/policy/hire/LftPartyAcceptPolicy.h"', CPP)
        self.assertIn('#include "HireLifecycle.h"', CPP)
        fn = strip_comments(body(CPP, "void LftBotFillService::AcceptPartyBotOffers()", "\n}\n"))
        self.assertIn("LftPartyBotMayAccept(", fn)
        self.assertIn("HireLifecycle::Instance().IsHired(guid)", fn)
        self.assertNotRegex(
            fn,
            r"if\s*\(BotManager::Instance\(\)\.IsRandomBot\(guid\)\)\s*\n\s*continue;",
        )

    def test_party_acceptor_keeps_fill_and_same_offer_guards(self):
        fn = strip_comments(body(CPP, "void LftBotFillService::AcceptPartyBotOffers()", "\n}\n"))
        self.assertLess(
            fn.index("m_pending.find(guidLow) != m_pending.end()"),
            fn.index("LftPartyBotMayAccept("),
        )
        # Both anchors (live master, group leader) are candidates, each a live
        # network player in an offer.
        self.assertIn("ai->GetLiveMaster()", fn)
        self.assertIn("sObjectMgr.GetPlayer(group->GetLeaderGuid())", fn)
        self.assertIn("IsInOffer(candidate->GetObjectGuid())", fn)
        self.assertIn("HasNetworkTransport()", fn)

    def test_hire_needs_its_hirer_in_the_same_offer(self):
        fn = strip_comments(body(CPP, "void LftBotFillService::AcceptPartyBotOffers()", "\n}\n"))
        # The anchor must be the hire's own master from the hire ledger.
        self.assertIn("HireLifecycle::Instance().GetMaster(guid)", fn)
        self.assertRegex(fn, r"anchorIsHireMaster\s*=\s*!hireMaster\.IsEmpty\(\)\s*&&\s*"
                             r"candidate->GetObjectGuid\(\)\s*==\s*hireMaster")
        # Same offer: the queue-block check against the core's queue snapshot.
        self.assertIn("sameOffer = InSameOffer(bot, candidate, queued)", fn)
        self.assertIn("sLFTMgr.GetQueuedPlayers()", fn)
        # The decision goes through the policy, before the anchor is taken.
        call = re.search(r"if \(LftPartyOfferMayAccept\(randomPool, hired, anchorIsHireMaster, sameOffer\)\)\s*"
                         r"\{\s*anchor = candidate;", fn)
        self.assertIsNotNone(call)
        self.assertLess(fn.index("sameOffer = InSameOffer("), call.start())
        # No other assignment of the anchor bypasses the policy.
        self.assertEqual(len(re.findall(r"\banchor\s*=(?!=)(?!\s*nullptr)", fn)), 1)
        self.assertLess(fn.index("if (!anchor)"), fn.index("sLFTMgr.AcceptOffer(guid)"))

    def test_same_offer_helper_uses_the_queue_block_traces(self):
        helper = strip_comments(body(CPP, "bool InSameOffer(", "\n}\n"))
        self.assertIn("sLFTMgr.IsInOffer(bot->GetObjectGuid())", helper)
        self.assertIn("sLFTMgr.IsInOffer(anchor->GetObjectGuid())", helper)
        self.assertIn("group && group == anchor->GetGroup()", helper)
        self.assertIn("LftQueuedTogether(", helper)
        self.assertIn("a->joinTime", helper)
        self.assertIn("b->instances", helper)


if __name__ == "__main__":
    unittest.main()

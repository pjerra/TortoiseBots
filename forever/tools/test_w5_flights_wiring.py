#!/usr/bin/env python3
"""Wiring test for W5 (flights, boats, zeppelins, tram).
Run:
  python3 -m unittest forever/tools/test_w5_flights_wiring.py
  python3 forever/tools/test_w5_flights_wiring.py --mutate   # must FAIL (exit non-zero)
"""
import os
import sys
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MUTATE = False


def read(rel):
    with open(os.path.join(ROOT, rel), "r", encoding="utf-8") as f:
        return f.read()


class TestW5FlightsWiring(unittest.TestCase):
    def test_no_teleport_in_w5_files(self):
        for rel in (
            "ai/playerbot/policy/travel/TaxiChoicePolicy.h",
            "ai/playerbot/policy/travel/TransportWaitPolicy.h",
            "forever/src/host/TbHostTaxi.cpp",
            "forever/src/host/TbHostTransport.cpp",
        ):
            src = read(rel)
            self.assertNotIn("TeleportTo", src, f"{rel} must not use TeleportTo")
            self.assertNotIn("CONFIG_INSTANT_TAXI", src, f"{rel} must not use CONFIG_INSTANT_TAXI")

    def test_taxi_host_uses_core_handlers_and_money_check(self):
        policy = read("ai/playerbot/policy/travel/TaxiChoicePolicy.h")
        if MUTATE:
            policy = policy.replace("if (fareCopper > botMoneyCopper)", "if (false)")
        self.assertIn("if (fareCopper > botMoneyCopper)", policy)

        host = read("forever/src/host/TbHostTaxi.cpp")
        self.assertIn("HandleTaxiQueryAvailableNodesOpcode", host)
        self.assertIn("HandleActivateTaxiOpcode", host)
        self.assertIn("TaxiPathGraph::GetCompleteNodeRoute", host)
        self.assertIn("UNIT_NPC_FLAG_2_NONE", host)
        self.assertIn("DecideTaxiChoice", host)

    def test_transport_host_uses_findmap_and_passenger_api(self):
        host = read("forever/src/host/TbHostTransport.cpp")
        self.assertIn("FindMap()", host)
        self.assertIn("GetTransport", host)
        self.assertIn("AddPassenger", host)
        self.assertIn("RemovePassenger", host)
        self.assertIn("DecideTransportWait", host)

    def test_ride_fixes_215(self):
        """#215: deck check with the model's collision on, a deck boarding point (not the origin), lost-transport fail,
        and the ride holding the engine tick and the catch-up teleport."""
        host = read("forever/src/host/TbHostTransport.cpp")
        if MUTATE:
            host = host.replace("go->m_model->EnableCollision(true);", "")
        self.assertIn("go->m_model->EnableCollision(true);", host)
        self.assertIn("go->m_model->EnableCollision(wasEnabled);", host)
        self.assertIn("FindBoardingPoint", host)
        self.assertIn("ChooseDeckApproach", host)
        self.assertIn("HasLostTransport", host)
        self.assertNotIn("MovePoint(0, found.obj->GetPositionX()", host, "the approach must not walk to the transport's origin")
        brain = read("forever/src/host/TbEngineBrain.cpp")
        self.assertIn("tbhost::ride::HoldsMovement(brain->bot)", brain)
        mgr = read("forever/src/TbBotMgr.cpp")
        self.assertIn('tbhost::ride::HoldsMovement(me)', mgr)

    def test_orders_and_state_wire_w5(self):
        orders = read("forever/src/host/TbOrders.cpp")
        for verb in ('"taxi-learn"', '"fly"', '"ride"'):
            self.assertIn(verb, orders, f"missing {verb} in TbOrders.cpp")

        state = read("forever/src/host/TbPartyState.cpp")
        for key in ('"taxi"', '"onTaxi"', '"transport"'):
            self.assertIn(key, state, f"missing {key} in TbPartyState.cpp")

    def test_slice_w5_exists(self):
        s5 = read("forever/SLICE-W5.txt")
        self.assertIn("ai/playerbot/PlayerbotAI.cpp", s5)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        MUTATE = True
        sys.argv.remove("--mutate")
    unittest.main()

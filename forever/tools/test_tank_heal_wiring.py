#!/usr/bin/env python3
"""Wiring pin for tank-heal fixes in forever/src/host/TbOrders.cpp."""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
orders = (ROOT / "forever" / "src" / "host" / "TbOrders.cpp").read_text(encoding="utf-8")

# 1. ApplyKit must preserve "free" movement when active before ResetStrategies.
m_apply = re.search(r"void ApplyKit\([^)]*\)\s*\{(?P<body>.*?)\n\}", orders, re.S)
assert m_apply, "ApplyKit not found in TbOrders.cpp"
apply_body = m_apply.group("body")
assert 'HasStrategy("free", BotState::BOT_STATE_NON_COMBAT)' in apply_body, (
    "ApplyKit must check HasStrategy(\"free\", BotState::BOT_STATE_NON_COMBAT)"
)
assert '"free"' in apply_body and "SetMovementStrategy" in apply_body, (
    "ApplyKit must preserve \"free\" via SetMovementStrategy"
)

# 2. BeforeEngineTick must throttle AiFactory::GetPlayerRoles(bot) per bot.
m_tick = re.search(r"void BeforeEngineTick\([^)]*\)\s*\{(?P<body>.*?\n)\}", orders, re.S)
assert m_tick, "BeforeEngineTick not found in TbOrders.cpp"
tick_body = m_tick.group("body")
assert "lastRoleCheck" in tick_body and "AiFactory::GetPlayerRoles(bot)" in tick_body, (
    "BeforeEngineTick must throttle AiFactory::GetPlayerRoles(bot) with lastRoleCheck"
)

print("PASS: test_tank_heal_wiring")

"""Wiring pins for the CastCancel diagnostic row (AiPlayerbot.RaidBossDiagnostics).

Healer and caster casts start and never land in Hakkar fights, and no data set could say why. The pure progress rule
lives in ai/playerbot/CastCancelPolicy.h (tested in tools/test_cast_cancel_policy.cpp); the writer is
ai/playerbot/CastCancelLog.cpp. These pins hold the gate and the cancel sites: with the diagnostics key off nothing is
written, and each module-side cancel logs before it interrupts.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
LOG = (ROOT / "ai/playerbot/CastCancelLog.cpp").read_text()
HEADER = (ROOT / "ai/playerbot/CastCancelLog.h").read_text()
ACTIONS = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.cpp").read_text()
SPELLS = (ROOT / "ai/playerbot/PlayerbotAISpells.cpp").read_text()
GENERIC_SPELL = (ROOT / "ai/playerbot/strategy/actions/GenericSpellActions.cpp").read_text()
CMAKE = (ROOT / "TortoiseBots.cmake").read_text()


def body(text, signature):
    start = text.index(signature)
    end = text.index("\n}\n", start)
    return text[start:end]


class CastCancelWiring(unittest.TestCase):
    def test_the_row_is_gated_by_the_diagnostics_key(self):
        fn = body(LOG, "void ai::LogCastCancel(PlayerbotAI* ai, const char* site, float movedYd)")
        # Key off: one branch, no slot read and no row. The bot_events.csv allowlist is the second gate.
        self.assertIn("sPlayerbotAIConfig.raidBossDiagnostics", fn)
        self.assertIn('hasLog("bot_events.csv")', fn)
        self.assertLess(fn.index("raidBossDiagnostics"), fn.index("GetBot()"))
        self.assertLess(fn.index('hasLog("bot_events.csv")'), fn.index("logEvent("))
        self.assertIn('logEvent(ai, "CastCancel", info1, "")', fn)
        self.assertEqual(fn.count("logEvent("), 1)

    def test_the_row_carries_the_briefs_fields(self):
        fn = body(LOG, "void ai::LogCastCancel(PlayerbotAI* ai, const char* site, float movedYd)")
        for field in ("site=%s", "spell=%u", "cast_pct=%d", "target=%s", "moved_yd=%.1f"):
            self.assertIn(field, fn)
        # The progress rule is the tested policy, not an inline division.
        self.assertIn("CastCancelPct(totalMs, cast.spell->GetCastedTime())", fn)
        self.assertIn('#include "CastCancelPolicy.h"', LOG)
        # Observation only: the writer never interrupts, moves or casts.
        for forbidden in ("Interrupt", "CastStop", "MoveTo(", "CastSpell"):
            self.assertNotIn(forbidden, fn)

    def test_the_writer_is_built(self):
        self.assertIn('"${TORTOISEBOTS_ROOT}/ai/playerbot/CastCancelLog.cpp"', CMAKE)
        self.assertIn("void LogCastCancel(PlayerbotAI* ai, const char* site, float movedYd = 0.0f);", HEADER)

    def test_the_ai_interrupt_choke_point_logs_first(self):
        fn = body(SPELLS, "void PlayerbotAI::InterruptSpell(bool withMeleeAndAuto)")
        self.assertIn('LogCastCancel(this, "ai")', fn)
        self.assertLess(fn.index('LogCastCancel(this, "ai")'), fn.index("bot->InterruptSpell("))

    def test_the_spread_slot_and_the_cast_cut_log(self):
        fn = body(ACTIONS, "bool BossTacticsSpreadSlotAction::Execute(Event& event)")
        for site in ("spread_cut", "spread_slot"):
            self.assertIn('LogCastCancel(ai, "%s"' % site, fn)
        self.assertLess(fn.index('"spread_cut"'), fn.index("bot->CastStop()"))
        self.assertLess(fn.index('"spread_slot"'), fn.index("MoveTo(bot->GetMapId(), task.x, task.y, task.z"))

    def test_the_seek_walkout_interrupts_log(self):
        for site in ("seek_shot", "seek_walk_back", "seek_walk_out"):
            self.assertIn('LogCastCancel(ai, "%s"' % site, ACTIONS)
        shot = ACTIONS.index('"seek_shot"')
        self.assertLess(shot, ACTIONS.index("bot->InterruptNonMeleeSpells(false);", shot))
        back = ACTIONS.index('"seek_walk_back"')
        self.assertLess(back, ACTIONS.index("bot->InterruptSpell(CURRENT_AUTOREPEAT_SPELL);", back))
        walk = ACTIONS.index('"seek_walk_out"')
        self.assertLess(walk, ACTIONS.index("bot->InterruptNonMeleeSpells(true);", walk))

    def test_the_wasteful_heal_cancel_logs(self):
        fn = body(GENERIC_SPELL, "bool InterruptCurrentSpellAction::Execute(Event& event)")
        self.assertIn('LogCastCancel(ai, "heal_full")', fn)
        self.assertLess(fn.index('"heal_full"'), fn.index("bot->InterruptSpell("))

    def test_every_module_side_cancel_is_instrumented(self):
        # Mechanical audit of the module sources: a direct `bot->CastStop/InterruptSpell/InterruptNonMeleeSpells`
        # loses the bot's own cast, so it must log on its own line or the line just above it. The AI's member
        # `InterruptSpell()` callers are covered by the choke point pinned above and are not these calls. A new
        # uninstrumented direct site fails here instead of shipping an invisible cancel.
        cancel = re.compile(r"\bbot\s*->\s*(?:CastStop|InterruptSpell|InterruptNonMeleeSpells)\s*\(")
        missing = []
        sites = 0
        for path in sorted((ROOT / "ai").rglob("*")):
            if path.suffix not in (".cpp", ".h"):
                continue
            lines = path.read_text().splitlines()
            for number, line in enumerate(lines):
                if not cancel.search(line):
                    continue
                sites += 1
                previous = lines[number - 1] if number else ""
                if "LogCastCancel(" in line or "LogCastCancel(" in previous:
                    continue
                missing.append(f"{path.relative_to(ROOT)}:{number + 1}: {line.strip()}")
        self.assertEqual(missing, [], "uninstrumented module-side cast cancels:\n" + "\n".join(missing))
        # The count only guards against the scan silently matching nothing.
        self.assertGreaterEqual(sites, 25)

    def test_the_encounter_and_movement_cancels_name_their_site(self):
        # The movement-driven cancels the review named (Heigan's dance, Onyxia's breath, the follow start and the
        # seek's at-spot stop) log the distance the pending walk takes the bot, so the row shows why the cast died.
        HEIGAN = (ROOT / "ai/playerbot/strategy/actions/HeiganDungeonActions.cpp").read_text()
        ONYXIA = (ROOT / "ai/playerbot/strategy/actions/OnyxiasLairDungeonActions.cpp").read_text()
        MOVEMENT = (ROOT / "ai/playerbot/strategy/actions/MovementActions.cpp").read_text()
        self.assertIn('ai::LogCastCancel(ai, "heigan_dance", spotYd);\n        bot->CastStop();', HEIGAN)
        self.assertIn('ai::LogCastCancel(ai, "onyxia_breath", bot->GetDistance2d(best->x, best->y));\n    bot->CastStop();', ONYXIA)
        self.assertIn('ai::LogCastCancel(ai, "follow");\n        bot->CastStop();', MOVEMENT)
        seek = ACTIONS.index('ai::LogCastCancel(ai, "seek_stop");')
        self.assertLess(seek, ACTIONS.index("bot->CastStop();", seek))


if __name__ == "__main__":
    unittest.main()

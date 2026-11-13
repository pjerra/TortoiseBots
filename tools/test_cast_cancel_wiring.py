"""Wiring pins for the CastCancel diagnostic row (AiPlayerbot.RaidBossDiagnostics).

Healer and caster casts start and never land in Hakkar fights, and no data set could say why. The pure progress rule
lives in ai/playerbot/CastCancelPolicy.h (tested in tools/test_cast_cancel_policy.cpp); the writer is
ai/playerbot/CastCancelLog.cpp. These pins hold the gate and the cancel sites: with the diagnostics key off nothing is
written, each row carries the slot the site really interrupts, and one real cancel writes one row.
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
DIAGNOSTIC_WRITERS = (
    "ai/playerbot/CastCancelLog.cpp",
    "ai/playerbot/BotDiagnostics.cpp",
    "ai/playerbot/HealerDiagnostics.cpp",
    "ai/playerbot/BotActionLog.cpp",
)
CACHED_TARGET = re.compile(r"\bgetUnitTarget\s*\(")

LOG_ROW = "void ai::LogCastCancel(PlayerbotAI* ai, const char* site, CurrentSpellTypes slot, float movedYd)"


def body(text, signature):
    start = text.index(signature)
    end = text.index("\n}\n", start)
    return text[start:end]


class CastCancelWiring(unittest.TestCase):
    def test_the_row_is_gated_by_the_diagnostics_key(self):
        # Key off: one branch, no slot read and no row. The bot_events.csv allowlist is the second gate, and both are
        # read before the slot, so a diagnostic-only call costs the branch.
        gate = body(LOG, "bool CastCancelEnabled()")
        self.assertIn("sPlayerbotAIConfig.raidBossDiagnostics", gate)
        self.assertIn('hasLog("bot_events.csv")', gate)
        fn = body(LOG, LOG_ROW)
        self.assertIn("!CastCancelEnabled()", fn)
        self.assertLess(fn.index("!CastCancelEnabled()"), fn.index("GetCurrentSpell("))
        self.assertIn('logEvent(ai, "CastCancel", info1, "")', fn)
        self.assertEqual(fn.count("logEvent("), 1)

    def test_the_row_carries_the_briefs_fields(self):
        fn = body(LOG, LOG_ROW)
        for field in ("site=%s", "slot=%s", "spell=%u", "cast_pct=%d", "target=%s", "moved_yd=%.1f"):
            self.assertIn(field, fn)
        # The slot is the one the site passes, never whichever cast is live first.
        self.assertIn("bot->GetCurrentSpell(slot)", fn)
        self.assertIn("SlotWord(slot)", fn)
        # The progress rule is the tested policy, not an inline division.
        self.assertIn("CastCancelPct(totalMs, cast->GetCastedTime())", fn)
        self.assertIn('#include "CastCancelPolicy.h"', LOG)
        # Observation only: the writer never interrupts, moves or casts.
        for forbidden in ("Interrupt", "CastStop", "MoveTo(", "CastSpell"):
            self.assertNotIn(forbidden, fn)

    def test_the_cast_cancel_row_resolves_its_target_by_guid(self):
        # The cast's unit target is a raw pointer the map refreshes only in the caster's next Spell::update, and the
        # map frees a removed unit at the end of its tick. A cancel that logs a target freed in that tick (Jin'do's
        # death despawns the summons the bots are casting at) would read freed memory. The row resolves the guid it
        # holds instead, so a target that left the world reads none.
        fn = body(LOG, LOG_ROW)
        self.assertIn("cast->m_targets.getUnitTargetGuid()", fn)
        self.assertIn("ObjectAccessor::GetUnit(*bot,", fn)
        self.assertNotIn("getUnitTarget()", fn)
        self.assertLess(fn.index("getUnitTargetGuid()"), fn.index("target ? target->GetName()"))

    def test_no_diagnostic_row_reads_a_cached_target_pointer(self):
        # The same rule for every writer that ends a row after a possible removal: a cached m_targets pointer is
        # never read in a diagnostic row.
        for rel in DIAGNOSTIC_WRITERS:
            text = (ROOT / rel).read_text()
            self.assertIsNone(CACHED_TARGET.search(text), f"{rel} reads a cached m_targets pointer")

    def test_the_multi_slot_helpers_log_one_row_per_real_cancel(self):
        # A CastStop drops every preparing non-melee slot with a cast time; one row each, and the rows are the same
        # set the core call takes, so nothing is logged that the interrupt does not really drop.
        stop = body(LOG, "void ai::LogCastCancelStop(PlayerbotAI* ai, const char* site, float movedYd)")
        self.assertIn(
            "for (const CurrentSpellTypes slot : { CURRENT_GENERIC_SPELL, CURRENT_AUTOREPEAT_SPELL, "
            "CURRENT_CHANNELED_SPELL })", stop)
        self.assertIn("SPELL_STATE_PREPARING", stop)
        self.assertIn("cast->GetCastedTime()", stop)
        self.assertEqual(stop.count("LogCastCancel("), 1)
        # InterruptNonMeleeSpells takes generic and autorepeat unless delayed and the channel always.
        non_melee = body(LOG, "void ai::LogCastCancelNonMelee(PlayerbotAI* ai, const char* site, bool withDelayed, "
                              "float movedYd)")
        self.assertIn("for (const CurrentSpellTypes slot : { CURRENT_GENERIC_SPELL, CURRENT_AUTOREPEAT_SPELL })",
                      non_melee)
        self.assertIn("SPELL_STATE_DELAYED", non_melee)
        self.assertIn("GetCurrentSpell(CURRENT_CHANNELED_SPELL)", non_melee)
        self.assertEqual(non_melee.count("LogCastCancel("), 2)
        # Both helpers read the key before the first slot.
        for fn in (stop, non_melee):
            self.assertIn("!CastCancelEnabled()", fn)
            self.assertLess(fn.index("!CastCancelEnabled()"), fn.index("GetCurrentSpell("))

    def test_the_writer_is_built(self):
        self.assertIn('"${TORTOISEBOTS_ROOT}/ai/playerbot/CastCancelLog.cpp"', CMAKE)
        self.assertIn(
            "void LogCastCancel(PlayerbotAI* ai, const char* site, CurrentSpellTypes slot, float movedYd = 0.0f);",
            HEADER)
        self.assertIn("void LogCastCancelStop(PlayerbotAI* ai, const char* site, float movedYd = 0.0f);", HEADER)
        self.assertIn(
            "void LogCastCancelNonMelee(PlayerbotAI* ai, const char* site, bool withDelayed, float movedYd = 0.0f);",
            HEADER)

    def test_the_ai_interrupt_choke_point_logs_first_and_names_the_slot(self):
        fn = body(SPELLS, "void PlayerbotAI::InterruptSpell(bool withMeleeAndAuto, const char* site, float movedYd)")
        # The row names the slot about to be interrupted, not the first live cast.
        self.assertIn('LogCastCancel(this, site ? site : "ai", (CurrentSpellTypes)type, movedYd);', fn)
        self.assertLess(fn.index("LogCastCancel(this,"), fn.index("bot->InterruptSpell("))

    def test_the_spread_slot_and_the_cast_cut_log(self):
        fn = body(ACTIONS, "bool BossTacticsSpreadSlotAction::Execute(Event& event)")
        # The 6 s cut: one row per slot CastStop really drops.
        self.assertIn('ai::LogCastCancelStop(ai, "spread_cut", SlotWalkYd(bot, task));', fn)
        self.assertLess(fn.index('"spread_cut"'), fn.index("bot->CastStop()"))
        # The walk: its own interrupt carries the walk's site, so the row names the slot really dropped and MoveTo's
        # own pass (which would log under `ai`) finds nothing live. One real cancel, one row.
        self.assertIn('ai->InterruptSpell(false, "spread_slot", SlotWalkYd(bot, task));', fn)
        self.assertLess(fn.index('"spread_slot"'), fn.index("MoveTo(bot->GetMapId(), task.x, task.y, task.z"))
        # No per-tick slot read just to gate the diagnostic: the key is read before the first slot inside the row.
        self.assertNotIn("IsNonMeleeSpellCasted", fn)

    def test_the_seek_walkout_interrupts_log(self):
        self.assertIn('ai::LogCastCancelNonMelee(ai, "seek_shot", false);', ACTIONS)
        self.assertIn('ai::LogCastCancel(ai, "seek_walk_back", CURRENT_AUTOREPEAT_SPELL);', ACTIONS)
        self.assertIn('ai::LogCastCancelNonMelee(ai, "seek_walk_out", true,', ACTIONS)
        shot = ACTIONS.index('"seek_shot"')
        self.assertLess(shot, ACTIONS.index("bot->InterruptNonMeleeSpells(false);", shot))
        back = ACTIONS.index('"seek_walk_back"')
        self.assertLess(back, ACTIONS.index("bot->InterruptSpell(CURRENT_AUTOREPEAT_SPELL);", back))
        walk = ACTIONS.index('"seek_walk_out"')
        self.assertLess(walk, ACTIONS.index("bot->InterruptNonMeleeSpells(true);", walk))

    def test_the_wasteful_heal_cancel_logs(self):
        fn = body(GENERIC_SPELL, "bool InterruptCurrentSpellAction::Execute(Event& event)")
        self.assertIn('LogCastCancel(ai, "heal_full", (CurrentSpellTypes)type)', fn)
        self.assertLess(fn.index('"heal_full"'), fn.index("bot->InterruptSpell("))

    def test_every_module_side_cancel_is_instrumented(self):
        # Mechanical audit of the module sources: a direct `bot->CastStop/InterruptSpell/InterruptNonMeleeSpells`
        # loses the bot's own cast, so it must log on its own line or the line just above it, with the site's own row
        # or the multi-slot helper. The AI's member `InterruptSpell()` callers are covered by the choke point pinned
        # above and are not these calls. A new uninstrumented direct site fails here instead of shipping an invisible
        # cancel.
        cancel = re.compile(r"\bbot\s*->\s*(?:CastStop|InterruptSpell|InterruptNonMeleeSpells)\s*\(")
        logged = re.compile(r"LogCastCancel(?:Stop|NonMelee)?\(")
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
                if logged.search(line) or logged.search(previous):
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
        self.assertIn('ai::LogCastCancelStop(ai, "heigan_dance", spotYd);\n        bot->CastStop();', HEIGAN)
        self.assertIn('ai::LogCastCancelStop(ai, "onyxia_breath", bot->GetDistance2d(best->x, best->y));\n    bot->CastStop();', ONYXIA)
        self.assertIn('ai::LogCastCancelStop(ai, "follow");\n        bot->CastStop();', MOVEMENT)
        seek = ACTIONS.index('ai::LogCastCancelStop(ai, "seek_stop");')
        self.assertLess(seek, ACTIONS.index("bot->CastStop();", seek))


if __name__ == "__main__":
    unittest.main()

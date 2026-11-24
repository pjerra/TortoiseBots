#!/usr/bin/env python3
"""Wiring pins for the combat and movement compat shims that now do the real thing.

The core calls cannot run without a server, so the pins are on the source and on the generated slice registry:

  1 summon-object slots: the helper reacts to the one effect the core routes to a slot, clears a slot that names
    another unit's object, and the feature row is gone
  2 item spell charges: the legacy slot index selects the ItemEffectEntry and the item answers its charges; the row
    stays for the icon half only
  3 the near-teleport ack interrupts the top generator through MotionMaster::InterruptOnTeleport; the heartbeat half
    still counts
  4 a bot jump is one MoveJump spline; the 1.12 stop and land packets are behind the Turtle core guard; the feature
    row is gone and the chat `jump` action and trigger come from the generator, not from editing the generated file

Run:
  python3 -m unittest forever/tools/test_compat_combat_move_wiring.py
  python3 forever/tools/test_compat_combat_move_wiring.py --mutate   # must FAIL (exit non-zero)
"""
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


def read(rel):
    return (ROOT / rel).read_text()


COMPAT = read("forever/src/compat/TbCompat_Unported.h")
FEATURES = read("ai/playerbot/UnportedFeatures.h")
UNPORTED = read("forever/docs/UNPORTED.md")
MOVEMENT = read("ai/playerbot/strategy/actions/MovementActions.cpp")
PLAYERBOT_AI = read("ai/playerbot/PlayerbotAI.cpp")
GENERATOR = read("forever/tools/slice_gen_context.py")
SLICE_CONTEXT = read("forever/src/slice/SliceAiObjectContext.cpp")


def function_body(text, signature):
    """The text from `signature` to the next line that closes the function."""
    start = text.index(signature)
    end = text.index("\n}\n", start)
    return text[start:end]


def check_summon_slots(compat, features, unported):
    body = function_body(compat, "inline void ReleaseForeignSummonObjectsCompat(")
    for needle in ("spellInfo->HasEffect(SPELL_EFFECT_SUMMON_OBJECT_SLOT1)", "bot->m_ObjectSlot[0]",
                   "bot->GetMap()->GetGameObject(slot)", "obj->GetOwnerGUID() != bot->GetGUID()", "slot.Clear()"):
        if needle not in body:
            raise AssertionError("the summon-slot helper misses " + needle)
    if "Feature::SummonSlots" in compat:
        raise AssertionError("the summon-slot helper still counts an unported feature")
    if "X(SummonSlots" in features or "| summon-object slots |" in unported:
        raise AssertionError("the ported summon-object slots still have a feature row")


def check_item_charges(compat, features, unported):
    body = function_body(compat, "inline int32 ItemSpellChargesCompat(")
    for needle in ("item->GetEffects()", "effect->LegacySlotIndex == index", "item->GetSpellCharges(effect)"):
        if needle not in body:
            raise AssertionError("the item-charge helper misses " + needle)
    if "Unported(" in body:
        raise AssertionError("the item-charge helper still counts an unported feature")
    # The mirror prototype fills Spells[] by the same field, so the two index spaces really are the same.
    mirror = read("forever/src/compat/TbItemPrototype.cpp")
    if "mirror.Spells[effect->LegacySlotIndex]" not in mirror:
        raise AssertionError("the mirror no longer fills Spells[] by LegacySlotIndex")
    if "spell data (icon ids)" not in features or "spell data (icon ids)" not in unported:
        raise AssertionError("the icon half of the spell-data row is gone")


def check_movement_interrupt(compat, features, unported):
    if ("inline void InterruptCurrentMovementCompat(Player* bot) { bot->GetMotionMaster()->InterruptOnTeleport(); }"
            not in compat):
        raise AssertionError("the near-teleport interrupt is not the core's InterruptOnTeleport")
    heartbeat = function_body(compat, "inline void SendHeartBeatCompat(")
    if "Feature::MovementInterrupt" not in heartbeat:
        raise AssertionError("the heartbeat half is counted as real")
    if "movement heartbeat" not in features or "movement heartbeat" not in unported:
        raise AssertionError("the movement row no longer names the heartbeat half only")


def check_jump(movement, playerbot_ai, features, unported):
    if "Feature::JumpFall" in movement or "Feature::JumpFall" in playerbot_ai:
        raise AssertionError("a jump call site still counts the unported feature")
    for text in (movement,):
        if "UnportedBlockResultCompat(tbcompat::Feature::JumpFall" in text:
            raise AssertionError("DoJump still runs through the unported block wrapper")
    if "X(JumpFall" in features or "| jump, fall and knockback |" in unported:
        raise AssertionError("the ported jump/fall feature still has a row")
    if "MoveJump(EVENT_JUMP, TbCorePosition(landing), hSpeed" not in movement:
        raise AssertionError("DoJump does not launch a core jump spline")
    # The 1.12 jump packets are Turtle only: the ForeverCore branch is the spline.
    dojump = function_body(movement, "bool JumpAction::DoJump(")
    core_branch = dojump[dojump.index("#else"):]
    if "MoveJump(EVENT_JUMP" not in core_branch:
        raise AssertionError("the jump spline is not in the ForeverCore branch")
    turtle_guard = "#if !defined(TORTOISEBOTS_CORE_FOREVER) || !TORTOISEBOTS_CORE_FOREVER"
    if turtle_guard not in dojump or dojump.index(turtle_guard) > dojump.index("#else"):
        raise AssertionError("the 1.12 jump packets are not behind the Turtle build")
    # The landing packets are the same story.
    landing = playerbot_ai[playerbot_ai.index("// normal landing"):]
    landing = landing[:landing.index("bot->StopMoving();")]
    for packet in ("MSG_MOVE_STOP", "MSG_MOVE_FALL_LAND"):
        at = landing.index(packet)
        before = landing.rfind("#if !defined(TORTOISEBOTS_CORE_FOREVER) || !TORTOISEBOTS_CORE_FOREVER", 0, at)
        endif = landing.find("#endif", at)
        if before < 0 or landing.find("#endif", before) < at or endif < 0:
            raise AssertionError("the landing packet %s is not behind the Turtle build" % packet)
    if "SetFallInformationCompat(bot, 0.0f)" not in landing:
        raise AssertionError("the landing no longer clears the fall start")


def check_registry(generator, context):
    for needle in ('"strategy": "follow jump"', '"action": "jump"', '"trigger": "jump"'):
        if needle not in generator:
            raise AssertionError("the generator does not list the jump keys")
    for needle in ('creators["follow jump"] = [](PlayerbotAI* ai) { return new FollowJumpStrategy(ai); };',
                   'creators["jump"] = [](PlayerbotAI* ai) { return new JumpAction(ai); };',
                   'creators["jump"] = [](PlayerbotAI* ai) { return new ChatCommandTrigger(ai, "jump"); };'):
        if needle not in context:
            raise AssertionError("the generated slice registry misses " + needle)


def run_all(compat=COMPAT, features=FEATURES, unported=UNPORTED, movement=MOVEMENT,
            playerbot_ai=PLAYERBOT_AI, generator=GENERATOR, context=SLICE_CONTEXT):
    check_summon_slots(compat, features, unported)
    check_item_charges(compat, features, unported)
    check_movement_interrupt(compat, features, unported)
    check_jump(movement, playerbot_ai, features, unported)
    check_registry(generator, context)


def mutation_proofs():
    """Each (name, callable) must raise AssertionError on the mutated text: the pin is what keeps the fix in."""
    summon = COMPAT.replace("if (obj && obj->GetOwnerGUID() != bot->GetGUID())",
                            "if (false)")
    charges = COMPAT.replace("effect->LegacySlotIndex == index", "effect->ID == index")
    charges_counted = COMPAT.replace("return item->GetSpellCharges(effect);",
                                     "tbcompat::Unported(tbcompat::Feature::SpellData); return 0;")
    jump_packets = MOVEMENT.replace("MoveJump(EVENT_JUMP, TbCorePosition(landing), hSpeed, {}, {}, {}, false, true)",
                                    "bot->Relocate(landing.getX(), landing.getY(), landing.getZ())")
    landing_unguarded = PLAYERBOT_AI.replace("#if !defined(TORTOISEBOTS_CORE_FOREVER) || !TORTOISEBOTS_CORE_FOREVER\n            // Turtle's bot is its own client: the stop and land packets are what ends the jump there.",
                                            "// Turtle's bot is its own client: the stop and land packets are what ends the jump there.")
    registry = SLICE_CONTEXT.replace('            creators["follow jump"] = [](PlayerbotAI* ai) { return new FollowJumpStrategy(ai); };\n', "")
    return [
        ("summon slot array", lambda: check_summon_slots(summon, FEATURES, UNPORTED)),
        ("charge effect index", lambda: check_item_charges(charges, FEATURES, UNPORTED)),
        ("charge counter", lambda: check_item_charges(charges_counted, FEATURES, UNPORTED)),
        ("jump spline", lambda: check_jump(jump_packets, PLAYERBOT_AI, FEATURES, UNPORTED)),
        ("landing guard", lambda: check_jump(MOVEMENT, landing_unguarded, FEATURES, UNPORTED)),
        ("jump registered", lambda: check_registry(GENERATOR, registry)),
    ]


class CompatCombatMoveWiring(unittest.TestCase):
    def test_all(self):
        run_all()

    def test_mutation_fails(self):
        for name, mutate in mutation_proofs():
            with self.assertRaises(AssertionError, msg=name):
                mutate()


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        missed = []
        for name, mutate in mutation_proofs():
            try:
                mutate()
            except AssertionError:
                continue
            missed.append(name)
        if missed:
            print("missed mutations (a pin is gone): " + ", ".join(missed))
            sys.exit(0)
        print("all %d mutations caught" % len(mutation_proofs()))
        sys.exit(1)
    unittest.main()

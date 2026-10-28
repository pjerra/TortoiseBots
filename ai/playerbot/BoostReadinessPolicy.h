#pragma once

// Boost triggers arm on a missing aura. A cooldown spell is missing its aura
// for almost the whole cooldown, and an untrained spell never has one, so a
// trigger that checks only the aura (or only training, at construction time)
// arms its row every combat tick while the cast dies in isPossible. Readiness
// must therefore be checked at trigger time.
//
// The predicates below are pure so they can be unit-tested; call sites pass
// the spellbook/cooldown facts: known = the "spell id" value is non-zero (it
// walks the bot's own spellbook, pet spells as fallback) and ready =
// sServerFacade.IsSpellReady(bot, spellId).

namespace ai
{
    // A boost trigger may arm only while its spell is trained and off cooldown.
    inline bool BoostReady(bool spellKnown, bool spellReady)
    {
        return spellKnown && spellReady;
    }

    // A heal boost (Ascendance) must not fire on the first quiet combat
    // tick: trained and off cooldown is not enough, someone must actually
    // need healing - a low party member or a hurt group.
    inline bool HealBoostReady(bool spellKnown, bool spellReady, bool partyMemberLow, bool groupHurt)
    {
        return BoostReady(spellKnown, spellReady) && (partyMemberLow || groupHurt);
    }

    // The shared "generic boost" racial row offers berserking then blood fury
    // to every class. Arm it only when one of the two is castable right now:
    // a bot trained in neither - or with its racial on cooldown - must not
    // arm the row at all, or both actions die in isPossible every tick.
    inline bool RacialBoostReady(bool firstCastable, bool secondCastable)
    {
        return firstCastable || secondCastable;
    }
}

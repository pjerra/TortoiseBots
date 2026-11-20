#pragma once

#include <cstdint>

// Which travel purposes may be picked while the slices that serve them are not merged (W4-A2, decision D5 of
// FOREVER-W0-W4-SPEC).
//
// A travel purpose is only worth a walk if the bot can finish the errand on arrival. W2 owns vendors, repairs and
// trainers, W3 owns quests; W4 (travel) is written before either is merged, and must be able to land first or last.
// So the purpose pick does not guess from a config flag: it asks whether the keys those slices register are actually
// in the bot's context (the registry is generated per slice, forever/tools/slice_gen_context.py), and refuses the
// purpose while they are missing.
//
// Grind and town are W4's own: the grind kit is W1's (`grind` strategy), the town visit is the rpg target pick W4-D
// registers, so both are always allowed.
//
// The refusal is the "do less" answer: a refused purpose is not picked, so the bot keeps grinding where it is instead
// of walking to a vendor it cannot use. It never invents a substitute purpose.
namespace ai
{
    namespace travel_purpose
    {
        // The purpose groups the gate distinguishes. Quest covers the three quest purposes (giver, objective, taker);
        // town is the generic rpg visit (innkeeper or a service cluster).
        enum class Group : std::uint8_t
        {
            Grind,
            Town,
            Vendor,
            Repair,
            Quest
        };

        // What the bot's context actually registered. The caller reads the registry once per decision (the keys do not
        // change after startup); this header only decides.
        struct Registered
        {
            bool vendorKeys = false; // W2: sell/repair/trainer keys are in the context
            bool questKeys = false;  // W3: the rpg quest keys are in the context
        };

        // Why a purpose is refused; None when it is allowed.
        enum class Refusal : std::uint8_t
        {
            None,
            VendorKeysMissing,
            QuestKeysMissing
        };

        inline Refusal RefusalOf(Group group, Registered const& registered)
        {
            switch (group)
            {
            case Group::Grind:
            case Group::Town:
                return Refusal::None;
            case Group::Vendor:
            case Group::Repair:
                return registered.vendorKeys ? Refusal::None : Refusal::VendorKeysMissing;
            case Group::Quest:
                return registered.questKeys ? Refusal::None : Refusal::QuestKeysMissing;
            }
            return Refusal::None;
        }

        inline bool Allows(Group group, Registered const& registered)
        {
            return RefusalOf(group, registered) == Refusal::None;
        }
    }
}

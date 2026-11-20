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
            bool vendorKeys = false; // W2: sell/repair/trainer keys are in the context, and the `vendor trip needed` value
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

        // The grind errand's clock (the legacy rule of the Turtle side, issue #393 included): a grind errand is open in the
        // first 45 minutes of the hour (`rpgPhase`, 1..60, the bot's fixed number). A beginner - a free pool bot below level 5
        // with no real player master - is open ALL hour: the generic RPG walk that fills the last 15 minutes refuses every
        // non-vendor errand below level 5 (RpgTravelDestination::IsPossible), so closing the grind there left a level 1-4 bot
        // with no errand at all and idling 25% of every hour.
        inline bool GrindPhaseOpen(std::uint32_t botLevel, bool freeBot, bool hasRealPlayerMaster, std::uint32_t rpgPhase)
        {
            bool const beginner = botLevel < 5 && freeBot && !hasRealPlayerMaster;
            return beginner || rpgPhase <= 45;
        }

        // The vendor errand's need (decision 5, issue #379). The old pair `should sell && can sell` is true for every pool bot
        // holding food (2,300 vendor errands in 65 minutes, no sale made); the need is the real one: W2's `vendor trip needed`
        // value (stock a vendor pays for and worth the walk, a durability below the repair line, or an empty food bag the bot can
        // pay to refill, never while the purpose is parked). It stays gated on W2's keys: with them absent the answer is false
        // whatever the value says, so W4 can merge before W2.
        inline bool VendorErrandNeeded(Registered const& registered, bool vendorTripNeeded)
        {
            return registered.vendorKeys && vendorTripNeeded;
        }
    }
}

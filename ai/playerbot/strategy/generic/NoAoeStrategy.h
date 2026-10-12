#pragma once
#include "playerbot/strategy/Strategy.h"

namespace ai
{
    // Vetoes every area spell (AoeOffPolicy.h) for this bot. Only present
    // while the player has set `.bot action aoe off` (or `.bot behavior
    // <bot> aoe off`), so bots with AoE on pay nothing.
    class NoAoeMultiplier : public Multiplier
    {
    public:
        NoAoeMultiplier(PlayerbotAI* ai) : Multiplier(ai, "no aoe") {}
        float GetValue(Action* action) override;
    };

    // Player order "no area spells": `.bot action aoe off` adds it,
    // `.bot action aoe on` removes it. Combat engine only; no triggers.
    class NoAoeStrategy : public Strategy
    {
    public:
        NoAoeStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "no aoe"; }
#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "no aoe"; } //Must equal iternal name
        virtual std::string GetHelpDescription() {
            return "Stops the bot from using any area spell (Blizzard, Rain of Fire, Multi-Shot, Consecration, Whirlwind, Frost Nova...). Set by .bot action aoe off.";
        }
        virtual std::vector<std::string> GetRelatedStrategies() { return { "aoe", "dps aoe" }; }
#endif

    private:
        void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
    };
}

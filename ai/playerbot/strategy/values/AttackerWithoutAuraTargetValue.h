#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"

namespace ai
{
    class AttackerWithoutAuraTargetValue : public UnitCalculatedValue, public Qualified
	{
	public:
        AttackerWithoutAuraTargetValue(PlayerbotAI* ai, std::string name = "attacker without aura", bool checkIsOwner = false) : UnitCalculatedValue(ai, name), Qualified(), checkIsOwner(checkIsOwner) {}

    protected:
        virtual Unit* Calculate() override;

    private:
        bool checkIsOwner;
	};

    // Same scan, but an aura cast by someone else does not count: the bot
    // wants an attacker that does not carry ITS OWN aura (per-caster DoTs).
    class AttackerWithoutMyAuraTargetValue : public AttackerWithoutAuraTargetValue
    {
    public:
        AttackerWithoutMyAuraTargetValue(PlayerbotAI* ai) : AttackerWithoutAuraTargetValue(ai, "attacker without my aura", true) {}
    };
}

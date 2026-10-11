#pragma once

#include "playerbot/HunterSwitchPolicy.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/Value.h"
#include "playerbot/strategy/AiObjectContext.h"

namespace ai
{
    // Per-bot kit-switch dwell and dead-zone step-out bookkeeping
    // (HunterSwitchPolicy.h). Hunter-only: registered in HunterAiObjectContext,
    // so other classes get a null value and callers must check for it.
    class HunterKitStateValue : public ManualSetValue<HunterKitState&>
    {
    public:
        HunterKitStateValue(PlayerbotAI* ai) : ManualSetValue<HunterKitState&>(ai, data, "hunter kit state") {}
    private:
        HunterKitState data = HunterKitState();
    };

    inline HunterKitState* GetHunterKitState(PlayerbotAI* ai)
    {
        AiObjectContext* context = ai ? ai->GetAiObjectContext() : nullptr;
        Value<HunterKitState&>* value = context ? context->GetValue<HunterKitState&>("hunter kit state") : nullptr;
        return value ? &value->Get() : nullptr;
    }
}

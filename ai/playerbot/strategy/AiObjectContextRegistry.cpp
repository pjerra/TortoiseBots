// AiObjectContext constructor: registers the generic strategy, action, trigger and value contexts.
// The ForeverCore slice build compiles forever/src/slice/SliceAiObjectContext.cpp in its place.
// The other AiObjectContext methods are in AiObjectContext.cpp.

#include "playerbot/playerbot.h"
#include "Action.h"
#include "AiObjectContext.h"
#include "NamedObjectContext.h"
#include "StrategyContext.h"
#include "triggers/TriggerContext.h"
#include "actions/ActionContext.h"
#include "triggers/ChatTriggerContext.h"
#include "actions/ChatActionContext.h"
#include "triggers/WorldPacketTriggerContext.h"
#include "actions/WorldPacketActionContext.h"
#include "values/ValueContext.h"


using namespace ai;

AiObjectContext::AiObjectContext(PlayerbotAI* ai) : PlayerbotAIAware(ai)
{
    strategyContexts.Add(new StrategyContext());
    strategyContexts.Add(new MovementStrategyContext());
    strategyContexts.Add(new AssistStrategyContext());
    strategyContexts.Add(new QuestStrategyContext());
    strategyContexts.Add(new FishStrategyContext());

    actionContexts.Add(new ActionContext());
    actionContexts.Add(new ChatActionContext());
    actionContexts.Add(new WorldPacketActionContext());

    triggerContexts.Add(new TriggerContext());
    triggerContexts.Add(new ChatTriggerContext());
    triggerContexts.Add(new WorldPacketTriggerContext());

    valueContexts.Add(new ValueContext());

    //valueContexts.Add(&sSharedValueContext);
}

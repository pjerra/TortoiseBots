#pragma once
#include "playerbot/PlayerbotAI.h"

#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/Action.h"

namespace ai
{
    class AcceptResurrectAction : public Action {
    public:
        AcceptResurrectAction(PlayerbotAI* ai) : Action(ai, "accept resurrect") {}

        virtual bool Execute(Event& event) override
        {
            if (!sServerFacade.IsAlive(bot))
            {
                ObjectGuid const guid = ResurrectOffererCompat(event);

                // Turtle: everybody's offer is taken (the helper is always true). ForeverCore: only a group member's or the commander's; the
                // request of anybody else is declined (spec section 3). Both helpers answer false when the core had no such request.
                if (!ResurrectOffererAcceptableCompat(ai, guid))
                {
                    DeclineResurrectCompat(bot, guid);
                    return false;
                }

                // queue the packet to get around race condition (Turtle); ForeverCore: the core's typed handler, Response 0 = accept
                return AcceptResurrectCompat(bot, guid);
            }

            return false;
        }

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "accept resurrect"; } //Must equal iternal name
        virtual std::string GetHelpDescription()
        {
            return "This action clicks the accept button asked if it wants to be resurrected by a player.";
        }
        virtual std::vector<std::string> GetUsedActions() { return {}; }
        virtual std::vector<std::string> GetUsedValues() { return {}; }
#endif
    };

}

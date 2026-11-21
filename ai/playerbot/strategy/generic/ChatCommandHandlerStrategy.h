#pragma once
#include "PassTroughStrategy.h"

namespace ai
{
    class ChatCommandHandlerStrategy : public PassTroughStrategy
    {
    public:
        ChatCommandHandlerStrategy(PlayerbotAI* ai);
        std::string getName() override { return "chat"; }
        // The commands this strategy answers (the pass-through list): `help` lists them (S1-R2: ForeverCore's ChatActionContext header does not
        // compile, so the whisper list there is this one — forever/src/compat/TbCompat_CH.h).
        std::list<std::string> const& GetSupported() const { return supported; }

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "chat"; } //Must equal internal name
        virtual std::string GetHelpDescription()
        {
            return "This strategy will make bots respond to various chat commands.";
        }
        virtual std::vector<std::string> GetRelatedStrategies() { return { }; }
#endif

    private:
        void InitReactionTriggers(std::list<TriggerNode*> &triggers) override;
    };
}

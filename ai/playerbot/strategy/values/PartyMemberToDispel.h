#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"
#include "PartyMemberValue.h"

namespace ai
{
    class PartyMemberToDispel : public PartyMemberValue, public Qualified
	{
	public:
        // claim=false is the read-only form (the drink gate asks who needs a
        // cure without reserving anybody); only the cleanse decision claims
        // its pick and writes the pick row.
        PartyMemberToDispel(PlayerbotAI* ai, std::string name = "party member to dispel", bool claim = true) :
          PartyMemberValue(ai, name), Qualified(), claim(claim) {}

    protected:
        virtual Unit* Calculate() override;

    private:
        bool claim;
	};
}

#pragma once
#include <vector>
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"
#include "PartyMemberValue.h"

namespace ai
{
    class PartyMemberToHeal : public PartyMemberValue
	{
	public:
        PartyMemberToHeal(PlayerbotAI* ai, std::string name = "party member to heal") :
          PartyMemberValue(ai, name) {}

    protected:
        virtual Unit* Calculate() override;
        bool CanHealPet(Pet* pet);
        virtual bool Check(Unit* player);

    private:
        std::vector<Player*> GetPartyMembers();
	};

    // The group's tank jobs in one order (RaidTankRolePolicy.h): raid Main
    // Tank flag, then a tank Main Assist, then promoted raid assistants
    // (assist jobs only), then live tanks in slot order.
    // Only alive members on the bot's map and instance count. Without a
    // group the bot is its own main tank when it is a tank.
    struct RaidTankRoles
    {
        std::vector<Player*> tanks; // [0] main tank, [1..] assist tanks
        int botIndex = -1;          // the bot's place in `tanks`, -1 if none

        Player* MainTank() const { return tanks.empty() ? nullptr : tanks.front(); }
        bool BotIsMainTank() const { return botIndex == 0; }
    };

    RaidTankRoles GetRaidTankRoles(PlayerbotAI* ai);

    // mod-playerbots PartyMemberMainTankValue (LD-8): the raid/party main
    // tank — explicit raid flag first, else the first live tank. No
    // Misdirection/Tricks consumers exist in 1.18.1; raid tactics read it.
    // Same order as the boss scripts via GetRaidTankRoles.
    class PartyMemberMainTankValue : public PartyMemberValue
    {
    public:
        PartyMemberMainTankValue(PlayerbotAI* ai, std::string name = "main tank") :
            PartyMemberValue(ai, name) {}

    protected:
        virtual Unit* Calculate() override;
    };

    class PartyMemberToProtect : public PartyMemberValue
    {
    public:
        PartyMemberToProtect(PlayerbotAI* ai, std::string name = "party member to protect") :
            PartyMemberValue(ai, name) {}

    protected:
        virtual Unit* Calculate() override;
    };

    // Lowest-mana alive group healer (mod-playerbots parity): other agents'
    // innervate / mana-tide rows target this. Skips self (the caster's own
    // mana is covered by the plain low/medium mana triggers) and non-mana
    // users.
    class HealerLowMana : public PartyMemberValue
    {
    public:
        HealerLowMana(PlayerbotAI* ai, std::string name = "healer low mana") :
            PartyMemberValue(ai, name) {}

    protected:
        virtual Unit* Calculate() override;
    };

    class PartyMemberToRemoveRoots : public PartyMemberValue
    {
    public:
        PartyMemberToRemoveRoots(PlayerbotAI* ai, std::string name = "party member to remove roots") :
            PartyMemberValue(ai, name) {}

    protected:
        virtual Unit* Calculate() override;
    };
}

#pragma once

#include <algorithm>
#include <vector>

#include "playerbot/GroupMembers.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/policy/heal/InnervateTargetPolicy.h"
#include "playerbot/strategy/actions/GenericSpellActions.h"

namespace ai
{
    inline Unit* FindBestInnervateTarget(PlayerbotAI* ai, Player* bot)
    {
        if (!ai || !bot || !ai->HasSpell("innervate"))
            return nullptr;

        uint32 innervateId = ai->GetAiObjectContext()->GetValue<uint32>("spell id", "innervate")->Get();
        if (!innervateId || !sServerFacade.IsSpellReady(bot, innervateId))
            return nullptr;

        float maxRange = 0.0f;
        if (!ai->GetSpellRange("innervate", &maxRange))
            return nullptr;

        uint8 const threshold = static_cast<uint8>(std::min<uint32>(sPlayerbotAIConfig.innervateManaThreshold, 100u));
        Group* group = bot->GetGroup();
        if (!group)
        {
            if (bot->GetMaxPower(POWER_MANA) > 0 && ai->GetManaPercent(*bot) < threshold && !ai->HasAura("innervate", bot))
                return bot;
            return nullptr;
        }

        std::vector<InnervateCandidate> candidates;
        std::vector<Player*> members;
        for (Player* member : LiveGroupMembers(group))
        {
            if (!member || !ai->IsSafe(member) || member->GetMapId() != bot->GetMapId() || !sServerFacade.IsAlive(member))
                continue;
            if (!member->GetMaxPower(POWER_MANA))
                continue;

            bool const isSelf = (member == bot);
            bool const inRange = isSelf ||
                (sServerFacade.getDistance2d(bot, member) <= maxRange && sServerFacade.IsWithinLOSInMap(bot, member));
            bool const hasInnervate = ai->HasAura("innervate", member);
            bool const claimed = !isSelf &&
                BuffClaimRegistry::IsClaimedByOther(bot->GetObjectGuid(), member->GetObjectGuid(), "innervate");

            members.push_back(member);
            candidates.push_back({
                static_cast<std::uint64_t>(members.size()),
                isSelf,
                ai->IsHeal(member),
                true,
                ai->GetManaPercent(*member),
                inRange,
                hasInnervate,
                claimed
            });
        }

        std::uint64_t const picked = PickInnervateTarget(candidates, threshold);
        return picked ? members[picked - 1] : nullptr;
    }
} // namespace ai

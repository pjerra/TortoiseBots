#include "ai/playerbot/policy/world/GuildMailPolicy.h"

#include <cassert>
#include <cstdio>

using namespace TortoiseBots::world;

int main()
{
    int checks = 0;

    // 1. Gold gift goes to the poorest online bot member, capped at kMaxGoldGiftCopper.
    {
        std::vector<GuildMemberMailView> members = {
            { 1, "RichBot", true, true, 500000 },
            { 2, "MidBot", true, true, 15000 },
            { 3, "PoorBot", true, true, 2000 },
            { 4, "OfflinePoor", true, false, 100 }
        };
        auto plan = PlanGuildGoldGift(1, true, 500000, false, false, members);
        assert(plan.shouldSend);
        assert(plan.recipientGuidLow == 3);
        assert(plan.recipientName == "PoorBot");
        assert(plan.copper == kMaxGoldGiftCopper);
        ++checks;
    }

    // 2. Never mail real players' gold or mail inside a mixed guild when disallowed.
    {
        std::vector<GuildMemberMailView> members = {
            { 2, "RealPlayer", false, true, 0 },
            { 3, "PoorBot", true, true, 100 }
        };
        // Non-free sender (e.g. player-owned bot) is refused.
        assert(!PlanGuildGoldGift(1, false, 500000, false, true, members).shouldSend);
        // Mixed guild refused when allowMixedGuild = false.
        assert(!PlanGuildGoldGift(1, true, 500000, true, false, members).shouldSend);
        // With allowMixedGuild = true, still skips RealPlayer (isBot == false) and picks PoorBot.
        auto plan = PlanGuildGoldGift(1, true, 500000, true, true, members);
        assert(plan.shouldSend);
        assert(plan.recipientGuidLow == 3);
        ++checks;
    }

    // 3. No ping-pong: sender does not mail if senderCopper < 2 * recipient + reserve.
    {
        std::vector<GuildMemberMailView> members = {
            { 2, "AlmostEqual", true, true, 20000 }
        };
        // 50000 < 20000*2 + 20000 (60000) -> refused.
        assert(!PlanGuildGoldGift(1, true, 50000, false, false, members).shouldSend);
        ++checks;
    }

    // 4. Material mail filter & recipient selection.
    {
        GuildMatItemCandidate ore{ 2770, 7, false, false, false, false, false };
        assert(CanMailGuildMaterial(ore));

        GuildMatItemCandidate ownOre = ore;
        ownOre.usedBySender = true;
        assert(!CanMailGuildMaterial(ownOre));

        GuildMatItemCandidate orderOre = ore;
        orderOre.neededByOrder = true;
        assert(!CanMailGuildMaterial(orderOre));

        GuildMatItemCandidate soulbound = ore;
        soulbound.isSoulbound = true;
        assert(!CanMailGuildMaterial(soulbound));

        std::vector<GuildMemberMailView> crafters = {
            { 5, "RealCrafter", false, true, 1000 },
            { 8, "BotSmithB", true, true, 1000 },
            { 4, "BotSmithA", true, true, 1000 }
        };
        auto const* picked = PickMaterialRecipient(1, true, true, true, crafters);
        assert(picked != nullptr);
        assert(picked->guidLow == 4); // Skips real player, picks lowest bot guid
        ++checks;
    }

    std::printf("PASS: test_guild_mail_policy (%d checks, 0 failed)\n", checks);
    return 0;
}

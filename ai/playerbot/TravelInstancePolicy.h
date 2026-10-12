#pragma once

// Pure policy for issue #388. A bot with a real player master bypasses the
// activity throttling that quiets instance bots (PlayerbotAI::GetPriorityType
// returns HAS_REAL_PLAYER_MASTER before the instance branch), so the travel
// strategy it was granted while standing in the open world keeps running its
// destination search after it zones in: a ~39-bot hired raid in Naxxramas
// measured single bot-update passes up to 25.7 s, all of it the deferred search
// ChooseTravelTargetAction runs on the world tick while
// AiPlayerbot.AsyncTravelPartitions is off. Travel destinations are overworld
// places, so there is nothing to select inside a dungeon or raid: the travel
// actions stand down until the bot leaves, which also means the guard needs no
// zone-in/zone-out bookkeeping. The world-facing part (master type, current
// map) lives in the travel actions; the decision below is pure so it can be
// tested on its own.

namespace ai
{
    inline bool TravelSelectionBlockedByInstance(bool hasRealPlayerMaster, bool inInstanceMap)
    {
        return hasRealPlayerMaster && inInstanceMap;
    }

    // Same rule for the "gather" strategy's automatic node sweep ("add gathering
    // loot"): a companion inside a dungeon or raid follows its player, and every
    // node there sits next to a pack until it is cleared, so the sweep only
    // scanned, refused ("X is blocking Iron Deposit") and scanned again (Uldaman
    // run). A player's own loot order ("loot", optionally with a node link) is a
    // different action and keeps working; masterless bots are unchanged.
    inline bool GatherSweepBlockedByInstance(bool hasRealPlayerMaster, bool inInstanceMap)
    {
        return TravelSelectionBlockedByInstance(hasRealPlayerMaster, inInstanceMap);
    }
}

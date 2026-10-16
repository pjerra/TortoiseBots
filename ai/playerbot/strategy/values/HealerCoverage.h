#pragma once

class PlayerbotAI;
class Unit;

namespace ai
{
    // True when another living healer of the bot's group (party or raid; a
    // player healer counts) already has "ally" in heal range and in sight
    // (HealerCoversAlly, HealReachPolicy.h). Group slots only: no grid scan,
    // no DB. The bot itself and the ally do not count. Shared by the reach
    // trigger, the reach action and the heal target pick (issue #904).
    // "maxDistance" is the reach limit the caller uses for itself.
    bool AnotherHealerCoversAlly(PlayerbotAI* ai, Unit* ally, float maxDistance);

    // The same limit the reach trigger and action use: heal range less the
    // contact distance.
    float HealReachLimit(PlayerbotAI* ai);
}

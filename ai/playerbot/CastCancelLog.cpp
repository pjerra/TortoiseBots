#include "CastCancelLog.h"
#include "CastCancelPolicy.h"
#include "playerbot/playerbot.h"
#include <cstdio>

namespace
{
    // The bot's own cast, in the order a cancel loses it: a normal cast bar, a channel, the auto-repeat (Auto Shot,
    // Shoot, a wand) and last a melee-slot spell. The row names the slot it read.
    struct LiveCast
    {
        Spell* spell = nullptr;
        const char* slot = "";
    };

    LiveCast FindLiveCast(Player* bot)
    {
        static const CurrentSpellTypes kSlots[] = {
            CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL, CURRENT_AUTOREPEAT_SPELL, CURRENT_MELEE_SPELL };
        static const char* const kWords[] = { "generic", "channeled", "autorepeat", "melee" };
        for (int i = 0; i < 4; ++i)
        {
            Spell* spell = bot->GetCurrentSpell(kSlots[i]);
            if (spell && spell->m_spellInfo)
            {
                LiveCast cast;
                cast.spell = spell;
                cast.slot = kWords[i];
                return cast;
            }
        }
        return LiveCast();
    }
}

void ai::LogCastCancel(PlayerbotAI* ai, const char* site, float movedYd)
{
    // The key first: off, the row costs one branch and no slot read.
    if (!ai || !site || !sPlayerbotAIConfig.raidBossDiagnostics || !sPlayerbotAIConfig.hasLog("bot_events.csv"))
        return;
    Player* const bot = ai->GetBot();
    if (!bot)
        return;
    const LiveCast cast = FindLiveCast(bot);
    if (!cast.spell)
        return;
    // A cast bar has its total in m_casttime; a channel's total is the spell's duration.
    int totalMs = cast.spell->GetCastTime();
    if (totalMs <= 0)
        totalMs = static_cast<int>(GetSpellDuration(cast.spell->m_spellInfo));
    const int pct = CastCancelPct(totalMs, cast.spell->GetCastedTime());
    Unit* const target = cast.spell->m_targets.getUnitTarget();
    char info1[192];
    snprintf(info1, sizeof(info1), "site=%s,slot=%s,spell=%u,cast_pct=%d,target=%s,moved_yd=%.1f",
             site, cast.slot, cast.spell->m_spellInfo->Id, pct, target ? target->GetName() : "none", movedYd);
    sPlayerbotAIConfig.logEvent(ai, "CastCancel", info1, "");
}

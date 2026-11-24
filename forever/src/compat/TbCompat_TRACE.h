// ForeverCore side of the both-core helpers of the run trace and the why trace (runtime/RunTrace.cpp,
// runtime/WhyTrace.cpp). Turtle side: ai/cmangos-compat-trace.h, same names and answers.
//
// The trace code itself is shared with the Turtle build. What differs here is only what the two cores call
// the same thing: a guid is 64 bits on Turtle and 128 on Forever (GuidKeyCompat, TbCompat_Unit.h:396), an
// aura is applied per spell (AuraHolderMapCompat, TbCompat_P.h:293), a spell's cooldown moved to SpellHistory
// (forever-compat-shim.h:223), and the chat handler is built from a session.
#pragma once

#include "Chat.h"
#include "Player.h"
#include "SpellHistory.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Threat/ThreatManager.h"

#include <cstdint>
#include <string>

// The remaining cooldown of one spell, in whole seconds: T Player::GetSpellCooldownDelay. F keeps cooldowns in
// SpellHistory with millisecond precision (SpellHistory.h:174), so the same question is asked there and floored.
// An unknown spell has no cooldown.
inline uint32 SpellCooldownDelaySecondsCompat(Player* player, uint32 spellId)
{
    if (!player)
        return 0;
    SpellInfo const* info = sSpellMgr.GetSpellInfo(spellId, DIFFICULTY_NONE);
    if (!info)
        return 0;
    return static_cast<uint32>(player->GetSpellHistory()->GetRemainingCooldown(info).count() / 1000);
}

// Visit every unit on `attacker`'s threat list with the threat it holds there: T HostileReference::getTarget /
// getThreat, F ThreatReference::GetVictim / GetThreat (ThreatManager.h:136,294). fn(Unit*, float).
template <class Fn>
inline void ForEachThreatListEntryCompat(Unit* attacker, Fn&& fn)
{
    for (ThreatReference const* ref : attacker->GetThreatManager().GetUnsortedThreatList())
        if (ref)
            fn(ref->GetVictim(), ref->GetThreat());
}

// One chat line to a player's session. The core's ChatHandler is built from a WorldSession on Forever; the
// Turtle handler also takes a Player.
inline void TraceSendSysMessageCompat(Player* player, std::string const& text)
{
    if (player)
        ChatHandler(player->GetSession()).PSendSysMessage("%s", text.c_str());
}

// The harmful area effect the bot stands in (a dynamic object's persistent aura or somebody else's area aura
// with a periodic damage effect), 0 when none: T Player::GetSpellAuraHolderMap with SpellAuraHolder's
// persistent/area tests; F the applied auras, the area test on the aura and the persistent-area effect on its
// spell. Reads the bot's own aura list only.
inline uint32 HarmfulGroundAuraCompat(Player* bot)
{
    if (!bot)
        return 0;
    ObjectGuid const self = bot->GetGUID();
    for (auto const& entry : AuraHolderMapCompat(bot))
    {
        Aura* aura = entry.second;
        if (!aura || AuraIsPositiveCompat(aura) || aura->GetCasterGUID() == self)
            continue;
        SpellEntry const* spell = GetSpellEntryCompat(aura->GetSpellProto());
        if (!spell)
            continue;
        if (!AuraIsAreaCompat(aura) && !IsSpellHaveEffect(spell, SPELL_EFFECT_PERSISTENT_AREA_AURA))
            continue;
        for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
            if (spell->EffectApplyAuraName[i] == SPELL_AURA_PERIODIC_DAMAGE ||
                spell->EffectApplyAuraName[i] == SPELL_AURA_PERIODIC_DAMAGE_PERCENT)
                return spell->Id;
    }
    return 0;
}

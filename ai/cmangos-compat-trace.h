// The Turtle side of the both-core helpers the ForeverCore port added for the run trace and the why trace
// (runtime/RunTrace.cpp, runtime/WhyTrace.cpp). Same names and answers as forever/src/compat/TbCompat_TRACE.h;
// each is the expression the bot already made on Turtle.
#pragma once

#include "Chat.h"
#include "Player.h"

#include <cstdint>
#include <cstdio>
#include <string>

// T Player::GetSpellCooldownDelay: the remaining cooldown in whole seconds.
inline uint32 SpellCooldownDelaySecondsCompat(Player* player, uint32 spellId)
{
    return player ? static_cast<uint32>(player->GetSpellCooldownDelay(spellId)) : 0;
}

// T HostileReference::getTarget / getThreat.
template <class Fn>
inline void ForEachThreatListEntryCompat(Unit* attacker, Fn&& fn)
{
    for (HostileReference* ref : attacker->GetThreatManager().getThreatList())
        if (ref)
            fn(ref->getTarget(), ref->getThreat());
}

inline void TraceSendSysMessageCompat(Player* player, std::string const& text)
{
    if (player)
        ChatHandler(player).PSendSysMessage("%s", text.c_str());
}

// T Player::GetSpellAuraHolderMap plus SpellAuraHolder's persistent/area tests and the effect scan.
inline uint32 HarmfulGroundAuraCompat(Player* bot)
{
    if (!bot)
        return 0;
    ObjectGuid const self = bot->GetObjectGuid();
    for (auto const& entry : AuraHolderMapCompat(bot))
    {
        SpellAuraHolder* holder = entry.second;
        if (!holder || holder->IsPositive() || holder->GetCasterGuid() == self)
            continue;
        if (!holder->IsPersistent() && !holder->IsAreaAura())
            continue;
        SpellEntry const* spell = holder->GetSpellProto();
        if (!spell)
            continue;
        for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
            if (spell->EffectApplyAuraName[i] == SPELL_AURA_PERIODIC_DAMAGE ||
                spell->EffectApplyAuraName[i] == SPELL_AURA_PERIODIC_DAMAGE_PERCENT)
                return spell->Id;
    }
    return 0;
}

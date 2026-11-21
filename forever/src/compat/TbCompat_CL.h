// ForeverCore side of the both-core chat link helpers of round S1-R1. Turtle side: ai/cmangos-compat-cl.h (the bodies the bot always had).
// Found through ai/playerbot/TbCompatCL.h. Notes: forever/docs/S1-R1-NOTES.md.
//
// Both helpers answer the question the 1.12 ChatHandler answered, from one core-free parser (compat/TbChatLinkPure.h). What this core
// cannot read answers nothing, never a guess: talents and trade links answer 0, and an item carries no random property.
#pragma once

#include "TbChatLinkPure.h"

#include <string>
#include <string_view>

// A spell or recipe id from a chat link, or from a bare number. The handler is there so both cores see the same signature: this side does not
// ask it (ForeverCore's ChatHandler has no ExtractSpellIdFromLink; the link layouts it accepts are in HyperlinkTags.cpp at the core pin).
template <class Handler>
inline std::uint32_t ExtractSpellIdCompat(Handler* /*handler*/, std::string str) { return TortoiseBots::chatlink::SpellId(str); }

// An item link's qualifier in the layout the bot's ItemQualifier reads. The modern link's field 3 is a gem, 1.12's is a random property:
// different fields, so the qualifier is normalized (see the parser).
inline std::string ItemLinkQualifierCompat(std::string_view qualifier) { return TortoiseBots::chatlink::LegacyItemQualifier(qualifier); }

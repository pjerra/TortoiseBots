// The Turtle side of the both-core chat link helpers round S1-R1. Same names and signatures as forever/src/compat/TbCompat_CL.h; each body
// is the expression the bot made before the move (PlayerbotAI.h's extractSpellId, ChatHelper::parseItemQualifiers), not changed: Turtle's
// ChatHandler and item links keep the 1.12 layouts, so a qualifier and a spell id are read exactly as they were.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

template <class Handler>
inline std::uint32_t ExtractSpellIdCompat(Handler* handler, std::string str)
{
    char* source = (char*)str.c_str();
    return handler->ExtractSpellIdFromLink(&source);
}

inline std::string ItemLinkQualifierCompat(std::string_view qualifier) { return std::string(qualifier); }

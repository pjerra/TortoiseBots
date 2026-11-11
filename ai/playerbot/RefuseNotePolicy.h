#pragma once

#include <cstdint>
#include <string>

// Why an action did not run when no cast check said so: a multiplier zeroed it, or its own safety guard refused.
// The engine keeps one code per verdict (Engine::NoteOutcome); these sit above every SpellCastResult.
namespace ai
{
    enum class RefuseNote : std::uint8_t { None = 0, Threat = 1, Veto = 2, Attacked = 3 };
    constexpr std::uint32_t kRefuseNoteBase = 0x10000;
    inline std::uint32_t OutcomeFailCode(std::uint32_t castFail, RefuseNote note)
    {
        return note != RefuseNote::None ? kRefuseNoteBase + static_cast<std::uint32_t>(note) : castFail;
    }
    // The note for a multiplier veto: the threat multiplier ("threat") has its own word, every other one is "veto".
    inline RefuseNote VetoNoteFor(const std::string& multiplierName)
    {
        return multiplierName == "threat" ? RefuseNote::Threat : RefuseNote::Veto;
    }
    // "threat", "veto", "attacked", or nullptr for a code that is no refuse note.
    inline const char* RefuseNoteWord(std::uint32_t code)
    {
        switch (code)
        {
            case kRefuseNoteBase + 1: return "threat";
            case kRefuseNoteBase + 2: return "veto";
            case kRefuseNoteBase + 3: return "attacked";
            default: return nullptr;
        }
    }
}

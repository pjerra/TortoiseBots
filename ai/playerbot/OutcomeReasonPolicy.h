#pragma once

#include <cstdint>

// The tail of the party record's reason word (ViewOutcomeReason in
// commands/Commander.cpp): what an outcome code that is no refuse note and no
// known cast failure reads as. The core's SPELL_CAST_OK (0xFF, SpellDefines.h)
// means no cast check failed, so it reads as no reason; 0 is the core's
// SPELL_FAILED_AFFECTING_COMBAT and reads "combat". Any other unmapped code
// reads "other". Tested in tools/test_outcome_reason_policy.cpp.
namespace ai
{
    constexpr std::uint32_t kOutcomeNoCheck = 0xFF;

    inline const char* OutcomeTailWord(std::uint32_t code)
    {
        if (code == kOutcomeNoCheck)
            return nullptr;
        if (code == 0)
            return "combat";
        return "other";
    }
}

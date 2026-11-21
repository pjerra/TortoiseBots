// The deterministic rolls the standalone pool test uses (`urand`): every call answers the next value of `fakeRandomValue`, inside
// the range, so the composition and the genders are the same on every run.
#pragma once

#include "Define.h"

#include <vector>

inline uint32 fakeRandomValue = 1;

inline uint32 urand(uint32 min, uint32 max)
{
    if (max <= min)
        return min;
    return min + (fakeRandomValue++ % (max - min + 1));
}

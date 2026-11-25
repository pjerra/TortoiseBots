// getMSTime as the add-on hint reads it: the test owns the clock.
#pragma once

#include "FakeWorld.h"

inline uint32_t getMSTime()
{
    return fakeworld::nowMs;
}

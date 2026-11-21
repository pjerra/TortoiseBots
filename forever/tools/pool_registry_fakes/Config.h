// The config surface the pool uses (ForeverCore's Config.h defines ConfigMgr and the `sConfigMgr` singleton). The test sets the
// keys it wants through `sConfigMgr->bools`/`ints`; a key that is not set answers the caller's fallback, like the core does.
#pragma once

#include "Define.h"

#include <map>
#include <string>

class FakeConfigMgr
{
public:
    bool GetBoolDefault(char const* key, bool fallback) const
    {
        auto const found = bools.find(key);
        return found == bools.end() ? fallback : found->second;
    }

    int32 GetIntDefault(char const* key, int32 fallback) const
    {
        auto const found = ints.find(key);
        return found == ints.end() ? fallback : found->second;
    }

    std::map<std::string, bool> bools;
    std::map<std::string, int32> ints;
};

inline FakeConfigMgr fakeConfig;
inline FakeConfigMgr* sConfigMgr = &fakeConfig;

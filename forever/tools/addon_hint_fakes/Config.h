// The config surface the add-on hint reads. The test sets the keys it wants; a key that is not set answers the
// caller's fallback, like the core does.
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

    std::string GetStringDefault(char const* key, std::string fallback) const
    {
        auto const found = strings.find(key);
        return found == strings.end() ? std::move(fallback) : found->second;
    }

    std::map<std::string, bool> bools;
    std::map<std::string, int32> ints;
    std::map<std::string, std::string> strings;
};

inline FakeConfigMgr fakeConfig;
inline FakeConfigMgr* sConfigMgr = &fakeConfig;

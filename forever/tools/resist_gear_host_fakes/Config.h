// The config accessor the host reads a key through: whatever the check sets is what GetIntDefault answers.
#pragma once

#include <map>
#include <string>

class FakeConfigMgr
{
public:
    int GetIntDefault(char const* key, int fallback) const
    {
        auto const it = _values.find(key ? key : "");
        return it == _values.end() ? fallback : it->second;
    }

    void Set(char const* key, int value) { _values[key ? key : ""] = value; }

private:
    std::map<std::string, int> _values;
};

inline FakeConfigMgr* sConfigMgr = nullptr;

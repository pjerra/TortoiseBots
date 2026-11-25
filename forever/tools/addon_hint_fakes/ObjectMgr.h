// normalizePlayerName as the `.tb addonhint` command needs it: the lookups are by the name the command got.
#pragma once

#include <string>

inline bool normalizePlayerName(std::string& name)
{
    if (name.empty())
        return false;
    for (char& c : name)
    {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    return true;
}

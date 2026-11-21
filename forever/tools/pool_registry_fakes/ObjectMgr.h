// The name check ForeverCore's ObjectMgr carries and the WL0 compat header calls; the pool itself only needs it to compile.
#pragma once

#include <set>
#include <string>

enum LocaleConstant : int
{
    LOCALE_enUS = 0,
};

enum CharNameResult : int
{
    CHAR_NAME_SUCCESS = 0,
    CHAR_NAME_FAILURE = 1,
};

// Names the core's own name rules refuse (reserved, profane, malformed): a test adds one.
inline std::set<std::string> fakeRefusedNames;

class ObjectMgr
{
public:
    static int CheckPlayerName(std::string const& name, LocaleConstant /*locale*/, bool /*create*/)
    {
        return fakeRefusedNames.count(name) ? CHAR_NAME_FAILURE : CHAR_NAME_SUCCESS;
    }
};

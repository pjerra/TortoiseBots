// Turtle side of the both-core helpers of the world slice W0 (the pool: registry and creation). Same names and signatures as
// forever/src/compat/TbCompat_WL0.h; the one legacy expression it moves is the name check, with the Turtle call left as it was,
// so the Turtle behaviour is untouched. Found through ai/playerbot/TbCompatWL0.h (`__has_include`).
//
// The pool itself (forever/src/host/pool) is ForeverCore's: decision D1 of FOREVER-W0-W4-SPEC.md keeps the module's legacy pool
// (runtime/RandomBotService.cpp) on Turtle. So the two pool-table helpers have no Turtle caller; they answer a null result, the
// "no rows" a missing table gives, in case one is ever reached from this build.
#pragma once

#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

// The original expression of runtime/BotNameService.cpp: `ObjectMgr::CheckPlayerName(name, true) == CHAR_NAME_SUCCESS`
// (T ObjectMgr.h:1306; ForeverCore adds the locale).
inline bool CheckPlayerNameCompat(std::string const& name)
{
    return ObjectMgr::CheckPlayerName(name, true) == CHAR_NAME_SUCCESS;
}

inline std::unique_ptr<QueryResult> PoolCharacterQueryCompat(char const* /*sql*/, bool* schemaMissing = nullptr)
{
    if (schemaMissing)
        *schemaMissing = true;
    return std::unique_ptr<QueryResult>();
}

inline void PoolCharacterExecuteCompat(std::string const& /*sql*/) {}

// The authoritative character reads have no Turtle caller either (the pool is ForeverCore's, decision D1): false = "the
// database did not answer", which keeps the caller from acting on a guess.
struct PoolCoreCharacter
{
    std::uint64_t guid = 0;
    std::uint32_t accountId = 0;
    std::string name;
    bool online = false;
};

inline bool PoolCharacterOwnerCompat(std::string const& /*name*/, bool& found, std::uint64_t& guid, std::uint32_t& accountId)
{
    found = false;
    guid = 0;
    accountId = 0;
    return false;
}

inline bool PoolAccountCharactersCompat(std::uint32_t /*accountId*/, std::vector<PoolCoreCharacter>& out)
{
    out.clear();
    return false;
}

inline bool PoolCharactersOnAccountsCompat(std::set<std::uint32_t> const& /*accountIds*/, std::uint64_t& count)
{
    count = 0;
    return false;
}

inline bool PoolRealmCharacterCountCompat(std::uint32_t /*accountId*/, std::uint64_t& count)
{
    count = 0;
    return false;
}

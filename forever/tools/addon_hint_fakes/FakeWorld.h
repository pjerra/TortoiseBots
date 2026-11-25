// The world the standalone add-on hint test drives (forever/tools/test_addon_hint_live.cpp): the fakes in this
// directory answer the core surface host/TbHostAddonHint.cpp names, and every observable thing (the clock, the
// characters store, connected players, sent lines, lookups) lives here so the test can read it.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

class Player;

namespace fakeworld
{
// The characters store's rows and every guarded statement the live half issued.
inline std::set<uint64_t> storedFlags;
inline std::vector<std::string> executed;

// The world tick's inputs.
inline uint32_t nowMs = 0;
inline bool botMgrEnabled = true;
inline bool schemaAvailable = true;
inline std::set<uint64_t> bots;
inline int connectedLookups = 0;

// Lines the hint sent, and a hook the lock test parks inside SendSysMessage.
inline std::vector<std::string> sent;
inline std::function<void()> sendHook;

inline std::map<uint64_t, Player*> online;

inline void Reset()
{
    storedFlags.clear();
    executed.clear();
    nowMs = 0;
    botMgrEnabled = true;
    schemaAvailable = true;
    bots.clear();
    connectedLookups = 0;
    sent.clear();
    sendHook = nullptr;
    online.clear();
}
} // namespace fakeworld

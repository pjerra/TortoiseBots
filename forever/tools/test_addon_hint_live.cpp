// The add-on hint's live half (forever/src/host/TbHostAddonHint.cpp) driven against the fakes in
// forever/tools/addon_hint_fakes/ — the same shape as cmd_binding_store_check.cpp: the real translation unit is
// compiled, so the seen sweep, the decision, the send and the characters-store write run as they do on the core.
//
// What it proves behaviourally (a source-text grep cannot):
//   * a TBM message before the tick wins: no line, and the row is written;
//   * two due players in one tick produce two lines and ONE batched INSERT (not one round trip each);
//   * an elevated account gets no line and no row, while a normal player in the same tick is told;
//   * the seen-check, the decision and the send share the one seenMutex: a NoteAddonSeen on another thread cannot
//     return while SendSysMessage runs (the wiring test compiles a copy that unlocks before the send and this
//     test must then fail);
//   * the row is written only once the hint really went out.
//
//   g++ -std=c++17 -O1 -Wall -Wextra -pthread -I forever/tools/addon_hint_fakes -I forever/src -I .
//       forever/tools/test_addon_hint_live.cpp forever/src/host/TbHostAddonHint.cpp -o /tmp/test_addon_hint_live
#include "FakeWorld.h"

#include "Config.h"
#include "ObjectGuid.h"
#include "Player.h"

#include "host/TbHostAddonHint.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <thread>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)

namespace
{
std::map<uint64_t, Player> players;

Player* AddPlayer(uint64_t key, int security)
{
    Player& player = players[key];
    player.guid = ObjectGuid::Create<HighGuid::Player>(key);
    player.session.security = security;
    fakeworld::online[key] = &player;
    return &player;
}

void Login(uint64_t key, uint32_t nowMs)
{
    fakeworld::nowMs = nowMs;
    TortoiseBots::addonhint::NoteLogin(&players.at(key));
}

size_t Executions()
{
    return fakeworld::executed.size();
}

bool LastStatementMentions(std::string const& token)
{
    return !fakeworld::executed.empty() && fakeworld::executed.back().find(token) != std::string::npos;
}
} // namespace

int main()
{
    using namespace TortoiseBots::addonhint;

    fakeworld::Reset();
    sConfigMgr->bools["TortoiseBots.Player.AddonHint"] = true;
    sConfigMgr->ints["TortoiseBots.Player.AddonHintDelay"] = 10;
    LoadConfig();
    LoadAtStartup();

    // 1. A TBM message before the decision wins: no line, and the row is written.
    {
        AddPlayer(42, SEC_PLAYER);
        Login(42, 1000);
        fakeworld::nowMs = 2000;
        NoteAddonSeen(42, 0);
        size_t const before = Executions();
        fakeworld::nowMs = 20000;
        Tick(20000);
        CHECK(fakeworld::sent.empty());
        CHECK(Executions() == before + 1);
        CHECK(LastStatementMentions("42"));
        CHECK(fakeworld::storedFlags.count(42) == 1);
        Tick(21000);
        CHECK(Executions() == before + 1);
        CHECK(fakeworld::sent.empty());
    }

    // 2. Two players due in one tick: two lines, one batched statement.
    {
        AddPlayer(46, SEC_PLAYER);
        AddPlayer(47, SEC_PLAYER);
        Login(46, 30000);
        Login(47, 31000);
        size_t const before = Executions();
        Tick(42000);
        CHECK(fakeworld::sent.size() == 2);
        CHECK(fakeworld::sent.back().find("add-on") != std::string::npos);
        CHECK(Executions() == before + 1);
        CHECK(LastStatementMentions("46"));
        CHECK(LastStatementMentions("47"));
        CHECK(fakeworld::storedFlags.count(46) == 1);
        CHECK(fakeworld::storedFlags.count(47) == 1);
    }

    // 3. An elevated account gets no line and no row; a normal player in the same tick does.
    {
        AddPlayer(48, SEC_GAMEMASTER);
        AddPlayer(49, SEC_PLAYER);
        Login(48, 50000);
        Login(49, 50000);
        size_t const sentBefore = fakeworld::sent.size();
        size_t const rowsBefore = fakeworld::storedFlags.size();
        fakeworld::connectedLookups = 0;
        Tick(61000);
        CHECK(fakeworld::sent.size() == sentBefore + 1);
        CHECK(fakeworld::storedFlags.count(48) == 0);
        CHECK(fakeworld::storedFlags.count(49) == 1);
        CHECK(fakeworld::storedFlags.size() == rowsBefore + 1);
        CHECK(fakeworld::connectedLookups == 2);
        // Both entries left the pending set: the tick's early return proves it.
        fakeworld::connectedLookups = 0;
        Tick(62000);
        CHECK(fakeworld::connectedLookups == 0);
    }

    // 4. The seen-check, the decision and the send share one mutex: a NoteAddonSeen from another thread cannot
    //    return while the send is running. A copy that unlocks before the send fails here.
    {
        AddPlayer(50, SEC_PLAYER);
        Login(50, 70000);
        size_t const sentBefore = fakeworld::sent.size();

        std::mutex mutex;
        std::condition_variable cv;
        bool sendEntered = false;
        bool workerTried = false;
        bool workerDone = false;
        bool overlap = false;

        fakeworld::sendHook = [&]() {
            {
                std::lock_guard<std::mutex> lock(mutex);
                sendEntered = true;
            }
            cv.notify_all();
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait_for(lock, std::chrono::seconds(2), [&] { return workerTried; });
            overlap = cv.wait_for(lock, std::chrono::seconds(1), [&] { return workerDone; });
        };

        std::thread worker([&]() {
            {
                std::unique_lock<std::mutex> lock(mutex);
                cv.wait_for(lock, std::chrono::seconds(2), [&] { return sendEntered; });
                workerTried = true;
            }
            cv.notify_all();
            NoteAddonSeen(50, 0);
            {
                std::lock_guard<std::mutex> lock(mutex);
                workerDone = true;
            }
            cv.notify_all();
        });

        Tick(81000);
        worker.join();
        fakeworld::sendHook = nullptr;
        CHECK(fakeworld::sent.size() == sentBefore + 1);
        CHECK(workerTried);
        CHECK(!overlap);
        Tick(82000);
        CHECK(fakeworld::sent.size() == sentBefore + 1);
    }

    // 5. The row is written only after the line really went out.
    {
        AddPlayer(51, SEC_PLAYER);
        Login(51, 90000);
        size_t const before = Executions();
        Tick(95000);
        CHECK(fakeworld::sent.size() == 4);
        CHECK(Executions() == before);
        CHECK(fakeworld::storedFlags.count(51) == 0);
        Tick(101000);
        CHECK(fakeworld::sent.size() == 5);
        CHECK(Executions() == before + 1);
        CHECK(fakeworld::storedFlags.count(51) == 1);
    }

    if (failures)
    {
        std::printf("test_addon_hint_live: %d failed\n", failures);
        return 1;
    }
    std::printf("test_addon_hint_live: all checks passed\n");
    return 0;
}

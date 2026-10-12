#ifndef _PlayerbotDbStore_H
#define _PlayerbotDbStore_H

#include "Common.h"
#include "PlayerbotAIBase.h"

class PlayerbotAI;

class PlayerbotDbStore
{
public:
    PlayerbotDbStore() {}
    virtual ~PlayerbotDbStore() {}
    // Serial id for every transaction on ai_playerbot_db_store. A snapshot
    // save is DELETE-by-guid then INSERTs; under REPEATABLE READ the DELETE
    // gap-locks the non-unique guid index up to the next guid, so two bots
    // with neighbouring guids saving on different DB workers deadlock
    // (MySQL error 1213) and one save is rolled back and lost. One constant
    // id sends them all to one worker, which runs them one at a time in
    // queue order, so they never overlap and one bot's saves stay in order.
    static constexpr uint32 DB_SERIAL_ID = 1;

    static PlayerbotDbStore& instance()
    {
        static PlayerbotDbStore instance;
        return instance;
    }

    void Save(PlayerbotAI *ai, std::string preset = "");
    void Load(PlayerbotAI *ai, std::string preset = "");
    void Reset(PlayerbotAI *ai, std::string preset = "");
    // Talent topology changes invalidate complete co/nc/dead/react snapshots,
    // but independent value rows (formation, targets, and other durable AI
    // state) remain valid and must be retained.
    void InvalidateStrategySnapshots(PlayerbotAI *ai);

private:
    void SaveValue(uint64 guid, std::string preset, std::string key, std::string value);
    std::string FormatStrategies(std::string type, std::list<std::string_view> strategies);
};

#define sPlayerbotDbStore PlayerbotDbStore::instance()

#endif

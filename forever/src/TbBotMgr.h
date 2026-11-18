// Owns the bot sessions and runs the spike behaviour (follow the master, help it fight), or, for a bot marked
// "engine" in a build with TORTOISEBOTS_SLICE1, the real PlayerbotAI engine (host/TbEngineBrain.h).
// Everything here runs on the world thread (WorldScript::OnBeforeMapsUpdate, chat/console commands and the
// PlayerScript logout hook).
#pragma once

#include "ObjectGuid.h"
#include "ai/playerbot/FollowTrailPolicy.h"   // shared with the Turtle build, no core includes
#include "host/TbEngineBrain.h"                 // the engine brain: opaque here, no legacy header
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Player;

namespace TortoiseBots
{
class BotSession;

class BotMgr
{
public:
    static BotMgr& Instance();

    void LoadConfig();
    bool Enabled() const { return _enabled; }

    // Empty string = started. Otherwise the reason it was refused. An empty master = the bot stands where it is.
    std::string Login(ObjectGuid characterGuid, ObjectGuid master);

    // Makes a new level 1 character on the account and saves it. Empty string = done.
    std::string CreateCharacter(uint32 accountId, std::string const& name, uint8 race, uint8 playerClass, uint8 gender);

    // Console test helpers for a bot without a master.
    bool WalkBy(ObjectGuid characterGuid, float dx, float dy);
    std::string AttackNearest(ObjectGuid characterGuid, float range);
    bool Logout(ObjectGuid characterGuid, char const* reason);
    void LogoutAll(char const* reason);

    // `.tb engine <character> on|off`: Empty string = done, else the reason it was refused.
    std::string SetEngine(ObjectGuid characterGuid, bool on);
    // Lines for `.tb where`: the first is `brain: engine` or `brain: spike`, then (engine only) state, strategies, target.
    std::vector<std::string> DescribeBrain(ObjectGuid characterGuid) const;
    // PlayerScript::OnLogout: the Player is about to be deleted, the engine must go first.
    void OnPlayerLogout(Player* player);

    Player* FindBot(ObjectGuid characterGuid) const;
    // The in-world bots whose master is `master` (host/TbHostBotManager.cpp answers the legacy BotManager::GetBotsForMaster with it).
    std::vector<Player*> BotsOfMaster(ObjectGuid master) const;
    // Re-binds a bot's master (empty = none). False when it is not a logged-in bot. The engine's own master pointer is the
    // caller's (host/TbHostBotManager.cpp BindBotMaster/ClearBotMaster set it too).
    bool SetMaster(ObjectGuid characterGuid, ObjectGuid master);
    bool IsBot(ObjectGuid characterGuid) const { return _bots.count(characterGuid) != 0; }
    std::size_t Count() const { return _bots.size(); }

    void Update(uint32 diff);

private:
    struct Bot
    {
        std::shared_ptr<BotSession> session;
        ObjectGuid master;
        ObjectGuid chasing;          // the unit the bot was last sent after
        bool following = false;
        bool placed = false;         // moved next to the master after login
        uint32 ownerLoadingMs = 0;   // how long the account's real session has been loading a character
        uint32 thinkMs = 0;
        bool useEngine = false;      // marked "engine": runs the real PlayerbotAI instead of the spike's follow/assist
        bool engineFailed = false;   // the brain could not be built: the bot stays on the spike behaviour until re-marked
        std::string engineNote;      // why, for `.tb where`
        std::unique_ptr<engine::Brain, engine::BrainDeleter> brain;
        std::vector<ai::FollowTrailPoint> trail;   // where the master walked, for catching up around corners
    };

    void Think(Bot& bot, Player* me);
    void DropBrain(Bot& bot);

    std::unordered_map<ObjectGuid, Bot> _bots;
    bool _enabled = false;
    float _followDistance = 3.0f;
    bool _engineByDefault = false;
};
}

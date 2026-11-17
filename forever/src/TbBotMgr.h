// Owns the bot sessions and runs the spike behaviour (follow the master, help it fight).
// Everything here runs on the world thread (WorldScript::OnBeforeMapsUpdate and chat commands).
#pragma once

#include "ObjectGuid.h"
#include "ai/playerbot/FollowTrailPolicy.h"   // shared with the Turtle build, no core includes
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

    Player* FindBot(ObjectGuid characterGuid) const;
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
        std::vector<ai::FollowTrailPoint> trail;   // where the master walked, for catching up around corners
    };

    void Think(Bot& bot, Player* me);

    std::unordered_map<ObjectGuid, Bot> _bots;
    bool _enabled = false;
    float _followDistance = 3.0f;
};
}

// A WorldSession without a client, for one bot character.
//
// Only PUBLIC WorldSession members are used: the core declares two friend classes for Karrz's own bot module, and this
// class is not one of them. That is on purpose (see docs/SPIKE.md, "private members").
#pragma once

#include "ObjectGuid.h"
#include "WorldSession.h"
#include <string>
#include <string_view>

namespace TortoiseBots
{
class BotSession final : public WorldSession
{
public:
    BotSession(uint32 accountId, std::string accountName);
    ~BotSession() override = default;

    BotSession(BotSession const&) = delete;
    BotSession& operator=(BotSession const&) = delete;

    // Starts the asynchronous character load. False when a load is already running or the query set cannot be built.
    bool BeginLogin(ObjectGuid characterGuid);

    // One world tick. Returns false once the session is finished and can be dropped by the owner.
    bool Tick(uint32 diff);

    // Ask for a clean logout on the next Tick.
    void RequestLogout(std::string_view reason);

    bool LoginPending() const { return _loginPending; }
    // A logout was asked for: the next Tick logs the Player out (BotMgr drops the engine brain before that).
    bool LogoutRequested() const { return _logoutRequested; }
    ObjectGuid GetCharacterGuid() const { return _characterGuid; }

    // Bots have no client: everything the server sends is dropped here. Later this is where the bot "reads" the server.
    void SendPacket(WorldPacket const* packet, bool forced = false) override;

private:
    void AcknowledgeTeleports();

    ObjectGuid _characterGuid;
    bool _loginPending = false;
    bool _logoutRequested = false;
    bool _sawPlayer = false;
};
}

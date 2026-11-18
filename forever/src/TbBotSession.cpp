#include "TbBotSession.h"
#include "ClientBuildInfo.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "LoginQueryHolder.h"
#include "MovementPackets.h"
#include "Opcodes.h"
#include "Player.h"
#include "WorldPacket.h"
#include "WorldSocket.h"

namespace TortoiseBots
{
BotSession::BotSession(uint32 accountId, std::string accountName)
    : WorldSession(accountId, std::move(accountName), accountId, std::string(), std::shared_ptr<WorldSocket>(), SEC_PLAYER,
        EXPANSION_LEVEL_CURRENT, 0, std::string(), Minutes(0), 0, ClientBuild::VariantId{}, LOCALE_enUS, 0, false,
        /*isBot*/ true)
{
    // A normal session gets this from World::AddSession_. Bot sessions are not in the world's session map, so the
    // account-wide data (collections, account data, tutorials) is loaded here. The handshake packets it sends are dropped.
    InitializeSession();
}

bool BotSession::BeginLogin(ObjectGuid characterGuid)
{
    if (_loginPending || GetPlayer())
        return false;

    std::shared_ptr<LoginQueryHolder> holder = std::make_shared<LoginQueryHolder>(GetAccountId(), characterGuid);
    if (!holder->Initialize())
    {
        TC_LOG_ERROR("tortoisebots", "BotSession: login query set failed for {}", characterGuid.ToString());
        return false;
    }

    _characterGuid = characterGuid;
    _loginPending = true;

    // The same path a client login takes after CMSG_PLAYER_LOGIN: load, then WorldSession::HandlePlayerLogin puts the
    // Player in the world. The callback runs from WorldSession::Update (our Tick), on the world thread.
    AddQueryHolderCallback(CharacterDatabase.DelayQueryHolder(holder)).AfterComplete([this](SQLQueryHolderBase const& done)
    {
        _loginPending = false;
        HandlePlayerLogin(static_cast<LoginQueryHolder const&>(done));
        if (!GetPlayer())
        {
            TC_LOG_ERROR("tortoisebots", "BotSession: character {} did not load", _characterGuid.ToString());
            _logoutRequested = true;
        }
    });

    return true;
}

void BotSession::SendPacket(WorldPacket const* /*packet*/, bool /*forced*/)
{
}

void BotSession::RequestLogout(std::string_view reason)
{
    TC_LOG_INFO("tortoisebots", "BotSession: logout asked for {} ({})", _characterGuid.ToString(), reason);
    _logoutRequested = true;
}

void BotSession::AcknowledgeTeleports()
{
    Player* player = GetPlayer();
    if (!player)
        return;

    switch (player->GetTeleportState())
    {
        case TeleportState::WaitingForTeleportAck:
        {
            // R10 (S3): the engine sees a same-map teleport BEFORE it is acknowledged (see SetTeleportAckHook): PlayerbotAI::HandleTeleportAck
            // acknowledges it itself, as on Turtle (AckNearTeleportCompat), and its landing logic reads IsBeingTeleportedNear(). Only a teleport still
            // pending afterwards (no engine, or the engine did not ack) is acknowledged here.
            if (_teleportAckHook)
            {
                _teleportAckHook();
                player = GetPlayer();
                if (!player || player->GetTeleportState() != TeleportState::WaitingForTeleportAck)
                    break;
            }
            // What the client answers to a teleport inside the map.
            WorldPackets::Movement::MoveTeleportAck ack{ WorldPacket(CMSG_MOVE_TELEPORT_ACK) };
            ack.MoverGUID = player->GetGUID();
            HandleMoveTeleportAck(ack);
            break;
        }
        case TeleportState::WaitingForSuspendTokenResponse:
        case TeleportState::WaitingForWorldPortAck:
            // Teleport to another map: the client would answer the suspend token and then the new-world packet.
            if (!player->IsInWorld())
            {
                HandleMoveWorldportAck();
                // R10 (S3): the engine hears of it once the Player is in the new world again (its bookkeeping and Reset() need a Player that is in the world;
                // the engine's own far-teleport branch, the ack itself, is already done above).
                if (_teleportAckHook)
                    _teleportAckHook();
            }
            break;
        default:
            break;
    }
}

bool BotSession::Tick(uint32 diff)
{
    // The base Update is safe without a socket: it reads no packets, runs the finished database callbacks (our login)
    // and the timed logout. It returns false for a session without a socket; that only means "drop me from the world's
    // session map", which a bot session is not in.
    WorldSessionFilter filter(this);
    WorldSession::Update(diff, filter);

    if (Player* player = GetPlayer())
    {
        _sawPlayer = true;

        // The core shows a player nothing (Player::CanNeverSee) until its client has reported its clock after the
        // loading screen (CMSG_MOVE_INIT_ACTIVE_MOVER_COMPLETE -> time sync). Without this a bot is in the world but
        // can see, and so attack or target, nothing. A bot has no client clock: the server's own time is its time.
        if (player->IsInWorld() && !player->HasPlayerLocalFlag(PLAYER_LOCAL_FLAG_OVERRIDE_TRANSPORT_SERVER_TIME))
        {
            player->SetPlayerLocalFlag(PLAYER_LOCAL_FLAG_OVERRIDE_TRANSPORT_SERVER_TIME);
            player->SetTransportServerTime(0);
            player->UpdateObjectVisibility(false);
        }
    }

    AcknowledgeTeleports();

    if (_logoutRequested && !_loginPending)
    {
        if (GetPlayer())
            LogoutPlayer(true);
        return false;
    }

    // The core logged the player out by itself (for example the owner logged in to this character).
    if (_sawPlayer && !GetPlayer() && !PlayerLoading())
        return false;

    return true;
}
}

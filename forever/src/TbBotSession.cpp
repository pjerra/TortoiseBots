#include "TbBotSession.h"
#include "ByteBuffer.h"
#include "ClientBuildInfo.h"
#include "CombatLogPacketsCommon.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "LoginQueryHolder.h"
#include "MovementPackets.h"
#include "Opcodes.h"
#include "Player.h"
#include "WorldPacket.h"
#include "Spell.h"
#include "SpellPackets.h"
#include "WorldSocket.h"

namespace TortoiseBots
{
namespace
{
// The bot session whose engine is being ticked on this thread (BotSession::EngineTickScope), or null.
thread_local BotSession const* t_tickingSession = nullptr;
// True while InjectCastResult sends its packet, so the queued entry is marked as injected.
thread_local bool t_injecting = false;
}

BotSession::EngineTickScope::EngineTickScope(BotSession const* session) : _previous(t_tickingSession)
{
    t_tickingSession = session;
}

BotSession::EngineTickScope::~EngineTickScope()
{
    t_tickingSession = _previous;
}

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

void BotSession::SendPacket(WorldPacket const* packet, bool /*forced*/)
{
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
    // Everything is dropped except the two packets that tell the bot its own cast ended badly. The opcode test comes first and
    // costs one load and two compares for every other packet; only a failure packet is copied and read.
    if (packet && _castResultSink.load(std::memory_order_relaxed))
    {
        uint32 const opcode = packet->GetOpcode();
        if (opcode == SMSG_SPELL_FAILURE || opcode == SMSG_CAST_FAILED)
            CaptureCastResult(*packet);
    }
#else
    (void)packet;
#endif
}

void BotSession::CaptureCastResult(WorldPacket const& packet)
{
    // The core's own readers read the fields back in the order the core's own Write wrote them (nothing is parsed by hand):
    //   SMSG_SPELL_FAILURE  WorldPackets::Spells::SpellFailure::Write (SpellPackets.cpp:532-542):
    //       ObjectGuid CasterUnit, ObjectGuid CastID, int32 SpellID, SpellCastVisual Visual, uint16 Reason, ObjectGuid FailedBy
    //   SMSG_CAST_FAILED    WorldPackets::Spells::CastFailed::Write (SpellPackets.cpp:555-565):
    //       ObjectGuid CastID, int32 SpellID, SpellCastVisual Visual, int32 Reason, int32 FailedArg1, int32 FailedArg2, ObjectGuid FailedBy
    // The fields after Reason are not read. The packet is copied first: reading moves the read position, and the same packet
    // object is also handed to the other receivers of a broadcast.
    CastResult result;
    try
    {
        WorldPacket in(packet);
        in.rpos(0);
        int32 spellId = 0;
        WorldPackets::Spells::SpellCastVisual visual;
        if (packet.GetOpcode() == SMSG_SPELL_FAILURE)
        {
            uint16 reason = 0;
            in >> result.caster >> result.castId >> spellId >> visual >> reason;
            // A broadcast of somebody else's failure that this bot "sees" is not this bot's cast.
            if (result.caster != _characterGuid)
                return;
            result.kind = CastResult::Kind::SpellFailure;
            result.reason = reason;
        }
        else
        {
            int32 reason = 0;
            in >> result.castId >> spellId >> visual >> reason;
            result.caster = _characterGuid;
            result.kind = CastResult::Kind::CastFailed;
            result.reason = uint32(reason);

            // The engine's own CastSpell call is refused synchronously (Spell::prepare sends the result and returns the same
            // code): the engine has it already, queueing it would make every refusal a second, "asynchronous" event.
            if (t_tickingSession == this)
            {
                result.spellId = uint32(spellId);
                std::lock_guard<std::mutex> lock(_castResultLock);
                ++_castResultCounts.refusedInTick;
                _inTickRefusal = result;    // kept for a helper whose core call returns nothing (Player::CastItemUseSpell, SLICE2-R0-NOTES.md U1)
                _hasInTickRefusal = true;
                return;
            }
        }
        result.spellId = uint32(spellId);
        result.injected = t_injecting;
    }
    catch (ByteBufferException const&)
    {
        std::lock_guard<std::mutex> lock(_castResultLock);
        ++_castResultCounts.malformed;
        return;
    }

    PushCastResult(result);
}

void BotSession::PushCastResult(CastResult const& result)
{
    std::lock_guard<std::mutex> lock(_castResultLock);
    if (_castResults.size() >= CastResultCapacity)
    {
        _castResults.pop_front();
        ++_castResultCounts.dropped;
    }
    _castResults.push_back(result);
    ++_castResultCounts.queued;
}

std::string BotSession::InjectCastResult(uint32 spellId, uint32 reason, bool interrupted)
{
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
    Player* player = GetPlayer();
    if (!player || !player->IsInWorld())
        return "the bot is not in the world";
    if (!CastResultSink())
        return "the cast-result queue is off (the bot has no running engine)";

    // A cast id the core would not produce (its counter starts at 1 per map), recognisable in a log.
    static std::atomic<uint32> counter{ 0x00F00000 };
    ObjectGuid const castId = ObjectGuid::Create<HighGuid::Cast>(SPELL_CAST_SOURCE_NORMAL, player->GetMapId(), spellId, counter.fetch_add(1));

    struct InjectingScope
    {
        InjectingScope() { t_injecting = true; }
        ~InjectingScope() { t_injecting = false; }
    } scope;

    if (interrupted)
    {
        WorldPackets::Spells::SpellFailure packet;
        packet.CasterUnit = _characterGuid;
        packet.CastID = castId;
        packet.SpellID = spellId;
        packet.Reason = uint16(reason);
        player->SendDirectMessage(packet.Write());
    }
    else
    {
        WorldPackets::Spells::CastFailed packet;
        packet.CastID = castId;
        packet.SpellID = int32(spellId);
        packet.Reason = int32(reason);
        player->SendDirectMessage(packet.Write());
    }
    return {};
#else
    (void)spellId; (void)reason; (void)interrupted;
    return "this build has no engine (TORTOISEBOTS_SLICE1 is off)";
#endif
}

bool BotSession::TakeInTickRefusal(CastResult& out)
{
    std::lock_guard<std::mutex> lock(_castResultLock);
    if (!_hasInTickRefusal)
        return false;
    out = _inTickRefusal;
    _hasInTickRefusal = false;
    return true;
}

std::size_t BotSession::DrainCastResults(std::vector<CastResult>& out)
{
    std::lock_guard<std::mutex> lock(_castResultLock);
    std::size_t const count = _castResults.size();
    out.insert(out.end(), _castResults.begin(), _castResults.end());
    _castResults.clear();
    _castResultCounts.delivered += count;
    return count;
}

CastResultCounts BotSession::GetCastResultCounts() const
{
    std::lock_guard<std::mutex> lock(_castResultLock);
    CastResultCounts counts = _castResultCounts;
    counts.pending = uint32_t(_castResults.size());
    return counts;
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
                HandleMoveWorldportAck();
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

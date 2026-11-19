// A WorldSession without a client, for one bot character.
//
// Only PUBLIC WorldSession members are used: the core declares two friend classes for Karrz's own bot module, and this
// class is not one of them. That is on purpose (see docs/SPIKE.md, "private members").
#pragma once

#include "ObjectGuid.h"
#include "WorldSession.h"
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace TortoiseBots
{
// One asynchronous cast result the core sent to a bot's session, reduced to the few fields the engine needs. Copied
// out of the packet on whatever thread the core sent it from (a map update thread for most results) and handed to the
// engine on the world thread (SLICE2-R0-NOTES.md, "The cast-result queue").
struct CastResult
{
    enum class Kind : uint8_t
    {
        SpellFailure,   // SMSG_SPELL_FAILURE whose CasterUnit is the bot: the cast was cancelled (Spell::SendInterrupted)
        CastFailed,     // SMSG_CAST_FAILED: the core refused or aborted the bot's own cast (Spell::SendCastResult)
    };

    Kind kind = Kind::SpellFailure;
    ObjectGuid caster;      // the bot (SMSG_CAST_FAILED names no caster: it is the session's own character)
    ObjectGuid castId;      // the core's cast id, the key that pairs the two packets of one failure
    uint32 spellId = 0;
    uint32 reason = 0;      // the core's SpellCastResult (SMSG_SPELL_FAILURE: uint16 on the wire, SMSG_CAST_FAILED: int32)
    bool injected = false;  // `.tb castfail`, not the core
};

// What `.tb where` prints for a bot.
struct CastResultCounts
{
    uint64_t queued = 0;         // accepted into the queue
    uint64_t delivered = 0;      // handed to the engine by DrainCastResults
    uint64_t dropped = 0;        // the queue was full: the oldest entry was removed to make room
    uint64_t refusedInTick = 0;  // SMSG_CAST_FAILED sent while this bot's own engine tick ran: the engine got that result as the return value of its own call, not queued
    uint64_t malformed = 0;      // a packet that did not read as the core writes it
    uint32_t pending = 0;        // queued, not delivered yet
};

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

    // R10 (S3): the host's way to tell the engine about a teleport the session acknowledges for its Player. Called for a same-map teleport BEFORE
    // the session acknowledges it (the engine's PlayerbotAI::HandleTeleportAck acknowledges it itself, as on Turtle, then runs its landing logic while the
    // core still reports the teleport; the session acknowledges only what is still pending afterwards), and for a far teleport AFTER the session has
    // completed it (the Player is in the new world again).
    void SetTeleportAckHook(std::function<void()> hook) { _teleportAckHook = std::move(hook); }

    bool LoginPending() const { return _loginPending; }
    // A logout was asked for: the next Tick logs the Player out (BotMgr drops the engine brain before that).
    bool LogoutRequested() const { return _logoutRequested; }
    ObjectGuid GetCharacterGuid() const { return _characterGuid; }

    // Bots have no client: everything the server sends is dropped here, except (in a build with the engine, for a session
    // whose cast-result sink is on) the asynchronous cast results, which are copied into the queue below. Called by the core
    // from any thread that sends to this player, so everything it touches is the queue's mutex or an atomic.
    void SendPacket(WorldPacket const* packet, bool forced = false) override;

    // The cast-result queue (SMSG_SPELL_FAILURE of the bot's own casts, SMSG_CAST_FAILED). Bounded: CastResultCapacity entries;
    // when full the OLDEST entry is removed and counted as dropped. Thread-safe. The engine is the only consumer and drains it
    // on the world thread (host/TbEngineBrain.cpp); nothing in this class calls the engine.
    static constexpr std::size_t CastResultCapacity = 32;
    void SetCastResultSink(bool on) { _castResultSink.store(on, std::memory_order_relaxed); }
    bool CastResultSink() const { return _castResultSink.load(std::memory_order_relaxed); }
    void PushCastResult(CastResult const& result);                  // any thread
    std::size_t DrainCastResults(std::vector<CastResult>& out);     // appends the queued results in arrival order; counts them delivered
    std::size_t PeekCastResults(std::vector<CastResult>& out) const;  // appends a COPY of the queued results; the queue and the counts are untouched (`.tb castlog`)
    CastResultCounts GetCastResultCounts() const;                   // any thread
    // The latest SMSG_CAST_FAILED the core sent during this bot's own engine tick (see EngineTickScope): the answer to a core call
    // that returns nothing, such as Player::CastItemUseSpell. Returns false when there is none since the last take. World thread.
    bool TakeInTickRefusal(CastResult& out);
    // `.tb castfail`: builds the core's own typed packet (WorldPackets::Spells::SpellFailure for `interrupted`, CastFailed
    // otherwise) and sends it through Player::SendDirectMessage -> SendPacket, the path the core takes, so the reader and the
    // queue are exercised end to end. World thread. Empty string = sent, else why not.
    std::string InjectCastResult(uint32 spellId, uint32 reason, bool interrupted);

    // While one of these lives on the world thread, an SMSG_CAST_FAILED sent to `session` is a refusal of the engine's own
    // call (Spell::prepare answers synchronously and the engine reads the same result from its call): it is counted, not queued.
    class EngineTickScope
    {
    public:
        explicit EngineTickScope(BotSession const* session);
        ~EngineTickScope();
        EngineTickScope(EngineTickScope const&) = delete;
        EngineTickScope& operator=(EngineTickScope const&) = delete;
    private:
        BotSession const* _previous;
    };

private:
    void AcknowledgeTeleports();
    void CaptureCastResult(WorldPacket const& packet);

    std::function<void()> _teleportAckHook;
    ObjectGuid _characterGuid;
    bool _loginPending = false;
    bool _logoutRequested = false;
    bool _sawPlayer = false;

    std::atomic<bool> _castResultSink{ false };
    mutable std::mutex _castResultLock;                 // guards _castResults and the counters below
    std::deque<CastResult> _castResults;
    CastResultCounts _castResultCounts;
    CastResult _inTickRefusal;
    bool _hasInTickRefusal = false;
};
}

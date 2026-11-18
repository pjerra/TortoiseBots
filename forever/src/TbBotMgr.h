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
    uint32 MasterGraceSeconds() const { return _masterGraceMs / 1000; }

    // Empty string = started. Otherwise the reason it was refused. An empty master = the bot stands where it is.
    std::string Login(ObjectGuid characterGuid, ObjectGuid master);

    // Makes a new level 1 character on the account and saves it. Empty string = done.
    std::string CreateCharacter(uint32 accountId, std::string const& name, uint8 race, uint8 playerClass, uint8 gender);

    // Console test helpers for a bot without a master.
    bool WalkBy(ObjectGuid characterGuid, float dx, float dy);
    // `.tb hop <bot> <dx> <dy>` (R10 test helper): a teleport of the bot on its own map, that many yards east/north (a short one is a near teleport, the
    // kind the engine's HandleTeleportAck covers). Empty string = started, else the reason.
    std::string Hop(ObjectGuid characterGuid, float dx, float dy);
    // `.tb raid <master>` (R10 test helper): converts the master's party into a raid (the core's Group::ConvertToRaid), to check that the host
    // refuses to add bots to one. Empty string = done, else the reason.
    std::string ConvertToRaid(ObjectGuid masterGuid);
    std::string AttackNearest(ObjectGuid characterGuid, float range);
    bool Logout(ObjectGuid characterGuid, char const* reason);
    void LogoutAll(char const* reason);

    // `.tb group <bot> <master>` for two logged-in characters, and the step `Login` does with a master: puts the bot into the
    // master's party, the master as leader, through the core's Group API (what an invite and its accept do, Handlers/GroupHandler.cpp
    // HandlePartyInviteOpcode / HandlePartyInviteResponseOpcode, without the packets). The bot's host master becomes `master`.
    // Empty string = done, else the reason it was refused. A party holds MAX_GROUP_SIZE (5) members, the master among them.
    std::string GroupWith(ObjectGuid botGuid, ObjectGuid masterGuid);
    // `.tb ungroup <bot>`: the bot leaves its party and has no master afterwards. Empty string = done, else the reason.
    std::string Ungroup(ObjectGuid botGuid);

    // `.tb engine <character> on|off`: Empty string = done, else the reason it was refused.
    std::string SetEngine(ObjectGuid characterGuid, bool on);
    // Lines for `.tb where`: the first is `brain: engine` or `brain: spike`, then (engine only) state, strategies, target.
    std::vector<std::string> DescribeBrain(ObjectGuid characterGuid) const;
    // PlayerScript::OnLogout: the Player is about to be deleted, the engine must go first. A bot whose master is the one leaving is detached
    // (R10, S2): it stays in the world and its party, stops acting, and is logged out when the master has not come back after the grace period.
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
        bool wantParty = false;      // joins the master's party once both are in the world (Login with a master)
        uint32 ownerLoadingMs = 0;   // how long the account's real session has been loading a character
        uint32 thinkMs = 0;
        bool useEngine = false;      // marked "engine": runs the real PlayerbotAI instead of the spike's follow/assist
        bool engineFailed = false;   // the brain could not be built: the bot stays on the spike behaviour until re-marked
        std::string engineNote;      // why, for `.tb where`
        std::unique_ptr<engine::Brain, engine::BrainDeleter> brain;
        std::vector<ai::FollowTrailPoint> trail;   // where the master walked, for catching up around corners
        // R10 (S2): the master logged out. The bot keeps its guid in `master`, has no brain, does not act, and waits for the master to be
        // in the world again (then it re-binds and the brain is built again) or for `graceLeftMs` to run out (then it logs out).
        bool masterAway = false;
        bool masterSeenGone = false; // the master was seen missing from the world at least once since it went away (the logout itself runs in the same update)
        uint32 graceLeftMs = 0;
    };

    void Think(Bot& bot, Player* me);
    static std::string JoinParty(Player* bot, Player* master);
    void DropBrain(Bot& bot);
    // R10 (S2): the master is gone: take the brain down and wait out the grace (or log the bot out at once when the grace is 0).
    void DetachFromMaster(Bot& bot);
    // R10 (S2): one update of a bot whose master is away. True when the master is back in the world and the bot has been re-bound.
    bool UpdateMasterAway(Bot& bot, uint32 diff);

    std::unordered_map<ObjectGuid, Bot> _bots;
    bool _enabled = false;
    float _followDistance = 3.0f;
    bool _engineByDefault = false;
    uint32 _masterGraceMs = 300000;   // TortoiseBots.Slice.MasterGraceSeconds
};
}

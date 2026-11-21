// Owns the bot sessions and runs the spike behaviour (follow the master, help it fight), or, for a bot marked
// "engine" in a build with TORTOISEBOTS_SLICE1, the real PlayerbotAI engine (host/TbEngineBrain.h).
// Everything here runs on the world thread (WorldScript::OnBeforeMapsUpdate, chat/console commands and the
// PlayerScript logout hook).
#pragma once

#include "ObjectGuid.h"
#include "ai/playerbot/FollowTrailPolicy.h"   // shared with the Turtle build, no core includes
#include "runtime/AdaptiveBudget.h"          // the tick-budget controller, shared with the Turtle build
#include "host/TbEngineBrain.h"                 // the engine brain: opaque here, no legacy header
#include "ai/playerbot/policy/death/GhostRescuePolicy.h"   // W1: when a ghost may be rescued (core-free)
#include "ai/playerbot/policy/world/FreeMapCapPolicy.h"     // F-population: the per-map free-bot cap (core-free)
#include "ai/playerbot/policy/world/PoolShareMobsPolicy.h"  // #24: pool bots leave camps to real players (core-free)
#include <map>
#include <set>
#include <unordered_set>
#include <memory>
#include <string>
#include <unordered_map>
#include <ctime>
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
    bool GuildMailAllowMixed() const { return _guildMailAllowMixed; }
    // Free-bot guild participation (accept/stay/management whispers) and the autonomous founding legs.
    bool GuildsEnabled() const { return _guildsEnabled; }
    bool GuildFormingEnabled() const { return _guildFormingEnabled; }
    // TBF #30: answer a guildless player's whisper asking a bot-run guild for an invite (see host/TbHostGuildRequestPure.h).
    bool GuildAcceptPlayersEnabled() const { return _guildAcceptPlayers; }

    // Empty string = started. Otherwise the reason it was refused. An empty master with `free` = a masterless bot that grinds,
    // loots and corpse-runs on its own (W1); without `free` it stands where it is. `pool` marks a login the pool scheduler
    // started (W0): a pool bot is always a free bot, and IsPoolBot answers for the scheduler. `engine` asks for the real brain
    // on a commanded bot that is neither free nor owned (a hire): on a server with TortoiseBots.Slice.EngineByDefault off, every
    // other commanded bot runs the follow-only spike.
    std::string Login(ObjectGuid characterGuid, ObjectGuid master, bool free = false, bool pool = false, bool owned = false,
        bool engine = false);

    // Makes a new level 1 character on the account the way the core's own creation does (realm character count, creation hooks).
    // Empty string = the saves were QUEUED; the character commit is asynchronous, and only when it succeeds are the login side
    // committed, OnPlayerCreate fired and the character cache told (ConfirmCreations in Update). A caller that needs a confirmed
    // character reads the database (the pool's registry does; see host/pool/TbPoolCreate.cpp).
    std::string CreateCharacter(uint32 accountId, std::string const& name, uint8 race, uint8 playerClass, uint8 gender);
    // True from CreateCharacter until its creation is complete (the character commit answered, the login side committed and
    // OnPlayerCreate fired). The pool marks a row ready only after that, so a crash in the window never leaves a ready pool
    // character with no `realmcharacters` row.
    bool CreationInFlight(std::string const& name) const;

    // Console test helpers for a bot without a master.
    bool WalkBy(ObjectGuid characterGuid, float dx, float dy);
    // `.tb hop <bot> <dx> <dy>` (R10 test helper): a teleport of the bot on its own map, that many yards east/north (a short one is a near teleport, the
    // kind the engine's HandleTeleportAck covers). Empty string = started, else the reason.
    std::string Hop(ObjectGuid characterGuid, float dx, float dy);
    // `.tb raid <master>` (R10 test helper): converts the master's party into a raid (the core's Group::ConvertToRaid), to check that the host
    // refuses to add bots to one. Empty string = done, else the reason.
    std::string ConvertToRaid(ObjectGuid masterGuid);
    // `.tb enter <character> <mapid>` (R11 test helper): what the core's Map::PlayerCannotEnter says about that character entering that map, with the
    // difficulty it asked for (the party's, or the character's own). Returns the lines to print; the first starts with `allowed` or `refused`.
    std::vector<std::string> CanEnter(ObjectGuid characterGuid, uint32 mapId);
    std::string AttackNearest(ObjectGuid characterGuid, float range);
    bool Logout(ObjectGuid characterGuid, char const* reason);
    void LogoutAll(char const* reason);

    // `.tb group <bot> <master>` for two logged-in characters, and the step `Login` does with a master: puts the bot into the
    // master's party, the master as leader, through the core's Group API (what an invite and its accept do, Handlers/GroupHandler.cpp
    // HandlePartyInviteOpcode / HandlePartyInviteResponseOpcode, without the packets). The bot's host master becomes `master`.
    // Empty string = done, else the reason it was refused. A party holds MAX_GROUP_SIZE (5) members, the master among them; with
    // TortoiseBots.Raid.Enable a sixth member turns it into a raid, which holds MAX_RAID_SIZE (40).
    std::string GroupWith(ObjectGuid botGuid, ObjectGuid masterGuid);
    // `.tb ungroup <bot>`: the bot leaves its party and has no master afterwards. Empty string = done, else the reason.
    std::string Ungroup(ObjectGuid botGuid);

    // W1: a free bot has no master and plays the grind/live kit. `.tb free <bot> on|off`; empty string = done, else the reason.
    std::string SetFree(ObjectGuid characterGuid, bool on);
    bool IsFreeBot(ObjectGuid characterGuid) const { auto itr = _bots.find(characterGuid); return itr != _bots.end() && itr->second.free; }

    // W0: a bot the pool scheduler logged in (Login(..., pool=true)), loading or in the world. The scheduler's own view of its
    // online count and sessions is built from this; `IsFreeBot` tells a pool bot that is still playing free from one that got a
    // master (BindMaster clears `free`, so it leaves the count and is never logged out by the pool).
    bool IsPoolBot(ObjectGuid characterGuid) const { auto itr = _bots.find(characterGuid); return itr != _bots.end() && itr->second.pool; }
    // D-3: an owner's alt (`.bot add`). Never a free bot, so #24 never applies to one (BotFacts::owned).
    bool IsOwnedBot(ObjectGuid characterGuid) const { auto itr = _bots.find(characterGuid); return itr != _bots.end() && itr->second.owned; }

    // #24: TortoiseBots.World.PoolShareMobs.*, read and clamped by LoadConfig; the host table (host/TbHostShareMobs.cpp) reads it.
    world::ShareMobsConfig GetShareMobsConfig() const { return _shareMobs; }

    // W0 (D2): a bot of this manager (pool or not, loading or in the world) sits on that account. One session per account is
    // the rule, so the scheduler counts the account as online and a second bot of it is refused.
    bool AccountHasBot(uint32 accountId) const;
    // D-3: how many owned bots this Battle.net account has in the manager, in any state (the Player.MaxBots cap).
    uint32 OwnedBotsOf(uint32 ownerBnet) const;
    // W0: every account this manager holds a bot on, sorted and unique: the scheduler's once-a-pass account query.
    std::vector<uint32> BotAccounts() const;

    // W1: what `.tb state` keeps of a bot's life: the free flag, kills and deaths since login, ghost rescues and teleports taken,
    // and whether the last revive was a reclaim (0), the spirit healer (1) or a rescue (2).
    struct LifeInfo
    {
        bool free = false;
        uint32 kills = 0;
        uint32 deaths = 0;
        uint32 rescues = 0;
        uint32 teleports = 0;
        bool rescuedThisDeath = false;
        bool wasGhost = false;            // the release teleport was already counted for this death
        time_t diedAt = 0;
        int reviveHow = -1;
        float ghostYd = -1.0f;            // yards to the corpse while a ghost on its map (-1 = no answer)
        std::string phase;                // release | walk | delay | safe-spot | reclaim | healer | rescue (empty = alive)
    };
    LifeInfo GetLifeInfo(ObjectGuid characterGuid) const;
    void NoteKill(ObjectGuid characterGuid);
    // The engine's revive paths call this through TbCompat_W1.h: kind 0 = reclaimed at the body, 1 = the spirit healer.
    void NoteReviveKind(ObjectGuid characterGuid, int kind);
    // `.tb tele` counts in `tele=`: every teleport the bot took, any cause.

    // `.tb engine <character> on|off`: Empty string = done, else the reason it was refused.
    std::string SetEngine(ObjectGuid characterGuid, bool on);
    // Lines for `.tb where`: the first is `brain: engine` or `brain: spike`, then (engine only) state, strategies, target.
    std::vector<std::string> DescribeBrain(ObjectGuid characterGuid) const;
    // PlayerScript::OnLogout: the Player is about to be deleted, the engine must go first. A bot whose master is the one leaving is detached
    // (R10, S2): it stays in the world and its party, stops acting, and is logged out when the master has not come back after the grace period.
    void OnPlayerLogout(Player* player);

    Player* FindBot(ObjectGuid characterGuid) const;
    // TBF-B1 (review finding 1): the bot as the ForeverLua provider reports it. The character while it is in the world OR on a
    // far teleport (a cross-map TeleportTo takes it off its map for a tick), and only while its session has not asked for a
    // logout: the provider keeps such a bot in its snapshot, so a teleport is never a BOT_REMOVED/BOT_ADDED pair. Null = not in
    // the snapshot (loading, out of the world, or leaving). World thread only.
    Player* SnapshotBot(ObjectGuid characterGuid) const;
    // The in-world bots whose master is `master` (host/TbHostBotManager.cpp answers the legacy BotManager::GetBotsForMaster with it).
    std::vector<Player*> BotsOfMaster(ObjectGuid master) const;
    // Re-binds a bot's master (empty = none). False when it is not a logged-in bot. A master and free mode are exclusive: a non-empty
    // one ends free mode then and there (BindMaster). The engine's own master pointer is the caller's (host/TbHostBotManager.cpp
    // BindBotMaster/ClearBotMaster set it too).
    bool SetMaster(ObjectGuid characterGuid, ObjectGuid master);
    bool IsBot(ObjectGuid characterGuid) const { return _bots.count(characterGuid) != 0; }
    // A character ever logged in as a bot (live or since logged out): an offline bot is still a bot, not a real player.
    bool IsKnownBot(ObjectGuid characterGuid) const { return _bots.count(characterGuid) != 0 || _knownBots.count(characterGuid) != 0; }
    // Every logged-in bot (the dashboard roster).
    std::vector<ObjectGuid> BotGuids() const { std::vector<ObjectGuid> out; out.reserve(_bots.size()); for (auto const& [guid, bot] : _bots) out.push_back(guid); return out; }
    std::size_t Count() const { return _bots.size(); }
    // BOTPERF and `.tb stats`: the fleet in one pass over the live table (no world read, no DB).
    struct FleetCounts
    {
        uint32 bots = 0;
        uint32 engines = 0;
        uint32 owned = 0;
        uint32 mastered = 0;
        uint32 free = 0;
        uint32 pool = 0;
    };
    FleetCounts GetFleetCounts() const;
    // H3 (slice 3): the host master guid of a bot (empty = none or not a bot), and what `.tb state` shows of its follow state.
    ObjectGuid MasterOf(ObjectGuid characterGuid) const { auto itr = _bots.find(characterGuid); return itr == _bots.end() ? ObjectGuid::Empty : itr->second.master; }
    // TBF-B1 (review finding 5): the Battle.net account that OWNS the bot's character: its own game account's Battle.net id, cached
    // at login from the session the module already builds (BotSession hands WorldSession Battlenet::AccountMgr::GetIdByGameAccount,
    // the module's one lookup at login: nothing is queried on a game thread here). It is the character's account for every login path
    // (an owner's `.bot add`, a GM's `.bot add` of a foreign character, `.tb login`, a pool login), never the id of whoever added it;
    // 0 only when the game account has no Battle.net account or the guid is not a bot.
    uint32 OwnerBnetOf(ObjectGuid characterGuid) const { auto itr = _bots.find(characterGuid); return itr == _bots.end() ? 0 : itr->second.characterBnet; }
    bool FollowStatus(ObjectGuid characterGuid, bool& away, uint32& awaySeconds, bool& blocked, std::string& blockedWhy, bool& hasBrain) const { auto itr = _bots.find(characterGuid); if (itr == _bots.end()) return false; Bot const& b = itr->second; away = b.masterAway; awaySeconds = (b.graceLeftMs + 999) / 1000; blocked = b.followBlocked; blockedWhy = b.followBlockedWhy; hasBrain = b.brain != nullptr; return true; }

    // ---- slice 4, host package H4: entering an instance as a group, the entrance rule, life state ----
    // The entry question with its code: `code` is empty when the core has no objection, else one of level full locked difficulty not-found teleporting
    // in-combat dead other (the `block=` words of `.tb state`) or `map-cap` (F-population: the target map already holds its free bots),
    // `text` says it in words. This is CanEnterMap's answer in two parts (C1: the ONE place that
    // calls Map::PlayerCannotEnter is still CanEnterMap).
    struct EntryCheck { std::string code; std::string text; };
    static EntryCheck CheckEntry(Player* who, uint32 mapId);

    // F-population: the per-map free-bot cap. How many free bots stand in the world on that map now (the entering bot is never
    // on it yet), and the configured caps by kind (TortoiseBots.Free.MaxPerMap.*). CheckEntry refuses a free bot past the cap.
    uint32 FreeBotsOnMap(uint32 mapId) const;
    world::FreeMapCaps GetFreeMapCaps() const { return _freeMapCaps; }
    // TortoiseBots.Raid.Enable: host joins may turn a full party into a raid and fill it up to 40.
    bool RaidsEnabled() const { return _raidsEnabled; }
    // `.tb enter`: the bot walks into the area trigger's box (its engine is down meanwhile) and the core's own area-trigger handler is called for it
    // (WorldSession::HandleAreaTriggerOpcode, MiscHandler.cpp:538), which asks the core's entry rule and teleports it. `leader` (not empty) = the
    // party leader's guid: the bot calls the handler only after the leader stands on the trigger's target map (followers enter after their leader
    // into the SAME instance: the group's recent instance, MapManager.cpp:300). `direct` = the D1 fallback: no walk and no handler, the same checks,
    // then Player::TeleportTo to the trigger's target (the instance's entrance when it names one), exactly the handler's last step.
    // Empty `code` = started (the walk begins), else the refusal (nothing was started or changed).
    struct EntryStart { std::string code; std::string text; };
    EntryStart StartEntry(ObjectGuid botGuid, uint32 triggerId, ObjectGuid leader, bool direct);
    // What `.tb state` shows: the way of the last map change and its refusal. `entering` is true while a walk or a teleport of an entry is running.
    struct EntryInfo
    {
        std::string entered;              // trigger | order | setup | empty (none yet: the grammar never writes the word none)
        std::string block;                // empty = none; else the code of the last refusal (see CheckEntry)
        std::string blockText;
        uint32 catchups = 0;              // open-world catch-up teleports (BotMgr::Think); never into or inside an instance
        bool entering = false;
        std::string phase;                // walking | waiting-leader | teleporting | "" (for `.tb where`)
    };
    EntryInfo GetEntryInfo(ObjectGuid botGuid) const;
    // `.tb tele`: a teleport the host started for a bot (`how` = setup | order): the landing is noted (`entered=`) when the core has finished it,
    // a teleport that ends elsewhere is a `block=teleport-refused`.
    void NoteTeleportStarted(ObjectGuid botGuid, char const* how, uint32 destMapId);
    // Life state the host keeps between ticks (`.tb state`): `running-back` = alive again after a death, still more than 30 yd from the commander or
    // leader, until it is within 20 yd; `practice` = a `.tb revive` happened and no combat has ended since (`.tb revive`, grammar tag PRACTICE).
    bool RunningBack(ObjectGuid botGuid) const { auto itr = _bots.find(botGuid); return itr != _bots.end() && itr->second.runBack; }
    bool Practice(ObjectGuid botGuid) const { auto itr = _bots.find(botGuid); return itr != _bots.end() && itr->second.practice; }
    void MarkPractice(ObjectGuid botGuid);
    // D2: the entrance the host gave an instance (world_safe_locs id), 0 = none. `entranceFor(instance id)` is what the landing of an entry recorded.
    uint32 InstanceEntrance(Player* player) const;   // the instance script's entrance location for the map the player is on (0 = none, or not an instance)
    // `.tb order <bot> release` and the release rule read this: false while the instance the bot is in has NO entrance (no script or no trigger target),
    // a release there would revive the bot at a graveyard outside (Player::RepopAtGraveyard, Player.cpp:5258-5263).
    bool ReleaseHasEntrance(Player* bot) const;

    void Update(uint32 diff);

    // What the last Update did with the tick budget, for `.tb perf`: the
    // controller's effective budgets, the pool pass it cut, and how much of it
    // the bots consumed (fed back to the controller next pass).
    struct BudgetStats
    {
        uint32 poolUs = 0;         // effective pool ceiling (controller output)
        uint32 combatUs = 0;       // effective combat ceiling, added to the pool's
        uint32 budgetUs = 0;       // the sum one pass could spend; 0 = no budget
        uint32 targetMs = 0;       // controller target; 0 = the controller is off
        double smoothedTickMs = 0.0;
        bool held = false;         // over target, but the core kept it there, not the pool
        uint32 poolBots = 0;       // bots the budget could cut
        uint32 processed = 0;      // of those, given a turn this pass
        bool cut = false;          // the pass stopped at the budget
    };
    BudgetStats LastBudget() const { return _lastBudget; }

private:
    // Slice 4 entry job of one bot (see StartEntry).
    struct Entry
    {
        bool active = false;
        uint32 triggerId = 0;
        uint32 targetMap = 0;
        uint32 safeLocId = 0;          // the trigger's target safe location (world_safe_locs id)
        ObjectGuid leader;             // empty = this bot is the leader (no gate)
        bool direct = false;
        bool teleporting = false;      // the handler (or the direct teleport) started a teleport: wait for the landing
        uint32 ageMs = 0;
        uint32 stepMs = 0;
        uint32 noProgressMs = 0;
        float bestDist = 1.0e9f;
        std::string phase;
    };
    struct Landing { bool pending = false; std::string how; uint32 mapId = 0; };
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
        uint32 lastTurnMs = 0;       // world time of this bot's last pass, so a budgeted bot gets the time since its last turn
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
        // R11: the master is on a map the bot could not be sent to (an instance the core refuses it, or a teleport the core refused). The bot stays
        // where it is, does not ask again every Think, and `.tb where` says why. It asks again when the master changes map or after FollowRetryMs.
        bool followBlocked = false;
        uint32 followBlockedMap = 0;
        uint32 followBlockedInstance = 0;
        uint32 followRetryMs = 0;
        uint32 outOfSightMs = 0;       // N2: how long the master has been over 100 yd away and out of line of sight
        uint32 catchUpCooldownMs = 0;  // N2: no second catch-up teleport before this has run out
        std::string followBlockedWhy;
        // Slice 4 (H4)
        Entry entry;
        Landing landing;
        std::string entered;            // trigger | order | setup | empty: the way of the last map change this host started
        std::string block;              // the code of the last refused entry or lost teleport (empty = none)
        std::string blockText;
        uint32 catchups = 0;
        bool wasDead = false;
        // W1: a masterless bot that plays the world (free kit); its kills/deaths, the ghost rescue tracker and the counters of `.tb state`.
        bool free = false;
        // W0: logged in by the pool scheduler (`.tb pool start`). A master binding clears `free`; a pool bot without it has left
        // the pool's count and is never logged out by the pool.
        bool pool = false;
        bool owned = false;            // D-3: an owner's alt (`.bot add`): logs out with its owner, may share the owner's game accounts
        uint32 ownerBnet = 0;          // D-3: the ADDING owner's Battle.net account at login (the cap and the sharing rule key on it)
        uint32 characterBnet = 0;      // TBF-B1 finding 5: the character's own game account's Battle.net id (the API's `owner`), from its session
        uint32 kills = 0;
        uint32 deaths = 0;
        uint32 rescues = 0;
        uint32 teleports = 0;
        bool rescuedThisDeath = false;
        bool wasGhost = false;            // the release teleport was already counted for this death
        time_t diedAt = 0;
        int reviveHow = -1;
        float ghostYd = -1.0f;
        TortoiseBots::GhostRescueProgress ghostRescue;
        bool deadEngineFailed = false;  // N2: the dead engine could not be built while the commander is away (logged once per death)
        uint32 deadMs = 0;              // N2/N3: how long the body has lain (reset when alive): the bound after which a commanded bot releases itself
        bool runBack = false;
        bool practice = false;
        bool practiceSawCombat = false;
    };

    void Think(Bot& bot, Player* me);
public:
    // N1: the one guarded call of Map::PlayerCannotEnter: "" = no objection, else the reason (see TbBotMgr.cpp).
    static std::string CanEnterMap(Player* who, uint32 mapId);
private:
    static std::string JoinParty(Player* bot, Player* master);
    // W1 (review): the ONE master-binding step. A master replaces free mode at that moment (the autonomous kit, the ghost bookkeeping),
    // so every binding path lands here: `.tb group` (GroupWith), the commander binding, a run and the engine's own adoption (SetMaster).
    void BindMaster(Bot& bot, ObjectGuid master);
    // H4 private steps
    void UpdateEntry(Bot& bot, Player* me, uint32 diff);
    void FailEntry(Bot& bot, std::string code, std::string text);
    void TrackLife(Bot& bot, Player* me);
    void FreeThink(Bot& bot, Player* me);
    void GhostRescue(Bot& bot, Player* me);
    void TickQuestGroups(uint32 diff);
    // A bot the pool budget may cut: not under a real player (a live master or
    // an owner's alt) and done with loading, teleports and logouts, so a turn
    // has AI work to give.
    bool Budgetable(Bot const& bot) const;
    // Feeds the previous world tick to the AdaptiveBudget controller, re-syncs it
    // from the engine's keys, and hands this pass its budget.
    void UpdateTickBudget(uint32 diff);
    void EnsureEntrance(Bot const& bot, Player* me);
    bool IsCommandedHere(Bot const& bot, Player* me) const;
    void DeadBackstop(Bot& bot, Player* me);
    void DropBrain(Bot& bot);
    // R10 (S2): the master is gone: take the brain down and wait out the grace (or log the bot out at once when the grace is 0).
    void DetachFromMaster(Bot& bot);
    // R10 (S2): one update of a bot whose master is away. True when the master is back in the world and the bot has been re-bound.
    bool UpdateMasterAway(Bot& bot, uint32 diff);
    // A creation whose character commit is in flight (the core's own order, CharacterHandler.cpp): the login side (the realm's
    // character count, the account data) is committed only after the character commit answered true, the creation hooks
    // (OnPlayerCreate) and the character cache follow, and a failed character commit leaves nothing behind. The record and
    // everything it holds is in TbBotMgr.cpp.
    struct PendingCreation;
    void ConfirmCreations();
    std::vector<std::shared_ptr<PendingCreation>> _creating;

    struct QuestGroupEntry
    {
        ObjectGuid groupGuid;
        uint32 questId = 0;
        uint64 formedAtMs = 0;
        std::vector<ObjectGuid> members;
    };
    std::vector<QuestGroupEntry> _questGroups;
    uint32 _questGroupTimerMs = 0;
    uint32 _actionCountsElapsedMs = 0;   // tick time since the last action_counts.csv snapshot (5 min)

    std::unordered_map<ObjectGuid, Bot> _bots;
    std::unordered_set<ObjectGuid> _knownBots;
    bool _enabled = false;
    float _followDistance = 3.0f;
    bool _engineByDefault = false;
    bool _raidsEnabled = false;
    bool _capitalErrandsEnabled = false;
    bool _guildMailEnabled = false;
    bool _guildMailAllowMixed = false;
    bool _guildsEnabled = false;
    bool _guildFormingEnabled = false;
    bool _guildAcceptPlayers = false;
    bool _groupQuestsEnabled = false;
    bool _professionsEnabled = false;
    bool _fishingEnabled = false;
    bool _disenchantUpkeepEnabled = true;
    bool _enchantGearEnabled = true;
    uint32 _deadBackstopMs = 180000;  // N2/N3: TortoiseBots.Slice.DeadBackstopSeconds
    uint32 _masterGraceMs = 300000;   // TortoiseBots.Slice.MasterGraceSeconds
    world::FreeMapCaps _freeMapCaps;  // F-population: TortoiseBots.Free.MaxPerMap.* (dungeon 5, raids 10/20/40, 0 = no cap)
    world::ShareMobsConfig _shareMobs;  // #24: TortoiseBots.World.PoolShareMobs.* (off until the live rows prove it)
    // Tick budget (runtime/AdaptiveBudget.h): the controller moves the effective
    // pool/combat ceilings each pass from the measured previous world tick. The
    // configured values stay the hard maximum; 0 in either means no budget.
    AdaptiveBudget _adaptiveBudget{50, 10000, 15000};
    bool _budgetSynced = false;
    uint32 _budgetTargetMs = 0;
    uint32 _budgetPoolCeilingUs = 0;
    uint32 _budgetCombatCeilingUs = 0;
    uint32 _budgetGateMs = 0;
    uint64 _budgetUs = 0;              // the sum this pass could spend; 0 = no budget
    uint32 _lastPoolTurnsMs = 0;       // budgeted turns the previous pass spent
    std::vector<ObjectGuid> _turnOrder; // the pass order of the bots the budget may cut
    std::vector<ObjectGuid> _alwaysOrder; // bots never cut: player-owned, loading, teleporting or logging out
    ObjectGuid _nextPoolTurn;           // first bot the previous pass cut off (first this pass)
    BudgetStats _lastBudget;
    // N1 (package N): NO decision is cached by instance id alone (the core hands out ids from one pool for all maps and reuses them): what is kept is keyed by
    // (map id, instance id), and a cached trigger is used only when its target is that map. The entrance itself is read from the instance script every time.
    std::map<std::pair<uint32, uint32>, uint32> _entryTriggerByInstance;   // (map, instance) -> the area trigger the last entry into it used
    std::set<std::pair<uint32, uint32>> _entranceFailLogged;                // (map, instance) whose "no entrance" was logged (cleared on success): the log is bounded, the retry is not
};
}

#pragma once

// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectGuid.h"

#include "policy/lfg/BotLfgSelectionPolicy.h"
#include "policy/lfg/BotLfgLifecyclePolicy.h"
#include "BotLfgWhisperInbox.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class Player;

namespace TortoiseBots
{

// Server-owned bot LFG groups: with AiPlayerbot.BotLfgGroups = 1 the server
// forms crews of four from the proven plans of ai_bot_lfg_plan, holds them
// for a player, then fills the open role with a bot. World thread only.
struct BotLfgMember
{
    ObjectGuid guid;
    BotLfg::Role role = BotLfg::Role::Damage;
    bool forcedRole = false;
};

struct BotLfgGroup
{
    uint32 id = 0;
    std::string plan;
    std::string lftCode;
    uint32 mapId = 0;
    uint8 team = 0;
    uint8 minLevel = 0;
    uint8 maxLevel = 0;
    uint32 groupId = 0;
    ObjectGuid leader;
    std::vector<BotLfgMember> members;
    BotLfg::Role openRole = BotLfg::Role::None;
    BotLfgLifecycle::State state = BotLfgLifecycle::State::Forming;
    uint32 stateSinceMs = 0;
    std::unordered_map<uint64, uint32> missingSinceMs;
    BotLfgLifecycle::FillKind filledBy = BotLfgLifecycle::FillKind::None;
    ObjectGuid player;
    uint64 bindingKey = 0;
    bool forceFill = false;
    bool everFormed = false;
    uint32 formedAtMs = 0;
    uint32 posts = 0;
    uint32 lastPostMs = 0;
    ObjectGuid invitee;
    uint32 inviteAtMs = 0;
    std::unordered_map<uint64, uint32> invitesSent;
    std::unordered_map<uint64, uint32> repliedAtMs;
    std::unordered_map<uint64, uint32> hintedAtMs;
};

class BotLfgService
{
public:
    static BotLfgService& Instance();
    void Initialize();
    void Update(uint32 diff);
    void Shutdown();
    uint32 LoadPlans();
    bool Enabled() const;
    BotLfgGroup const* FindById(uint32 id) const;
    BotLfgGroup const* FindByMember(ObjectGuid guid) const;
    std::vector<BotLfgGroup const*> Groups() const;
    uint32 NowMs() const { return m_nowMs; }
    bool FormNow(std::string const& plan, uint8 team, std::string& code, std::string& message);
    bool FillNow(uint32 id, std::string& code, std::string& message);
    bool EndNow(uint32 id, std::string& code, std::string& message);
private:
    struct PlanRow
    {
        std::string plan;
        std::string lftCode;
        uint8 teams = 0;
        uint32 mapId = 0;
        uint8 minLevel = 0;
        uint8 maxLevel = 0;
        bool entranceKnown = false;
        uint32 entranceMap = 0;
        float entranceX = 0.0f;
        float entranceY = 0.0f;
    };
    struct Scanned
    {
        Player* bot = nullptr;
        uint64 guid = 0;
        uint8 level = 0;
        uint8 team = 0;
        bool tank = false;
        bool healer = false;
        bool damage = false;
        bool shieldPending = false;
    };
    std::vector<Scanned> ScanCandidates();
    std::vector<BotLfg::Candidate> CandidatesFor(std::vector<Scanned> const& scanned, uint8 team, uint8 minLevel,
        uint8 maxLevel, uint32 nearMap, float nearX, float nearY, bool nearKnown) const;
    bool ReadyForRole(Player* bot, BotLfg::Role role) const;
    bool TryForm(PlanRow const* only, uint8 onlyTeam, bool gm, std::string& code, std::string& message);
    bool AddMember(BotLfgGroup& group, Player* leader, Player* bot, BotLfg::Role role);
    void Transition(BotLfgGroup& group, BotLfgLifecycle::State to);
    void End(BotLfgGroup& group, BotLfgLifecycle::EndReason reason, bool release);
    bool TrackOutOfWorld(BotLfgGroup& group, ObjectGuid guid, Player* bot);
    bool MemberMissing(BotLfgGroup& group, Player* bot, ObjectGuid guid, Group* grp);
    bool LeaderMissing(BotLfgGroup& group, Player* leader, Group* grp, bool ours);
    void TickGroups(bool enabled);
    void TickGroup(BotLfgGroup& group, bool enabled);
    bool RealPlayerInGroup(BotLfgGroup const& group) const;
    bool Advertising(BotLfgGroup const& group) const;
    void AdvertiseGroups();
    void PostInChannel(BotLfgGroup& group, Player* leader);
    void StopAdvertising(BotLfgGroup& group);
    void PublishLeaders() const;
    void HandleWhispers();
    void HandleWhisper(LfgWhisper const& whisper);
    void Reply(Player* leader, Player* to, std::string const& text) const;
    bool InviteHeld(BotLfgGroup const& group) const;
    BotLfgGroup* FindByLeader(ObjectGuid leader);
    bool RealPlayerOfTeamOnline(uint8 team) const;
    void Log(BotLfgGroup const& group, std::string const& text) const;
    bool m_initialized = false;
    bool m_shutDown = false;
    bool m_wasEnabled = false;
    uint32 m_elapsedMs = 0, m_nowMs = 0, m_sinceFormMs = BotLfg::kFormIntervalMs, m_formedCount = 0, m_nextId = 1;
    uint32 m_channelPostMs[3] = {0, 0, 0};
    bool m_channelPosted[3] = {false, false, false};
    std::vector<PlanRow> m_plans;
    std::vector<BotLfgGroup> m_groups;
    struct FormStamp
    {
        uint32 atMs = 0;
        bool formed = false;
    };
    std::unordered_map<std::string, FormStamp> m_lastFormedMs;
};

} // namespace TortoiseBots

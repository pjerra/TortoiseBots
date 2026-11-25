// The free-bot guild driver: accept/stay/found on the host tick and the owner's guild management
// whispers. The decisions are in host/TbHostGuildJoinPure.h (core-free, tested); this file fills the facts from
// the core and calls the guild host seams (host/TbHostGuild.h).
#include "host/TbHostGuildJoin.h"
#include "host/TbHostGuild.h"
#include "host/TbHostGuildJoinPure.h"
#include "host/TbHostGuildRequestPure.h"
#include "TbBotMgr.h"

#include "CharacterCache.h"
#include "CellImpl.h"
#include "Creature.h"
#include "GameTime.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "PetitionMgr.h"
#include "Player.h"
#include "SharedDefines.h"
#include "World.h"
#include "WorldSession.h"

#include <list>
#include <string>
#include <unordered_map>
#include <vector>

namespace tbhost
{
namespace guildjoin
{

namespace
{

// How close a petitioner NPC must stand for the buy and turn-in legs, and a signer for a charter signature. The
// core checks no distance for a signature, so this is the "nearby" the bot keeps.
constexpr float kPetitionerRangeYd = 30.0f;
constexpr float kSignerRangeYd = 30.0f;

// A refused accept stays refused for a while (the invite is not cleared by the core on every refusal), a refused
// charter is not retried every few seconds (a taken name or a full bag would otherwise log forever), and the
// founding legs scan the grid, so neither runs more than once a few seconds per bot.
constexpr uint32_t kAcceptRetryMs = 30000;
constexpr uint32_t kFoundingIntervalMs = 5000;
constexpr uint32_t kCharterRefusedRetryMs = 600000;

struct BotTimers
{
    uint32_t acceptRetryMs = 0;
    uint32_t nextFoundingMs = 0;
};

std::unordered_map<uint32_t, BotTimers>& Timers()
{
    static std::unordered_map<uint32_t, BotTimers> timers;
    return timers;
}

BotTimers& TimersOf(Player* bot)
{
    return Timers()[bot->GetGUID().GetCounter()];
}

Creature* FindNearestPetitioner(Player* bot)
{
    if (!bot || !bot->IsInWorld() || !bot->FindMap())
        return nullptr;
    std::list<Creature*> creatures;
    Trinity::AnyUnitInObjectRangeCheck check(bot, kPetitionerRangeYd);
    Trinity::CreatureListSearcher<Trinity::AnyUnitInObjectRangeCheck> searcher(bot, creatures, check);
    Cell::VisitGridObjects(bot, searcher, kPetitionerRangeYd);

    Creature* best = nullptr;
    float bestDist = kPetitionerRangeYd + 1.0f;
    for (Creature* creature : creatures)
    {
        if (!creature || !creature->IsAlive() || !creature->HasNpcFlag(UNIT_NPC_FLAG_PETITIONER))
            continue;
        if (!creature->InSamePhase(bot) || bot->IsHostileTo(creature))
            continue;
        float const dist = bot->GetDistance(creature);
        if (dist <= kPetitionerRangeYd && dist < bestDist)
        {
            bestDist = dist;
            best = creature;
        }
    }
    return best;
}

bool MemberGuidByName(uint64_t guildId, std::string rawName, ObjectGuid& out)
{
    if (!guildId || !normalizePlayerName(rawName))
        return false;
    Guild const* guild = sGuildMgr->GetGuildById(guildId);
    if (!guild)
        return false;
    for (auto const& [guid, member] : guild->GetMembers())
    {
        if (member.GetName() == rawName)
        {
            out = guid;
            return true;
        }
    }
    return false;
}

void Tell(Player* bot, Player* sender, std::string const& text)
{
    if (bot && sender)
        bot->Whisper(text, LANG_UNIVERSAL, sender);
}

// The invite leg: accept what the policy allows, decline the rest so an unwanted invite does not sit on the bot
// and get re-read every tick.
void AcceptOrDecline(Player* bot)
{
    if (bot->GetGuildId() != 0)
        return;
    uint32_t const invited = static_cast<uint32_t>(bot->GetGuildIdInvited());
    if (!invited)
        return;

    BotTimers& timers = TimersOf(bot);
    uint32_t const nowMs = GameTime::GetGameTimeMS();
    if (timers.acceptRetryMs && nowMs < timers.acceptRetryMs)
        return;

    Guild const* guild = sGuildMgr->GetGuildById(invited);
    if (!guild)
    {
        std::string why;
        tbhost::guild::DeclineInvite(bot, &why);
        return;
    }

    Team const leaderTeam = sCharacterCache->GetCharacterTeamByGuid(guild->GetLeaderGUID());
    bool const sameTeam = static_cast<uint32_t>(leaderTeam) == bot->GetTeam();
    bool const twoSideGuilds = sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_GUILD);

    if (!InviteAcceptable(false, true, sameTeam, twoSideGuilds, guild->GetMembersCount()))
    {
        std::string why;
        tbhost::guild::DeclineInvite(bot, &why);
        TC_LOG_INFO("tortoisebots", "[W9] guild-decline bot={} guild={} name={} members={} two-side={}",
            bot->GetName(), invited, guild->GetName(), guild->GetMembersCount(), uint32_t(twoSideGuilds));
        return;
    }

    std::string why;
    if (!tbhost::guild::AcceptInvite(bot, invited, &why))
    {
        timers.acceptRetryMs = nowMs + kAcceptRetryMs;
        TC_LOG_INFO("tortoisebots", "[W9] guild-accept-refused bot={} guild={} reason={}", bot->GetName(), invited, why);
        return;
    }
    timers.acceptRetryMs = 0;
    TC_LOG_INFO("tortoisebots", "[W9] guild-accept bot={} guild={} name={}", bot->GetName(), invited, guild->GetName());
}

// The stay leg: a free bot does not outlive the member limit in a guild, so the rank never takes the seats a
// player could use.
void LeaveOversizedGuild(Player* bot)
{
    uint64_t const guildId = bot->GetGuildId();
    if (!guildId)
        return;
    Guild const* guild = sGuildMgr->GetGuildById(guildId);
    if (!guild || !ShouldLeaveOversized(guild->GetMembersCount()))
        return;

    std::string why;
    if (!tbhost::guild::LeaveGuild(bot, &why))
    {
        TC_LOG_INFO("tortoisebots", "[W9] guild-leave-refused bot={} guild={} reason={}", bot->GetName(), guildId, why);
        return;
    }
    TC_LOG_INFO("tortoisebots", "[W9] guild-leave-full bot={} guild={} members={}", bot->GetName(), guildId, guild->GetMembersCount());
}

// The founding legs: buy a charter at a petitioner, nearby free bots sign it, turn it
// in at a petitioner. The bot pays the core's charter price itself; nothing is granted.
void FoundGuild(Player* bot)
{
    uint32_t const nowMs = GameTime::GetGameTimeMS();
    BotTimers& timers = TimersOf(bot);
    if (timers.nextFoundingMs && nowMs < timers.nextFoundingMs)
        return;
    timers.nextFoundingMs = nowMs + kFoundingIntervalMs;

    if (bot->GetGuildId() != 0 || bot->GetGuildIdInvited() != 0)
        return;

    uint32_t const required = tbhost::guild::RequiredPetitionSigns();
    Petition* petition = sPetitionMgr->GetPetitionByOwner(bot->GetGUID());
    if (petition)
    {
        if (CharterComplete(petition->Signatures.size(), required))
        {
            Creature* petitioner = FindNearestPetitioner(bot);
            if (!petitioner)
                return;
            std::string why;
            if (!tbhost::guild::TurnInPetition(bot, petition->PetitionGuid, &why))
            {
                TC_LOG_INFO("tortoisebots", "[W9] guild-found-refused bot={} petitioner={} reason={}",
                    bot->GetName(), petitioner->GetName(), why);
                return;
            }
            TC_LOG_INFO("tortoisebots", "[W9] guild-founded bot={} guild={} name={} signs={}",
                bot->GetName(), bot->GetGuildId(), bot->GetGuild() ? bot->GetGuild()->GetName() : std::string(), uint32_t(required));
            return;
        }

        for (ObjectGuid const& guid : TortoiseBots::BotMgr::Instance().BotGuids())
        {
            if (guid == bot->GetGUID())
                continue;
            if (!TortoiseBots::BotMgr::Instance().IsFreeBot(guid))
                continue;
            Player* signer = TortoiseBots::BotMgr::Instance().FindBot(guid);
            if (!signer || !signer->IsInWorld() || signer->GetGuildId() != 0 || signer->GetGuildIdInvited() != 0)
                continue;
            if (signer->GetTeam() != bot->GetTeam() || bot->GetDistance(signer) > kSignerRangeYd)
                continue;

            std::string why;
            if (tbhost::guild::SignPetition(signer, petition->PetitionGuid, &why))
                TC_LOG_INFO("tortoisebots", "[W9] guild-sign bot={} signer={} signs={}",
                    bot->GetName(), signer->GetName(), uint32_t(petition->Signatures.size()));
            if (CharterComplete(petition->Signatures.size(), required))
                break;
        }
        return;
    }

    if (!FoundingEligible(false, false, false, bot->GetLevel(), bot->GetMoney(), tbhost::guild::CharterCost()))
        return;

    Creature* petitioner = FindNearestPetitioner(bot);
    if (!petitioner)
        return;

    std::string const guildName = bot->GetName() + " Guild";
    std::string why;
    if (!tbhost::guild::BuyPetition(bot, petitioner->GetGUID(), guildName, &why))
    {
        timers.nextFoundingMs = nowMs + kCharterRefusedRetryMs;
        TC_LOG_INFO("tortoisebots", "[W9] guild-charter-refused bot={} petitioner={} name={} cost={} why={}",
            bot->GetName(), petitioner->GetName(), guildName, tbhost::guild::CharterCost(), why);
        return;
    }
    TC_LOG_INFO("tortoisebots", "[W9] guild-charter bot={} petitioner={} name={} cost={}",
        bot->GetName(), petitioner->GetName(), guildName, tbhost::guild::CharterCost());
}

std::string DoneText(Command command, std::string const& name)
{
    switch (command)
    {
        case Command::Invite: return "Invited " + name + ".";
        case Command::Join: return "Joined the guild.";
        case Command::Leave: return "Left the guild.";
        case Command::Promote: return "Promoted " + name + ".";
        case Command::Demote: return "Demoted " + name + ".";
        case Command::Remove: return "Removed " + name + " from the guild.";
        case Command::Leader: return "Made " + name + " the guild master.";
        case Command::None: break;
    }
    return std::string();
}

bool RunWhisperCommand(Player* bot, Whisper const& whisper, std::string& reason, std::string& targetName)
{
    targetName = whisper.argument;
    switch (whisper.command)
    {
        case Command::Invite:
        {
            if (targetName.empty())
            {
                reason = "missing-name";
                return false;
            }
            if (!bot->GetGuildId())
            {
                reason = "not-in-guild";
                return false;
            }
            if (!tbhost::guild::HasAnyRankRight(bot->GetGuildId(), bot->GetGUID(), GR_RIGHT_INVITE))
            {
                reason = "no-rank-right";
                return false;
            }
            if (!normalizePlayerName(targetName))
            {
                reason = "missing-name";
                return false;
            }
            if (!ObjectAccessor::FindPlayerByName(targetName))
            {
                reason = "no-such-player";
                return false;
            }
            return tbhost::guild::InviteSucceeded(tbhost::guild::InviteByName(bot, targetName, &reason));
        }
        case Command::Join:
        {
            uint32_t const invited = static_cast<uint32_t>(bot->GetGuildIdInvited());
            if (!invited)
            {
                reason = "no-invite";
                return false;
            }
            return tbhost::guild::AcceptInvite(bot, invited, &reason);
        }
        case Command::Leave:
            return tbhost::guild::LeaveGuild(bot, &reason);
        case Command::Promote:
        case Command::Demote:
        case Command::Remove:
        {
            if (targetName.empty())
            {
                reason = "missing-name";
                return false;
            }
            uint64_t const guildId = bot->GetGuildId();
            if (!guildId)
            {
                reason = "not-in-guild";
                return false;
            }
            ObjectGuid target;
            if (!MemberGuidByName(guildId, targetName, target))
            {
                reason = "not-a-member";
                return false;
            }
            if (whisper.command == Command::Promote)
                return tbhost::guild::PromoteMember(bot, target, &reason);
            if (whisper.command == Command::Demote)
                return tbhost::guild::DemoteMember(bot, target, &reason);
            return tbhost::guild::RemoveMember(bot, target, &reason);
        }
        case Command::Leader:
        {
            if (targetName.empty())
            {
                reason = "missing-name";
                return false;
            }
            return tbhost::guild::SetLeader(bot, targetName, &reason);
        }
        case Command::None:
            break;
    }
    reason = "unknown-command";
    return false;
}

char const* CommandName(Command command)
{
    switch (command)
    {
        case Command::Invite: return "invite";
        case Command::Join: return "join";
        case Command::Leave: return "leave";
        case Command::Promote: return "promote";
        case Command::Demote: return "demote";
        case Command::Remove: return "remove";
        case Command::Leader: return "leader";
        case Command::None: break;
    }
    return "none";
}

// ---- TBF #30: the join-by-whisper request; the rules are in host/TbHostGuildRequestPure.h ----

// The askers that made a handled request and when: world thread only, like the guild timers.
RequestCooldowns& RequestTimes()
{
    static RequestCooldowns times;
    return times;
}

// The guild's side as the asker has to match it: a bot in the guild is of the guild's faction.
char const* GuildSideWord(Player* bot)
{
    return bot->GetTeam() == TEAM_ALLIANCE ? "Alliance" : "Horde";
}

// The inviter: the whispered bot when its own rank may invite; else the online pool bot member with that right and
// the lowest rank (the guild master's is the lowest). A player's owned alt is never an inviter even when it has the
// right: it is not a pool bot, so only the fleet's own bots speak for the guild.
Player* PickInviter(Player* bot, uint64_t guildId, Guild* guild)
{
    auto const& mgr = TortoiseBots::BotMgr::Instance();
    std::vector<InviterCandidate> candidates;
    std::vector<Player*> players;
    for (auto const& entry : guild->GetMembers())
    {
        ObjectGuid const& guid = entry.first;
        InviterCandidate candidate;
        candidate.preferred = guid == bot->GetGUID();
        candidate.bot = mgr.IsFreeBot(guid);
        std::uint8_t rank = 0;
        candidate.canInvite = tbhost::guild::GetMemberRankId(guildId, guid, rank)
            && tbhost::guild::HasAnyRankRight(guildId, guid, GR_RIGHT_INVITE);
        candidate.rank = rank;
        Player* member = candidate.bot ? mgr.FindBot(guid) : nullptr;
        candidate.online = member && member->IsInWorld() && tbhost::guild::IsMemberOnline(guildId, guid);
        candidates.push_back(candidate);
        players.push_back(member);
    }
    int const picked = PickInviterIndex(candidates);
    return picked < 0 ? nullptr : players[std::size_t(picked)];
}

// The one whisper a request is answered with; empty for the silent and not-handled verdicts.
std::string RequestAnswer(RequestVerdict verdict, std::string const& guildName, char const* side,
    Player* inviter, Player* bot)
{
    switch (verdict)
    {
        case RequestVerdict::NotInGuild: return "I am not in a guild.";
        case RequestVerdict::NotBotRun: return "My guild does not take members this way.";
        case RequestVerdict::AlreadyInGuild: return "You are already in a guild.";
        case RequestVerdict::WrongFaction: return std::string("We only take ") + side + " members.";
        case RequestVerdict::Full: return "Our guild is full.";
        case RequestVerdict::NoOfficer: return "No officer of " + guildName + " is online, try later.";
        case RequestVerdict::Invite:
            if (!inviter || inviter == bot)
                return "Sent you an invite to " + guildName + ".";
            return "I asked " + inviter->GetName() + " to invite you to " + guildName + ".";
        case RequestVerdict::NotHandled:
        case RequestVerdict::Silent:
            break;
    }
    return std::string();
}

} // namespace

void TickFreeBotGuild(Player* bot, bool formingEnabled)
{
    if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported())
        return;
    if (bot->GetGuildId() != 0)
    {
        LeaveOversizedGuild(bot);
        return;
    }
    AcceptOrDecline(bot);
    if (formingEnabled && bot->GetGuildId() == 0)
        FoundGuild(bot);
}

void ForgetBot(ObjectGuid const& botGuid)
{
    Timers().erase(botGuid.GetCounter());
}

bool HandleWhisper(Player* bot, Player* sender, std::string const& text)
{
    if (!bot || !sender || bot == sender)
        return false;
    if (!TortoiseBots::BotMgr::Instance().GuildsEnabled())
        return false;
    Whisper const whisper = ParseGuildWhisper(text);
    if (whisper.command == Command::None)
        return false;

    ObjectGuid const master = TortoiseBots::BotMgr::Instance().MasterOf(bot->GetGUID());
    bool const owner = !master.IsEmpty() && master == sender->GetGUID();
    WorldSession* const senderSession = sender->GetSession();
    bool const gm = senderSession && senderSession->GetSecurity() >= SEC_GAMEMASTER;
    if (!WhisperAuthorized(owner, gm))
    {
        TC_LOG_INFO("tortoisebots", "[W9] guild-whisper bot={} from={} cmd={} result=denied",
            bot->GetName(), sender->GetName(), CommandName(whisper.command));
        Tell(bot, sender, "Only my owner can manage my guild.");
        return true;
    }

    std::string reason;
    std::string targetName;
    bool const ok = RunWhisperCommand(bot, whisper, reason, targetName);
    TC_LOG_INFO("tortoisebots", "[W9] guild-whisper bot={} from={} cmd={} target={} result={}",
        bot->GetName(), sender->GetName(), CommandName(whisper.command), targetName,
        ok ? std::string("ok") : ("fail:" + reason));
    if (ok)
        Tell(bot, sender, DoneText(whisper.command, targetName));
    else
        Tell(bot, sender, "I cannot do that: " + reason + ".");
    return true;
}

bool HandleJoinRequest(Player* bot, Player* asker, std::string const& text)
{
    if (!bot || !asker || bot == asker)
        return false;

    auto const& mgr = TortoiseBots::BotMgr::Instance();
    RequestFacts facts;
    facts.featureOn = mgr.GuildAcceptPlayersEnabled() && mgr.GuildsEnabled();
    facts.isRequest = IsJoinRequest(text);
    if (!facts.featureOn || !facts.isRequest)
        return false;

    // One handled request per asker per cooldown, keyed by the asker's guid counter; a repeat inside it is handled
    // silently, and either way the line never joins the inbox (also for an owner).
    uint32_t const askerKey = asker->GetGUID().GetCounter();
    uint32_t const nowMs = GameTime::GetGameTimeMS();
    facts.rateLimited = !RequestTimes().Admit(askerKey, nowMs);
    if (facts.rateLimited)
    {
        TC_LOG_INFO("tortoisebots", "[W9] guild-request bot={} from={} guild={} inviter=- result=silent",
            bot->GetName(), asker->GetName(), bot->GetGuildId());
        return true;
    }

    uint64_t const guildId = bot->GetGuildId();
    Guild* guild = guildId ? sGuildMgr->GetGuildById(guildId) : nullptr;
    facts.botInGuild = guild != nullptr;
    facts.askerInGuild = asker->GetGuildId() != 0;
    if (guild)
    {
        facts.guildBotRun = tbhost::guild::IsPoolCharacter(guild->GetLeaderGUID());
        facts.sameTeam = static_cast<uint32_t>(bot->GetTeam()) == asker->GetTeam();
        facts.twoSideGuilds = sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_GUILD);
        facts.memberCount = guild->GetMembersCount();
    }

    // The inviter is looked for only once every other check passed; the pure policy keeps its NoOfficer verdict for
    // a caller that has no inviter at all.
    facts.inviterFound = true;
    RequestVerdict verdict = DecideRequest(facts);
    Player* inviter = nullptr;
    if (verdict == RequestVerdict::Invite)
    {
        inviter = PickInviter(bot, guildId, guild);
        if (!inviter)
            verdict = RequestVerdict::NoOfficer;
    }

    std::string const guildName = guild ? guild->GetName() : std::string();
    char const* const side = GuildSideWord(bot);

    if (verdict == RequestVerdict::Invite)
    {
        std::string why;
        tbhost::guild::InviteOutcome const outcome = tbhost::guild::InviteByName(inviter, asker->GetName(), &why);
        if (!tbhost::guild::InviteSucceeded(outcome))
        {
            TC_LOG_INFO("tortoisebots", "[W9] guild-request bot={} from={} guild={} inviter={} result=invite-refused reason={}",
                bot->GetName(), asker->GetName(), guildId, inviter->GetName(), tbhost::guild::InviteOutcomeCode(outcome));
            Tell(bot, asker, InviteRefusalAnswer(outcome, guildName, side, inviter->GetName()));
            return true;
        }
        Tell(bot, asker, RequestAnswer(RequestVerdict::Invite, guildName, side, inviter, bot));
        TC_LOG_INFO("tortoisebots", "[W9] guild-request bot={} from={} guild={} inviter={} result=invited",
            bot->GetName(), asker->GetName(), guildId, inviter->GetName());
        return true;
    }

    Tell(bot, asker, RequestAnswer(verdict, guildName, side, nullptr, bot));
    TC_LOG_INFO("tortoisebots", "[W9] guild-request bot={} from={} guild={} inviter=- result={}",
        bot->GetName(), asker->GetName(), guildId, RequestCode(verdict));
    return true;
}

} // namespace guildjoin
} // namespace tbhost

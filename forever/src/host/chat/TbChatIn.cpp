// S1-R2: the chat hook, the inboxes and their counters (see host/chat/TbChatIn.h for what this file is for).
//
// The rules of one line are in host/chat/TbChatInPure.h (core-free, tested by forever/tools/test_chat_in.cpp); this file only fills the
// facts from the core and owns the world's inboxes.
#include "host/chat/TbChatIn.h"

#include "TbBotMgr.h"
#include "host/TbEngineBrain.h"
#include "host/cmd/TbCmdBinding.h"
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
#include "host/TbH3.h"          // h3::Rec, for the `.tb state` block
#include "host/TbHostGuildJoin.h"   // TBF #30: a player's whisper asking a bot-run guild for an invite
#endif

#include "Group.h"
#include "GroupMgr.h"
#include "GroupReference.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "WorldSession.h"

#include <map>
#include <mutex>
#include <utility>
#include <vector>

// The core's numbers this decision compares against, so a core that renumbers them fails here instead of dropping chat lines silently.
static_assert(unsigned(CHAT_MSG_PARTY) == TortoiseBots::chatin::KindParty, "CHAT_MSG_PARTY moved in the core");
static_assert(unsigned(CHAT_MSG_PARTY_LEADER) == TortoiseBots::chatin::KindPartyLeader, "CHAT_MSG_PARTY_LEADER moved in the core");
static_assert(unsigned(CHAT_MSG_WHISPER) == TortoiseBots::chatin::KindWhisper, "CHAT_MSG_WHISPER moved in the core");
static_assert(unsigned(LANG_ADDON) == TortoiseBots::chatin::LangAddon, "LANG_ADDON moved in the core");
static_assert(unsigned(LANG_ADDON_LOGGED) == TortoiseBots::chatin::LangAddonLogged, "LANG_ADDON_LOGGED moved in the core");

namespace TortoiseBots
{
namespace chatin
{
namespace
{
// The bot's guid counter is the key, as the engine brain knows it (`Brain::guidLow`, the action log's key): unique per character and stable
// while it is in the world.
using BotKey = std::uint32_t;

BotKey KeyOf(ObjectGuid guid) { return BotKey(guid.GetCounter()); }

struct Box
{
    Inbox inbox;
};

// One lock for every box and the ingress: a line is queued by the hook (which the core's chat handlers call on the world thread, but
// Player::Whisper is a public script entry too) and taken by the world thread's drain.
std::mutex& Lock()
{
    static std::mutex mutex;
    return mutex;
}

std::map<BotKey, Box>& Boxes()
{
    static std::map<BotKey, Box> boxes;
    return boxes;
}

Ingress& Incoming()
{
    static Ingress ingress;
    return ingress;
}

bool Authorized(Player* bot, Player* sender)
{
    // D2: the bot's master, the commander a binding names, or a member of the bot's group (the legacy rule of
    // PlayerbotAIPackets.cpp:376-384). A raid is one Group, so any real member of the bot's raid may whisper it; the narrower party
    // case is the subgroup rule (Judge's `sameSubGroup`, filled by Resolve). A stranger reaches neither.
    if (!bot || !sender)
        return false;
    if (BotMgr::Instance().MasterOf(bot->GetGUID()) == sender->GetGUID())
        return true;
    if (cmd::IsCommandedBy(sender, bot))
        return true;
    Group* group = bot->GetGroup();
    return group && group->IsMember(sender->GetGUID());
}

// isRealPlayer_Helper (compat/TbCompat_Unit.h:79): a player whose session has a network transport. Written out here because that header is
// written against the legacy prelude, which this file — core headers only, in both builds — does not carry; the two calls are its body.
bool RealPlayer(Player* player)
{
    WorldSession* session = player ? player->GetSession() : nullptr;
    return session && !session->IsBot();
}

bool HasBrain(ObjectGuid bot)
{
    bool away = false, blocked = false, hasBrain = false;
    uint32 awaySeconds = 0;
    std::string blockedWhy;
    BotMgr::Instance().FollowStatus(bot, away, awaySeconds, blocked, blockedWhy, hasBrain);
    return hasBrain;
}

// Judge one line for one bot; an accepted line joins the bot's inbox. Returns the verdict (the hook ignores it, `.tb hear` prints it).
Verdict Consider(Player* bot, Player* sender, unsigned kind, unsigned lang, std::string const& text, bool injected, bool sameSubGroup = true)
{
    Verdict verdict;
    if (!bot || !sender)
        return verdict;

    ChatInFacts facts;
    facts.kind = kind;
    facts.lang = lang;
    facts.senderIsSelf = sender == bot;
    facts.senderReal = RealPlayer(sender);
    facts.authorized = Authorized(bot, sender);
    facts.sameSubGroup = sameSubGroup;
    facts.engine = engine::Built() && HasBrain(bot->GetGUID());
    facts.injected = injected;
    facts.text = text;
    verdict = Judge(facts);

    std::lock_guard<std::mutex> guard(Lock());
    Box& box = Boxes()[KeyOf(bot->GetGUID())];
    box.inbox.Note(verdict.code);
    if (verdict.accepted())
    {
        Envelope line;
        line.senderHigh = sender->GetGUID().GetRawValue(1);
        line.senderLow = sender->GetGUID().GetRawValue(0);
        line.kind = verdict.kind;
        line.lang = lang;
        line.text = text;
        line.injected = injected;
        box.inbox.Push(line);
    }
    return verdict;
}

// The hook's only work: copy the guids and the text into the ingress. No Player, Group, BotMgr or binding state is read here, so the
// call is safe from any thread the core may call Player::Whisper on.
void Queue(bool party, ObjectGuid sender, ObjectGuid target, unsigned kind, unsigned lang, std::string const& text)
{
    Arrival line;
    line.party = party;
    line.senderHigh = sender.GetRawValue(1);
    line.senderLow = sender.GetRawValue(0);
    line.targetHigh = target.GetRawValue(1);
    line.targetLow = target.GetRawValue(0);
    line.kind = kind;
    line.lang = lang;
    line.text = text;
    std::lock_guard<std::mutex> guard(Lock());
    Incoming().Push(line);
}

// World thread: one arrival, resolved and judged for every receiving bot.
void Resolve(Arrival const& line)
{
    ObjectGuid senderGuid;
    senderGuid.SetRawValue(line.senderHigh, line.senderLow);
    ObjectGuid targetGuid;
    targetGuid.SetRawValue(line.targetHigh, line.targetLow);

    Player* sender = ObjectAccessor::FindConnectedPlayer(senderGuid);
    if (!sender)
        return;                              // gone before its line could mean anything: nothing was judged, nothing to count

    if (!line.party)
    {
        Player* receiver = ObjectAccessor::FindConnectedPlayer(targetGuid);
        if (!receiver || !BotMgr::Instance().IsBot(receiver->GetGUID()))
            return;
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
        // TBF #30: a real player asking a bot of a bot-run guild for an invite. A handled request is answered here
        // (invite or one refusal whisper) and never joins the inbox, so the engine's chat commands never see the
        // phrase; the owner's `guild <verb>` whispers are not phrases and still reach them.
        if (line.kind == KindWhisper && line.lang != LangAddon && line.lang != LangAddonLogged
            && RealPlayer(sender) && !BotMgr::Instance().IsBot(sender->GetGUID())
            && tbhost::guildjoin::HandleJoinRequest(receiver, sender, line.text))
            return;
#endif
        Consider(receiver, sender, line.kind, line.lang, line.text, false);
        return;
    }

    Group* group = sGroupMgr->GetGroupByGUID(targetGuid);
    if (!group)
        return;
    for (GroupReference const& reference : group->GetMembers())
    {
        Player* member = reference.GetSource();
        if (!member || !BotMgr::Instance().IsBot(member->GetGUID()))
            continue;
        // The core's party broadcast keys on the member slots (`GetMemberGroup(sender)`); the guid overload reads the same slots,
        // including a battleground's original group, where the Player overload's current-group test would refuse every member.
        bool const sameSubGroup = group->SameSubGroup(senderGuid, member->GetGUID());
        Consider(member, sender, line.kind, line.lang, line.text, false, sameSubGroup);
    }
}

// The PlayerScript the core's chat handlers call. Neither overload writes the text or reads a world object beyond its guid.
class tortoisebots_chat : public PlayerScript
{
public:
    tortoisebots_chat() : PlayerScript("tortoisebots_chat") { }

    // Player::Whisper (Player.cpp): a whisper to one player. The receiver is a bot only when BotMgr logged it in with a headless session.
    void OnChat(Player* player, uint32 type, uint32 lang, std::string& text, Player* receiver) override
    {
        if (!engine::Built() || !player || !receiver)
            return;
        Queue(false, player->GetGUID(), receiver->GetGUID(), type, lang, text);
    }

    // Handlers/ChatHandler.cpp: a party line (a leader's line arrives as CHAT_MSG_PARTY_LEADER). The bots of the group are found on
    // the world thread; the core delivers the line to the sender's subgroup only, and Resolve applies the same rule.
    void OnChat(Player* player, uint32 type, uint32 lang, std::string& text, Group* group) override
    {
        if (!engine::Built() || !player || !group)
            return;
        Queue(true, player->GetGUID(), group->GetGUID(), type, lang, text);
    }
};
}

void AddScripts()
{
    new tortoisebots_chat();
}

void DrainArrivals()
{
    std::vector<Arrival> batch;
    {
        std::lock_guard<std::mutex> guard(Lock());
        Arrival line;
        while (batch.size() < IngressCapacity && Incoming().Pop(line))
            batch.push_back(std::move(line));
    }
    for (Arrival const& line : batch)
        Resolve(line);
}

bool Pop(std::uint32_t botKey, Envelope& out)
{
    std::lock_guard<std::mutex> guard(Lock());
    auto itr = Boxes().find(botKey);
    return itr != Boxes().end() && itr->second.inbox.Pop(out);
}

void Drop(std::uint32_t botKey)
{
    std::lock_guard<std::mutex> guard(Lock());
    auto itr = Boxes().find(botKey);
    if (itr != Boxes().end())
        itr->second.inbox.Drop();
}

bool StillAuthorized(Player* bot, Player* sender)
{
    return Authorized(bot, sender);
}

void Forget(ObjectGuid bot)
{
    std::lock_guard<std::mutex> guard(Lock());
    Boxes().erase(KeyOf(bot));
}

Verdict Hear(Player* bot, Player* sender, unsigned kind, std::string const& text)
{
    return Consider(bot, sender, kind, LANG_UNIVERSAL, text, true);
}

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
void AppendState(h3::Rec& rec, ObjectGuid bot)
{
    std::uint64_t accepted = 0, refused = 0, dropped = 0;
    bool hasLast = false;
    Code last = Code::Kind;
    {
        std::lock_guard<std::mutex> guard(Lock());
        auto itr = Boxes().find(KeyOf(bot));
        if (itr != Boxes().end())
        {
            accepted = itr->second.inbox.Accepted();
            refused = itr->second.inbox.Refused();
            dropped = itr->second.inbox.Dropped();
            hasLast = itr->second.inbox.HasLast();
            last = itr->second.inbox.Last();
        }
    }
    rec.P("chatin", { std::to_string(accepted), std::to_string(refused), std::to_string(dropped) });
    rec.S("chatlast", hasLast ? CodeName(last) : "");
}
#endif
}
}

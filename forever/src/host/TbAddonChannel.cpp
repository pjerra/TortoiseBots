// The addon channel live half: the TBM prefix hooks, the world-thread drain
// and the Send/Ack/Err answers (TBF #130 piece f-chan). The rule without the
// core is host/TbAddonPure.h.
#include "host/TbAddonChannel.h"
#include "host/TbAddonPure.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
#include "host/TbHostAddonHint.h"

#include "Timer.h"
#endif

#include "TbBotMgr.h"
#include "host/TbEngineBrain.h"

#include "Group.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "WorldSession.h"

#include <mutex>
#include <set>
#include <string>
#include <unordered_map>

namespace TortoiseBots
{
namespace addon
{
namespace
{
std::mutex g_mutex;
std::unordered_map<std::string, VerbHandler> g_verbs;
std::set<std::string> g_advertised;   // sorted and unique by construction
std::set<std::string> g_advertisedCaps;   // sorted and unique by construction
pure::PendingQueue g_queue;
bool g_truncationLogged = false;   // world thread only (Send runs in the drain)

bool ModuleUp()
{
    // The same decline as .bot Dispatch (TbPlayerCmd.cpp): the module off, or
    // built without the engine, and the core answers the command as it does
    // for any other. Here that means the message is not ours to answer.
    return BotMgr::Instance().Enabled() && engine::Built();
}

pure::RequesterKey KeyOf(Player const* player)
{
    ObjectGuid const guid = player->GetGUID();
    return pure::RequesterKey{ guid.GetRawValue(0), guid.GetRawValue(1) };
}

void Enqueue(Player* player, std::string const& text)
{
    if (!player || !player->GetSession())
        return;
    if (BotMgr::Instance().IsBot(player->GetGUID()))
        return;   // bots never drive: owned alts count as IsBot
    if (!ModuleUp())
        return;
    // The cap counts the drop for one Err at the drain; the hook thread never
    // sends (it may not be the world thread).
    g_queue.Enqueue(KeyOf(player), text);
}

void OnAddonText(Player* player, std::string const& prefix, std::string const& text)
{
    if (prefix != pure::kPrefix)
        return;
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
    // The prefix is the add-on talking: this character has it for this run.
    if (player)
    {
        ObjectGuid const guid = player->GetGUID();
        TortoiseBots::addonhint::NoteAddonSeen(guid.GetRawValue(0), guid.GetRawValue(1));
    }
#endif
    Enqueue(player, text);
}

class tortoisebots_addon : public PlayerScript
{
public:
    tortoisebots_addon() : PlayerScript("tortoisebots_addon") { }

    // A whisper to anyone but self is not the addon talking to us (the addon
    // whispers the requester itself); only self-whispers route verbs.
    void OnAddonMessageSent(Player* player, uint32 /*type*/, std::string const& prefix, std::string const& text,
        Player* receiver) override
    {
        if (player && receiver == player)
            OnAddonText(player, prefix, text);
    }

    // Party, raid and instance chat arrive here (the core fires no addon hook
    // for guild, officer or channels).
    void OnAddonMessageSent(Player* player, uint32 /*type*/, std::string const& prefix, std::string const& text,
        Group* group) override
    {
        if (group)
            OnAddonText(player, prefix, text);
    }
};

void Drain()
{
    g_queue.Drain(
        [](pure::RequesterKey key) -> Player*
        {
            ObjectGuid guid;
            guid.SetRawValue(key.hi, key.lo);
            Player* player = ObjectAccessor::FindConnectedPlayer(guid);
            return player && player->GetSession() ? player : nullptr;
        },
        [](Player* requester, pure::PendingCommand const& command)
        {
            if (!ModuleUp())
                return;
            pure::Parsed const parsed = pure::Parse(command.text);
            VerbHandler handler = nullptr;
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                auto it = g_verbs.find(parsed.verb);
                if (it != g_verbs.end())
                    handler = it->second;
            }
            if (!handler)
            {
                Err(requester, parsed.verb, "unsupported", "not available on this server yet");
                return;
            }
            handler(requester, parsed.args);
        },
        [](Player* requester, pure::DroppedReport const& dropped)
        {
            Err(requester, "-", "flood", pure::FloodMessage(dropped.count));
        });
}

class tortoisebots_addon_tick : public WorldScript
{
public:
    tortoisebots_addon_tick() : WorldScript("tortoisebots_addon_tick") { }

    void OnUpdate(uint32 /*diff*/) override
    {
        Drain();
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
        TortoiseBots::addonhint::Tick(getMSTime());
#endif
    }
};
} // namespace

void RegisterVerb(std::string_view verb, VerbHandler handler)
{
    if (!handler)
        return;
    std::string const key = pure::Lower(verb);
    if (key.empty())
        return;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_verbs[key] = handler;
    g_advertised.insert(key);
}

void Advertise(std::string_view token)
{
    std::string const text(pure::Trim(token));
    if (text.empty())
        return;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_advertised.insert(std::string(text));
}

std::vector<std::string> AdvertisedVerbs()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return std::vector<std::string>(g_advertised.begin(), g_advertised.end());
}

void AdvertiseCap(std::string_view token)
{
    std::string const text(pure::Trim(token));
    if (text.empty())
        return;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_advertisedCaps.insert(std::string(text));
}

std::vector<std::string> AdvertisedCaps()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return std::vector<std::string>(g_advertisedCaps.begin(), g_advertisedCaps.end());
}

std::string Safe(std::string_view field)
{
    return pure::Safe(field);
}

void Send(Player* to, std::string_view line)
{
    if (!to || !to->GetSession())
        return;
    for (std::string const& part : pure::SplitLines(line))
    {
        if (part.size() > pure::kMaxLineBytes && !g_truncationLogged)
        {
            g_truncationLogged = true;
            TC_LOG_WARN("tortoisebots", "TortoiseBots: addon reply line truncated to {} bytes", pure::kMaxLineBytes);
        }
        to->WhisperAddon(pure::Truncate(part), std::string(pure::kPrefix), false, to);
    }
}

void Ack(Player* to, std::string_view intent, std::string_view scope, std::uint32_t count, std::string_view detail)
{
    Send(to, pure::AckLine(intent, scope, count, detail));
}

void Err(Player* to, std::string_view intent, std::string_view code, std::string_view message)
{
    Send(to, pure::ErrLine(intent, code.empty() ? "rejected" : code, message.empty() ? "request rejected" : message));
}

void AddScripts()
{
    new tortoisebots_addon();
    new tortoisebots_addon_tick();
}
} // namespace addon
} // namespace TortoiseBots

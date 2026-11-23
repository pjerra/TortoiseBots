// The `.bot totem` and `.bot behavior` handlers (#133 round 1). The pure rules are host/player/TbChoiceCmdPure.h (tested
// standalone by forever/tools/test_choice_cmd.cpp); the bot lookup, the pure ownership decision and the reply channel are
// TbPlayerCmd.cpp's, shared through TbPlayerCmd.h. Values live until logout: persistence is #135.
#include "host/player/TbChoiceCmd.h"
#include "host/player/TbChoiceCmdPure.h"
#include "host/player/TbPlayerCmd.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

// The bot brain's headers are written against cmangos names: the ForeverCore preinclude goes in front of them, as in TbCc.cpp.
#include "compat/botpch_forever.h"

#include "TbBotMgr.h"
#include "PlayerbotAIStorage.h"

#include "Group.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "playerbot/BotState.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"
#include "playerbot/strategy/generic/OffSpecPolicy.h"
#include "playerbot/strategy/shaman/ShamanTotemCommandPolicy.h"

namespace TortoiseBots
{
namespace choice
{
namespace
{
// D-4: a `party` line touches at most this many bots.
constexpr std::size_t kMaxPartyBots = 40;

// One addressed bot and the pure ownership verdict for the line's tier: a refused bot is counted, never fatal.
struct Target
{
    Player* bot = nullptr;
    bool allowed = false;
    char const* code = "";   // the pure refusal code when !allowed (a literal from CodeOf)
};

PlayerbotAI* AiOf(Player* bot)
{
    return bot ? PlayerbotAIStorage::Instance().GetAI(bot) : nullptr;
}

BotState StateOf(ToggleState state)
{
    switch (state)
    {
        case ToggleState::Combat:   return BotState::BOT_STATE_COMBAT;
        case ToggleState::Reaction: return BotState::BOT_STATE_REACTION;
        default:                    return BotState::BOT_STATE_NON_COMBAT;
    }
}

// A stored manual value ("manual saved string"), the way the strategy glue reads it. Empty without an engine.
std::string ValueOf(PlayerbotAI* botAi, char const* key)
{
    if (!botAi || !botAi->GetAiObjectContext())
        return std::string();
    return botAi->GetAiObjectContext()->GetValue<std::string>("manual saved string", key)->Get();
}

void PutValue(PlayerbotAI* botAi, char const* key, std::string const& value)
{
    botAi->GetAiObjectContext()->GetValue<std::string>("manual saved string", key)->Set(value);
}

// The tail from `from` on, joined with single spaces (a `set` list may be typed with blanks around the commas).
std::string JoinWords(std::vector<std::string> const& words, std::size_t from)
{
    std::string out;
    for (std::size_t i = from; i < words.size(); ++i)
    {
        if (!out.empty())
            out += ' ';
        out += words[i];
    }
    return out;
}

void OkBot(Player* player, std::string const& verb, Player* bot, std::string const& what)
{
    player::Say(player, "TortoiseBots: " + verb + " " + bot->GetName() + ": ok " + what);
}

void RefuseBot(Player* player, std::string const& verb, Player* bot, char const* code, std::string const& text)
{
    player::Refuse(player, verb + " " + bot->GetName(), code, text);
}

// For `party`: one line, the write count and the refusal count. A named bot answers for itself.
void Summary(Player* player, std::string const& verb, ParsedScope const& parsed, std::size_t ok, std::size_t refused)
{
    if (parsed.kind == ScopeKind::Party)
        player::Say(player, "TortoiseBots: " + verb + " party: " + std::to_string(ok) + " set, " + std::to_string(refused) + " refused");
}

// The refusal of one bot's four stored orders: "" when every named totem is trained (auto and none need nothing).
bool TotemSetKnown(PlayerbotAI* botAi, std::string const stored[4], std::string& unknown)
{
    for (int slot = 0; slot < 4; ++slot)
    {
        std::string const& totem = stored[slot];
        if (totem.empty() || totem == "auto")
            continue;
        if (!botAi || !botAi->HasSpell(totem))
        {
            unknown = std::string(ai::TotemSlotName(slot)) + "=" + totem;
            return false;
        }
    }
    return true;
}

// The stored words of one bot, as the status line and a `set` answer print them (an unset slot reads "none").
std::string TotemWords(PlayerbotAI* botAi)
{
    std::string out;
    for (int slot = 0; slot < 4; ++slot)
    {
        std::string const stored = ValueOf(botAi, ai::TotemOrderKey(slot));
        out += std::string(slot ? "," : "") + ai::TotemSlotName(slot) + "=" + (stored.empty() ? "none" : stored);
    }
    return out;
}

std::string SetWords(std::string const stored[4])
{
    std::string out;
    for (int slot = 0; slot < 4; ++slot)
        out += std::string(slot ? "," : "") + ai::TotemSlotName(slot) + "=" + (stored[slot].empty() ? "none" : stored[slot]);
    return out;
}

std::string ModeOf(PlayerbotAI* botAi)
{
    std::string const stored = ValueOf(botAi, ai::TotemModeKey());
    return stored.empty() ? "auto" : stored;
}

// The bots this line addresses: the caller's group's bots in name order (at most 40) for `party`, else the named one. The pure
// decision runs for every target at the line's tier before a handler changes anything.
bool TargetsOf(Player* player, player::Caller const& caller, player::OwnerScope const& scope, std::string const& verb,
    ParsedScope const& parsed, player::Tier tier, std::vector<Target>& targets)
{
    targets.clear();
    if (parsed.kind == ScopeKind::Party)
    {
        Group* group = player->GetGroup();
        if (!group)
        {
            player::Refuse(player, verb, "no-party", "you are not in a group");
            return false;
        }
        std::vector<Player*> bots;
        for (Group::MemberSlot const& slot : group->GetMemberSlots())
        {
            Player* member = ObjectAccessor::FindPlayer(slot.guid);
            if (!member || !member->IsInWorld())
                continue;
            if (!BotMgr::Instance().IsBot(member->GetGUID()))
                continue;
            bots.push_back(member);
        }
        std::sort(bots.begin(), bots.end(), [](Player* left, Player* right) { return left->GetName() < right->GetName(); });
        if (bots.size() > kMaxPartyBots)
            bots.resize(kMaxPartyBots);
        if (bots.empty())
        {
            player::Refuse(player, verb, "no-bot", "no live bots in your party");
            return false;
        }
        for (Player* bot : bots)
        {
            player::Verdict const verdict = player::Decide(caller, player::FactsOf(player, bot, scope), tier);
            targets.push_back({ bot, verdict == player::Verdict::Allowed, player::CodeOf(verdict) });
        }
        return true;
    }

    Player* bot = nullptr;
    std::string code;
    if (!player::ResolveBot(parsed.name, bot, code))
    {
        player::Refuse(player, verb, code.c_str(), code == "not-found" ? "no such online bot" : "that character is not a bot");
        return false;
    }
    player::Verdict const verdict = player::Decide(caller, player::FactsOf(player, bot, scope), tier);
    if (verdict != player::Verdict::Allowed)
    {
        player::Refuse(player, verb, player::CodeOf(verdict), "you may not change that bot");
        return false;
    }
    targets.push_back({ bot, true, "" });
    return true;
}

// The one check every write shares before a bot's class matters: an allowed bot with a running engine.
bool LiveTarget(Player* player, std::string const& verb, Target const& target, PlayerbotAI*& botAi)
{
    botAi = AiOf(target.bot);
    if (!target.allowed)
    {
        RefuseBot(player, verb, target.bot, target.code, "you may not change that bot");
        return false;
    }
    if (!botAi || !botAi->GetAiObjectContext())
    {
        RefuseBot(player, verb, target.bot, "no-engine", "the bot has no running engine");
        return false;
    }
    return true;
}

// A totem order belongs to a shaman only.
bool ShamanTarget(Player* player, std::string const& verb, Target const& target, PlayerbotAI*& botAi)
{
    if (!LiveTarget(player, verb, target, botAi))
        return false;
    if (target.bot->GetClass() != CLASS_SHAMAN)
    {
        RefuseBot(player, verb, target.bot, "not-shaman", "only shamans keep totem orders");
        return false;
    }
    return true;
}
}

bool HandleTotem(Player* player, player::Caller const& caller, player::OwnerScope const& scope, std::string_view tail)
{
    std::string const verb = "totem";
    std::string const usage =
        ".bot totem <bot|party> <earth|fire|water|air> <totem|auto|none> | set <earth>,<fire>,<water>,<air> | "
        "mode <auto|prepull|manual> | status";
    std::vector<std::string> const words = ChoiceWords(tail);
    if (words.empty())
    {
        player::Refuse(player, verb, "usage", usage);
        return true;
    }
    ParsedScope const parsed = ParseScope(words[0]);
    std::string const sub = words.size() > 1 ? words[1] : std::string();

    if (sub == "status")
    {
        std::vector<Target> targets;
        if (!TargetsOf(player, caller, scope, verb, parsed, TierOfChoice(verb, sub), targets))
            return true;
        std::size_t ok = 0, refused = 0;
        for (Target const& target : targets)
        {
            PlayerbotAI* botAi = nullptr;
            if (!ShamanTarget(player, verb, target, botAi))
            {
                ++refused;
                continue;
            }
            OkBot(player, verb, target.bot, "status " + TotemWords(botAi) + " mode=" + ModeOf(botAi));
            ++ok;
        }
        Summary(player, verb, parsed, ok, refused);
        return true;
    }

    if (sub == "mode")
    {
        std::string const word = words.size() > 2 ? words[2] : std::string();
        if (word != "auto" && word != "prepull" && word != "pre-pull" && word != "manual")
        {
            player::Refuse(player, verb, "usage", ".bot totem <bot|party> mode <auto|prepull|manual>");
            return true;
        }
        std::string const stored = ai::TotemDropModeName(ai::ParseTotemDropMode(word));
        std::vector<Target> targets;
        if (!TargetsOf(player, caller, scope, verb, parsed, TierOfChoice(verb, sub), targets))
            return true;
        std::size_t ok = 0, refused = 0;
        for (Target const& target : targets)
        {
            PlayerbotAI* botAi = nullptr;
            if (!ShamanTarget(player, verb, target, botAi))
            {
                ++refused;
                continue;
            }
            PutValue(botAi, ai::TotemModeKey(), stored);
            OkBot(player, verb, target.bot, "mode " + stored);
            ++ok;
        }
        Summary(player, verb, parsed, ok, refused);
        return true;
    }

    if (sub == "set")
    {
        std::string stored[4];
        if (!ai::ParseTotemSetList(JoinWords(words, 2), stored))
        {
            player::Refuse(player, verb, "usage", ".bot totem <bot|party> set <earth>,<fire>,<water>,<air>");
            return true;
        }
        std::vector<Target> targets;
        if (!TargetsOf(player, caller, scope, verb, parsed, TierOfChoice(verb, sub), targets))
            return true;
        std::size_t ok = 0, refused = 0;
        std::vector<Player*> ready;
        // Pass 1: every target is checked (class, every slot trained) before the first write; a bot is set whole or not at all.
        for (Target const& target : targets)
        {
            PlayerbotAI* botAi = nullptr;
            if (!ShamanTarget(player, verb, target, botAi))
            {
                ++refused;
                continue;
            }
            std::string unknown;
            if (!TotemSetKnown(botAi, stored, unknown))
            {
                RefuseBot(player, verb, target.bot, "not-known", "not trained: " + unknown);
                ++refused;
                continue;
            }
            ready.push_back(target.bot);
        }

        // Pass 2: nothing but writes, for the bots that passed every check.
        for (Player* bot : ready)
        {
            PlayerbotAI* botAi = AiOf(bot);
            for (int slot = 0; slot < 4; ++slot)
                PutValue(botAi, ai::TotemOrderKey(slot), stored[slot]);
            OkBot(player, verb, bot, "set " + SetWords(stored));
            ++ok;
        }
        Summary(player, verb, parsed, ok, refused);
        return true;
    }

    // `<slot> <totem|auto|none>`: one slot of the four.
    int const slot = ai::TotemSlotIndex(sub);
    std::string const word = words.size() > 2 ? words[2] : std::string();
    if (slot < 0 || word.empty())
    {
        player::Refuse(player, verb, "usage", usage);
        return true;
    }
    bool valid = false;
    std::string const stored = ai::NormalizeTotemOrder(slot, word, valid);
    if (!valid)
    {
        player::Refuse(player, verb, "not-known", "not a " + std::string(ai::TotemSlotName(slot)) + " totem: " + word);
        return true;
    }
    std::vector<Target> targets;
    if (!TargetsOf(player, caller, scope, verb, parsed, TierOfChoice(verb, sub), targets))
        return true;
    std::size_t ok = 0, refused = 0;
    for (Target const& target : targets)
    {
        PlayerbotAI* botAi = nullptr;
        if (!ShamanTarget(player, verb, target, botAi))
        {
            ++refused;
            continue;
        }
        if (!stored.empty() && stored != "auto" && !botAi->HasSpell(stored))
        {
            RefuseBot(player, verb, target.bot, "not-known", "not trained: " + stored);
            ++refused;
            continue;
        }
        PutValue(botAi, ai::TotemOrderKey(slot), stored);
        OkBot(player, verb, target.bot, std::string(ai::TotemSlotName(slot)) + "=" + (stored.empty() ? "none" : stored));
        ++ok;
    }
    Summary(player, verb, parsed, ok, refused);
    return true;
}

bool HandleBehavior(Player* player, player::Caller const& caller, player::OwnerScope const& scope, std::string_view tail)
{
    std::string const verb = "behavior";
    std::string const usage =
        ".bot behavior <bot|party> <loot|aoe|autocc|savemana|boost|threat|potions|mimic|spread|hold|noblink|offheal|offdps> "
        "<on|off> | status";
    std::vector<std::string> const words = ChoiceWords(tail);
    if (words.empty())
    {
        player::Refuse(player, verb, "usage", usage);
        return true;
    }
    ParsedScope const parsed = ParseScope(words[0]);
    std::string const sub = words.size() > 1 ? words[1] : std::string();

    if (sub == "status")
    {
        std::vector<Target> targets;
        if (!TargetsOf(player, caller, scope, verb, parsed, TierOfChoice(verb, sub), targets))
            return true;
        std::size_t ok = 0, refused = 0;
        for (Target const& target : targets)
        {
            PlayerbotAI* botAi = nullptr;
            if (!LiveTarget(player, verb, target, botAi))
            {
                ++refused;
                continue;
            }
            bool const offSpec = ai::ClassHasOffSpec(target.bot->GetClass());
            std::size_t count = 0;
            Toggle const* table = ToggleTable(count);
            std::string line;
            for (std::size_t i = 0; i < count; ++i)
            {
                if (!ToggleAppliesToClass(table[i], offSpec))
                    continue;
                bool const has = botAi->HasStrategy(table[i].strategy, StateOf(table[i].state));
                line += std::string(line.empty() ? "" : ",") + table[i].key + "=" + (IsToggleOn(has, table[i]) ? "on" : "off");
            }
            OkBot(player, verb, target.bot, "status " + line);
            ++ok;
        }
        Summary(player, verb, parsed, ok, refused);
        return true;
    }

    // `<toggle> <on|off>`
    Toggle const* toggle = FindToggle(sub);
    bool enable = false;
    if (!toggle || words.size() < 3 || !ParseOnOff(words[2], enable))
    {
        player::Refuse(player, verb, "usage", usage);
        return true;
    }
    std::vector<Target> targets;
    if (!TargetsOf(player, caller, scope, verb, parsed, TierOfChoice(verb, sub), targets))
        return true;
    std::size_t ok = 0, refused = 0;
    for (Target const& target : targets)
    {
        PlayerbotAI* botAi = nullptr;
        if (!LiveTarget(player, verb, target, botAi))
        {
            ++refused;
            continue;
        }
        if (!ToggleAppliesToClass(*toggle, ai::ClassHasOffSpec(target.bot->GetClass())))
        {
            RefuseBot(player, verb, target.bot, "class", "only paladins, priests, shamans and druids have off-spec switches");
            ++refused;
            continue;
        }
        if (!botAi->GetAiObjectContext()->GetStrategy(toggle->strategy))
        {
            RefuseBot(player, verb, target.bot, "not-in-build", toggle->strategy + std::string(" is not a strategy this build registers"));
            ++refused;
            continue;
        }
        botAi->ChangeStrategy(ToggleChange(enable, *toggle), StateOf(toggle->state));
        bool const holds = IsToggleOn(botAi->HasStrategy(toggle->strategy, StateOf(toggle->state)), *toggle);
        if (holds != enable)
        {
            RefuseBot(player, verb, target.bot, "failed", "the engine does not hold " + std::string(toggle->key) + " as asked");
            ++refused;
            continue;
        }
        OkBot(player, verb, target.bot, std::string(toggle->key) + "=" + (enable ? "on" : "off"));
        ++ok;
    }
    Summary(player, verb, parsed, ok, refused);
    return true;
}
}
}

#else   // no engine: the verbs stay the core's unknown command (Dispatch never reaches them)

namespace TortoiseBots
{
namespace choice
{
bool HandleTotem(Player*, player::Caller const&, player::OwnerScope const&, std::string_view) { return false; }
bool HandleBehavior(Player*, player::Caller const&, player::OwnerScope const&, std::string_view) { return false; }
}
}

#endif

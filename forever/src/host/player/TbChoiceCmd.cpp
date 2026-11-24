// The `.bot totem` and `.bot behavior` handlers (#133 round 1) and `.bot blessing`, `.bot choice` and `.bot service` (#133
// round 2). The pure rules are host/player/TbChoiceCmdPure.h and the class policies it reads (tested standalone by
// forever/tools/test_choice_cmd.cpp); the bot lookup, the pure ownership decision and the reply channel are TbPlayerCmd.cpp's,
// shared through TbPlayerCmd.h. Values live until logout: persistence is #135.
#include "host/player/TbChoiceCmd.h"
#include "host/player/TbChoiceCmdPure.h"
#include "host/player/TbPlayerCmd.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

// The bot brain's headers are written against cmangos names: the ForeverCore preinclude goes in front of them, as in TbCc.cpp.
#include "compat/botpch_forever.h"
#include "compat/TbCompat_K11w.h"   // AddItemCompat: the portal rune, the old Player::StoreNewItemInInventorySlot

#include "TbBotMgr.h"
#include "PlayerbotAIStorage.h"

#include "Group.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "playerbot/BotState.h"
#include "playerbot/MagePortalPolicy.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"
#include "playerbot/strategy/generic/ClassChoicePolicy.h"
#include "playerbot/strategy/generic/OffSpecPolicy.h"
#include "playerbot/strategy/paladin/PaladinBlessingOrderPolicy.h"
#include "playerbot/strategy/rogue/RogueTriggers.h"
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
std::string ValueOf(PlayerbotAI* botAi, std::string const& key)
{
    if (!botAi || !botAi->GetAiObjectContext())
        return std::string();
    return botAi->GetAiObjectContext()->GetValue<std::string>("manual saved string", key)->Get();
}

void PutValue(PlayerbotAI* botAi, std::string const& key, std::string const& value)
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

// A blessing order belongs to a paladin only.
bool PaladinTarget(Player* player, std::string const& verb, Target const& target, PlayerbotAI*& botAi)
{
    if (!LiveTarget(player, verb, target, botAi))
        return false;
    if (target.bot->GetClass() != CLASS_PALADIN)
    {
        RefuseBot(player, verb, target.bot, "not-paladin", "only paladins keep blessing orders");
        return false;
    }
    return true;
}

// Whether the paladin's own book has the blessing the word names ("auto" needs nothing: it is the automatic pick).
bool BlessingKnown(PlayerbotAI* botAi, std::string const& word)
{
    std::string const spell = ai::BlessingOrderSpell(word);
    return spell.empty() || (botAi && botAi->HasSpell(spell));
}

// The status row: every class word with the stored order ("auto" when unset), the blessings the bot knows, and the
// per-character exceptions.
std::string BlessingStatusWords(PlayerbotAI* botAi)
{
    std::string out;
    for (int i = 0; i < ai::kBlessingOrderClassCount; ++i)
    {
        std::string const stored = ValueOf(botAi, ai::BlessingOrderKey(i));
        out += std::string(i ? "," : "") + ai::BlessingOrderClassWord(i) + "=" +
               (ai::BlessingOrderSpell(stored).empty() ? "auto" : stored);
    }
    out += ",known=";
    bool first = true;
    for (int i = 0; i < ai::kBlessingOrderBlessingCount; ++i)
    {
        std::string const word = ai::BlessingOrderWord(i);
        if (!BlessingKnown(botAi, word))
            continue;
        out += (first ? "" : "+") + word;
        first = false;
    }
    std::string const players = ai::FormatBlessingPlayerOrders(
        ai::ParseBlessingPlayerOrders(ValueOf(botAi, ai::BlessingOrderPlayersKey())));
    if (!players.empty())
        out += ",players=" + players;
    return out;
}

// A choice word the bot can use (old TB's `ChoiceKnown`, commands/Choice.cpp:69): a trained spell, or for a poison (a bag
// item, not a spell) a usable poison item for that hand.
bool ChoiceKnown(PlayerbotAI* botAi, Player* bot, int kind, std::string const& word)
{
    if (ai::ChoiceKindUsesItem(kind))
        return ai::HasUsablePoisonItem(botAi, bot, ai::ChoicePoisonItemIds(word));
    return botAi && botAi->HasSpell(ai::ChoiceSpell(kind, word));
}

// The status row: every kind the class has, the stored word ("auto" when unset) and the words this bot can use.
std::string ChoiceStatusWords(PlayerbotAI* botAi, Player* bot)
{
    std::string out;
    for (int kind : ai::ChoiceKindsForClass(bot->GetClass()))
    {
        std::string const stored = ValueOf(botAi, ai::ChoiceOrderKey(kind));
        out += std::string(out.empty() ? "" : ",") + ai::ChoiceKindWord(kind) + "=" + (stored.empty() ? "auto" : stored);
        out += "," + std::string(ai::ChoiceKindWord(kind)) + ".known=";
        bool first = true;
        for (int i = 0; i < ai::ChoiceWordCount(kind); ++i)
        {
            std::string const word = ai::ChoiceWord(kind, i);
            if (!ChoiceKnown(botAi, bot, kind, word))
                continue;
            out += (first ? "" : "+") + word;
            first = false;
        }
    }
    return out;
}

// The highest rank of a conjure line the bot's own book has (the id the cast should use), 0 when none: a rank is "higher"
// when its spell level is, a tie broken by the spell id (the newer row).
std::uint32_t HighestSpellOfLine(Player* bot, std::string const& line)
{
    if (!bot)
        return 0;
    std::string const prefix = player::LowerWord(line);
    std::uint32_t best = 0;
    std::uint32_t bestLevel = 0;
    for (auto const& known : bot->GetSpellMap())
    {
        std::uint32_t const spellId = known.first;
        if (known.second.state == PLAYERSPELL_REMOVED || known.second.disabled)
            continue;
        SpellEntry const* info = sServerFacade.LookupSpellInfo(spellId);
        if (!info)
            continue;
        if (player::LowerWord(info->SpellName[0]).compare(0, prefix.size(), prefix) != 0)
            continue;
        if (best == 0 || info->spellLevel > bestLevel || (info->spellLevel == bestLevel && spellId > best))
        {
            best = spellId;
            bestLevel = info->spellLevel;
        }
    }
    return best;
}

// Any rank of the line counts as known, for the service query.
bool KnowsSpellLine(Player* bot, char const* line)
{
    return HighestSpellOfLine(bot, std::string(line)) != 0;
}

// The context's own action lookup (TbOrders.cpp's ActionRegistered); it caches what it finds, so asking never creates a
// second object.
bool ActionRegistered(PlayerbotAI* botAi, char const* key)
{
    return botAi->GetAiObjectContext() && botAi->GetAiObjectContext()->GetAction(key) != nullptr;
}

// A decimal spell id, 0 for anything else (a leading zero or an overflow reads as no id).
std::uint32_t ParseSpellId(std::string const& word)
{
    if (word.empty() || word.size() > 9)
        return 0;
    std::uint32_t value = 0;
    for (char c : word)
    {
        if (c < '0' || c > '9')
            return 0;
        value = value * 10 + static_cast<std::uint32_t>(c - '0');
    }
    return value;
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

bool HandleBlessing(Player* player, player::Caller const& caller, player::OwnerScope const& scope, std::string_view tail)
{
    std::string const verb = "blessing";
    std::string const usage =
        ".bot blessing <bot|party> <warrior|paladin|hunter|rogue|priest|shaman|mage|warlock|druid> "
        "<might|wisdom|kings|salvation|sanctuary|light|auto> | player <name> <might|wisdom|kings|salvation|sanctuary|light|auto> | "
        "status";
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
            if (!PaladinTarget(player, verb, target, botAi))
            {
                ++refused;
                continue;
            }
            OkBot(player, verb, target.bot, "status " + BlessingStatusWords(botAi));
            ++ok;
        }
        Summary(player, verb, parsed, ok, refused);
        return true;
    }

    bool const playerMode = sub == "player";
    int classIndex = -1;
    std::string targetName;
    std::string word;
    if (playerMode)
    {
        if (words.size() < 4 || words[2].empty())
        {
            player::Refuse(player, verb, "usage", usage);
            return true;
        }
        targetName = words[2];
        // The stored list is `Name:blessing+Name:blessing`: a name holding one of its separators, or any other character
        // a character name cannot hold, would split or corrupt the list, so it is refused before any paladin is touched.
        if (!IsBlessingPlayerName(targetName) || !normalizePlayerName(targetName))
        {
            player::Refuse(player, verb, "usage", "not a character name: " + targetName);
            return true;
        }
        word = words[3];
    }
    else
    {
        classIndex = ai::BlessingOrderClassIndex(sub);
        if (classIndex < 0 || words.size() < 3)
        {
            player::Refuse(player, verb, "usage", usage);
            return true;
        }
        word = words[2];
    }
    if (!ai::IsBlessingOrderWord(word))
    {
        player::Refuse(player, verb, "usage", "not a blessing: " + word);
        return true;
    }

    std::vector<Target> targets;
    if (!TargetsOf(player, caller, scope, verb, parsed, TierOfChoice(verb, sub), targets))
        return true;
    std::size_t ok = 0, refused = 0;
    for (Target const& target : targets)
    {
        PlayerbotAI* botAi = nullptr;
        if (!PaladinTarget(player, verb, target, botAi))
        {
            ++refused;
            continue;
        }
        if (!BlessingKnown(botAi, word))
        {
            RefuseBot(player, verb, target.bot, "not-known", "not trained: " + ai::BlessingOrderSpell(word));
            ++refused;
            continue;
        }
        if (playerMode)
            PutValue(botAi, ai::BlessingOrderPlayersKey(),
                ai::SetBlessingPlayerOrder(ValueOf(botAi, ai::BlessingOrderPlayersKey()), targetName, word));
        else
            PutValue(botAi, ai::BlessingOrderKey(classIndex), word);
        OkBot(player, verb, target.bot,
            "set " + (playerMode ? "player " + targetName : std::string(ai::BlessingOrderClassWord(classIndex))) + "=" + word);
        ++ok;
    }
    Summary(player, verb, parsed, ok, refused);
    return true;
}

bool HandleChoice(Player* player, player::Caller const& caller, player::OwnerScope const& scope, std::string_view tail)
{
    std::string const verb = "choice";
    std::string const usage =
        ".bot choice <bot|party> <aura|aspect|curse|poison_mh|poison_oh|sting|pet> <value|auto> | status";
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
            if (ai::ChoiceKindsForClass(target.bot->GetClass()).empty())
            {
                RefuseBot(player, verb, target.bot, "class", "that class keeps no choice");
                ++refused;
                continue;
            }
            OkBot(player, verb, target.bot, "status " + ChoiceStatusWords(botAi, target.bot));
            ++ok;
        }
        Summary(player, verb, parsed, ok, refused);
        return true;
    }

    // `<kind> <value|auto>`
    int const kind = ai::ChoiceKindIndex(sub);
    std::string const word = words.size() > 2 ? words[2] : std::string();
    if (kind < 0 || word.empty() || !ai::IsChoiceWord(kind, word))
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
        std::vector<int> const kinds = ai::ChoiceKindsForClass(target.bot->GetClass());
        if (std::find(kinds.begin(), kinds.end(), kind) == kinds.end())
        {
            RefuseBot(player, verb, target.bot, "class", "that class keeps no " + std::string(ai::ChoiceKindWord(kind)) + " choice");
            ++refused;
            continue;
        }
        if (word != "auto" && !ChoiceKnown(botAi, target.bot, kind, word))
        {
            RefuseBot(player, verb, target.bot, "not-known",
                std::string(ai::ChoiceKindUsesItem(kind) ? "no usable item: " : "not trained: ") + ai::ChoiceSpell(kind, word));
            ++refused;
            continue;
        }
        PutValue(botAi, ai::ChoiceOrderKey(kind), word);
        OkBot(player, verb, target.bot, std::string(ai::ChoiceKindWord(kind)) + "=" + word);
        ++ok;
    }
    Summary(player, verb, parsed, ok, refused);
    return true;
}

bool HandleService(Player* player, player::Caller const& caller, player::OwnerScope const& scope, std::string_view tail)
{
    std::string const verb = "service";
    std::string const usage =
        ".bot service <bot|party> | portal <spellId> | conjure <food|water|healthstone>";
    std::vector<std::string> const words = ChoiceWords(tail);
    if (words.empty())
    {
        player::Refuse(player, verb, "usage", usage);
        return true;
    }
    ParsedScope const parsed = ParseScope(words[0]);
    std::string const sub = words.size() > 1 ? words[1] : std::string();
    std::vector<Target> targets;
    if (!TargetsOf(player, caller, scope, verb, parsed, TierOfChoice(verb, sub), targets))
        return true;

    // The bare query reads only (Tactical tier): what the bot can conjure and which portals it knows.
    if (sub.empty())
    {
        std::size_t ok = 0, refused = 0;
        for (Target const& target : targets)
        {
            PlayerbotAI* botAi = nullptr;
            if (!LiveTarget(player, verb, target, botAi))
            {
                ++refused;
                continue;
            }
            std::vector<std::uint32_t> known;
            std::size_t count = 0;
            ai::MagePortalSpell const* table = ai::MagePortalTable(&count);
            for (std::size_t i = 0; i < count; ++i)
                if (target.bot->HasSpell(table[i].spellId))
                    known.push_back(table[i].spellId);
            OkBot(player, verb, target.bot,
                "food=" + std::to_string(KnowsSpellLine(target.bot, ai::ConjureFoodSpellName()) ? 1 : 0) +
                " water=" + std::to_string(KnowsSpellLine(target.bot, ai::ConjureWaterSpellName()) ? 1 : 0) +
                " healthstone=" + std::to_string(KnowsSpellLine(target.bot, ai::CreateHealthstoneSpellName()) ? 1 : 0) +
                " portals=" + ai::FormatServicePortals(known));
            ++ok;
        }
        Summary(player, verb, parsed, ok, refused);
        return true;
    }

    if (sub == "portal")
    {
        std::uint32_t const spellId = ParseSpellId(words.size() > 2 ? words[2] : std::string());
        if (!spellId || !*ai::PortalCityName(spellId))
        {
            player::Refuse(player, verb, "unknown", "unknown portal city");
            return true;
        }
        std::size_t ok = 0, refused = 0;
        for (Target const& target : targets)
        {
            PlayerbotAI* botAi = nullptr;
            if (!LiveTarget(player, verb, target, botAi))
            {
                ++refused;
                continue;
            }
            // The policy's own refusal order (MagePortalPolicy.h): dead, combat, casting, not a mage, not learned, not grouped.
            ai::PortalCastFacts facts;
            facts.alive = target.bot->IsAlive();
            facts.inCombat = target.bot->IsInCombat();
            facts.casting = target.bot->IsNonMeleeSpellCasted(false);
            facts.isMage = target.bot->GetClass() == CLASS_MAGE;
            facts.requesterGrouped = player && player->IsInSameRaidWith(target.bot);
            ai::PortalCastDecision const decision = ai::DecidePortalCast(facts, spellId, target.bot->HasSpell(spellId),
                target.bot->HasItemCount(ai::PortalRuneItemId(), 1), target.bot->isMoving());
            if (decision.refusal != ai::PortalRefusal::None)
            {
                RefuseBot(player, verb, target.bot, ai::PortalRefusalCode(decision.refusal),
                    ai::PortalRefusalText(decision.refusal));
                ++refused;
                continue;
            }
            if (!ActionRegistered(botAi, "cast"))
            {
                RefuseBot(player, verb, target.bot, "action-not-registered",
                    "the legacy action `cast` is not registered in this build");
                ++refused;
                continue;
            }
            if (decision.needsRune)
                AddItemCompat(target.bot, ai::PortalRuneItemId(), 1);
            if (decision.needsStop)
                botAi->StopMoving();
            // A seated bot (drinking) would stand up and fail this one cast attempt.
            if (target.bot->GetStandState() != UNIT_STAND_STATE_STAND)
                target.bot->SetStandState(UNIT_STAND_STATE_STAND);
            if (!botAi->DoSpecificAction("cast", ai::Event("service portal", std::to_string(spellId), player), true))
            {
                RefuseBot(player, verb, target.bot, "failed", "the cast did not run; try again");
                ++refused;
                continue;
            }
            OkBot(player, verb, target.bot, "portal " + std::string(ai::PortalCityName(spellId)));
            ++ok;
        }
        Summary(player, verb, parsed, ok, refused);
        return true;
    }

    if (sub == "conjure")
    {
        std::string const word = words.size() > 2 ? words[2] : std::string();
        char const* line = nullptr;
        if (word == "food")
            line = ai::ConjureFoodSpellName();
        else if (word == "water")
            line = ai::ConjureWaterSpellName();
        else if (word == "healthstone")
            line = ai::CreateHealthstoneSpellName();
        if (!line)
        {
            player::Refuse(player, verb, "usage", usage);
            return true;
        }
        std::size_t ok = 0, refused = 0;
        for (Target const& target : targets)
        {
            PlayerbotAI* botAi = nullptr;
            if (!LiveTarget(player, verb, target, botAi))
            {
                ++refused;
                continue;
            }
            if (!target.bot->IsAlive())
            {
                RefuseBot(player, verb, target.bot, "dead", "the bot is dead");
                ++refused;
                continue;
            }
            std::uint32_t const spellId = HighestSpellOfLine(target.bot, line);
            if (!spellId)
            {
                RefuseBot(player, verb, target.bot, "not-known", std::string("has not trained: ") + line);
                ++refused;
                continue;
            }
            if (!ActionRegistered(botAi, "cast"))
            {
                RefuseBot(player, verb, target.bot, "action-not-registered",
                    "the legacy action `cast` is not registered in this build");
                ++refused;
                continue;
            }
            if (target.bot->GetStandState() != UNIT_STAND_STATE_STAND)
                target.bot->SetStandState(UNIT_STAND_STATE_STAND);
            if (!botAi->DoSpecificAction("cast", ai::Event("service conjure", std::to_string(spellId), player), true))
            {
                RefuseBot(player, verb, target.bot, "failed", "the cast did not run; try again");
                ++refused;
                continue;
            }
            OkBot(player, verb, target.bot, "conjure " + word + " " + std::to_string(spellId));
            ++ok;
        }
        Summary(player, verb, parsed, ok, refused);
        return true;
    }

    player::Refuse(player, verb, "usage", usage);
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
bool HandleBlessing(Player*, player::Caller const&, player::OwnerScope const&, std::string_view) { return false; }
bool HandleChoice(Player*, player::Caller const&, player::OwnerScope const&, std::string_view) { return false; }
bool HandleService(Player*, player::Caller const&, player::OwnerScope const&, std::string_view) { return false; }
}
}

#endif

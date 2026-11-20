// Slice 11, package P11: see TbPet.h. The command parsing and the replies use the core and TbH3.h only; the engine-facing part is behind TORTOISEBOTS_SLICE1 exactly as
// TbOrders.cpp and TbCc.cpp keep it, and the default build answers `error code=engine-not-built` (the second half of this file).
#include "host/TbPet.h"
#include "host/TbH3.h"
#include "TbBotMgr.h"
#include "host/TbEngineBrain.h"

#include "Chat.h"
#include "Player.h"

#include <cctype>
#include <string>

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "compat/botpch_forever.h"

#include <algorithm>
#include <string_view>
#include <vector>
#include "Group.h"
#include "ObjectAccessor.h"
#include "Pet.h"
#include "SpellMgr.h"
#include "Unit.h"

#include "compat/TbCompat_Unit.h"
#include "compat/TbCompat_V.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotFactory.h"
#include "playerbot/TbCompatP11.h"
#include "compat/TbCompat_Unported.h"
#include "host/TbPetFactory.h"
#include "PlayerbotAIStorage.h"
#include "runtime/policy/pet/WarlockPetPolicy.h"

namespace TortoiseBots
{
namespace p11
{
using namespace h3;

namespace
{
PlayerbotAI* AiOf(Player* player)
{
    return player ? PlayerbotAIStorage::Instance().GetAI(player) : nullptr;
}

char const* ReactWord(ReactStates state)
{
    switch (state)
    {
        case REACT_PASSIVE:    return "passive";
        case REACT_DEFENSIVE:  return "defensive";
        case REACT_AGGRESSIVE: return "aggressive";
        default:               return "other";
    }
}

uint32 PercentOfHealth(Unit const* unit)
{
    uint64 const max = unit->GetMaxHealth();
    return max ? uint32(std::min<uint64>(100, unit->GetHealth() * 100 / max)) : 0;
}

// The pet as the world has it now: the live pet, else the stable row of a dead one (Health 0: Call Pet refuses and Revive Pet reads it, G2), else none.
struct PetView
{
    uint32 entry = 0;
    char const* state = "none";   // alive | dead | none
    uint32 hppct = 0;
    Pet* live = nullptr;
};

PetView ViewOf(Player* bot)
{
    PetView view;
    if (Pet* pet = bot->GetPet())
    {
        view.live = pet;
        view.entry = pet->GetEntry();
        view.state = pet->IsAlive() ? "alive" : "dead";
        view.hppct = PercentOfHealth(pet);
        return view;
    }
    if (PetDeadCompat(bot))
    {
        view.state = "dead";
        if (PetStable const* stable = bot->GetPetStable())
            for (Optional<PetStable::PetInfo> const& info : stable->ActivePets)
                if (info && info->Health == 0)
                {
                    view.entry = info->CreatureId;
                    break;
                }
    }
    return view;
}

std::vector<std::string> AutocastIds(Pet const* pet)
{
    std::vector<std::string> ids;
    for (uint32 const spellId : pet->m_autospells)
        ids.push_back(std::to_string(spellId));
    return ids;
}

Player* PartyLeaderOrBot(Player* bot)
{
    if (Player* commander = CommanderOf(bot))
        return commander;
    Group* group = bot->GetGroup();
    Player* leader = group ? ObjectAccessor::FindPlayer(group->GetLeaderGUID()) : nullptr;
    return leader && leader->IsInWorld() ? leader : bot;
}

// Grammar addendum A6: `none` has no meaning and an unknown number is never written as 0: a bot with no pet has `state=` empty and no `entry` or `hppct`.
void PutPetFields(Rec& rec, Player* bot)
{
    PetView const view = ViewOf(bot);
    bool const any = view.live || view.state[0] != 'n';
    rec.S("state", any ? view.state : "");
    if (any)
    {
        if (view.entry)
            rec.N("entry", view.entry);
        rec.N("hppct", view.hppct);
    }
    if (view.live)
    {
        rec.S("react", ReactWord(view.live->GetReactState()));
        rec.L("auto", AutocastIds(view.live));
        if (bot->GetClass() == CLASS_HUNTER)
            rec.N("loyal", view.live->GetLoyaltyLevel());
    }
}

Player* ResolveBot(ChatHandler* handler, Words const& args, bool& failed)
{
    failed = false;
    if (args.w.empty())
    {
        Fail(handler, args, "pet", "missing-target", "name a bot");
        failed = true;
        return nullptr;
    }
    if (Lower(args.w[0]) == "party")
    {
        Fail(handler, args, "pet", "bad-target", ".tb pet takes one bot, not a party");
        failed = true;
        return nullptr;
    }
    size_t pos = 0;
    Address who = ResolveAddress(args, pos, SenderPlayer(handler), false);
    if (!who.ok())
    {
        Fail(handler, args, "pet", who.code, who.detail);
        failed = true;
        return nullptr;
    }
    return who.bots.front();
}

// The words `.tb pet <bot> order` hands to the engine's chat `pet` action (GenericActions.cpp SetPetAction). `abandon` and `dismiss` are not among them: the pet is
// never deleted by a word (lead decision D5), and dismissing has its own verb. `attack` takes the SENDER's selection in that action and has none here.
bool OrderWordAllowed(std::string const& word)
{
    return word == "autocast" || word == "aggressive" || word == "defensive" || word == "passive" || word == "follow" || word == "stay";
}

// One reply line `pet bot= verb= status= ...` plus the end line; the fields are read from the world after the verb.
bool Answer(ChatHandler* handler, Words const& args, Player* bot, char const* verb, char const* status, bool ok, std::string const& note = {})
{
    Reply reply(handler, args, "pet");
    reply.Begin();
    Rec rec("pet");
    rec.S("bot", bot->GetName()).S("verb", verb).S("status", status);
    PutPetFields(rec, bot);
    if (!note.empty())
        rec.T("note", note);
    reply.Line(rec);
    Rec end("pet");
    end.S("verb", verb).S("target", bot->GetName()).N("ok", ok ? 1 : 0).N("refused", ok ? 0 : 1);
    reply.End(end);
    if (!ok)
        handler->SetSentErrorMessage(true);
    return ok;
}
}

bool HandlePet(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    if (!RequireBuilt(handler, args, "pet"))
        return false;
    bool failed = false;
    Player* bot = ResolveBot(handler, args, failed);
    if (failed)
        return false;
    if (args.w.size() < 2)
        return Fail(handler, args, "pet", "missing-argument", "give | call | dismiss | show | order <words>");
    std::string const verb = Lower(args.w[1]);
    PlayerbotAI* ai = AiOf(bot);
    Player* owner = PartyLeaderOrBot(bot);

    if (verb == "show")
    {
        if (args.w.size() > 2)
            return Fail(handler, args, "pet", "unexpected-argument", args.w[2]);
        Reply reply(handler, args, "pet");
        reply.Begin();
        Rec rec("pet");
        rec.S("bot", bot->GetName()).S("verb", "show").S("status", "ok");
        PutPetFields(rec, bot);
        PetView const view = ViewOf(bot);
        if (view.live)
        {
            std::vector<std::string> known;
            for (uint32 const spellId : PetSpellIdsCompat(view.live))
                known.push_back(std::to_string(spellId));
            rec.L("spells", known);
            rec.N("tp_used", view.live->GetTrainingPointsUsed()).N("tp_total", view.live->GetTrainingPointsTotal());
        }
        reply.Line(rec);
        Rec end("pet");
        end.S("verb", "show").S("target", bot->GetName()).N("ok", 1).N("refused", 0);
        reply.End(end);
        return true;
    }

    if (verb == "give")
    {
        if (args.w.size() > 2)
            return Fail(handler, args, "pet", "unexpected-argument", args.w[2]);
        GiveResult const result = GiveHunterPet(bot);
        if (result != GiveResult::Ok)
            return Fail(handler, args, "pet", GiveResultCode(result), GiveResultText(result));
        // The spells follow at once (the factory's own call), so the reply shows the trained pet.
        PlayerbotFactory factory(bot, bot->GetLevel(), ITEM_QUALITY_LEGENDARY);
        factory.InitPetSpells();
        return Answer(handler, args, bot, "give", "ok", true);
    }

    if (!ai || !ai->GetAiObjectContext())
        return Fail(handler, args, "pet", "no-engine", "the bot has no running engine (`.tb engine <bot> on`)");

    if (verb == "call")
    {
        if (args.w.size() > 2)
            return Fail(handler, args, "pet", "unexpected-argument", args.w[2]);
        if (bot->GetClass() != CLASS_HUNTER)
            return Fail(handler, args, "pet", "wrong-class", "Call Pet is a hunter spell");
        if (bot->GetPet())
            return Fail(handler, args, "pet", "has-pet", "a pet is out already");
        bool const started = ai->DoSpecificAction("call pet", Event("tb pet", std::string(), owner), true);
        // Call Pet is a cast: `started` means the cast began, the pet is out when the cast ends (`.tb pet <bot> show`).
        return Answer(handler, args, bot, "call", started ? "started" : "refused", started,
            started ? std::string("the cast began; the pet is out when it ends") : std::string("the engine did not start Call Pet (no spell, or the core refuses)"));
    }

    if (verb == "dismiss")
    {
        if (args.w.size() > 2)
            return Fail(handler, args, "pet", "unexpected-argument", args.w[2]);
        Pet* pet = bot->GetPet();
        if (!pet)
            return Fail(handler, args, "pet", "no-pet", "the bot has no pet out");
        if (bot->GetClass() == CLASS_HUNTER)
        {
            bool const started = ai->DoSpecificAction("dismiss pet", Event("tb pet", std::string(), owner), true);
            return Answer(handler, args, bot, "dismiss", started ? "started" : "refused", started,
                started ? std::string("the Dismiss Pet cast began; the pet is gone when it ends") : std::string("the engine did not start Dismiss Pet"));
        }
        if (bot->GetClass() != CLASS_WARLOCK)
            return Fail(handler, args, "pet", "wrong-class", "only a hunter's or a warlock's pet");
        bool const gone = PetDismissCommandCompat(bot, pet);
        return Answer(handler, args, bot, "dismiss", gone ? "ok" : "refused", gone, gone ? std::string() : std::string("the core did not put the pet away (a mounted owner, or the pet is not its first controlled unit)"));
    }

    if (verb == "order")
    {
        if (args.w.size() < 3)
            return Fail(handler, args, "pet", "missing-argument", "order <autocast <spell name> | aggressive | defensive | passive | follow | stay>");
        std::string const word = Lower(args.w[2]);
        if (!OrderWordAllowed(word))
            return Fail(handler, args, "pet", "bad-argument", "an order is autocast <spell name>, aggressive, defensive, passive, follow or stay");
        if (word != "autocast" && args.w.size() > 3)
            return Fail(handler, args, "pet", "unexpected-argument", args.w[3]);
        if (!bot->GetPet())
            return Fail(handler, args, "pet", "no-pet", "the bot has no pet out");
        std::string text = word;
        for (size_t i = 3; i < args.w.size(); ++i)
            text += " " + args.w[i];
        bool const ok = ai->DoSpecificAction("pet", Event("tb pet", text, owner), true);
        return Answer(handler, args, bot, "order", ok ? "ok" : "refused", ok,
            ok ? std::string() : std::string("the engine's pet action refused the order (no such spell on the pet, not autocastable, or the action is not registered)"));
    }

    return Fail(handler, args, "pet", "bad-argument", "give | call | dismiss | show | order <words>");
}

void AppendPetState(Rec& rec, Player* bot)
{
    uint8 const cls = bot->GetClass();
    if (cls != CLASS_HUNTER && cls != CLASS_WARLOCK)
        return;
    PetView const view = ViewOf(bot);
    // `pet=` is empty with no pet (addendum A6: no `none` word, no 0 for "not there"); a dead pet whose body is gone is `pet=<entry>:dead:0`.
    if (view.live || view.state[0] != 'n')
        rec.P("pet", { std::to_string(view.entry), view.state, std::to_string(view.hppct) });
    else
        rec.S("pet", "");
    if (view.live)
    {
        rec.S("petreact", ReactWord(view.live->GetReactState()));
        rec.L("petauto", AutocastIds(view.live));
        if (cls == CLASS_HUNTER)
            rec.N("loyal", view.live->GetLoyaltyLevel());
    }
    if (cls == CLASS_HUNTER)
    {
        uint32 const ammo = AmmoItemIdCompat(bot);
        if (ammo)
            rec.P("ammo", { std::to_string(ammo), std::to_string(bot->GetItemCount(ammo)) });
        else
            rec.S("ammo", "");
    }
    else
        rec.N("shards", bot->GetItemCount(TortoiseBots::WARLOCK_SOUL_SHARD_ITEM));
}

void SelfTestRecords(Reply& reply)
{
    reply.Line(Rec("selftest").S("case", "state-p11-hunter").S("bot", "Mhunt").P("pet", { "1196", "alive", "100" }).S("petreact", "defensive").L("petauto", { "3150", "1742" }).N("loyal", 6).P("ammo", { "2512", "985" }));
    reply.Line(Rec("selftest").S("case", "state-p11-warlock").S("bot", "Mlock").P("pet", { "1860", "dead", "0" }).N("shards", 2));
    reply.Line(Rec("pet").S("bot", "Mhunt").S("verb", "order").S("status", "ok").N("entry", 1196).S("state", "alive").N("hppct", 100).S("react", "defensive").L("auto", { "3150" }).N("loyal", 6));
    reply.Line(Rec("selftest").S("case", "pet-end").S("verb", "order").S("target", "Mhunt").N("ok", 1).N("refused", 0));
}
}
}

#else   // not built with the slice: the same refusal the slice-3 stub gives (TbH3Off.cpp)

namespace TortoiseBots
{
namespace p11
{
namespace
{
std::string TokenOf(std::string_view tail)
{
    size_t end = tail.size();
    while (end > 0 && std::isspace(static_cast<unsigned char>(tail[end - 1])))
        --end;
    size_t start = end;
    while (start > 0 && !std::isspace(static_cast<unsigned char>(tail[start - 1])))
        --start;
    std::string_view const word = tail.substr(start, end - start);
    if (word.size() < 2 || word.size() > 33 || word[0] != '#')
        return {};
    for (size_t i = 1; i < word.size(); ++i)
    {
        char const c = word[i];
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '.' && c != ':' && c != '-')
            return {};
    }
    return std::string(word);
}

bool NotBuilt(ChatHandler* handler, Trinity::ChatCommands::Tail args, char const* cmd)
{
    static char const* const text = "the engine is not built in (this build has no group-fight commands)";
    std::string msg;
    for (char const* c = text; *c; ++c)
    {
        unsigned char const u = static_cast<unsigned char>(*c);
        if (std::isalnum(u) || u == '.' || u == '_' || u == '-')
            msg += *c;
        else
        {
            static char const* const hex = "0123456789ABCDEF";
            msg += '%';
            msg += hex[u >> 4];
            msg += hex[u & 15];
        }
    }
    std::string line = std::string("error code=engine-not-built cmd=") + cmd + " msg=" + msg;
    std::string const token = TokenOf(std::string_view(args));
    if (!token.empty())
        line += " " + token;
    handler->SendSysMessage(line);
    handler->SetSentErrorMessage(true);
    return false;
}
}

bool HandlePet(ChatHandler* handler, Trinity::ChatCommands::Tail args) { return NotBuilt(handler, args, "pet"); }
}
}

#endif

// Slice 8, host package H8: see TbCc.h. The command parsing and the replies use the core and TbH3.h only; the engine-facing part is behind TORTOISEBOTS_SLICE1
// exactly as TbOrders.cpp keeps it, and the default build answers `error code=engine-not-built` (the second half of this file).
#include "host/TbCc.h"
#include "host/TbCcPure.h"
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
#include <cstdlib>
#include <vector>
#include "Group.h"
#include "ObjectAccessor.h"
#include "Unit.h"

#include "compat/TbCompat_V.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"
#include "playerbot/strategy/values/PossibleAttackTargetsValue.h"
#include "PlayerbotAIStorage.h"
#include "runtime/CcOrderFamily.h"

namespace TortoiseBots
{
namespace h8
{
using namespace h3;

namespace
{
char const* const MarkWords[8] = { "star", "circle", "diamond", "triangle", "moon", "square", "cross", "skull" };

// A raid mark by its word (the legacy RtiTargetValue::GetRtiIndex order), -1 when the word is none of them.
int MarkIndex(std::string const& word)
{
    for (int i = 0; i < 8; ++i)
        if (word == MarkWords[i])
            return i;
    return -1;
}

constexpr int SkullIndex = 7;

PlayerbotAI* AiOf(Player* player)
{
    return player ? PlayerbotAIStorage::Instance().GetAI(player) : nullptr;
}

ai::Value<std::string>* StringValue(PlayerbotAI* ai, char const* key)
{
    return ai && ai->GetAiObjectContext() ? ai->GetAiObjectContext()->GetValue<std::string>(key) : nullptr;
}

// "none" is how the legacy values say "no mark / no family".
std::string NormalFamily(std::string const& family)
{
    std::string const token = DungeonRun::CcFamilyTokenNorm(family);
    return token.empty() ? std::string("none") : token;
}

// The value of a legacy string value, "none" and a missing value both read as the empty string (the grammar never writes `none`: addendum A6).
std::string ValueText(PlayerbotAI* ai, char const* key)
{
    ai::Value<std::string>* value = StringValue(ai, key);
    std::string text = value ? value->Get() : std::string();
    return text == "none" ? std::string() : text;
}

// Everything the order touches, so a result that does not hold can be put back exactly.
struct Undo
{
    struct Entry { ai::Value<std::string>* value; std::string old; };
    std::vector<Entry> values;
    ObjectGuid icons[8];
    Group* group = nullptr;
    ObjectGuid by;

    void Set(ai::Value<std::string>* value, std::string const& text)
    {
        values.push_back({ value, value->Get() });
        value->Set(text);
    }
    void Restore()
    {
        for (auto itr = values.rbegin(); itr != values.rend(); ++itr)
            itr->value->Set(itr->old);
        if (group)
            for (int pass = 0; pass < 2; ++pass)
                for (uint8 i = 0; i < 8; ++i)
                    if (group->GetTargetIcon(i) != icons[i])
                        group->SetTargetIcon(i, icons[i], by);
    }
};

// The cached "rti ..." targets read the group's icons: ask again.
void ResetTargets(PlayerbotAI* ai)
{
    if (!ai || !ai->GetAiObjectContext())
        return;
    for (char const* key : { "rti cc target", "rti target", "cc target" })
        if (ai::Value<Unit*>* value = ai->GetAiObjectContext()->GetValue<Unit*>(key))
            value->Reset();
}

// The two values a clear resets (TbCcPure.h ClearCcState).
struct CcValueAccess
{
    ai::Value<std::string>* cc;
    ai::Value<std::string>* family;

    std::string GetCc() { return cc->Get(); }
    std::string GetFamily() { return family->Get(); }
    void SetCc(std::string const& text) { cc->Set(text); }
    void SetFamily(std::string const& text) { family->Set(text); }
};

// The party's raid icons as the clear's undo sees them: every slot is snapshotted before the write, so a read-back that does not hold puts the slots back
// (two passes: a slot whose icon is still on its unit is asked again, as ApplyCcOrder's Undo does).
struct CcIconAccess
{
    Group* group;
    ObjectGuid by;
    ObjectGuid before[8];

    ObjectGuid Get(int index) { return group->GetTargetIcon(uint8(index)); }
    void Set(int index, ObjectGuid const& unit) { group->SetTargetIcon(uint8(index), unit, by); }
    void Snapshot()
    {
        if (group)
            for (uint8 i = 0; i < 8; ++i)
                before[i] = group->GetTargetIcon(i);
    }
    void Restore()
    {
        if (!group)
            return;
        for (int pass = 0; pass < 2; ++pass)
            for (uint8 i = 0; i < 8; ++i)
                if (group->GetTargetIcon(i) != before[i])
                    group->SetTargetIcon(i, before[i], by);
    }
};

std::vector<Player*> MembersOf(Group* group)
{
    std::vector<Player*> members;
    for (Group::MemberSlot const& slot : group->GetMemberSlots())
        if (Player* member = ObjectAccessor::FindPlayer(slot.guid))
            if (member->IsInWorld())
                members.push_back(member);
    return members;
}

ObjectGuid ChangedBy(Player* leader, Player* bot)
{
    return leader && leader->IsInWorld() ? leader->GetGUID() : bot->GetGUID();
}

bool Refuse(std::string& code, std::string* detail, char const* codeWord, std::string const& text)
{
    code = codeWord;
    if (detail)
        *detail = text;
    return false;
}
}

bool ApplyCcOrder(Player* leader, Player* bot, std::string const& mark, ObjectGuid unit, std::string const& family, std::string& code, std::string* detail)
{
    code.clear();
    if (!bot)
        return Refuse(code, detail, "no-engine", "no bot");
    std::string const word = Lower(mark);
    int const index = MarkIndex(word);
    if (index < 0)
        return Refuse(code, detail, "bad-mark", "a crowd-control mark is star, circle, diamond, triangle, moon, square or cross");
    if (index == SkullIndex)
        return Refuse(code, detail, "bad-mark", "skull is the party's kill mark and never a crowd-control mark");
    if (!DungeonRun::CcOrderFamilyKnown(family))
        return Refuse(code, detail, "bad-family", "polymorph, shackleundead, banish, hibernate, seduction, freezingtrap, sap, repentance, scarebeast, fear or none");
    PlayerbotAI* ai = AiOf(bot);
    ai::Value<std::string>* ownCc = StringValue(ai, "rti cc");
    ai::Value<std::string>* ownFamily = StringValue(ai, "run cc family");
    if (!ownCc || !ownFamily)
        return Refuse(code, detail, "no-engine", "the bot has no running engine (`.tb engine <bot> on`)");
    Group* group = bot->GetGroup();
    if (!group)
        return Refuse(code, detail, "no-party", "the bot is in no party, so it sees no raid mark");
    if (leader && leader->GetGroup() != group)
        return Refuse(code, detail, "no-party", "the leader and the bot are not in one party");

    Unit* target = unit.IsEmpty() ? nullptr : ObjectAccessor::GetUnit(*bot, unit);
    if (!target || !target->IsInWorld())
        return Refuse(code, detail, "target-not-found", "no such unit on the bot's map");
    if (target->IsPlayer())
        return Refuse(code, detail, "bad-target", "a crowd-control mark goes on a creature");
    if (!target->IsAlive())
        return Refuse(code, detail, "target-dead", "the target is dead");
    if (!bot->IsValidAttackTarget(target))
        return Refuse(code, detail, "not-attackable", "the core says this bot may not attack that unit");
    // I13: a listed mob is left alone, not crowd-controlled either: take it off the list first (`.tb ignore ... remove ...`).
    if (IsIgnoredMobCompat(bot, unit))
        return Refuse(code, detail, "ignored", "the commander's ignore list names that mob");
    std::string const wantedFamily = NormalFamily(family);
    ObjectGuid const by = ChangedBy(leader, bot);
    Undo undo;
    undo.group = group;
    undo.by = by;
    for (uint8 i = 0; i < 8; ++i)
        undo.icons[i] = group->GetTargetIcon(i);

    std::vector<PlayerbotAI*> touched;
    touched.push_back(ai);
    for (Player* member : MembersOf(group))
    {
        if (member == bot)
            continue;
        PlayerbotAI* memberAi = AiOf(member);
        if (!memberAi)
            continue;
        touched.push_back(memberAi);
        // Exclusive ownership: the new owner keeps the mark.
        if (ai::Value<std::string>* memberCc = StringValue(memberAi, "rti cc"))
            if (memberCc->Get() == word)
                undo.Set(memberCc, "none");
        // A damage bot that focuses this mark would attack the unit the crowd control holds: back to skull. (A healer has no focus mark.)
        if (!PlayerbotAI::IsHeal(member))
            if (ai::Value<std::string>* memberRti = StringValue(memberAi, "rti"))
                if (memberRti->Get() == word)
                    undo.Set(memberRti, "skull");
    }
    // The executor itself: its own focus is not its crowd-control mark either.
    if (!PlayerbotAI::IsHeal(bot))
        if (ai::Value<std::string>* ownRti = StringValue(ai, "rti"))
            if (ownRti->Get() == word)
                undo.Set(ownRti, "skull");

    undo.Set(ownCc, word);
    undo.Set(ownFamily, wantedFamily);
    group->SetTargetIcon(uint8(index), unit, by);
    for (PlayerbotAI* touchedAi : touched)
        ResetTargets(touchedAi);

    // Read back: the order is only true when all three hold.
    if (group->GetTargetIcon(uint8(index)) != unit || ownCc->Get() != word || ownFamily->Get() != wantedFamily)
    {
        undo.Restore();
        for (PlayerbotAI* touchedAi : touched)
            ResetTargets(touchedAi);
        return Refuse(code, detail, "failed", "the mark, the bot's `rti cc` or its `run cc family` did not take; everything was put back");
    }
    return true;
}

bool ClearCcOrder(Player* leader, Player* bot, std::string const& mark, ObjectGuid unit, std::string& code, std::string* detail, bool* iconRemoved)
{
    code.clear();
    if (iconRemoved)
        *iconRemoved = false;
    if (!bot)
        return Refuse(code, detail, "no-engine", "no bot");
    PlayerbotAI* ai = AiOf(bot);
    ai::Value<std::string>* ownCc = StringValue(ai, "rti cc");
    ai::Value<std::string>* ownFamily = StringValue(ai, "run cc family");
    if (!ownCc || !ownFamily)
        return Refuse(code, detail, "no-engine", "the bot has no running engine (`.tb engine <bot> on`)");
    bool const markNamed = !mark.empty();
    bool const unitNamed = !unit.IsEmpty();
    int namedIndex = -1;
    if (markNamed || unitNamed)
    {
        namedIndex = MarkIndex(Lower(mark));
        if (namedIndex < 0 || namedIndex == SkullIndex)
            return Refuse(code, detail, "bad-mark", "a crowd-control mark is star, circle, diamond, triangle, moon, square or cross");
    }
    // `clear` with no mark and no unit (G8): the bot's own crowd-control mark names the icon. The icon of that mark goes too, whichever unit it holds: it is
    // this bot's mark by `rti cc` (a later order of ANOTHER bot would have taken `rti cc` off this one: exclusive ownership), so no other work uses it.
    int ownIndex = -1;
    if (!markNamed && !unitNamed)
    {
        int const own = MarkIndex(Lower(ownCc->Get()));
        if (own >= 0 && own != SkullIndex)
            ownIndex = own;
    }
    CcClearTarget const target = CcClearTargetOf(markNamed, unitNamed, namedIndex, ownIndex);
    Group* group = bot->GetGroup();
    if (target.needsGroup && !group)
        return Refuse(code, detail, "no-party", "the bot is in no party, so it has no raid mark to take down");
    // The icon goes only while it still holds the unit the clear names (a later order may have moved it to another); with no unit named, the bot's own mark's
    // icon, whatever it holds.
    CcClearPlan<ObjectGuid> plan;
    if (group && target.icon >= 0)
    {
        ObjectGuid const held = group->GetTargetIcon(uint8(target.icon));
        if (target.own ? !held.IsEmpty() : held == unit)
        {
            plan.icon = target.icon;
            plan.held = held;
        }
    }
    CcValueAccess values{ ownCc, ownFamily };
    CcIconAccess icons{ group, ChangedBy(leader, bot) };
    CcClearFailure const failure = ClearCcState(values, icons, plan);
    ResetTargets(ai);
    if (failure == CcClearFailure::Icon)
        return Refuse(code, detail, "failed", "the raid mark is still on the unit");
    if (failure == CcClearFailure::Values)
        return Refuse(code, detail, "failed", "the bot's `rti cc` or `run cc family` did not reset");
    if (iconRemoved && plan.icon >= 0)
        *iconRemoved = true;
    return true;
}

// The bot's `rti cc` and `run cc family` as `.tb state` reads them ("" for "none" or no engine): the run's end (TbRun.cpp ClearRunCc) checks that a bot it
// marked still carries the run's mark before it resets it.
void CcOrderValues(Player* bot, std::string& mark, std::string& family)
{
    mark.clear();
    family.clear();
    PlayerbotAI* ai = AiOf(bot);
    mark = ValueText(ai, "rti cc");
    family = ValueText(ai, "run cc family");
}

namespace
{
// The unit a `.tb cc` argument names: `target` (the sender's selection, read when the command runs) or the 32 hex digits of a guid (`.tb state` writes them).
bool ResolveUnitWord(Player* sender, std::string const& word, ObjectGuid& out, std::string& code, std::string& detail)
{
    if (Lower(word) == "target")
    {
        if (!sender)
        {
            code = "needs-player-sender";
            detail = "`target` is the selection of the player who sent the command; from the console name the guid";
            return false;
        }
        out = sender->GetTarget();
        if (out.IsEmpty())
        {
            code = "no-selection";
            detail = "the sender has no target selected";
            return false;
        }
        return true;
    }
    if (!ParseGuidText(word, out))
    {
        code = "bad-target";
        detail = "a unit is `target` or the 32 hex digits of a guid";
        return false;
    }
    return true;
}

// The one bot a command is about (a name; `party` is refused).
Player* ResolveBot(ChatHandler* handler, Words const& args, char const* cmd, bool& failed)
{
    failed = false;
    if (args.w.empty())
    {
        Fail(handler, args, cmd, "missing-target", "name a bot");
        failed = true;
        return nullptr;
    }
    if (Lower(args.w[0]) == "party")
    {
        Fail(handler, args, cmd, "bad-target", std::string(".tb ") + cmd + " takes one bot, not a party");
        failed = true;
        return nullptr;
    }
    size_t pos = 0;
    Address who = ResolveAddress(args, pos, SenderPlayer(handler), false);
    if (!who.ok())
    {
        Fail(handler, args, cmd, who.code, who.detail);
        failed = true;
        return nullptr;
    }
    return who.bots.front();
}

Player* PartyLeaderOf(Player* bot)
{
    Group* group = bot->GetGroup();
    if (!group)
        return nullptr;
    Player* leader = ObjectAccessor::FindPlayer(group->GetLeaderGUID());
    return leader && leader->IsInWorld() ? leader : nullptr;
}

std::string Trim(std::string const& text)
{
    size_t begin = 0, end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])))
        ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
        --end;
    return text.substr(begin, end - begin);
}
}

// ---- `.tb cc <bot> <mark> <unit guid|target> [family]` and `.tb cc <bot> clear [<mark> <unit guid>]` ----
bool HandleCc(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    if (!RequireBuilt(handler, args, "cc"))
        return false;
    bool failed = false;
    Player* bot = ResolveBot(handler, args, "cc", failed);
    if (failed)
        return false;
    if (args.w.size() < 2)
        return Fail(handler, args, "cc", "missing-argument", "<mark> <unit guid | target> [family]  |  clear [<mark> <unit guid>]");
    Player* sender = SenderPlayer(handler);
    Player* leader = PartyLeaderOf(bot);
    std::string const second = Lower(args.w[1]);
    std::string code, detail;

    if (second == "clear")
    {
        std::string mark;
        ObjectGuid unit;
        if (args.w.size() == 3 || args.w.size() > 4)
            return Fail(handler, args, "cc", "unexpected-argument", args.w.size() == 3 ? "clear takes no mark alone: `clear <mark> <unit guid>` or `clear`" : args.w[4]);
        if (args.w.size() == 4)
        {
            mark = Lower(args.w[2]);
            if (!ResolveUnitWord(sender, args.w[3], unit, code, detail))
                return Fail(handler, args, "cc", code, detail);
        }
        bool iconRemoved = false;
        // A bare `clear` takes the icon of the bot's own crowd-control mark (G8): remember which one it was before it is reset.
        std::string const ownMark = mark.empty() ? ValueText(AiOf(bot), "rti cc") : std::string();
        if (!ClearCcOrder(leader, bot, mark, unit, code, &detail, &iconRemoved))
            return Fail(handler, args, "cc", code, detail);
        PlayerbotAI* ai = AiOf(bot);
        Reply reply(handler, args, "cc");
        reply.Begin();
        Rec rec("cc");
        rec.S("bot", bot->GetName()).S("verb", "clear").S("status", "ok").S("rticc", ValueText(ai, "rti cc")).S("ccfam", ValueText(ai, "run cc family"));
        if (!mark.empty())
            rec.S("mark", mark).B("icon_removed", iconRemoved);
        else if (!ownMark.empty() && ownMark != "none")
            rec.S("mark", ownMark).B("icon_removed", iconRemoved);
        reply.Line(rec);
        Rec end("cc");
        end.S("verb", "clear").S("target", bot->GetName()).N("ok", 1).N("refused", 0);
        reply.End(end);
        return true;
    }

    if (args.w.size() < 3)
        return Fail(handler, args, "cc", "missing-argument", "<mark> <unit guid | target> [family]");
    if (args.w.size() > 4)
        return Fail(handler, args, "cc", "unexpected-argument", args.w[4]);
    ObjectGuid unit;
    if (!ResolveUnitWord(sender, args.w[2], unit, code, detail))
        return Fail(handler, args, "cc", code, detail);
    std::string const family = args.w.size() == 4 ? args.w[3] : std::string();
    if (!ApplyCcOrder(leader, bot, second, unit, family, code, &detail))
        return Fail(handler, args, "cc", code, detail);

    // The reply is what the world says now, read back (never what was asked).
    PlayerbotAI* ai = AiOf(bot);
    Group* group = bot->GetGroup();
    Unit* marked = ObjectAccessor::GetUnit(*bot, unit);
    int const index = MarkIndex(second);
    bool const onIcon = group && index >= 0 && group->GetTargetIcon(uint8(index)) == unit;
    Reply reply(handler, args, "cc");
    reply.Begin();
    Rec rec("cc");
    rec.S("bot", bot->GetName()).S("verb", "set").S("status", onIcon ? "ok" : "refused").S("mark", second).S("unit", GuidText(unit));
    if (marked)
        rec.N("entry", marked->GetEntry());
    rec.S("rticc", ValueText(ai, "rti cc")).S("ccfam", ValueText(ai, "run cc family"));
    reply.Line(rec);
    Rec end("cc");
    end.S("verb", "set").S("target", bot->GetName()).N("ok", onIcon ? 1 : 0).N("refused", onIcon ? 0 : 1);
    reply.End(end);
    return onIcon;
}

// ---- `.tb strategy <bot> co|nc <+name,-name ...>` (test setup) ----
bool HandleStrategy(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    if (!RequireBuilt(handler, args, "strategy"))
        return false;
    bool failed = false;
    Player* bot = ResolveBot(handler, args, "strategy", failed);
    if (failed)
        return false;
    if (args.w.size() < 2)
        return Fail(handler, args, "strategy", "missing-argument", "co|nc <+name,-name ...>");
    std::string const stateWord = Lower(args.w[1]);
    if (stateWord != "co" && stateWord != "nc")
        return Fail(handler, args, "strategy", "bad-argument", "the state is co (combat) or nc (non-combat)");
    BotState const state = stateWord == "co" ? BotState::BOT_STATE_COMBAT : BotState::BOT_STATE_NON_COMBAT;
    std::string rest;
    for (size_t i = 2; i < args.w.size(); ++i)
        rest += (rest.empty() ? "" : " ") + args.w[i];
    if (rest.empty())
        return Fail(handler, args, "strategy", "missing-argument", "co|nc <+name,-name ...>");
    for (char c : rest)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '+' || c == '-' || c == ',' || c == '_'))
            return Fail(handler, args, "strategy", "bad-argument", "strategy changes use letters, digits, space, + - , _");

    struct Item { char op; std::string name; };
    std::vector<Item> items;
    size_t from = 0;
    while (from <= rest.size())
    {
        size_t const comma = rest.find(',', from);
        std::string const piece = Trim(rest.substr(from, comma == std::string::npos ? std::string::npos : comma - from));
        if (piece.empty())
            return Fail(handler, args, "strategy", "bad-argument", "an empty strategy change (a stray comma)");
        if (piece[0] != '+' && piece[0] != '-')
            return Fail(handler, args, "strategy", "bad-argument", "each change starts with + (add) or - (remove): " + piece);
        std::string const name = Trim(piece.substr(1));
        if (name.empty())
            return Fail(handler, args, "strategy", "bad-argument", "a + or - without a strategy name");
        items.push_back({ piece[0], name });
        if (comma == std::string::npos)
            break;
        from = comma + 1;
    }
    if (items.size() > 16)
        return Fail(handler, args, "strategy", "bad-argument", "at most 16 changes in one command");

    PlayerbotAI* ai = AiOf(bot);
    if (!ai || !ai->GetAiObjectContext())
        return Fail(handler, args, "strategy", "no-engine", "the bot has no running engine (`.tb engine <bot> on`)");
    // Every name must be a strategy of the bot's context before anything changes (no half-applied list).
    for (Item const& item : items)
        if (!ai->GetAiObjectContext()->GetStrategy(item.name))
            return Fail(handler, args, "strategy", "unknown-strategy", item.name + " is not a strategy this build registers for the bot");

    Reply reply(handler, args, "strategy");
    reply.Begin();
    uint32 applied = 0, refused = 0;
    for (Item const& item : items)
    {
        bool const had = ai->HasStrategy(item.name, state);
        ai->ChangeStrategy(std::string(1, item.op) + item.name, state);
        bool const has = ai->HasStrategy(item.name, state);
        bool const holds = item.op == '+' ? has : !has;
        Rec rec("strategy");
        rec.S("bot", bot->GetName()).S("state", stateWord).S("op", item.op == '+' ? "add" : "remove").S("name", item.name).S("tag", "SETUP");
        if (holds)
        {
            ++applied;
            rec.S("status", "ok").B("changed", had != has);
        }
        else
        {
            ++refused;
            rec.S("status", "refused").S("code", "not-applied").T("msg", "the engine does not hold the strategy as asked after the change");
        }
        reply.Line(rec);
    }
    Rec end("strategy");
    end.S("target", bot->GetName()).S("state", stateWord).N("applied", applied).N("refused", refused);
    reply.End(end);
    if (refused)
        handler->SetSentErrorMessage(true);
    return refused == 0;
}

void AppendCcState(Rec& rec, Player* bot)
{
    if (bot->GetClass() == CLASS_ROGUE)
        rec.N("combo", ComboPointsCompat(bot));
    PlayerbotAI* ai = AiOf(bot);
    if (!ai || !ai->GetAiObjectContext())
        return;
    std::string const mark = ValueText(ai, "rti cc");
    rec.S("rticc", mark).S("ccfam", ValueText(ai, "run cc family"));
    int const index = MarkIndex(mark);
    Group* group = bot->GetGroup();
    if (index < 0 || !group)
        return;
    ObjectGuid const guid = group->GetTargetIcon(uint8(index));
    Unit* unit = guid.IsEmpty() ? nullptr : ObjectAccessor::GetUnit(*bot, guid);
    // held: the unit on the bot's mark exists, lives, and a breakable crowd control (polymorph, sap, shackle, ...) holds it now.
    if (unit && unit->IsAlive() && ai::PossibleAttackTargetsValue::HoldsBreakableCc(ai, unit, bot))
        rec.P("held", { std::to_string(unit->GetEntry()), mark });
}

void SelfTestRecords(Reply& reply)
{
    reply.Line(Rec("selftest").S("case", "state-h8").S("bot", "Mmage").S("rticc", "moon").S("ccfam", "polymorph").P("held", { "11322", "moon" }));
    reply.Line(Rec("selftest").S("case", "state-h8-rogue").S("bot", "Mrogue").S("rticc", "").S("ccfam", "").N("combo", 3));
    reply.Line(Rec("cc").S("bot", "Mmage").S("verb", "set").S("status", "ok").S("mark", "moon").S("unit", "0000000000000000F130002C4A000001").N("entry", 11322).S("rticc", "moon").S("ccfam", "polymorph"));
    reply.Line(Rec("cc").S("bot", "Mmage").S("verb", "clear").S("status", "ok").S("rticc", "").S("ccfam", "").S("mark", "moon").B("icon_removed", true));
    reply.Line(Rec("selftest").S("case", "cc-end").S("verb", "set").S("target", "Mmage").N("ok", 1).N("refused", 0));
    reply.Line(Rec("strategy").S("bot", "Mrogue").S("state", "nc").S("op", "add").S("name", "stealth").S("tag", "SETUP").S("status", "ok").B("changed", true));
    reply.Line(Rec("selftest").S("case", "strategy-end").S("target", "Mrogue").S("state", "nc").N("applied", 1).N("refused", 0));
}
}
}

#else   // not built with the slice: the same refusal the slice-3 stub gives (TbH3Off.cpp)

namespace TortoiseBots
{
namespace h8
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

bool NoEngine(std::string& code, std::string* detail)
{
    code = "engine-not-built";
    if (detail)
        *detail = "the engine is not built in";
    return false;
}
}

bool ApplyCcOrder(Player*, Player*, std::string const&, ObjectGuid, std::string const&, std::string& code, std::string* detail) { return NoEngine(code, detail); }
bool ClearCcOrder(Player*, Player*, std::string const&, ObjectGuid, std::string& code, std::string* detail, bool*) { return NoEngine(code, detail); }
void CcOrderValues(Player*, std::string& mark, std::string& family) { mark.clear(); family.clear(); }
bool HandleCc(ChatHandler* handler, Trinity::ChatCommands::Tail args)       { return NotBuilt(handler, args, "cc"); }
bool HandleStrategy(ChatHandler* handler, Trinity::ChatCommands::Tail args) { return NotBuilt(handler, args, "strategy"); }
}
}

#endif

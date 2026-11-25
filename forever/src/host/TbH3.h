// Slice 3, host package H3: the ORDER and STATE surface of the `.tb` commands (forever/docs/TB-COMMANDS.md is the interface a
// network client is written against; forever/docs/SLICE3-H-NOTES.md says how it was built).
//
// This header names no legacy type: the command handlers and the engine hooks are implemented in TbOrders.cpp, TbPartyState.cpp
// and TbTestKit.cpp, which hold the engine-facing parts behind TORTOISEBOTS_SLICE1 exactly as TbEngineBrain.cpp does. Without the
// option every handler answers "the engine is not built in".
//
// Threading: every function here runs on the world thread (a console command: World::ProcessCliCommands, World.cpp:3191, called at
// World.cpp:2617; a GM chat command: World::UpdateSessions, World.cpp:3144, called at World.cpp:2446, the chat opcodes are
// PROCESS_THREADUNSAFE, Opcodes.cpp:334-338; the engine tick: WorldScript::OnBeforeMapsUpdate, World.cpp:2490). The order queue is
// therefore plain data with no lock.
#pragma once

#include "ChatCommand.h"
#include "Define.h"
#include "ObjectGuid.h"
#include <cctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class ChatHandler;
class Player;
class PlayerbotAI;

namespace TortoiseBots
{
namespace h3
{
// ---- command handlers (rows of the `.tb` table in TbScripts.cpp) ----
bool HandleOrder(ChatHandler* handler, Trinity::ChatCommands::Tail args);      // .tb order <bot|party [leader]> <verb> [arg] [#token]
bool HandleRole(ChatHandler* handler, Trinity::ChatCommands::Tail args);       // .tb role [<bot|character|party [leader]>] tank|healer|dps|none [#token]
bool HandleSpec(ChatHandler* handler, Trinity::ChatCommands::Tail args);       // .tb spec <bot|party [leader]> <tab|spec|none> [#token]
bool HandleMaster(ChatHandler* handler, Trinity::ChatCommands::Tail args);     // .tb master <bot|party [leader]> <commander|none> [#token]
bool HandleParty(ChatHandler* handler, Trinity::ChatCommands::Tail args);      // .tb party <leader> | .tb party <leader> <member>... [#token]
bool HandleOnyxiaOff(ChatHandler* handler, Trinity::ChatCommands::Tail args);
bool HandleState(ChatHandler* handler, Trinity::ChatCommands::Tail args);      // .tb state <bot|party [leader]> [#token]
bool HandleThreat(ChatHandler* handler, Trinity::ChatCommands::Tail args);     // .tb threat <bot|party [leader]> [#token]
bool HandleSchool(ChatHandler* handler, Trinity::ChatCommands::Tail args);      // .tb school <bot|party [leader]> [#token]
bool HandleLearn(ChatHandler* handler, Trinity::ChatCommands::Tail args);      // .tb learn <bot> <spell id>... [#token]
bool HandleEquip(ChatHandler* handler, Trinity::ChatCommands::Tail args);      // .tb equip <bot> <item id> [replace] [#token]
bool HandleRemoveItem(ChatHandler* handler, Trinity::ChatCommands::Tail args);  // .tb removeitem [<character>] <item id> [count] [#token]
bool HandleItemScan(ChatHandler* handler, Trinity::ChatCommands::Tail args);   // .tb itemscan <bot> <item id>... [#token]
bool HandleStatCensus(ChatHandler* handler, Trinity::ChatCommands::Tail args); // .tb statcensus [#token]
bool HandleGear(ChatHandler* handler, Trinity::ChatCommands::Tail args);       // .tb gear <bot|party [leader]> upgrade [#token]
bool HandleQuest(ChatHandler* handler, Trinity::ChatCommands::Tail args);      // .tb quest <bot> status|accept|turnin|share|drop|reward ... [#token]
bool HandleGuildOrder(ChatHandler* handler, Trinity::ChatCommands::Tail args); // .tb guildorder create|list|cancel|status ... [#token]
bool HandleCraftOrder(ChatHandler* handler, Trinity::ChatCommands::Tail args); // .tb craftorder create|status|cancel ... [#token]

// The option-OFF answer of a command this build has no engine for: `error code=engine-not-built cmd=<cmd> ... #token` (TbH3Off.cpp,
// the default build's stand-in for TbH3.cpp). The default build's `ai`/`cs` commands answer through it too (TbAddonAi.cpp).
bool NotBuilt(ChatHandler* handler, Trinity::ChatCommands::Tail args, char const* cmd);

// Rebuilds the quest eligibility sets (TbCompat_Quest.h): at world load and after a quest reload.
void RebuildQuestEligibility();
// Drops the quest eligibility sets (TbCompat_Quest.h): on a quest reload while the module is off, so the next check rebuilds them.
void ClearQuestEligibility();

// ---- engine hooks (only built, and only called, with TORTOISEBOTS_SLICE1) ----
// Called by engine::Tick (TbEngineBrain.cpp) right before the bot's UpdateAI: runs the bot's queued orders through the legacy actions.
void BeforeEngineTick(Player* bot, PlayerbotAI* ai);
// TbHostOwnership.cpp BotCommands::IsInCommandedGroup: true for a bot that `.tb master` bound to a commander outside its group.
bool IsCommandedBot(Player* bot);

// What `.tb state` shows of the bot's last order, and the role `.tb role` asked for (0 = none). Defined with the slice only.
struct OrderStatus
{
    uint32 seq = 0;
    std::string verb = "none";
    std::string result = "none";    // none | queued | ok | fail:<code>
};
OrderStatus LastOrder(ObjectGuid bot);
uint8 WantedRole(ObjectGuid bot);
// Bot API (TBF-B1, host/TbLuaBots.cpp): the words a Bot handle reads instead of the whole `.tb state` record.
// RoleText is RoleOf with "" for an unknown role; EngineStateText is the `.tb state` word (combat|non-combat|dead);
// SpecText is the spec name `.tb state` shows; RoleForced is the `.tb role` override flag. Defined with the slice only,
// and only when the engine is built in (TBF_WITH_FOREVER_LUA): no other build carries their code.
#if defined(TBF_WITH_FOREVER_LUA) && TBF_WITH_FOREVER_LUA
std::string RoleText(Player* bot, PlayerbotAI* ai);
bool RoleForced(ObjectGuid bot);
std::string SpecText(Player* bot);
std::string EngineStateText(Player* bot, PlayerbotAI* ai);
// The next order transition the provider has not taken yet (queued, ok, fail:<code>), in ring order: one per snapshot, so an order
// that a newer one overwrote still reaches the engine as its own transition (TBF-B1 third review finding 2). LastOrder stays the
// record's NEWEST word (what `.tb state` shows).
bool PollOrderEvent(ObjectGuid bot, OrderStatus& out);
#endif
// H4 (slice 4): the member of the dead bot's party (or its commander) that offered it a resurrection that is still pending; null when there is none or the
// bot is alive. Defined with the slice only.
Player* ResurrectOffererOf(Player* bot);
// H6 (W6a): appends usage=, w=, worn=, wornw= to a `.tb itemscan` record. Defined with the slice only (TbPartyState.cpp).
class Rec;
void AppendItemScanEngine(Player* bot, uint32 itemId, Rec& rec);

// ---- helpers shared by the three files (TbH3.cpp) ----
// A command's words (whitespace separated) and its echo token: the last word that starts with '#' is taken out of the words and
// kept, with the '#', in `token` ("" when there is none). A token is at most 32 characters of [A-Za-z0-9_.:-] after the '#'.
struct Words
{
    std::vector<std::string> w;
    std::string token;
    bool badToken = false;     // a '#' word that is longer or has other characters: the command refuses it
};
Words Split(std::string_view tail);

// ---- reply grammar version 2 (TB-STATE-GRAMMAR-V2, lead decision 2026-11-20): the ONE encoder every writer below uses ----
// A reply line is the record kind, then `key=value` words separated by one space, and the caller's `#token` as the last word. Values are
// percent-encoded (every byte outside [A-Za-z0-9._-] as %XX, upper-case hex; `_` stays literal); structure is `;` between list items, `:` between the
// parts of an item (each part encoded on its own), `,` between coordinates; an empty value is the empty string; a key appears once per line.
// A line holds at most 3,000 bytes: a record that is longer continues on `<kind>+ <first key=value> <more keys> #token` lines. A multi-line reply is
// `<command>-begin #token`, the record lines, `<command>-end count=<lines between> records=<lines that are not continuations> ... #token`; only `error` is a one-line reply (addendum 1).
constexpr size_t MaxLineBytes = 3000;
std::string Enc(std::string_view bytes);

uint32 LineTooLongCount();   // lines the encoder refused to write because they passed MaxLineBytes (a host bug; `.tb grammar selftest` prints it)

class Rec
{
public:
    explicit Rec(std::string_view kind) : _kind(kind) {}
    Rec& S(std::string_view key, std::string_view text);                                  // text, percent-encoded
    Rec& T(std::string_view key, std::string_view text, size_t cap = 400);               // free text cut to `cap` bytes BEFORE encoding; the record then carries `trunc=yes`
    bool Truncated() const { return _truncated; }
    Rec& N(std::string_view key, int64 value);                                           // a decimal number
    Rec& F(std::string_view key, double value, int decimals = 1);                        // a decimal fraction
    Rec& B(std::string_view key, bool value);                                            // yes / no
    Rec& L(std::string_view key, std::vector<std::string> const& items);                 // `a;b;c`, each item encoded
    Rec& P(std::string_view key, std::vector<std::string> const& parts);                 // one item of parts: `a:b:c`, each part encoded
    Rec& LP(std::string_view key, std::vector<std::vector<std::string>> const& items);   // `a:b;c:d`
    Rec& Coord(std::string_view key, double x, double y, double z);                      // `x,y,z` with one decimal
    Rec& Csv(std::string_view key, std::vector<int64> const& values);                    // `a,b,c`: the tuple form for plain numbers (`resist=`, `res=`)
    std::string const& Kind() const { return _kind; }
    std::vector<std::pair<std::string, std::string>> const& Pairs() const { return _kv; }
private:
    Rec& Put(std::string_view key, std::string value);
    std::string _kind;
    std::vector<std::pair<std::string, std::string>> _kv;
    bool _truncated = false;
};

// One reply of one command: counts the record lines between its begin and end markers.
class Reply
{
public:
    Reply(ChatHandler* handler, Words const& args, std::string_view kind) : _handler(handler), _args(args), _kind(kind) {}
    void Begin();                         // `<kind>-begin #token`
    void Line(Rec const& rec);            // a record line (split into continuation lines when over MaxLineBytes); counts the lines
    void Single(Rec const& rec);          // the ONE one-line reply, `error` (addendum A1): the record and nothing else
    void End(Rec extra);                  // `<kind>-end count=<lines> <extra keys> #token`; `extra` is a Rec of the extra keys (its kind is ignored)
    uint32 Lines() const { return _lines; }
    uint32 Records() const { return _records; }
private:
    void Send(std::string line);
    ChatHandler* _handler;
    Words const& _args;
    std::string _kind;
    uint32 _lines = 0;
    uint32 _records = 0;
};

// A reply's target label ("Mtank", "party:Mtank", "party:commander:Mcmd") as the parts of one value (`P`).
std::vector<std::string> LabelParts(std::string const& label);

// `.tb grammar selftest`: encodes and prints fixed records (a value with a space, `=`, `:`, `;`, `#`, `%`, `_`, an empty value, an over-long line).
bool HandleGrammar(ChatHandler* handler, Trinity::ChatCommands::Tail args);

// The refusal: `error code=<word> cmd=<word> msg=<encoded text, at most 400 bytes> #token`, and the core is told an error message was sent (no usage text).
// Always returns false.
bool Fail(ChatHandler* handler, Words const& args, std::string_view cmd, std::string_view code, std::string_view detail = {});

// True when the engine is built in; otherwise says `error ... code=engine-not-built` and returns false.
bool RequireBuilt(ChatHandler* handler, Words const& args, std::string_view cmd);

// Inline here, not in TbH3.cpp: TbScripts.cpp is compiled in both builds and lowercases the `.tb power` words, while the option-OFF build
// has no TbH3.cpp (TbH3Off.cpp stands in for it), so the definition has to live where both builds see it.
inline std::string Lower(std::string_view text)
{
    std::string out(text);
    for (char& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}
std::string GuidText(ObjectGuid guid);                 // 32 hex digits: GetRawValue(1) then GetRawValue(0), "" for the empty guid
bool ParseGuidText(std::string_view text, ObjectGuid& out);

Player* SenderPlayer(ChatHandler* handler);            // the GM's character for a chat command, null for the console
Player* FindOnline(std::string_view name);             // an in-world character by name (bot or not)
bool IsHostBot(Player const* player);                  // BotMgr::IsBot
Player* CommanderOf(Player* bot);
bool MemberKnowsOocResurrection(Player* member);      // N3: the party member knows an out-of-combat resurrection (TbPartyState.cpp; the engine side asks it through MemberKnowsOocResurrectionCompat)
// N5 (package N): who may be made the leader or master of host bots: a host bot, or the character of the GM who typed the command. A real player's
// character is never taken over (from the console only host bots qualify). Used by `.tb party`, `.tb master`, `.tb login <bot> <master>`, `.tb group`.
bool MayLeadBots(ChatHandler* handler, ObjectGuid character);                      // the in-world player the bot's host master guid names, or null
bool CheckBotAccess(Player const* sender, ObjectGuid botGuid, std::string& code, std::string& detail);
bool CheckBotAccess(ChatHandler* handler, ObjectGuid botGuid, std::string& code, std::string& detail);
bool CheckBotAccess(ChatHandler* handler, Player const* bot, std::string& code, std::string& detail);
bool CheckPartyLeadAccess(Player const* sender, Player const* leader, std::string& code, std::string& detail);
bool CheckPartyLeadAccess(ChatHandler* handler, Player const* leader, std::string& code, std::string& detail);
// proof-1 A: the narrow GM-observer exemption of `.tb run start|pause|resume|stop` (TbGuardPure.h, ObserverMayControlRun). CheckBotAccess first; when it
// refuses, a GM whose commander chain from the bot reaches no real player may still control the run. Both is empty on an allowed answer.
bool CheckRunObserverAccess(ChatHandler* handler, Player const* bot, std::string& code, std::string& detail);
bool CheckRunObserverAccess(ChatHandler* handler, ObjectGuid botGuid, std::string& code, std::string& detail);

// TB-1: the character a test-kit command changes: a host bot, or the sender's own character; another real player's character is refused. An empty `name`
// means the sender (the no-name form of `.tb role`, `.tb equip`, `.tb learn`, `.tb hurt`, `.tb power`). On a refusal `code`/`detail` say why and it returns null.
Player* ResolveTestTarget(ChatHandler* handler, std::string_view name, std::string& code, std::string& detail);

// "who the command is about": one bot by name, or `party [leader]` (every host bot of that party).
struct Address
{
    std::vector<Player*> bots;
    std::string label;          // for replies: the bot's name, or "party:<leader>"
    Player* leader = nullptr;   // the party leader, when the address was a party
    std::string code;           // set when it could not be resolved
    std::string detail;
    bool ok() const { return code.empty(); }
};
// Reads the address at words[pos] and advances pos past it. `party` takes the next word as the leader's name when `leaderFollows` says
// so (the caller knows whether the word after `party` could be the leader or is already the next argument). `allowSender` also accepts the
// sender's own character as a single name (`MayLeadBots`); the other commands keep bots only.
Address ResolveAddress(Words const& args, size_t& pos, Player* sender, bool leaderFollows, bool allowSender = false, bool requireControl = true);

// Every in-world host bot (for `party` with no leader).
std::vector<Player*> AllHostBots();

// The `.tb master` bindings (bot guid -> commander guid). Plain data, world thread. A binding counts only while BotMgr still names the
// same commander as the bot's host master (IsCommandedBot checks it), so a re-login or `.tb group` makes it lapse by itself.
void SetCommander(ObjectGuid bot, ObjectGuid commander);   // an empty commander removes the binding
ObjectGuid CommanderGuid(ObjectGuid bot);
// The binding as the table holds it, WITHOUT the "the host master still names it" check CommanderGuid makes: the entry a re-login or a move of
// the host master has lapsed, which is how the run's tick tells "the engine moved the master under the run's entry" from "somebody else bound it".
ObjectGuid StoredCommander(ObjectGuid bot);
std::vector<std::pair<ObjectGuid, ObjectGuid>> CommanderBindings();
}
}

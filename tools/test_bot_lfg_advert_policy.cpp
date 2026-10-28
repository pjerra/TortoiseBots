// Standalone tests for the bot LFG advert policy (bot LFG part 3):
// how a waiting bot LFG group advertises in the LookingForGroup channel and
// answers whispers (texts, cadence, the "inv" grammar, the player role
// check).
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_bot_lfg_advert_policy.cpp -o /tmp/lfg34_adv
//   /tmp/lfg34_adv

#include "../runtime/policy/lfg/BotLfgAdvertPolicy.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace TortoiseBots::BotLfgAdvert;
using TortoiseBots::BotLfg::Role;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static bool StartsWithI(std::string const& s, char const* p)
{
    for (size_t i = 0; p[i]; ++i)
        if (i >= s.size() || std::tolower((unsigned char)s[i]) != std::tolower((unsigned char)p[i]))
            return false;
    return true;
}

static void TestNames()
{
    CHECK(DungeonName("deadmines") == "Deadmines");
    CHECK(DungeonName("scarlet_library") == "SM Library");
    CHECK(DungeonName("zulfarrak") == "Zul'Farrak");
    CHECK(DungeonName("dire_maul_north") == "DM North");
    CHECK(DungeonName("frostmane_hollow") == "Frostmane Hollow");   // not in the table: built from the key
    CHECK(DungeonNameTable().size() == 31);
    CHECK(std::string(ChatRoleWord(Role::Damage)) == "dps" && std::string(ChatRoleWord(Role::Tank)) == "tank" &&
        std::string(ChatRoleWord(Role::None)).empty());
}

static void TestPostText()
{
    CHECK(PostText(0, "Deadmines", Role::Tank, 17, 24) == "LF1M tank Deadmines 17-24, w me inv");
    CHECK(PostText(1, "Deadmines", Role::Tank, 17, 24) == "Deadmines need tank (17-24), /w inv");
    CHECK(PostText(2, "Deadmines", Role::Damage, 17, 24) == "LF dps for Deadmines, lvl 17-24, whisper inv");
    CHECK(PostText(3, "Deadmines", Role::Healer, 17, 24) == PostText(0, "Deadmines", Role::Healer, 17, 24));
    // other bots answer lines that start with LFG/LFM (SayAction.cpp:521)
    for (auto const& entry : DungeonNameTable())
        for (uint32_t v = 0; v < 3; ++v)
            for (Role r : {Role::Tank, Role::Healer, Role::Damage})
            {
                std::string t = PostText(v, entry.second, r, 10, 60);
                CHECK(!StartsWithI(t, "LFG") && !StartsWithI(t, "LFM") && t.size() < 120);
            }
}

static void TestPostDue()
{
    CHECK(PostDue(1000, false, 0, false, 0));                                   // first post at once
    CHECK(!PostDue(100000, true, 0, false, 0));                                 // group: 3 min apart
    CHECK(PostDue(kPostIntervalMs, true, 0, false, 0));
    CHECK(!PostDue(kPostIntervalMs, true, 0, true, kPostIntervalMs - kChannelGapMs + 1)); // channel: 30 s apart
    CHECK(PostDue(kPostIntervalMs, true, 0, true, kPostIntervalMs - kChannelGapMs));
    CHECK(!PostDue(5000, false, 0, true, 0));                                   // a new group waits for the channel too
}

static void TestInviteGrammar()
{
    for (char const* yes : {"inv", "INV pls", "inv me!", "invite please", "+", "can i join?", "invv", "pls inv",
                            "lf tank? i can inv you"})
        CHECK(IsInviteRequest(yes));
    for (char const* no : {"", "hi", "inventory full lol", "what are you doing", "inv inv inv inv inv inv inv",
                           "joining", "?"})
        CHECK(!IsInviteRequest(no));
}

static void TestRoleFit()
{
    CHECK(ClassRoleMask(1) == 5 && ClassRoleMask(2) == 7 && ClassRoleMask(3) == 4 && ClassRoleMask(4) == 4 &&
        ClassRoleMask(5) == 6 && ClassRoleMask(7) == 7 && ClassRoleMask(8) == 4 && ClassRoleMask(9) == 4 &&
        ClassRoleMask(11) == 7 && ClassRoleMask(0) == 0 && ClassRoleMask(6) == 0);
    CHECK(PlayerFitsRole(8, 4, 30, Role::Damage));          // mage dps
    CHECK(!PlayerFitsRole(8, 4, 30, Role::Tank));           // mage tank: class says no
    CHECK(!PlayerFitsRole(5, 4, 0, Role::Tank));            // priest tank: class says no, even with no talents
    CHECK(PlayerFitsRole(1, 4, 9, Role::Tank));             // warrior, 9 points: no lean yet, trusted
    CHECK(!PlayerFitsRole(1, 4, 10, Role::Tank));           // arms warrior with 10 points: leans damage
    CHECK(PlayerFitsRole(1, 5, 31, Role::Tank));            // protection warrior
    CHECK(PlayerFitsRole(5, 2, 31, Role::Healer) && PlayerFitsRole(5, 2, 31, Role::Damage)); // a healer may dps
    CHECK(!PlayerFitsRole(0, 7, 0, Role::Damage) && !PlayerFitsRole(1, 5, 31, Role::None));
    CHECK(RoleBit(Role::Tank) == 1 && RoleBit(Role::Healer) == 2 && RoleBit(Role::Damage) == 4 && RoleBit(Role::None) == 0);
}

static WhisperFacts Fit()
{
    WhisperFacts f;
    f.sameTeam = true; f.inviteRequest = true; f.waiting = true; f.level = 20; f.minLevel = 17; f.maxLevel = 24;
    f.fitsRole = true;
    return f;
}

static void TestDecideWhisperOrder()
{
    CHECK(DecideWhisper(Fit()) == Answer::Invite);
    auto with = [](void (*edit)(WhisperFacts&)) { WhisperFacts f = Fit(); edit(f); return DecideWhisper(f); };
    CHECK(with([](WhisperFacts& f) { f.sameTeam = false; }) == Answer::Ignore);
    CHECK(with([](WhisperFacts& f) { f.repliedRecently = true; }) == Answer::Ignore);
    CHECK(with([](WhisperFacts& f) { f.inviteRequest = false; }) == Answer::Hint);
    CHECK(with([](WhisperFacts& f) { f.inviteRequest = false; f.hintedRecently = true; }) == Answer::Ignore);
    CHECK(with([](WhisperFacts& f) { f.inviteRequest = false; f.waiting = false; }) == Answer::Ignore);
    CHECK(with([](WhisperFacts& f) { f.waiting = false; }) == Answer::Full);
    CHECK(with([](WhisperFacts& f) { f.senderInvited = true; f.invitePending = true; }) == Answer::AlreadyInvited);
    CHECK(with([](WhisperFacts& f) { f.senderRefusedResend = true; }) == Answer::Busy);
    CHECK(with([](WhisperFacts& f) { f.invitePending = true; }) == Answer::Busy);
    CHECK(with([](WhisperFacts& f) { f.hardcore = true; }) == Answer::Hardcore);
    CHECK(with([](WhisperFacts& f) { f.grouped = true; }) == Answer::InGroup);
    CHECK(with([](WhisperFacts& f) { f.level = 16; }) == Answer::WrongLevel);
    CHECK(with([](WhisperFacts& f) { f.level = 25; }) == Answer::WrongLevel);
    CHECK(with([](WhisperFacts& f) { f.fitsRole = false; }) == Answer::WrongRole);
    // first match wins: a full group answers "full" before the level or role
    CHECK(with([](WhisperFacts& f) { f.waiting = false; f.level = 5; f.fitsRole = false; }) == Answer::Full);
    CHECK(with([](WhisperFacts& f) { f.sameTeam = false; f.waiting = false; }) == Answer::Ignore);
    CHECK(with([](WhisperFacts& f) { f.grouped = true; f.level = 5; }) == Answer::InGroup);
    CHECK(std::string(AnswerCode(Answer::WrongRole)) == "wrong-role" && std::string(AnswerCode(Answer::Ignore)) == "ignore");
}

// lfg3 live: "inv", "inv me" and "invite" from a fitting player, a wrong-level
// player and a wrong-role player all got the hint; the decision on the text.
static void TestWhisperTextDecides()
{
    for (char const* text : {"inv", "inv me", "invite", "INV", "Inv Me", "InViTe", "inv   ", "inv me!", "invite.",
                             " inv me pls ", "inv?"})
    {
        CHECK(IsInviteRequest(text));
        auto decide = [&](void (*edit)(WhisperFacts&)) {
            WhisperFacts f = Fit();
            f.inviteRequest = IsInviteRequest(text);
            edit(f);
            return DecideWhisper(f);
        };
        CHECK(decide([](WhisperFacts&) {}) == Answer::Invite);
        CHECK(decide([](WhisperFacts& f) { f.level = 30; }) == Answer::WrongLevel);
        CHECK(decide([](WhisperFacts& f) { f.level = 16; }) == Answer::WrongLevel);
        CHECK(decide([](WhisperFacts& f) { f.fitsRole = false; }) == Answer::WrongRole);
        CHECK(decide([](WhisperFacts& f) { f.hintedRecently = true; }) == Answer::Invite); // a hint never blocks an inv
    }
    WhisperFacts hello = Fit();
    hello.inviteRequest = IsInviteRequest("hello");
    CHECK(DecideWhisper(hello) == Answer::Hint);
    CHECK(ReplyText(Answer::WrongLevel, "Maraudon Purple", Role::Damage, 45, 55) == "sorry, looking for 45-55");
    CHECK(ReplyText(Answer::WrongRole, "Maraudon Purple", Role::Tank, 45, 55) == "sorry, need a tank");
    CHECK(ReplyText(Answer::Invite, "Maraudon Purple", Role::Damage, 45, 55) == "inv sent");
}

static void TestReplies()
{
    CHECK(ReplyText(Answer::Invite, "Deadmines", Role::Tank, 17, 24) == "inv sent");
    CHECK(ReplyText(Answer::Hint, "Deadmines", Role::Tank, 17, 24) == "need tank for Deadmines (17-24), w me inv to join");
    CHECK(ReplyText(Answer::Full, "Deadmines", Role::Tank, 17, 24) == "sorry, we're full now");
    CHECK(ReplyText(Answer::AlreadyInvited, "Deadmines", Role::Tank, 17, 24) == "already sent you an inv");
    CHECK(ReplyText(Answer::Busy, "Deadmines", Role::Tank, 17, 24) == "someone's joining atm, w me again in a min");
    CHECK(ReplyText(Answer::Hardcore, "Deadmines", Role::Tank, 17, 24) == "sorry, can't group with hardcore chars");
    CHECK(ReplyText(Answer::InGroup, "Deadmines", Role::Tank, 17, 24) == "leave your group and w me inv again");
    CHECK(ReplyText(Answer::WrongLevel, "Deadmines", Role::Tank, 17, 24) == "sorry, looking for 17-24");
    CHECK(ReplyText(Answer::WrongRole, "Deadmines", Role::Damage, 17, 24) == "sorry, need a dps");
    CHECK(ReplyText(Answer::InviteFailed, "Deadmines", Role::Tank, 17, 24) == "couldn't inv you, got another invite open?");
    CHECK(ReplyText(Answer::Ignore, "Deadmines", Role::Tank, 17, 24).empty());
}

static void TestInvitePending()
{
    CHECK(InvitePending(true, true, 59999, 0));
    CHECK(!InvitePending(true, true, kInviteHoldMs, 0));
    CHECK(!InvitePending(true, false, 1000, 0));     // declined: the core no longer holds our invite
    CHECK(!InvitePending(false, true, 1000, 0));
}

int main()
{
    TestNames();
    TestPostText();
    TestPostDue();
    TestInviteGrammar();
    TestRoleFit();
    TestDecideWhisperOrder();
    TestWhisperTextDecides();
    TestReplies();
    TestInvitePending();
    std::printf("bot lfg advert policy: %d checks passed\n", checks);
    return 0;
}

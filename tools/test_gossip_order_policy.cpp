// Standalone test for `.bot cmd gossip` (runtime/GossipOrderPolicy.h, issue #1012):
// the argument parse, which menu options a bot may pick, and which option an
// index or a text names.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_gossip_order_policy.cpp -o /tmp/test_gossip_order_policy
//   /tmp/test_gossip_order_policy

#include "../runtime/GossipOrderPolicy.h"
#include "../runtime/RemoteCommanderPolicy.h"
#include "../runtime/RemoteCommanderTasks.h"

#include <cstdio>
#include <cstdlib>

using namespace TortoiseBots::GossipOrder;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static Option ScriptChat(std::string const& text, uint32_t action = 1001)
{
    Option option;
    option.text = text;
    option.icon = 0;           // GOSSIP_ICON_CHAT
    option.sender = kSenderMain;
    option.action = action;    // GOSSIP_ACTION_INFO_DEF + n
    return option;
}

static Option DbOption(std::string const& text, uint32_t optionId, uint8_t icon = 0)
{
    Option option;
    option.text = text;
    option.icon = icon;
    option.sender = 0;
    option.action = optionId;
    return option;
}

static void TestParse()
{
    Request r;
    CHECK(ParseRequest("7607 Lcwarrior 1", r));
    CHECK(r.entry == 7607 && r.bot == "Lcwarrior" && r.byIndex && r.index == 1);

    CHECK(ParseRequest("  7604   Lcmage   \"That's it! I'm tired, of helping you out.\"  ", r));
    CHECK(r.entry == 7604 && r.bot == "Lcmage" && !r.byIndex);
    CHECK(r.text == "That's it! I'm tired, of helping you out.");

    // Unquoted text keeps its spaces and commas.
    CHECK(ParseRequest("7607 Bot blow up, that door", r));
    CHECK(!r.byIndex && r.text == "blow up, that door");

    // Quotes make a number a text.
    CHECK(ParseRequest("7607 Bot \"2\"", r));
    CHECK(!r.byIndex && r.text == "2");

    // Refused: missing parts, bad entry, bad index, empty or unterminated quotes.
    CHECK(!ParseRequest("", r));
    CHECK(!ParseRequest("7607", r));
    CHECK(!ParseRequest("7607 Bot", r));
    CHECK(!ParseRequest("weegli Bot 1", r));
    CHECK(!ParseRequest("0 Bot 1", r));
    CHECK(!ParseRequest("16777216 Bot 1", r));
    CHECK(!ParseRequest("7607 Bot 0", r));
    CHECK(!ParseRequest("7607 Bot 16", r));
    CHECK(!ParseRequest("7607 Bot 100", r));
    CHECK(!ParseRequest("7607 Bot \"\"", r));
    CHECK(!ParseRequest("7607 Bot \"  \"", r));
    CHECK(!ParseRequest("7607 Bot \"open", r));
    CHECK(ParseRequest("7607 Bot 15", r) && r.index == 15);
}

static void TestPlain()
{
    // Script options (Weegli, Bly): chat icon, GOSSIP_SENDER_MAIN, INFO_DEF + 1.
    CHECK(IsPlainOption(ScriptChat("Will you blow up that door now?")));
    // Database options: only GOSSIP_OPTION_GOSSIP.
    CHECK(IsPlainOption(DbOption("Tell me about the war.", kOptionGossip)));
    for (uint32_t id = 0; id < kOptionMax; ++id)
        if (id != kOptionGossip)
            CHECK(!IsPlainOption(DbOption("x", id)));
    CHECK(!IsPlainOption(DbOption("I want to browse your goods.", 3, kIconVendor)));   // vendor
    CHECK(!IsPlainOption(DbOption("Show me where I can fly.", 4, kIconTaxi)));         // taxi
    CHECK(!IsPlainOption(DbOption("Train me.", 5, kIconTrainer)));                     // trainer
    CHECK(!IsPlainOption(DbOption("Make this inn your home.", 8)));                    // innkeeper
    CHECK(!IsPlainOption(DbOption("I would like to check my deposit box.", 9)));      // banker
    // A script's action number is its own business: black_morass offers its plain
    // option as GOSSIP_SENDER_MAIN with action 1, which a listed NPC may use.
    CHECK(IsPlainOption(ScriptChat("black_morass-shaped option", 1)));
    // Script service icons are refused.
    Option money = ScriptChat("Buy a world buff", 1001);
    money.icon = kIconMoneyBag;
    CHECK(!IsPlainOption(money));
    Option vendorIcon = ScriptChat("Wares", 1002);
    vendorIcon.icon = kIconVendor;
    CHECK(!IsPlainOption(vendorIcon));
    Option coded = ScriptChat("Enter the code", 1001);
    coded.coded = true;
    CHECK(!IsPlainOption(coded));
    // A script's own sender with a small action number is its own business.
    Option own = ScriptChat("Hire bots", 1);
    own.sender = 900;
    CHECK(IsPlainOption(own));
}

static Request ByIndex(uint32_t index)
{
    Request r;
    r.byIndex = true;
    r.index = index;
    return r;
}

static Request ByText(std::string const& text)
{
    Request r;
    r.text = text;
    return r;
}

static void TestMatch()
{
    std::vector<Option> const weegli = { ScriptChat("Will you blow up that door now?") };

    // Index.
    Match m = MatchOption(weegli, ByIndex(1));
    CHECK(m.ok && m.listIndex == 0);
    m = MatchOption(weegli, ByIndex(2));
    CHECK(!m.ok && m.code == kCodeNoOption);
    CHECK(m.message.find("1 \"Will you blow up that door now?\"") != std::string::npos);

    // Before the pyramid event Weegli has no option at all (menu 1511).
    std::vector<Option> const none;
    m = MatchOption(none, ByIndex(1));
    CHECK(!m.ok && m.code == kCodeNoOption && m.message.find("no options") != std::string::npos);
    m = MatchOption(none, ByText("door"));
    CHECK(!m.ok && m.code == kCodeNoOption);

    // Exact text, case-insensitive text, substring.
    m = MatchOption(weegli, ByText("Will you blow up that door now?"));
    CHECK(m.ok && m.listIndex == 0);
    m = MatchOption(weegli, ByText("WILL YOU BLOW UP THAT DOOR NOW?"));
    CHECK(m.ok && m.listIndex == 0);
    m = MatchOption(weegli, ByText("blow up"));
    CHECK(m.ok && m.listIndex == 0);
    m = MatchOption(weegli, ByText("open the gate"));
    CHECK(!m.ok && m.code == kCodeNoOption);

    // Ambiguous substring is refused; a more exact step wins over it.
    std::vector<Option> const two = {
        ScriptChat("Tell me more", 1001),
        ScriptChat("Tell me more about the trolls", 1002),
    };
    m = MatchOption(two, ByText("tell me"));
    CHECK(!m.ok && m.code == kCodeAmbiguous);
    CHECK(m.message.find("2 options") != std::string::npos);
    m = MatchOption(two, ByText("Tell me more"));
    CHECK(m.ok && m.listIndex == 0);        // exact beats the substring of option 2
    m = MatchOption(two, ByText("tell me MORE"));
    CHECK(m.ok && m.listIndex == 0);        // case-insensitive exact beats the substring
    m = MatchOption(two, ByText("trolls"));
    CHECK(m.ok && m.listIndex == 1);

    // Service options are refused by index and by text, and do not make a text ambiguous.
    std::vector<Option> const innkeeper = {
        DbOption("Make this inn your home.", 8, 5),
        DbOption("I want to browse your goods.", 3, kIconVendor),
        DbOption("Tell me about this inn.", kOptionGossip),
    };
    m = MatchOption(innkeeper, ByIndex(1));
    CHECK(!m.ok && m.code == kCodeNoOption && m.message.find("service") != std::string::npos);
    m = MatchOption(innkeeper, ByIndex(2));
    CHECK(!m.ok && m.code == kCodeNoOption);
    m = MatchOption(innkeeper, ByIndex(3));
    CHECK(m.ok && m.listIndex == 2);
    m = MatchOption(innkeeper, ByText("browse your goods"));
    CHECK(!m.ok && m.code == kCodeNoOption && m.message.find("service") != std::string::npos);
    m = MatchOption(innkeeper, ByText("inn"));   // "inn your home" (service) and "this inn" (plain)
    CHECK(m.ok && m.listIndex == 2);
    CHECK(DescribeMenu(innkeeper).find("1 \"Make this inn your home.\" (service)") != std::string::npos);
}

// The order is known, owner-or-GM like open/use, names its bot second, and runs as a
// walking task whose act is over at once.
static void TestOrderWiring()
{
    using namespace TortoiseBots::RemoteCommander;
    CHECK(IsKnownOrder("gossip"));
    CHECK(TierForOrder("gossip") == OrderTier::OwnerOrGm);
    CHECK(!MayIssueOrder(true, true, TierForOrder("gossip"), false));
    CHECK(MayIssueOrder(true, true, TierForOrder("gossip"), true));
    CHECK(OwnerOrderBotName("gossip", SplitOrderArgs("7607 Lcwarrior \"Will you, blow up\"")) == "Lcwarrior");
    TaskState state = MakeTask(TaskKind::Gossip, 20.0f, true);
    CHECK(state.stage == TaskStage::Walk && state.walks && state.actBudgetMs == kOpenEffectMs);
    TaskFacts facts;
    facts.inCombat = true;
    TaskVerdict verdict = AdvanceTask(state, facts, 250);
    CHECK(verdict.step == TaskStep::Fail && std::string(verdict.code) == kCodeInCombat);
}

// The allowlist (AiPlayerbot.BotGossipNpcs) is the guard: empty = off, only listed
// entries pass, whatever their menus look like.
static void TestAllowlist()
{
    std::set<uint32_t> list;
    CHECK(ParseNpcList("", list) == 0 && list.empty());
    CHECK(ParseNpcList("7607,7604", list) == 0 && list.size() == 2 && list.count(7607) && list.count(7604));
    CHECK(ParseNpcList(" 7607 ; 7604 ,, 7607 ", list) == 0 && list.size() == 2);
    CHECK(ParseNpcList("7607,weegli,0,16777216,-5,7604", list) == 4 && list.size() == 2);
    CHECK(ParseNpcList("123456789", list) == 1 && list.empty());

    std::set<uint32_t> const none;
    CHECK(!IsAllowedNpc(none, 7607));
    CHECK(NotAllowedText(none).find("empty") != std::string::npos);
    ParseNpcList("7607,7604", list);
    CHECK(IsAllowedNpc(list, 7607) && IsAllowedNpc(list, 7604));
    // An unlisted NPC is refused although its options look plain: title_masker
    // (chat icon, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF) takes titles away,
    // carlos_matos (talk icon, INFO_DEF + 1/2) buys and sells for money.
    CHECK(IsPlainOption(ScriptChat("title_masker-shaped option", 1000)));
    Option carlos = ScriptChat("carlos_matos-shaped option", 1001);
    carlos.icon = 7; // GOSSIP_ICON_TALK
    CHECK(IsPlainOption(carlos));
    CHECK(!IsAllowedNpc(list, 50000));
    CHECK(NotAllowedText(list).find("not in AiPlayerbot.BotGossipNpcs") != std::string::npos);
}

// cmd gossip follows a walking NPC once its standing point moved a yard.
static void TestFollow()
{
    using namespace TortoiseBots::RemoteCommander;
    Point3 const a{ 10.0f, 10.0f, 5.0f };
    CHECK(!StandMoved(a, Point3{ 10.5f, 10.5f, 5.0f }));
    CHECK(StandMoved(a, Point3{ 11.0f, 10.0f, 5.0f }));
    CHECK(StandMoved(a, Point3{ 10.0f, 10.0f, 7.0f }));
}

int main()
{
    TestParse();
    TestAllowlist();
    TestFollow();
    TestOrderWiring();
    TestPlain();
    TestMatch();
    std::printf("test_gossip_order_policy: %d checks passed\n", checks);
    return 0;
}

// Standalone pure test for piece P135-C (forever/src/slice/SliceCustomStrategyPure.h). No core, no DB, no legacy headers.
#include "../src/slice/SliceCustomStrategyPure.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using namespace ai::slice::aipreset;

namespace
{

void EscapeStub(std::string& s)
{
    std::string out;
    for (char c : s)
    {
        if (c == '\'' || c == '\\')
            out.push_back('\\');
        out.push_back(c);
    }
    s.swap(out);
}

void TestAiArgs()
{
    {
        ParsedAiArgs p = ParseAiArgs("Thrall save raid");
        assert(p.ok);
        assert(p.op == AiOp::Save);
        assert(p.botName == "Thrall");
        assert(p.preset == "raid");
    }
    {
        ParsedAiArgs p = ParseAiArgs("Thrall load");
        assert(p.ok);
        assert(p.op == AiOp::Load);
        assert(p.botName == "Thrall");
        assert(p.preset.empty());
    }
    {
        ParsedAiArgs p = ParseAiArgs("Thrall LIST");
        assert(p.ok);
        assert(p.op == AiOp::List);
    }
    {
        ParsedAiArgs p = ParseAiArgs("Thrall reset");
        assert(p.ok);
        assert(p.op == AiOp::Reset);
    }
    {
        ParsedAiArgs p = ParseAiArgs("Thrall list extra");
        assert(!p.ok);
        assert(p.errCode == "bad-argument");
    }
    {
        ParsedAiArgs p = ParseAiArgs("Thrall save this_preset_name_is_far_too_long_for_db");
        assert(!p.ok);
        assert(p.errCode == "bad-argument");
    }
    {
        ParsedAiArgs p = ParseAiArgs("Thrall");
        assert(!p.ok);
        assert(p.errCode == "missing-argument");
    }
}

void TestCsArgs()
{
    {
        ParsedCsArgs p = ParseCsArgs("");
        assert(p.ok);
        assert(p.op == CsOp::Help);
    }
    {
        ParsedCsArgs p = ParseCsArgs("say ?");
        assert(p.ok);
        assert(p.op == CsOp::Print);
        assert(p.name == "say");
    }
    {
        ParsedCsArgs p = ParseCsArgs("say 1 timer>say{hello,world}!90,emote!10");
        assert(p.ok);
        assert(p.op == CsOp::Edit);
        assert(p.name == "say");
        assert(p.idx == 1);
        assert(p.line == "timer>say{hello,world}!90,emote!10");
    }
    {
        ParsedCsArgs p = ParseCsArgs("say 1");
        assert(p.ok);
        assert(p.op == CsOp::Edit);
        assert(p.idx == 1);
        assert(p.line.empty());
    }
    {
        ParsedCsArgs p = ParseCsArgs("say abc line");
        assert(!p.ok);
        assert(p.errCode == "bad-argument");
    }
}

void TestSqlBuilders()
{
    assert(ListPresetsSql(42) ==
        "SELECT DISTINCT `preset` FROM `ai_playerbot_db_store` WHERE `guid` = 42 ORDER BY `preset`");
    assert(SelectCustomLinesSql("say's", 0, EscapeStub) ==
        "SELECT `action_line` FROM `ai_playerbot_custom_strategy` WHERE `name` = 'say\\'s' AND `owner` = 0 ORDER BY `idx`");
    assert(InsertCustomLineSql("my_strat", 7, 2, "timer>say'hi'", EscapeStub) ==
        "INSERT INTO `ai_playerbot_custom_strategy` (`name`, `owner`, `idx`, `action_line`) VALUES ('my_strat', 7, 2, 'timer>say\\'hi\\'')");
    assert(UpdateCustomLineSql("my_strat", 7, 2, "timer>dance", EscapeStub) ==
        "UPDATE `ai_playerbot_custom_strategy` SET `action_line` = 'timer>dance' WHERE `name` = 'my_strat' AND `owner` = 7 AND `idx` = 2");
    assert(DeleteCustomLineSql("my_strat", 7, 2, EscapeStub) ==
        "DELETE FROM `ai_playerbot_custom_strategy` WHERE `name` = 'my_strat' AND `owner` = 7 AND `idx` = 2");
}

void TestActionLineSplit()
{
    std::string trigger;
    std::vector<ParsedActionItem> items;
    assert(SplitCustomActionLine("timer>say{a,b}!90,dance", trigger, items));
    assert(trigger == "timer");
    assert(items.size() == 2);
    assert(items[0].action == "say{a,b}");
    assert(items[0].priority == "90");
    assert(items[1].action == "dance");
    assert(items[1].priority.empty());

    assert(!SplitCustomActionLine("no_arrow", trigger, items));
    assert(!SplitCustomActionLine(">say", trigger, items));
    assert(!SplitCustomActionLine("a>b>c", trigger, items));
}

} // namespace

int main()
{
    TestAiArgs();
    TestCsArgs();
    TestSqlBuilders();
    TestActionLineSplit();
    std::cout << "aipreset pure: all assertions passed\n";
    return 0;
}

// The Lua API's pure rules (runtime/LuaApiPolicy.h): guid and command checks, the reply buffer, the callback
// ledger and the plain-table field lists. Standalone: no core, no Eluna.
#include "../runtime/LuaApiPolicy.h"

#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace TortoiseBots::LuaApi;

namespace {

struct FakeSnapshot
{
    uint32_t guid = 42;
    std::string name = "Dudette";
    std::string className = "Mage";
    std::string role = "dps";
    std::string kind = "hired";
    uint32_t level = 60;
    uint32_t hp = 1200, maxHp = 3000, power = 500, maxPower = 900;
    std::string powerType = "mana";
    uint32_t mapId = 509, zoneId = 3429, instanceId = 7;
    bool raidMap = true;
    uint32_t groupId = 3, subgroupId = 2;
    float x = 1.5f, y = -2.0f, z = 30.0f, o = 0.5f;
    std::string target = "Moam";
    std::string strategy = "dps, aoe";
    std::string state = "combat";
    std::string lastAction = "melee";
    std::string travelTo;
    bool isAlt = false;
};

Field const* Find(Fields const& fields, char const* key)
{
    for (Field const& f : fields)
        if (f.key == key)
            return &f;
    return nullptr;
}

void GuidCounter()
{
    uint32_t g = 0;
    assert(ParseGuidCounter(1.0, g) && g == 1);
    assert(ParseGuidCounter(4294967295.0, g) && g == 4294967295u);
    assert(!ParseGuidCounter(0.0, g));
    assert(!ParseGuidCounter(-3.0, g));
    assert(!ParseGuidCounter(1.5, g));
    assert(!ParseGuidCounter(4294967296.0, g));
    assert(!ParseGuidCounter(std::numeric_limits<double>::quiet_NaN(), g));
    assert(!ParseGuidCounter(std::numeric_limits<double>::infinity(), g));
}

void CommandForms()
{
    CommandRequest r = ParseCommand(".bot hire Dudette");
    assert(r.ok && !r.topLevel && r.args == "hire Dudette");
    r = ParseCommand("bot co +avoid aoe");
    assert(r.ok && r.args == "co +avoid aoe");
    r = ParseCommand("  Bot   action follow  ");
    assert(r.ok && r.args == "action follow");
    // a bare subcommand is the same as `.bot <subcommand>`
    r = ParseCommand("hire Dudette");
    assert(r.ok && !r.topLevel && r.args == "hire Dudette");
    // the top-level player command keeps its full text
    r = ParseCommand(".autogear blue");
    assert(r.ok && r.topLevel && r.args == "autogear blue");
    r = ParseCommand("AUTOGEAR");
    assert(r.ok && r.topLevel && r.args == "autogear");
    // a word that merely starts with bot is a subcommand, not the prefix
    r = ParseCommand("botnet x");
    assert(r.ok && r.args == "botnet x");
}

void CommandRefusals()
{
    assert(std::strcmp(ParseCommand("").error, "empty") == 0);
    assert(std::strcmp(ParseCommand("   ").error, "empty") == 0);
    assert(std::strcmp(ParseCommand(".bot").error, "empty") == 0);
    assert(std::strcmp(ParseCommand(".bot   ").error, "empty") == 0);
    assert(std::strcmp(ParseCommand("hire x\nreload config").error, "bad_char") == 0);
    assert(std::strcmp(ParseCommand(std::string("hire\0x", 6)).error, "bad_char") == 0);
    assert(!ParseCommand(std::string(kMaxCommandLength + 1, 'a')).ok);
    assert(std::strcmp(ParseCommand(std::string(kMaxCommandLength + 1, 'a')).error, "too_long") == 0);
    assert(ParseCommand(std::string(kMaxCommandLength, 'a')).ok);
}

void Replies()
{
    ReplyBuffer buffer;
    buffer.Append("one\ntwo\r\n\nthree");
    buffer.Append(nullptr);
    buffer.Append("");
    assert(buffer.Lines().size() == 3 && buffer.Lines()[1] == "two" && !buffer.Truncated());

    ReplyBuffer many;
    for (size_t i = 0; i < kMaxReplyLines + 5; ++i)
        many.Append("line");
    assert(many.Lines().size() == kMaxReplyLines && many.Truncated());

    ReplyBuffer longLine;
    longLine.Append(std::string(kMaxReplyLineLength + 50, 'x').c_str());
    assert(longLine.Lines().size() == 1 && longLine.Lines()[0].size() == kMaxReplyLineLength && longLine.Truncated());
}

void Ledger()
{
    CallbackLedger ledger;
    assert(ledger.Add(1, 1000));
    assert(!ledger.Add(1, 1000)); // taken
    assert(ledger.Has(1) && ledger.Take(1) && !ledger.Take(1) && !ledger.Has(1));

    for (uint64_t id = 10; id < 10 + kMaxPendingCallbacks; ++id)
        assert(ledger.Add(id, 5000));
    assert(!ledger.Add(999, 5000)); // full
    assert(ledger.Take(10) && ledger.Add(999, 5000));

    std::vector<uint64_t> expired;
    ledger.Expire(5000 + kCallbackExpiryMs, expired); // exactly at the limit: still waiting
    assert(expired.empty());
    ledger.Expire(5000 + kCallbackExpiryMs + 1, expired);
    assert(expired.size() == kMaxPendingCallbacks && ledger.Size() == 0);

    // the millisecond clock wraps: 4294967000 -> 10 is 306 ms, not 49 days
    CallbackLedger wrap;
    assert(wrap.Add(7, 4294967000u));
    expired.clear();
    wrap.Expire(10, expired);
    assert(expired.empty() && wrap.Has(7));
}

void BotTable()
{
    FakeSnapshot s;
    Fields f = BotFields(s);
    Field const* guid = Find(f, "guid");
    assert(guid && guid->kind == Field::Kind::Number && guid->number == 42);
    Field const* name = Find(f, "name");
    assert(name && name->kind == Field::Kind::String && name->text == "Dudette");
    Field const* raid = Find(f, "raidMap");
    assert(raid && raid->kind == Field::Kind::Bool && raid->boolean);
    Field const* map = Find(f, "map");
    assert(map && map->number == 509);
    Field const* instance = Find(f, "instance");
    assert(instance && instance->number == 7);
    assert(Find(f, "strategy") && Find(f, "strategy")->text == "dps, aoe");
    // no duplicate keys, none empty
    for (size_t i = 0; i < f.size(); ++i)
    {
        assert(!f[i].key.empty());
        for (size_t j = i + 1; j < f.size(); ++j)
            assert(f[i].key != f[j].key);
    }
}

void RunTable()
{
    RunView idle;
    Fields none = RunFields(idle);
    assert(none.size() == 1 && Find(none, "active") && !Find(none, "active")->boolean);

    RunView run;
    run.active = true;
    run.plan = "zg-jindo";
    run.state = "running";
    run.leg = "3";
    run.step = 12;
    run.steps = 40;
    run.pass = 2;
    run.elapsedMs = 90000;
    Fields f = RunFields(run);
    assert(Find(f, "active")->boolean);
    assert(Find(f, "plan")->text == "zg-jindo");
    assert(Find(f, "step")->number == 12 && Find(f, "steps")->number == 40);
    assert(Find(f, "pass")->number == 2 && Find(f, "elapsedMs")->number == 90000);
}

} // namespace

int main()
{
    GuidCounter();
    CommandForms();
    CommandRefusals();
    Replies();
    Ledger();
    BotTable();
    RunTable();
    return 0;
}

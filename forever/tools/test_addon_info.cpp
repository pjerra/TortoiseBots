// The addon info readers without the core (forever/src/host/TbAddonInfoPure.h).
//   g++ -std=c++17 -Wall -Wextra -I. -Iforever/src forever/tools/test_addon_info.cpp -o /tmp/t && timeout 30 /tmp/t
#include "forever/src/host/TbAddonInfoPure.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace TortoiseBots::addoninfo;
using TortoiseBots::addon::pure::Safe;
static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)

int main()
{
    // 1. The wire escaping: '|' CR LF become blanks, empty becomes "-".
    {
        CHECK(Safe("") == "-");
        CHECK(Safe("a|b\rc\nd") == "a b c d");
        CHECK(Safe("moon") == "moon");
    }
    // 2. Config values turn '|' into '/' (TB's SendConfigSearch), keys the normal Safe.
    {
        CHECK(SafeConfigValue("a|b") == "a/b");
        CHECK(ConfigLine("k", "a|b") == "TBM:CONFIG|k|a/b");
        CHECK(ConfigLine("", "v") == "TBM:CONFIG|-|v");
        CHECK(ConfigBeginLine(7) == "TBM:CONFIG_BEGIN|7");
        CHECK(ConfigEndLine(7, 250) == "TBM:CONFIG_END|7|250");
        CHECK(ConfigSummaryLine(3, "pool") == "Config: 3 keys match 'pool'.");
    }
    // 3. The search matches TB's FindConfigEntries: case-insensitive
    // substring, empty matches all; secrets read back masked.
    {
        CHECK(ConfigMatches("pool", "TortoiseBots.Pool.Enable"));
        CHECK(ConfigMatches("POOL", "TortoiseBots.Pool.Enable"));
        CHECK(ConfigMatches("", "anything"));
        CHECK(!ConfigMatches("pool", "TortoiseBots.Bg.Enable"));
        CHECK(ConfigSecret("LoginDatabaseInfo") == false);
        CHECK(ConfigSecret("SomePasswordKey"));
        CHECK(ConfigSecret("oauthTOKENx"));
        CHECK(ConfigSecret("llmApiKey"));
        CHECK(MaskedConfigValue("DbPassword", "hunter2") == "***");
        CHECK(MaskedConfigValue("TortoiseBots.Pool.Enable", "1") == "1");
    }
    // 4. The "features" verb splits the two addon sends: bare lists the
    // switches, "all <text>" searches, anything else is usage.
    {
        FeaturesRequest bare = ParseFeaturesArgs("");
        CHECK(!bare.search && !bare.usageError);
        FeaturesRequest search = ParseFeaturesArgs("all pool");
        CHECK(search.search && !search.usageError && search.filter == "pool");
        CHECK(ParseFeaturesArgs("all").usageError);
        CHECK(ParseFeaturesArgs("all x").usageError);
        CHECK(ParseFeaturesArgs("everything").usageError);
        CHECK(FeatureLine("pool", "on", "Bot pool") == "TBM:FEATURE|pool|on|Bot pool");
        CHECK(FeatureLine("x", "", "L") == "TBM:FEATURE|x|-|L");
    }
    // 5. The CC stream: BEGIN count, one row per mark, END; only the seven
    // crowd-control marks pass (skull is the kill mark, never CC).
    {
        CHECK(CcBeginLine(2) == "TBM:CC_ASSIGN_BEGIN|2");
        CHECK(CcBeginLine(0) == "TBM:CC_ASSIGN_BEGIN|0");
        CHECK(CcLine("Alpha", "Moon") == "TBM:CC_ASSIGN|Alpha|moon");
        CHECK(std::string(kCcEnd) == "TBM:CC_ASSIGN_END");
        CHECK(ValidCcMark("moon") && ValidCcMark("STAR") && ValidCcMark("cross"));
        CHECK(!ValidCcMark("skull") && !ValidCcMark("") && !ValidCcMark("focus"));
    }
    // 6. The "run" verb routes the three readers; anything else keeps its word
    // for the error intent.
    {
        CHECK(ParseRunArgs("status").reader == RunReader::Status);
        CHECK(ParseRunArgs("  HERE  ").reader == RunReader::Here);
        CHECK(ParseRunArgs("list").reader == RunReader::List);
        RunRequest other = ParseRunArgs("start foo");
        CHECK(other.reader == RunReader::Other && other.sub == "start");
        CHECK(ParseRunArgs("").sub == "unknown");
    }
    // 7. No run going is TB's byte-exact line, not an empty stream.
    {
        CHECK(NoRunLine() == "TBM:RUN|-|none|0/0|-|-|-|no run");
        CHECK(NoRunHereLine() == "TBM:RUN_HERE|-|0");
        CHECK(RunHereLine("Uldaman", 42) == "TBM:RUN_HERE|Uldaman|42");
    }
    // 8. The "here" lookup takes the nearest routed step on the map and skips
    // unrouted (0,0,0) steps.
    {
        std::vector<PlanPoint> points = {
            { "Uldaman", 70, 0.0f, 0.0f, 0.0f },
            { "Uldaman", 70, 10.0f, 0.0f, 0.0f },
            { "ZF", 209, 11.0f, 0.0f, 0.0f },
        };
        NearestPlan near = PickNearestPlan(70, 9.0f, 0.0f, 0.0f, points);
        CHECK(near.found && near.plan == "Uldaman" && near.yards == 1);
        NearestPlan far = PickNearestPlan(70, 6.0f, 0.0f, 0.0f, points);
        CHECK(far.found && far.plan == "Uldaman" && far.yards == 4);
        CHECK(!PickNearestPlan(1, 9.0f, 0.0f, 0.0f, points).found);
        CHECK(!PickNearestPlan(70, 0.0f, 0.0f, 0.0f, {}).found);
    }
    // 9. The plan list mirrors TB's "Dungeon plans:" reply, with the
    // unsupported tail only when some step is not supported yet.
    {
        CHECK(RunListLine({}) == "Dungeon plans: none loaded");
        std::vector<PlanEntry> plans = { { "Uldaman", 70, 41, 2 }, { "ZF", 209, 30, 0 } };
        CHECK(RunListLine(plans) == "Dungeon plans: Uldaman (map 70, 41 steps, 2 not supported yet), ZF (map 209, 30 steps)");
    }
    // 10. The layout line carries all five fields; TBF fills unknown roles
    // with "-" (the addon parses "-" tanks as no main tank, not a break).
    {
        CHECK(RaidLayoutLine("5", "-", "-", "-", "-") == "TBM:RAID_LAYOUT|5|-|-|-|-");
        CHECK(RaidLayoutWarning("a member is not where planned") ==
            "TBM:RAID_LAYOUT|warning|a member is not where planned");
    }

    if (failures == 0)
        std::printf("test_addon_info: all cases pass\n");
    return failures == 0 ? 0 : 1;
}

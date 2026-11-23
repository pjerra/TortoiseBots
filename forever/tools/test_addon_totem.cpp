// Standalone test of the `totem` addon's core-free half (forever/src/host/TbAddonTotemPure.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. -Iforever/src forever/tools/test_addon_totem.cpp -o /tmp/test_addon_totem && /tmp/test_addon_totem
// The parse ports TB's `.bot totem` grammar (TortoiseBots/commands/Totem.cpp:229-464); the TOTEM line matches
// TB's TBM:TOTEM record (Totem.cpp:142-164) which TortoiseBotsForeverManager/BotTotem.lua parses.
#include "forever/src/host/TbAddonTotemPure.h"

#include <cstdint>
#include <cstdio>

namespace totem = TortoiseBots::totem;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void TestParseSlot()
{
    totem::Request r = totem::ParseTotemRequest("Garr earth strength of earth");
    CHECK(r.ok && !r.partyScope && r.scope == "garr");
    CHECK(r.kind == totem::Request::Kind::Slot && r.slot == 0);
    CHECK(r.stored == "strength of earth totem");

    // Short names and case do not matter; "none" stores as "" (TB: unset behaves same).
    r = totem::ParseTotemRequest("Garr air Windfury");
    CHECK(r.ok && r.slot == 3 && r.stored == "windfury totem");
    r = totem::ParseTotemRequest("Garr fire none");
    CHECK(r.ok && r.slot == 1 && r.stored.empty());
    r = totem::ParseTotemRequest("Garr water AUTO");
    CHECK(r.ok && r.slot == 2 && r.stored == "auto");
    r = totem::ParseTotemRequest("Garr earth tremor totem");
    CHECK(r.ok && r.stored == "tremor totem");
}

static void TestParseSlotRejects()
{
    CHECK(!totem::ParseTotemRequest("").ok);
    CHECK(!totem::ParseTotemRequest("Garr").ok);
    CHECK(!totem::ParseTotemRequest("Garr frost magma totem").ok); // no such slot
    CHECK(totem::ParseTotemRequest("Garr frost magma totem").error == "invalid");
    totem::Request r = totem::ParseTotemRequest("Garr fire frostbolt");
    CHECK(!r.ok && r.error == "unknown-totem");
    CHECK(r.message.find("fire") != std::string::npos);
    CHECK(!totem::ParseTotemRequest("Garr earth").ok); // slot with no totem word
    CHECK(!totem::ParseTotemRequest("Garr water fire resistance totem extra").ok);
    // "fire resistance totem" is on both the fire and the water lists (TB's table).
    r = totem::ParseTotemRequest("Garr fire fire resistance totem");
    CHECK(r.ok && r.stored == "fire resistance totem");
    r = totem::ParseTotemRequest("Garr water fire resistance totem");
    CHECK(r.ok && r.stored == "fire resistance totem");
}

static void TestParseSet()
{
    totem::Request r = totem::ParseTotemRequest("party set stoneskin totem,searing totem,mana spring totem,windfury totem");
    CHECK(r.ok && r.partyScope && r.scope == "party");
    CHECK(r.kind == totem::Request::Kind::Set);
    CHECK(r.storedSet[0] == "stoneskin totem" && r.storedSet[1] == "searing totem");
    CHECK(r.storedSet[2] == "mana spring totem" && r.storedSet[3] == "windfury totem");

    // Whitespace and short names are tolerated; raid scopes like party.
    r = totem::ParseTotemRequest("raid set none , auto , mana spring , grace of air");
    CHECK(r.ok && r.partyScope && r.scope == "raid");
    CHECK(r.storedSet[0].empty() && r.storedSet[1] == "auto");
    CHECK(r.storedSet[2] == "mana spring totem" && r.storedSet[3] == "grace of air totem");

    CHECK(!totem::ParseTotemRequest("Garr set stoneskin totem,searing totem").ok); // not four
    CHECK(!totem::ParseTotemRequest("Garr set a,b,c,d,e").ok);
    CHECK(!totem::ParseTotemRequest("Garr set stoneskin totem,frostbolt,mana spring totem,windfury totem").ok);
}

static void TestParseModeDropStatus()
{
    totem::Request r = totem::ParseTotemRequest("Garr mode manual");
    CHECK(r.ok && r.kind == totem::Request::Kind::Mode && r.mode == "manual");
    r = totem::ParseTotemRequest("party mode pre-pull");
    CHECK(r.ok && r.partyScope && r.mode == "prepull");
    r = totem::ParseTotemRequest("Garr mode AUTO");
    CHECK(r.ok && r.mode == "auto");
    CHECK(!totem::ParseTotemRequest("Garr mode always").ok);

    r = totem::ParseTotemRequest("Garr drop");
    CHECK(r.ok && r.kind == totem::Request::Kind::Drop && !r.partyScope);
    r = totem::ParseTotemRequest("party drop");
    CHECK(r.ok && r.kind == totem::Request::Kind::Drop && r.partyScope);

    r = totem::ParseTotemRequest("Garr status");
    CHECK(r.ok && r.kind == totem::Request::Kind::Status && r.scope == "garr");
    r = totem::ParseTotemRequest("party status");
    CHECK(r.ok && r.kind == totem::Request::Kind::Status && r.partyScope);
}

static void TestLineFormat()
{
    // The wire the addon parses (BotTotem.lua ParseTotemLine): slots in
    // earth/fire/water/air order, mode, then the trained names joined by '+'.
    CHECK(totem::DisplaySlot("") == "none");
    CHECK(totem::DisplaySlot("auto") == "auto");
    CHECK(totem::DisplaySlot("windfury totem") == "windfury totem");
    CHECK(totem::DisplayMode("") == "auto");
    CHECK(totem::DisplayMode("manual") == "manual");
    CHECK(totem::JoinKnown({}) == "");
    CHECK(totem::JoinKnown({"stoneskin totem", "fire resistance totem", "fire resistance totem"}) ==
        "stoneskin totem+fire resistance totem");
    std::string line = totem::FormatTotemLine("Garr", "none", "searing totem", "auto", "windfury totem", "auto",
        "searing totem+windfury totem");
    CHECK(line == "TBM:TOTEM|Garr|earth=none,fire=searing totem,water=auto,air=windfury totem,mode=auto,"
        "known=searing totem+windfury totem");
    // A short list keeps TB's exact `known` encoding; a trained-everything
    // shaman drops the list instead of truncating mid-name (the addon reads
    // a missing `known` as nil and offers the slot's totems, the same shape
    // an older server sends). Either way the line fits 255 bytes.
    CHECK(line.size() <= TortoiseBots::addon::pure::kMaxLineBytes);
    CHECK(totem::AckScope(true, "Garr") == "party");
    CHECK(totem::AckScope(false, "Garr") == "bot:Garr");
}

static void TestLineFits()
{
    // A trained-everything shaman: every slot's full list joined, the shape
    // the live realm truncated mid-`known` at 255 bytes.
    std::string all;
    for (int slot = 0; slot < 4; ++slot)
    {
        std::uint32_t count = 0;
        char const* const* choices = ai::TotemChoicesForSlot(slot, count);
        for (std::uint32_t i = 0; i < count; ++i)
        {
            if (!all.empty())
                all += "+";
            all += choices[i];
        }
    }
    std::string line = totem::FormatTotemLine("Flivsham", "strength of earth totem", "none",
        "mana spring totem", "none", "manual", all);
    CHECK(line.size() <= TortoiseBots::addon::pure::kMaxLineBytes);
    CHECK(line.find(",known=") == std::string::npos);
    CHECK(line.find("earth=strength of earth totem") != std::string::npos);
    CHECK(line.find("water=mana spring totem") != std::string::npos);
    CHECK(line.find("mode=manual") != std::string::npos);
}
int main()
{
    TestParseSlot();
    TestParseSlotRejects();
    TestParseSet();
    TestParseModeDropStatus();
    TestLineFormat();
    TestLineFits();
    if (g_fail)
        std::printf("FAIL %d/%d\n", g_fail, g_checks);
    else
        std::printf("OK %d checks\n", g_checks);
    return g_fail != 0;
}

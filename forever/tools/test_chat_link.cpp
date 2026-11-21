// S1-R1: the chat link parser of the port, core-free (forever/src/compat/TbChatLinkPure.h and ai/playerbot/ChatLinkScan.h).
//   g++ -std=c++17 -Wall -Wextra -I. forever/tools/test_chat_link.cpp -o /tmp/t && timeout 30 /tmp/t   (the test also arms its own 20 s alarm)
//
// The first case is the bug: the port read the modern item link's field 3 (a gem) as 1.12's random property, so
// `|cnIQ4:|Hitem:19019::3:...` named item 19019 with a random property of 3 instead of none.
#include "forever/src/compat/TbChatLinkPure.h"

#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>
#if defined(__unix__) || defined(__APPLE__)
#include <csignal>
#include <unistd.h>
#endif

using namespace TortoiseBots::chatlink;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)

// A modern item link whose name is padded: five of them with 43/44 extra name bytes make a 511-character message, the core's chat limit,
// whose fifth link starts at index 426. The old `uint8 pos` scan wrapped there and never ended.
static std::string ModernLink(int id, size_t nameLength)
{
    return "|cnIQ4:|Hitem:" + std::to_string(id) + "::3::::::60:::::::::|h[" + std::string(nameLength, 'T') + "]|h|r";
}

// The run's own timer: a parser that never ends (an 8-bit counter over a 511-character message) fails here instead of hanging the runner.
static void OnTimeout(int)
{
    static char const msg[] = "FAIL: timed out (a chat parser did not finish)\n";
    if (write(2, msg, sizeof(msg) - 1) < 0) {}
    _exit(124);
}

int main()
{
#if defined(__unix__) || defined(__APPLE__)
    std::signal(SIGALRM, OnTimeout);
    alarm(20);
#endif
    // The modern item link: field 3 is a gem, and the port has no random property, so the qualifier is id:enchant:0.
    std::string const thunderfury = "|cnIQ4:|Hitem:19019::3::::::60:::::::::|h[Thunderfury]|h|r";
    CHECK(LegacyItemQualifier("19019::3::::::60:::::::::") == "19019:0:0");

    // The 1.12 layouts keep their fields, written as numbers: 3 or 4 fields, the third one the random property.
    CHECK(LegacyItemQualifier("19019:0:0") == "19019:0:0");
    CHECK(LegacyItemQualifier("2512:0:0:0") == "2512:0:0:0");
    CHECK(LegacyItemQualifier("10090:0:960:0") == "10090:0:960:0");
    CHECK(ItemQualifiers("|cffffffff|Hitem:2512:0:0:0|h[Rough Arrow]|h|r").size() == 1);
    CHECK(ItemQualifiers("|cffffffff|Hitem:2512:0:0:0|h[Rough Arrow]|h|r")[0] == "2512:0:0:0");
    // An enchantment survives the normalization; an empty one is 0.
    CHECK(LegacyItemQualifier("19019:1234:3:::::::60:::::::::") == "19019:1234:0");

    // Every field goes through the non-throwing 1-9 digit rule: an enchant above 2^31 and one above 2^32 answer 0, and no field of a
    // 3/4-field qualifier lets a space, '=' or a newline through (the reply cannot gain a key or a line).
    CHECK(ai::chatlink::FieldValue("19019") == 19019);
    CHECK(ai::chatlink::FieldValue("999999999") == 999999999);
    CHECK(ai::chatlink::FieldValue("3000000000") == 0);
    CHECK(ai::chatlink::FieldValue("5000000000") == 0);
    CHECK(ai::chatlink::FieldValue("") == 0);
    CHECK(ai::chatlink::FieldValue("abc") == 0);
    CHECK(ai::chatlink::FieldValue("12a") == 0);
    CHECK(ai::chatlink::FieldValue("-1") == 0);
    CHECK(LegacyItemQualifier("19019:3000000000:3::::::60:::::::::") == "19019:0:0");
    CHECK(LegacyItemQualifier("19019:5000000000:3::::::60:::::::::") == "19019:0:0");
    CHECK(LegacyItemQualifier("19019:abc:3::::::60:::::::::") == "19019:0:0");
    CHECK(LegacyItemQualifier("19019:0:0 b=c") == "19019:0:0");
    CHECK(LegacyItemQualifier("19019:0:\n=") == "19019:0:0");
    CHECK(LegacyItemQualifier("") == "");

    // Quest links, bare numbers and nothing.
    CHECK(QuestId("|cffffff00|Hquest:783:1:1:60|h[A Threat Within]|h|r") == 783);
    CHECK(QuestId("783") == 783);
    CHECK(QuestId("  783\t") == 783);
    CHECK(QuestId("abc") == 0);
    CHECK(QuestId("|Hquest:|h") == 0);
    CHECK(QuestId("0") == 0);
    CHECK(QuestId("1234567890") == 0);          // 10 digits are not an id
    // The first link wins, and a `|H` with another tag is not this one.
    CHECK(QuestId("|Hquest:111:1|h[A]|h and |Hquest:222:1|h[B]|h") == 111);
    CHECK(QuestId("|Hitem:19019:0:0:0|h[T]|h") == 0);

    // Spell links: spell, then the enchant recipe, else a bare number. A talent (its RankID lookup is not ported) and a trade link are 0.
    CHECK(SpellId("|cff71d5ff|Hspell:585:0|h[Smite]|h|r") == 585);
    CHECK(SpellId("|cffffd000|Henchant:3919|h[Engineering: Rough Dynamite]|h|r") == 3919);
    CHECK(SpellId("585") == 585);
    CHECK(SpellId("|cff4e96f7|Htalent:2232:-1|h[Taste for Blood]|h|r") == 0);
    CHECK(SpellId("|cff71d5ff|Htrade:51304:0|h[T]|h|r") == 0);
    CHECK(SpellId("|Hspell:|h") == 0);
    CHECK(SpellId("smite") == 0);
    // A spell link beats an enchant link that comes first? No: each tag is searched on its own, and `spell` is asked first.
    CHECK(SpellId("|Henchant:7418|h[E]|h |Hspell:585|h[S]|h") == 585);

    // Every item id, once each, and not the id of another tag (`|Hitemset:` is not `|Hitem:`).
    std::string const two = "|cffffffff|Hitem:2512:0:0:0|h[Rough Arrow]|h|r and |cnIQ4:|Hitem:19019::3:0:0:0:0:60:0:0:0:0:0|h[Thunderfury]|h|r";
    CHECK(AllIds(two, "item") == (std::set<std::uint32_t>{2512, 19019}));
    CHECK(AllIds("|Hitem:2512:0:0:0|h and |Hitem:2512:0:0:0|h", "item") == (std::set<std::uint32_t>{2512}));
    CHECK(AllIds("|Hitemset:1|h", "item").empty());
    CHECK(AllIds("no links here", "item").empty());

    // The qualifier list in the order written, and the reply's two lists agree on which links they read.
    std::vector<std::string> const quals = ItemQualifiers(thunderfury + " " + "|cffffffff|Hitem:2512:0:0:0|h[Rough Arrow]|h|r");
    CHECK(quals.size() == 2);
    CHECK(quals.size() == 2 && quals[0] == "19019::3::::::60:::::::::" && quals[1] == "2512:0:0:0");
    // The real unterminated link (no '|' after the qualifier) has no qualifier and ends the scan; `|h` after the qualifier is terminated.
    CHECK(ItemQualifiers("|Hitem:19019").empty());
    CHECK(ItemQualifiers("|Hitem:19019:0:0:0").empty());
    CHECK(ItemQualifiers("|Hitem:19019|h").size() == 1);
    CHECK(ItemQualifiers("nothing").empty());
    CHECK(LinkId("|Hitem:19019:0:0:0|h[T]|h", "item") == 19019);
    CHECK(LinkId("|Hitemset:1|h", "item") == 0);

    // items= and quals= come from one scan (ai::chatlink::ItemLinks), so both name the same links: a `Hitem:` with no digit id is in
    // neither, and each qualifier starts with the id the items list writes.
    std::string const agree = "Hitem:5:0:0| and |cffffffff|Hitem:2512:0:0:0|h[Rough Arrow]|h|r";
    std::vector<ai::chatlink::ItemLink> const links = ItemLinks(agree);
    CHECK(links.size() == 2);
    CHECK(ItemQualifiers(agree).size() == links.size());
    CHECK(links.size() == 2 && links[0].id == 5 && links[0].qualifier == "5:0:0");
    CHECK(links.size() == 2 && links[1].id == 2512 && links[1].qualifier == "2512:0:0:0");
    for (ai::chatlink::ItemLink const& link : links)
        CHECK(link.qualifier.compare(0, std::to_string(link.id).size(), std::to_string(link.id)) == 0);
    CHECK(ItemLinks("Hitem:abc:0:0|").empty());
    CHECK(ItemQualifiers("Hitem:abc:0:0|").empty());

    // The five-link, 511-character message: the scan must finish and read all five (the old `uint8 pos` loop hung; see the notes).
    std::string const longMessage = ModernLink(19019, 54) + " " + ModernLink(19020, 54) + " " + ModernLink(19021, 54) + " " +
        ModernLink(19022, 55) + " " + ModernLink(19023, 55);
    CHECK(longMessage.size() == 511);
    CHECK(longMessage.rfind("|Hitem:") > 250);
    std::vector<ai::chatlink::ItemLink> const many = ItemLinks(longMessage);
    CHECK(many.size() == 5);
    CHECK(many.size() == 5 && many[4].id == 19023);
    CHECK(ItemQualifiers(longMessage).size() == 5);
    CHECK(LegacyItemQualifier(many.empty() ? std::string() : many[4].qualifier) == "19023:0:0");

    // The reply's values are encoded: a space, '=' or newline from the text cannot add a key or split the record.
    CHECK(EncodedValue("19019:0:0") == "19019:0:0");
    CHECK(EncodedValue("a b=c\nd") == "a%20b%3Dc%0Ad");
    CHECK(EncodedValue("100%") == "100%25");

    // Money text: size_t indexes, so a 600-character digit/g/s/c string finishes (the old `uint8 i` loop wrapped at 255 and spun forever),
    // and the sums saturate instead of wrapping.
    CHECK(ai::chatlink::ParseMoney("5g10s3c") == 51003);
    CHECK(ai::chatlink::ParseMoney("100g") == 1000000);
    CHECK(ai::chatlink::ParseMoney("10s") == 1000);
    CHECK(ai::chatlink::ParseMoney("7c") == 7);
    CHECK(ai::chatlink::ParseMoney("5g 10s") == 50000);          // the first space ends it
    CHECK(ai::chatlink::ParseMoney("100") == 0);                 // digits with no unit are dropped
    CHECK(ai::chatlink::ParseMoney("5gx3c") == 0);               // a foreign character voids the amount
    CHECK(ai::chatlink::ParseMoney("") == 0);
    {
        std::string digits600;
        for (int k = 0; k < 600; ++k)
            digits600 += "1234567890gsc"[k % 13];
        CHECK(digits600.size() == 600);
        std::uint32_t const unitsMix = ai::chatlink::ParseMoney(digits600);
        (void)unitsMix;                                           // it must finish; the timer above fails the run otherwise
        CHECK(ai::chatlink::ParseMoney(std::string(600, '9') + "g") == 0xFFFFFFFFu);   // saturates, no wrap
        CHECK(ai::chatlink::ParseMoney(std::string(600, '9')) == 0);                   // no unit letter
        CHECK(ai::chatlink::ParseMoney(std::string(300, '1') + std::string(300, 'g')) <= 0xFFFFFFFFu);
        CHECK(ai::chatlink::ParseMoney(std::string(300, 'g') + std::string(300, 's')) == 0);
        CHECK(ai::chatlink::ParseMoney("1g" + std::string(598, 'c')) == 10000);
    }

    // Unsigned chat numbers (the quest/item/skill/faction ids, @random=N): no throw on an overflow or garbage; valid input unchanged.
    {
        std::uint32_t v = 7;
        CHECK(ai::chatlink::ParseUnsigned("19019", v) && v == 19019);
        CHECK(ai::chatlink::ParseUnsigned("0", v) && v == 0);
        CHECK(ai::chatlink::ParseUnsigned("2147483647", v) && v == 2147483647u);
        CHECK(ai::chatlink::ParseUnsigned("4294967295", v) && v == 4294967295u);
        v = 7;
        CHECK(!ai::chatlink::ParseUnsigned("4294967296", v) && v == 7);
        CHECK(!ai::chatlink::ParseUnsigned("99999999999", v));
        CHECK(!ai::chatlink::ParseUnsigned("1234567890123456789012345", v) && v == 7);   // 25 digits: the cancel-by-id chat case
        CHECK(!ai::chatlink::ParseUnsigned(std::string(600, '9'), v));
        CHECK(!ai::chatlink::ParseUnsigned("", v));
        CHECK(!ai::chatlink::ParseUnsigned("12a", v));
        CHECK(!ai::chatlink::ParseUnsigned("-5", v));
        CHECK(!ai::chatlink::ParseUnsigned(" 5", v));
        CHECK(v == 7);
    }
    {
        std::int32_t v = 7;
        CHECK(ai::chatlink::ParseSigned("1", v) && v == 1);
        CHECK(ai::chatlink::ParseSigned("+5", v) && v == 5);
        CHECK(ai::chatlink::ParseSigned("-5", v) && v == -5);
        CHECK(ai::chatlink::ParseSigned("2147483647", v) && v == INT32_MAX);
        CHECK(ai::chatlink::ParseSigned("-2147483648", v) && v == INT32_MIN);
        v = 7;
        CHECK(!ai::chatlink::ParseSigned("2147483648", v) && v == 7);
        CHECK(!ai::chatlink::ParseSigned("-2147483649", v));
        CHECK(!ai::chatlink::ParseSigned("99999999999", v));
        CHECK(!ai::chatlink::ParseSigned("-" + std::string(600, '9'), v));
        CHECK(!ai::chatlink::ParseSigned("-", v));
        CHECK(!ai::chatlink::ParseSigned("+", v));
        CHECK(!ai::chatlink::ParseSigned("", v));
        CHECK(!ai::chatlink::ParseSigned("--5", v));
        CHECK(!ai::chatlink::ParseSigned("5-", v));
        CHECK(!ai::chatlink::ParseSigned("abc", v));
        CHECK(v == 7);
    }

    // The random property of an item qualifier is a signed int32 (negative for "of the X" suffix items; ItemQualifier(string) reads
    // it back from GetQualifier()): a valid negative survives, an out-of-range or garbage one is refused (0), never a throw.
    CHECK(ai::chatlink::SignedFieldValue("-1234") == -1234);
    CHECK(ai::chatlink::SignedFieldValue("-27") == -27);
    CHECK(ai::chatlink::SignedFieldValue("960") == 960);
    CHECK(ai::chatlink::SignedFieldValue("0") == 0);
    CHECK(ai::chatlink::SignedFieldValue("-2147483648") == INT32_MIN);
    CHECK(ai::chatlink::SignedFieldValue("-2147483649") == 0);
    CHECK(ai::chatlink::SignedFieldValue("-5000000000") == 0);
    CHECK(ai::chatlink::SignedFieldValue("5000000000") == 0);
    CHECK(ai::chatlink::SignedFieldValue("-") == 0);
    CHECK(ai::chatlink::SignedFieldValue("-abc") == 0);
    CHECK(ai::chatlink::SignedFieldValue("") == 0);
    CHECK(LegacyItemQualifier("10090:0:-1234:0") == "10090:0:-1234:0");
    CHECK(LegacyItemQualifier("10090:0:-27") == "10090:0:-27");
    CHECK(LegacyItemQualifier("10090:0:-5000000000:0") == "10090:0:0:0");
    CHECK(LegacyItemQualifier("10090:0:-:0") == "10090:0:0:0");
    CHECK(LegacyItemQualifier("19019:1234:-3:::::::60:::::::::") == "19019:1234:0");   // modern layout: field 3 is no random property

    if (failures)
        return 1;
    std::printf("RESULT: PASS (test_chat_link)\n");
    return 0;
}

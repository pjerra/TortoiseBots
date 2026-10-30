// Whispers to an advertising bot LFG leader are taken on the packet hook
// (any thread) and handed to BotLfgService on the world thread; the bot's
// AI never sees them. Exercises the whisper parse, the leader set, the cap
// and two producer threads against a draining world thread.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra -pthread tools/test_bot_lfg_whisper_inbox.cpp -o /tmp/lfg34_inbox
//   /tmp/lfg34_inbox

#include "../runtime/BotChatGate.h"
#include "../runtime/BotLfgWhisperInbox.h"
#include "../runtime/policy/lfg/BotLfgAdvertPolicy.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

using namespace TortoiseBots;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

constexpr uint8_t kWhisper = 6, kChannel = 14;
constexpr uint32_t kAddon = 0xFFFFFFFF, kUniversal = 0;

static std::vector<uint8_t> Packet(uint8_t type, uint32_t lang, uint64_t sender, std::string const& text)
{
    std::vector<uint8_t> p;
    p.push_back(type);
    for (int i = 0; i < 4; ++i) p.push_back(uint8_t(lang >> (8 * i)));
    for (int i = 0; i < 8; ++i) p.push_back(uint8_t(sender >> (8 * i)));
    uint32_t len = uint32_t(text.size() + 1);
    for (int i = 0; i < 4; ++i) p.push_back(uint8_t(len >> (8 * i)));
    p.insert(p.end(), text.begin(), text.end());
    p.push_back(0);
    p.push_back(0); // chat tag
    return p;
}

// SMSG_MESSAGECHAT for a whisper byte for byte as ChatHandler::BuildChatPacket
// (~/tortoise-wow src/game/Chat/Chat.cpp) writes it from MasterPlayer::Whisper:
// a sender whose chat tag is CHAT_TAG_GM (3; a GM account with GM.Chat on at
// login keeps it in MasterPlayer::m_chatTag for the whole session) has the
// text wrapped in "|c1049e6ff" ... "|r" unless the language is LANG_ADDON.
static std::vector<uint8_t> CorePacket(uint8_t type, uint32_t lang, uint64_t sender, std::string const& message,
    uint8_t chatTag)
{
    std::string text = chatTag == 3 && lang != kAddon ? "|c1049e6ff" + message + "|r" : message;
    std::vector<uint8_t> p;
    p.push_back(type);
    for (int i = 0; i < 4; ++i) p.push_back(uint8_t(lang >> (8 * i)));
    for (int i = 0; i < 8; ++i) p.push_back(uint8_t(sender >> (8 * i)));
    uint32_t len = uint32_t(text.size() + 1);
    for (int i = 0; i < 4; ++i) p.push_back(uint8_t(len >> (8 * i)));
    p.insert(p.end(), text.begin(), text.end());
    p.push_back(0);
    p.push_back(chatTag);
    return p;
}

static std::string Received(uint8_t chatTag, std::string const& message)
{
    BotLfgWhisperInbox inbox;
    inbox.PublishLeaders({42});
    std::vector<uint8_t> p = CorePacket(kWhisper, kUniversal, 77, message, chatTag);
    CHECK(inbox.Offer(42, p.data(), p.size(), kWhisper, kAddon));
    std::vector<LfgWhisper> got = inbox.Drain();
    CHECK(got.size() == 1 && got[0].leader == 42 && got[0].sender == 77);
    return got[0].text;
}

// lfg3 live: every "inv" from the test players (GM accounts) got the hint.
static void TestCoreWhisperIsInviteRequest()
{
    for (uint8_t tag : {uint8_t(0), uint8_t(1), uint8_t(2), uint8_t(3)}) // none, AFK, DND, GM
    {
        for (char const* yes : {"inv", "inv me", "invite", "INV", "Inv Me", "InViTe", "inv  ", "inv me!", "invite.",
                                " inv ", "inv?", "invite me pls..."})
        {
            std::string text = Received(tag, yes);
            CHECK(text == yes);
            CHECK(BotLfgAdvert::IsInviteRequest(text));
        }
        for (char const* no : {"hello", "hi", "inventory full"})
            CHECK(!BotLfgAdvert::IsInviteRequest(Received(tag, no)));
    }
    // Only the core's own GM wrap is taken off: the same bytes under another tag are the player's text.
    CHECK(Received(0, "|c1049e6ffinv|r") == "|c1049e6ffinv|r");
    CHECK(Received(3, "|c1049e6ffinv|r") == "|c1049e6ffinv|r");
    CHECK(Received(3, "") == "");
}

static void TestParse()
{
    std::vector<uint8_t> p = Packet(kWhisper, kUniversal, 0x0000000000001234ULL, "inv pls");
    WhisperChatLine line;
    CHECK(ParseWhisperChatLine(p.data(), p.size(), line));
    CHECK(line.language == kUniversal && line.sender == 0x1234 && std::string(line.text, line.textLength) == "inv pls");
    CHECK(!ParseWhisperChatLine(p.data(), 16, line));
    CHECK(!ParseWhisperChatLine(nullptr, 0, line));
}

static void TestOffer()
{
    BotLfgWhisperInbox inbox;
    std::vector<uint8_t> p = Packet(kWhisper, kUniversal, 77, "inv");
    CHECK(!inbox.Offer(42, p.data(), p.size(), kWhisper, kAddon));        // nobody published
    inbox.PublishLeaders({42});
    CHECK(inbox.IsLeader(42) && !inbox.IsLeader(43));
    CHECK(!inbox.Offer(43, p.data(), p.size(), kWhisper, kAddon));        // not a leader: the AI gets it
    CHECK(inbox.Offer(42, p.data(), p.size(), kWhisper, kAddon));
    std::vector<LfgWhisper> got = inbox.Drain();
    CHECK(got.size() == 1 && got[0].leader == 42 && got[0].sender == 77 && got[0].text == "inv");
    CHECK(inbox.Drain().empty());
    std::vector<uint8_t> addon = Packet(kWhisper, kAddon, 77, "x");
    CHECK(!inbox.Offer(42, addon.data(), addon.size(), kWhisper, kAddon));
    std::vector<uint8_t> chan = Packet(kChannel, kUniversal, 77, "inv");
    CHECK(!inbox.Offer(42, chan.data(), chan.size(), kWhisper, kAddon));
    std::vector<uint8_t> self = Packet(kWhisper, kUniversal, 42, "inv");
    CHECK(!inbox.Offer(42, self.data(), self.size(), kWhisper, kAddon));
    std::vector<uint8_t> longText = Packet(kWhisper, kUniversal, 77, std::string(400, 'a'));
    CHECK(inbox.Offer(42, longText.data(), longText.size(), kWhisper, kAddon));
    CHECK(inbox.Drain()[0].text.size() == BotLfgWhisperInbox::kMaxTextBytes);
    inbox.PublishLeaders({});
    CHECK(!inbox.Offer(42, p.data(), p.size(), kWhisper, kAddon));
}

static void TestCoalesce()
{
    BotLfgWhisperInbox inbox;
    inbox.PublishLeaders({42, 43});
    std::vector<uint8_t> first = Packet(kWhisper, kUniversal, 77, "hi");
    std::vector<uint8_t> second = Packet(kWhisper, kUniversal, 77, "inv");
    CHECK(inbox.Offer(42, first.data(), first.size(), kWhisper, kAddon));
    CHECK(inbox.Offer(42, second.data(), second.size(), kWhisper, kAddon));
    CHECK(inbox.Offer(43, first.data(), first.size(), kWhisper, kAddon));
    std::vector<LfgWhisper> got = inbox.Drain();
    CHECK(got.size() == 2);
    CHECK(got[0].leader == 42 && got[0].sender == 77 && got[0].text == "inv");
    CHECK(got[1].leader == 43 && got[1].sender == 77 && got[1].text == "hi");
}

static void TestSenderCap()
{
    BotLfgWhisperInbox inbox;
    inbox.PublishLeaders({42, 43, 44, 45, 46});
    uint64_t leaders[] = {42, 43, 44, 45};
    for (uint64_t leader : leaders)
    {
        std::vector<uint8_t> p = Packet(kWhisper, kUniversal, 77, "inv");
        CHECK(inbox.Offer(leader, p.data(), p.size(), kWhisper, kAddon));
    }
    std::vector<uint8_t> fifth = Packet(kWhisper, kUniversal, 77, "inv");
    CHECK(!inbox.Offer(46, fifth.data(), fifth.size(), kWhisper, kAddon));
    CHECK(inbox.Drain().size() == BotLfgWhisperInbox::kMaxQueuedPerSender);
    std::vector<uint8_t> other = Packet(kWhisper, kUniversal, 78, "inv");
    CHECK(inbox.Offer(46, other.data(), other.size(), kWhisper, kAddon));
    CHECK(inbox.Drain().size() == 1);
}

static void TestCap()
{
    BotLfgWhisperInbox inbox;
    inbox.PublishLeaders({42});
    for (size_t i = 0; i < BotLfgWhisperInbox::kMaxQueued; ++i)
    {
        std::vector<uint8_t> q = Packet(kWhisper, kUniversal, 1000 + i, "inv");
        CHECK(inbox.Offer(42, q.data(), q.size(), kWhisper, kAddon));
    }
    std::vector<uint8_t> full = Packet(kWhisper, kUniversal, 78, "inv");
    CHECK(inbox.Offer(42, full.data(), full.size(), kWhisper, kAddon));
    for (size_t i = 0; i < BotLfgWhisperInbox::kMaxQueued; ++i)
    {
        std::vector<uint8_t> q = Packet(kWhisper, kUniversal, 1000 + i, "inv");
        CHECK(inbox.Offer(42, q.data(), q.size(), kWhisper, kAddon));
    }
    CHECK(inbox.Drain().size() == BotLfgWhisperInbox::kMaxQueued);
    CHECK(inbox.TakeDropped() == 1 && inbox.TakeDropped() == 0);
}

static void TestFloodLeavesRoom()
{
    BotLfgWhisperInbox inbox;
    inbox.PublishLeaders({42, 43});
    for (int i = 0; i < 200; ++i)
    {
        std::vector<uint8_t> p = Packet(kWhisper, kUniversal, 77, "inv " + std::to_string(i));
        CHECK(inbox.Offer(42, p.data(), p.size(), kWhisper, kAddon));
    }
    std::vector<uint8_t> other42 = Packet(kWhisper, kUniversal, 78, "inv");
    std::vector<uint8_t> other43 = Packet(kWhisper, kUniversal, 78, "inv");
    CHECK(inbox.Offer(42, other42.data(), other42.size(), kWhisper, kAddon));
    CHECK(inbox.Offer(43, other43.data(), other43.size(), kWhisper, kAddon));
    std::vector<LfgWhisper> got = inbox.Drain();
    CHECK(got.size() == 3);
    CHECK(got[0].leader == 42 && got[0].sender == 77 && got[0].text == "inv 199");
    CHECK(got[1].leader == 42 && got[1].sender == 78 && got[1].text == "inv");
    CHECK(got[2].leader == 43 && got[2].sender == 78 && got[2].text == "inv");
}

static void TestThreads()
{
    BotLfgWhisperInbox inbox;
    inbox.PublishLeaders({42});
    std::atomic<bool> go{false};
    auto produce = [&](uint64_t base) {
        while (!go) {}
        for (int i = 0; i < 1000; ++i)
        {
            std::vector<uint8_t> p = Packet(kWhisper, kUniversal, base + uint64_t(i), "inv");
            inbox.Offer(42, p.data(), p.size(), kWhisper, kAddon);
        }
    };
    std::thread a(produce, 1000), b(produce, 1000000);
    go = true;
    size_t drained = 0;
    for (int i = 0; i < 2000; ++i) drained += inbox.Drain().size();
    a.join(); b.join();
    drained += inbox.Drain().size();
    CHECK(drained + inbox.TakeDropped() == 2000);
}

static void TestWithdrawPurges()
{
    BotLfgWhisperInbox inbox;
    inbox.PublishLeaders({42, 43});
    std::vector<uint8_t> p = Packet(kWhisper, kUniversal, 77, "inv");
    CHECK(inbox.Offer(42, p.data(), p.size(), kWhisper, kAddon));
    CHECK(inbox.Offer(43, p.data(), p.size(), kWhisper, kAddon));
    inbox.PublishLeaders({43});
    std::vector<LfgWhisper> got = inbox.Drain();
    CHECK(got.size() == 1 && got[0].leader == 43 && got[0].text == "inv");
    CHECK(!inbox.Offer(42, p.data(), p.size(), kWhisper, kAddon));
    CHECK(inbox.Offer(43, p.data(), p.size(), kWhisper, kAddon));
    CHECK(inbox.Drain().size() == 1);
}

// Codex r3: the withdrawal is atomic. Once WithdrawLeader returns, no whisper for that leader is accepted
// (it goes to the bot's AI), and nothing queued for him before is left; other leaders are untouched.
static void TestWithdrawLeader()
{
    BotLfgWhisperInbox inbox;
    inbox.PublishLeaders({42, 43});
    std::vector<uint8_t> p = Packet(kWhisper, kUniversal, 77, "inv");
    CHECK(inbox.Offer(42, p.data(), p.size(), kWhisper, kAddon));
    CHECK(inbox.Offer(43, p.data(), p.size(), kWhisper, kAddon));
    inbox.WithdrawLeader(42);
    CHECK(!inbox.IsLeader(42) && inbox.IsLeader(43));
    CHECK(!inbox.Offer(42, p.data(), p.size(), kWhisper, kAddon));
    std::vector<LfgWhisper> got = inbox.Drain();
    CHECK(got.size() == 1 && got[0].leader == 43);
    inbox.WithdrawLeader(43);
    CHECK(!inbox.Offer(43, p.data(), p.size(), kWhisper, kAddon));
}

static void TestWithdrawLeaderUnderLoad()
{
    BotLfgWhisperInbox inbox;
    inbox.PublishLeaders({42});
    std::atomic<bool> go{false}, withdrawn{false};
    std::atomic<int> acceptedAfter{0};
    auto produce = [&](uint64_t base) {
        while (!go) {}
        for (int i = 0; i < 20000; ++i)
        {
            bool after = withdrawn.load();
            std::vector<uint8_t> p = Packet(kWhisper, kUniversal, base + uint64_t(i % 3), "inv");
            if (inbox.Offer(42, p.data(), p.size(), kWhisper, kAddon) && after)
                ++acceptedAfter;
        }
    };
    std::thread a(produce, 1000), b(produce, 2000);
    go = true;
    for (volatile int spin = 0; spin < 200000; ++spin) {}
    inbox.WithdrawLeader(42);
    withdrawn = true;
    a.join(); b.join();
    CHECK(acceptedAfter == 0);
    for (LfgWhisper const& w : inbox.Drain())
        CHECK(w.leader != 42);
}

int main()
{
    TestParse();
    TestOffer();
    TestCoreWhisperIsInviteRequest();
    TestCoalesce();
    TestSenderCap();
    TestCap();
    TestFloodLeavesRoom();
    TestThreads();
    TestWithdrawPurges();
    TestWithdrawLeader();
    TestWithdrawLeaderUnderLoad();
    std::printf("bot lfg whisper inbox: %d checks passed\n", checks);
    return 0;
}

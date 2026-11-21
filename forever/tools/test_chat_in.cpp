// S1-R2 (chat commands in): the routing and ownership decision and the inbox, core-free
// (forever/src/host/chat/TbChatInPure.h).
//   g++ -std=c++17 -Wall -Wextra -I. forever/tools/test_chat_in.cpp -o /tmp/t && timeout 30 /tmp/t   (the test also arms its own 20 s alarm)
//
// The first case is the one nothing answered before this round: a party-leader line from the bot's master reaches the bot as a PARTY
// command (the core turns a leader's /p into CHAT_MSG_PARTY_LEADER, Handlers/ChatHandler.cpp). Then the refusals, the inbox, and the
// hostile text (long lines, many links, unicode) the hook must get through quickly.
#include "forever/src/host/chat/TbChatInPure.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#if defined(__unix__) || defined(__APPLE__)
#include <csignal>
#include <unistd.h>
#endif

using namespace TortoiseBots::chatin;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)

// A line from a real player who may command this bot, by whisper unless the caller says otherwise.
static ChatInFacts FromMaster(unsigned kind, std::string const& text)
{
    ChatInFacts f;
    f.kind = kind;
    f.lang = 0;                 // LANG_UNIVERSAL
    f.senderReal = true;
    f.authorized = true;
    f.engine = true;
    f.text = text;
    return f;
}

// The run's own timer: a decision or an inbox that never ends fails here instead of hanging the runner.
static void OnTimeout(int)
{
    static char const msg[] = "FAIL: timed out (a chat-in rule did not finish)\n";
    if (write(2, msg, sizeof(msg) - 1) < 0) {}
    _exit(124);
}

int main()
{
#if defined(__unix__) || defined(__APPLE__)
    std::signal(SIGALRM, OnTimeout);
    alarm(20);
#endif
    // 1. The failing case first: a party-leader line (0x31) from the master is accepted, and the engine sees a PARTY line (its
    //    command path reads CHAT_MSG_PARTY, PlayerbotAI::HandleCommand).
    {
        Verdict const v = Judge(FromMaster(KindPartyLeader, "follow"));
        CHECK(v.accepted());
        CHECK(v.kind == KindParty);
        CHECK(std::string(CodeName(v.code)) == "accepted");
    }

    // 2. Ownership: a stranger's whisper is refused; a group member's and a commander's (outside the group) are accepted.
    {
        ChatInFacts stranger = FromMaster(KindWhisper, "stay");
        stranger.authorized = false;
        CHECK(Judge(stranger).code == Code::NotAuthorized);
        CHECK(std::string(CodeName(Judge(stranger).code)) == "not-authorized");

        ChatInFacts member = FromMaster(KindParty, "stay");
        CHECK(Judge(member).accepted());

        ChatInFacts commander = FromMaster(KindWhisper, "stay");
        CHECK(Judge(commander).accepted());
        CHECK(Judge(commander).kind == KindWhisper);
    }

    // 3. Sender: a bot's own whisper or party reply re-enters the hook and is refused (not real); the bot itself is `self`.
    {
        ChatInFacts bot = FromMaster(KindWhisper, "follow");
        bot.senderReal = false;
        CHECK(Judge(bot).code == Code::NotReal);
        CHECK(std::string(CodeName(Judge(bot).code)) == "not-real");

        ChatInFacts self = FromMaster(KindParty, "follow");
        self.senderIsSelf = true;
        CHECK(Judge(self).code == Code::Self);
    }

    // 4. Language and kind: addon traffic never commands a bot; raid, say, yell and guild lines are not routed (D2).
    {
        ChatInFacts addon = FromMaster(KindWhisper, "follow");
        addon.lang = LangAddon;
        CHECK(Judge(addon).code == Code::Addon);
        addon.lang = LangAddonLogged;
        CHECK(Judge(addon).code == Code::Addon);

        ChatInFacts raid = FromMaster(0x03, "follow");          // CHAT_MSG_RAID
        CHECK(Judge(raid).code == Code::Kind);
        ChatInFacts say = FromMaster(0x01, "follow");           // CHAT_MSG_SAY
        CHECK(Judge(say).code == Code::Kind);
        ChatInFacts guild = FromMaster(0x04, "follow");         // CHAT_MSG_GUILD
        CHECK(Judge(guild).code == Code::Kind);
        CHECK(std::string(CodeName(Judge(raid).code)) == "kind");
    }

    // 5. Length: 1 to 255 bytes (the legacy path's limit); empty and 256 bytes are refused.
    {
        CHECK(Judge(FromMaster(KindWhisper, "x")).accepted());
        CHECK(Judge(FromMaster(KindWhisper, std::string(255, 'x'))).accepted());
        CHECK(Judge(FromMaster(KindWhisper, std::string(256, 'x'))).code == Code::Length);
        CHECK(Judge(FromMaster(KindWhisper, std::string())).code == Code::Length);
    }

    // 6. The receiver: no engine brain, no delivery (the refusal `.tb hear` reports on a build without the engine).
    {
        ChatInFacts noEngine = FromMaster(KindWhisper, "follow");
        noEngine.engine = false;
        CHECK(Judge(noEngine).code == Code::NoEngine);
        CHECK(std::string(CodeName(Judge(noEngine).code)) == "no-engine");
    }

    // 7. `.tb hear` makes a line up: the sender has no session, so exactly `not-real` is skipped and every other rule still holds.
    {
        ChatInFacts injected = FromMaster(KindWhisper, "follow");
        injected.senderReal = false;
        injected.injected = true;
        CHECK(Judge(injected).accepted());
        injected.authorized = false;
        CHECK(Judge(injected).code == Code::NotAuthorized);
        injected.authorized = true;
        injected.lang = LangAddon;
        CHECK(Judge(injected).code == Code::Addon);
        injected.lang = 0;
        injected.text = std::string(256, 'x');
        CHECK(Judge(injected).code == Code::Length);
        injected.text = "follow";
        injected.engine = false;
        CHECK(Judge(injected).code == Code::NoEngine);
    }

    // 8. The inbox: at most 16 lines, in arrival order; the 17th drops the OLDEST and counts it.
    {
        Inbox box;
        CHECK(box.Size() == 0);
        Envelope out;
        CHECK(!box.Pop(out));
        for (unsigned i = 1; i <= 16; ++i)
        {
            Envelope line;
            line.text = "line " + std::to_string(i);
            CHECK(!box.Push(line));
        }
        CHECK(box.Size() == 16);
        CHECK(box.Dropped() == 0);
        Envelope seventeenth;
        seventeenth.text = "line 17";
        CHECK(box.Push(seventeenth));
        CHECK(box.Size() == 16);
        CHECK(box.Dropped() == 1);
        CHECK(box.Pop(out));
        CHECK(out.text == "line 2");           // line 1 was the oldest, and it is gone
        CHECK(box.Size() == 15);
        for (unsigned i = 18; i <= 40; ++i)
        {
            Envelope line;
            line.text = "line " + std::to_string(i);
            box.Push(line);
        }
        CHECK(box.Dropped() == 23);            // 23 pushes into a box holding 15: 22 drop, plus the one dropped at the 17th push
        std::string last;
        while (box.Pop(out))
            last = out.text;
        CHECK(last == "line 40");
        CHECK(box.Size() == 0);
    }

    // 9. The counters `.tb state` prints, and the last word it prints.
    {
        Inbox box;
        CHECK(!box.HasLast());
        box.Note(Code::Accepted);
        box.Note(Code::Accepted);
        box.Note(Code::NotAuthorized);
        CHECK(box.Accepted() == 2);
        CHECK(box.Refused() == 1);
        CHECK(box.HasLast());
        CHECK(box.Last() == Code::NotAuthorized);
    }

    // 10. Hostile text must be decided in constant work: long lines, many links, unicode. Nothing here parses the text; the length
    //     rule answers on the byte count, and a 511-byte line of 200 item links is refused at once instead of being scanned.
    {
        std::string links;
        while (links.size() < 500)
            links += "|Hitem:19019:0:0:0|h[T]|h|r";
        CHECK(links.size() >= 500);
        CHECK(Judge(FromMaster(KindWhisper, links)).code == Code::Length);
        std::string manyLinks;
        for (int i = 0; i < 200 && manyLinks.size() < 240; ++i)
            manyLinks += "|cff|h";
        CHECK(Judge(FromMaster(KindWhisper, manyLinks)).accepted() == (manyLinks.size() <= MaxTextBytes));

        // Two- and three-byte characters count as their bytes: 128 two-byte characters (256 bytes) are too long, 127 are not,
        // and a 200-byte line of multi-byte text is accepted (the engine's own parsers read the bytes).
        std::string cyrillic;
        for (int i = 0; i < 128; ++i)
            cyrillic += "\xD0\x96";              // Ж
        CHECK(cyrillic.size() == 256);
        CHECK(Judge(FromMaster(KindWhisper, cyrillic)).code == Code::Length);
        CHECK(Judge(FromMaster(KindWhisper, cyrillic.substr(0, 254))).accepted());
        std::string mixed = "\xE2\x82\xAC";      // €
        for (int i = 0; i < 60; ++i)
            mixed += "\xC3\xA9";                 // é
        CHECK(mixed.size() == 123);
        CHECK(Judge(FromMaster(KindWhisper, mixed)).accepted());

        // A burst of lines through the ring: 30,000 pushes finish at once (only the last 16 survive).
        Inbox box;
        for (int i = 0; i < 30000; ++i)
        {
            Envelope line;
            line.text = links;
            box.Push(line);
        }
        CHECK(box.Size() == InboxCapacity);
        CHECK(box.Dropped() == 30000 - InboxCapacity);
    }

    // 11. A party line in a raid reaches only the sender's own subgroup, exactly as the core broadcasts it; a whisper has no
    //     subgroup, and a raid line is still refused by kind.
    {
        ChatInFacts otherSubGroup = FromMaster(KindPartyLeader, "stay");
        otherSubGroup.sameSubGroup = false;
        CHECK(Judge(otherSubGroup).code == Code::OtherSubGroup);
        CHECK(std::string(CodeName(Judge(otherSubGroup).code)) == "other-subgroup");

        ChatInFacts ownSubGroup = FromMaster(KindPartyLeader, "stay");
        ownSubGroup.sameSubGroup = true;
        CHECK(Judge(ownSubGroup).accepted());

        ChatInFacts whisper = FromMaster(KindWhisper, "stay");
        whisper.sameSubGroup = false;             // a whisper target has no subgroup: the field is not consulted
        CHECK(Judge(whisper).accepted());

        ChatInFacts raid = FromMaster(0x03, "stay");              // CHAT_MSG_RAID
        raid.sameSubGroup = true;
        CHECK(Judge(raid).code == Code::Kind);

        // A stranger in another subgroup is refused for the stronger reason (not-authorized comes first).
        ChatInFacts stranger = otherSubGroup;
        stranger.authorized = false;
        CHECK(Judge(stranger).code == Code::NotAuthorized);
    }

    // 12. A queued line thrown away on the way to the engine is counted, and the last word says `dropped`: `.tb state` never
    //     reads `accepted` for a command that never ran.
    {
        Inbox box;
        box.Note(Code::Accepted);
        CHECK(box.Accepted() == 1);
        CHECK(box.Dropped() == 0);
        CHECK(box.Last() == Code::Accepted);
        box.Drop();
        CHECK(box.Accepted() == 1);
        CHECK(box.Dropped() == 1);
        CHECK(box.HasLast());
        CHECK(box.Last() == Code::Dropped);
        CHECK(std::string(CodeName(box.Last())) == "dropped");
        CHECK(box.Size() == 0);
    }

    // 13. The ingress between the hook and the world thread: guids and text only, arrival order, at most 64, the 65th drops the
    //     oldest and counts it, and nothing is judged before the drain.
    {
        Ingress ingress;
        CHECK(ingress.Size() == 0);
        Arrival out;
        CHECK(!ingress.Pop(out));
        for (unsigned i = 1; i <= 64; ++i)
        {
            Arrival line;
            line.party = (i % 2) == 1;
            line.senderLow = i;
            line.targetLow = 1000 + i;
            line.kind = KindWhisper;
            line.text = "line " + std::to_string(i);
            CHECK(!ingress.Push(line));
        }
        CHECK(ingress.Size() == IngressCapacity);
        CHECK(ingress.Dropped() == 0);
        Arrival sixtyFifth;
        sixtyFifth.text = "line 65";
        CHECK(ingress.Push(sixtyFifth));
        CHECK(ingress.Dropped() == 1);
        CHECK(ingress.Size() == IngressCapacity);
        CHECK(ingress.Pop(out));
        CHECK(out.text == "line 2");              // line 1 was the oldest, and it is gone
        CHECK(out.senderLow == 2);
        CHECK(out.party == false);
        CHECK(out.targetLow == 1002);
        CHECK(out.kind == KindWhisper);
        std::size_t seen = 1;
        while (ingress.Pop(out))
            ++seen;
        CHECK(seen == IngressCapacity);
        CHECK(ingress.Size() == 0);
    }

    if (failures)
        return 1;
    std::printf("RESULT: PASS (test_chat_in)\n");
    return 0;
}

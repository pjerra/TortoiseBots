"""Bot LFG whisper inbox wiring (part 3, Task 2).

Pins that BotPacketAdapter::CanPacketSend takes whispers to an advertising
bot LFG leader into the world-thread inbox (before the bot chat gate) and
that BotLfgWhisperInbox::Offer stays cheap and thread-safe (type and flag
first, no lock; no game objects off the world thread).
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ADAPTER = (ROOT / "host/BotPacketAdapter.cpp").read_text()
INBOX = (ROOT / "runtime/BotLfgWhisperInbox.h").read_text()


def strip_comments(text):
    return "\n".join(line.split("//", 1)[0].rstrip() for line in text.splitlines())


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


SVC = strip_comments(ADAPTER)
INBOX_BODY = strip_comments(INBOX)


class BotLfgWhisperHookWiringTest(unittest.TestCase):
    def test_hook_takes_whispers_before_the_chat_gate(self):
        fn = body(SVC, "bool BotPacketAdapter::CanPacketSend(", "\n}\n")
        offer = "BotLfgWhisperInbox::Instance().Offer(botGuid.GetRawValue(), packet.contents(), packet.size(), CHAT_MSG_WHISPER, LANG_ADDON)"
        self.assertIn(offer, fn)
        self.assertLess(fn.index("GetBotGuid("), fn.index(offer))
        self.assertLess(fn.index(offer), fn.index("IsBotHandledChatType(packet)"))
        self.assertLess(fn.index(offer), fn.index("if (opcode != SMSG_MESSAGECHAT && OnWorldThread())"))

    def test_adapter_includes_the_inbox(self):
        self.assertIn('#include "../runtime/BotLfgWhisperInbox.h"', ADAPTER)


class BotLfgWhisperInboxWiringTest(unittest.TestCase):
    def test_offer_checks_type_and_flag_before_locking(self):
        fn = body(INBOX_BODY, "bool Offer(", "\n    }")
        self.assertLess(fn.index("data[0] != whisperType"), fn.index("std::lock_guard"))
        self.assertLess(fn.index("m_any.load("), fn.index("std::lock_guard"))

    def test_offer_touches_no_game_objects(self):
        fn = body(INBOX_BODY, "bool Offer(", "\n    }")
        self.assertNotIn("Player", fn)
        self.assertNotIn("ObjectAccessor", fn)
        self.assertNotIn("sObjectMgr", fn)


if __name__ == "__main__":
    unittest.main()

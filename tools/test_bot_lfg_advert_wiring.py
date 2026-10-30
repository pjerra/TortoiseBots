"""Bot LFG chat advertising wiring (part 3, Task 4).

Pins that a waiting bot LFG group posts in its faction's LookingForGroup
channel, that every exit from Waiting goes through StopAdvertising, and
that the leader set is published to the whisper inbox.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
CPP = (ROOT / "runtime/BotLfgService.cpp").read_text()
INBOX = (ROOT / "runtime/BotLfgWhisperInbox.h").read_text()
CMD = (ROOT / "commands/Commander.cpp").read_text()
TEMPLATE = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
GUIDE = (ROOT / "docs/play/dungeon-run-leader.md").read_text()
CHANGELOG = (ROOT / "CHANGELOG.md").read_text()


def strip_comments(text):
    return "\n".join(line.split("//", 1)[0].rstrip() for line in text.splitlines())


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


SVC = strip_comments(CPP)
INBOX_BODY = strip_comments(INBOX)


class BotLfgChatWiringTest(unittest.TestCase):
    def test_advertising_only_while_waiting(self):
        fn = body(SVC, "bool BotLfgService::Advertising(", "\n}\n")
        self.assertIn("BotLfgLifecycle::State::Waiting", fn)
        self.assertIn("sPlayerbotAIConfig.botLfgChat", fn)

    def test_advertise_groups_pins(self):
        fn = body(SVC, "void BotLfgService::AdvertiseGroups()", "\n}\n")
        for snippet in ("BotLfgAdvert::PostDue(", "m_channelPostMs[", "RealPlayerOfTeamOnline(",
                        "PostInChannel("):
            self.assertIn(snippet, fn)
        self.assertLess(fn.index("RealPlayerOfTeamOnline("), fn.index("PostInChannel("))

    def test_post_in_channel_pins(self):
        fn = body(SVC, "void BotLfgService::PostInChannel(", "\n}\n")
        for snippet in ("BotLfgAdvert::PostText(", "BotLfgAdvert::DungeonName(",
                        "GetOrCreateChannel(BotLfgAdvert::kChannel)", "AsyncSay(", "++group.posts",
                        "group.lastPostMs = m_nowMs", "m_channelPosted["):
            self.assertIn(snippet, fn)

    def test_transition_and_end_stop_advertising(self):
        transition = body(SVC, "void BotLfgService::Transition(", "\n}\n")
        self.assertIn("StopAdvertising(group)", transition)
        self.assertLess(transition.index("StopAdvertising(group)"), transition.index("group.state = to"))
        self.assertLess(transition.index("group.state = to"), transition.index("PublishLeaders()"))
        end = body(SVC, "void BotLfgService::End(", "\n}\n")
        self.assertIn("StopAdvertising(group)", end)
        self.assertLess(end.index("StopAdvertising(group)"), end.index("RemoveMember("))
        self.assertLess(end.index("State::Ended"), end.index("PublishLeaders()"))

    def test_stop_advertising_publishes_nothing(self):
        fn = body(SVC, "void BotLfgService::StopAdvertising(", "\n}\n")
        self.assertNotIn("PublishLeaders()", fn)
        # Codex r3: the leader leaves the inbox before anything else of the withdrawal runs.
        first = fn.split("{", 1)[1].strip().splitlines()[0].strip()
        self.assertEqual(first, "BotLfgWhisperInbox::Instance().WithdrawLeader(group.leader.GetRawValue());")

    def test_publish_leaders_purges_withdrawn_queue(self):
        self.assertIn("m_queue.erase(std::remove_if(m_queue.begin(), m_queue.end(),", INBOX)
        fn = body(INBOX_BODY, "void PublishLeaders(", "\n    }")
        self.assertLess(fn.index("m_queue.erase("), fn.index("m_any.store("))
        self.assertIn("m_leaders.find(queued.leader)", fn)

    def test_update_advertises_after_tick_groups(self):
        fn = body(SVC, "void BotLfgService::Update(uint32 diff)", "\n}\n")
        self.assertLess(fn.index("TickGroups("), fn.index("m_groups.erase("))
        self.assertLess(fn.index("m_groups.erase("), fn.index("AdvertiseGroups()"))
        self.assertLess(fn.index("AdvertiseGroups()"), fn.index("if (!enabled)"))
        self.assertIn("PublishLeaders()", fn)

    def test_shutdown_clears_leaders(self):
        fn = body(SVC, "void BotLfgService::Shutdown()", "\n}\n")
        self.assertIn("BotLfgWhisperInbox::Instance().PublishLeaders({})", fn)

    def test_commander_list_shows_posts_and_invite(self):
        self.assertIn("posts:%u invite:%s", CMD)


class BotLfgWhisperWiringTest(unittest.TestCase):
    def test_update_drains_whispers_first(self):
        fn = body(SVC, "void BotLfgService::Update(uint32 diff)", "\n}\n")
        self.assertIn("HandleWhispers();", fn)
        self.assertLess(fn.index("HandleWhispers();"), fn.index("if (m_elapsedMs < BotLfgLifecycle::kTickMs)"))
        self.assertLess(fn.index("if (!m_initialized)"), fn.index("HandleWhispers();"))

    def test_handle_whispers_pins(self):
        fn = body(SVC, "void BotLfgService::HandleWhispers()", "\n}\n")
        self.assertIn("BotLfgWhisperInbox::Instance().Drain()", fn)
        self.assertIn("HandleWhisper(", fn)

    def test_handle_whisper_pins(self):
        fn = body(SVC, "void BotLfgService::HandleWhisper(", "\n}\n")
        ordered = ("HasNetworkTransport()", "BotManager::Instance().IsBot(", "BotLfgAdvert::IsInviteRequest(",
                   "BotLfgAdvert::PlayerFitsRole(", "BotLfgAdvert::DecideWhisper(", "HandleGroupInviteOpcode(",
                   "GetGroupInvite()")
        for snippet in ordered:
            self.assertIn(snippet, fn)
        for first, second in zip(ordered, ordered[1:]):
            self.assertLess(fn.index(first), fn.index(second))
        for snippet in ("GetPlayerSpecTabs(", "GetBotRoleMask(", "IsHardcore()", "BotLfgAdvert::kReplyGapMs",
                        "BotLfgAdvert::kHintGapMs", "group->repliedAtMs[", "Answer::InviteFailed",
                        "BotLfgAdvert::kMaxInvitesPerGroup", "group->invitesSent[", "senderRefusedResend"):
            self.assertIn(snippet, fn)

    def test_reply_pins(self):
        fn = body(SVC, "void BotLfgService::Reply(", "\n}\n")
        self.assertIn("ChatHandler::BuildChatPacket(", fn)
        self.assertIn("CHAT_MSG_WHISPER", fn)

    def test_tick_group_holds_invite(self):
        fn = body(SVC, "void BotLfgService::TickGroup(", "\n}\n")
        self.assertIn("facts.invitePending = InviteHeld(group);", fn)
        self.assertLess(fn.index("facts.invitePending = InviteHeld(group);"),
                        fn.index("BotLfgLifecycle::DecideStep(facts)"))
        self.assertIn("RemoveInvite(", fn)

    def test_invite_held_pins(self):
        fn = body(SVC, "bool BotLfgService::InviteHeld(", "\n}\n")
        self.assertIn("BotLfgAdvert::InvitePending(", fn)

class BotLfgChatDocsTest(unittest.TestCase):
    def test_part3_docs(self):
        block = TEMPLATE[TEMPLATE.index("# Bot LFG dungeon groups:"):TEMPLATE.index("AiPlayerbot.BotLfgChat = 1")]
        self.assertIn("AiPlayerbot.BotLfgChat", block)
        self.assertIn("## Joining a bot LFG group", GUIDE)
        unreleased = CHANGELOG[CHANGELOG.index("## Unreleased"):CHANGELOG.index("## v1.8.0")]
        self.assertIn("AiPlayerbot.BotLfgChat", unreleased)

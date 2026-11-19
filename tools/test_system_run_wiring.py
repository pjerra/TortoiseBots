"""The system binding glue: a server-owned binding of a bot-only party
(runtime/SystemRunPolicy.h), whose replies go to the server log.

The policy decides; these checks pin that commands/Commander.cpp wires it:
the reply path (ServerLogChatHandler/CommanderReplyHandler), the world-bot
gate (IsWorldBot), the commanded set, the run line, the tick's joined-player
pause, the bind entry point, telemetry, the chat refusal name and the trace
anchor.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
COMMANDER = (ROOT / "commands" / "Commander.cpp").read_text()
HANDLERS = (ROOT / "commands" / "BotCommandHandlers.h").read_text()


def strip_comments(text):
    return "\n".join(line.split("//", 1)[0].rstrip() for line in text.splitlines())


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


COMMANDS = strip_comments(COMMANDER)


class SystemReplyWiringTest(unittest.TestCase):
    def test_log_handler(self):
        self.assertIn("class ServerLogChatHandler final : public ChatHandler", COMMANDS)
        self.assertIn("void SendSysMessage(char const* str) override", COMMANDS)

    def test_run_line_goes_to_the_log(self):
        run = body(COMMANDS, "void SendRunLine(", "\n}\n")
        self.assertIn("IsSystemBinding(", run)
        self.assertIn("TB_LOG_BASIC(", run)
        self.assertLess(run.index("IsSystemBinding("),
                        run.index("ChatHandler(commander).PSendSysMessage"))

    def test_issue_run_order_uses_the_log_handler(self):
        order = body(COMMANDS, "bool IssueRunOrder(", "\n}\n")
        self.assertIn("ServerLogChatHandler", order)
        self.assertNotIn("ChatHandler handler(commander);", order)

    def test_reply_sites_use_the_handler(self):
        for start in ("void TaskReplyAck(", "void TaskReplyErr(", "void RitualReply(",
                      "void NoteEnterResult("):
            with self.subTest(start=start):
                site = body(COMMANDS, start, "\n}\n")
                self.assertIn("CommanderReplyHandler(", site)
                self.assertNotIn("ChatHandler handler(commander);", site)

    def test_release_notifies_through_the_handler(self):
        release = body(COMMANDS, "uint32 ReleaseBinding(", "\n}\n")
        self.assertIn("CommanderReplyHandler(commanderRaw, removed.system)", release)

    def test_reply_handler_ignores_bot_sessions(self):
        handler = body(COMMANDS, "std::unique_ptr<ChatHandler> CommanderReplyHandler(", "\n}\n")
        self.assertIn("BotManager::Instance().IsBot(", handler)
        self.assertIn("if (system)\n        return std::unique_ptr<ChatHandler>(new ServerLogChatHandler(commander));",
                      handler)


class SystemBindingWiringTest(unittest.TestCase):
    def test_commanded_bots_are_world_bots(self):
        fill = body(COMMANDS, "void FillCommandedBots(", "\n}\n")
        self.assertIn("IsWorldBot(", fill)
        self.assertIn("if (system)\n            return IsWorldBot(bot);", fill)

    def test_world_bot_gate(self):
        gate = body(COMMANDS, "bool IsWorldBot(Player* bot)\n{", "\n}\n")
        self.assertIn("ownerAccountId", gate)
        self.assertIn("HireLifecycle::Instance().IsHired(", gate)
        self.assertIn("ClaimLifecycle::Instance().IsClaimed(", gate)
        self.assertIn("AltBotService::Instance().IsAutonomous(", gate)
        self.assertIn("HasNetworkTransportCompat(master->GetSession())", gate)
    def test_owner_checks_follow_the_binding(self):
        self.assertEqual(COMMANDS.count("CanControlBot(order.commander"), 1)

    def test_tick_pauses_for_a_joined_player(self):
        tick = body(COMMANDS, "void TickDungeonRun(uint64 commanderRaw, uint32 dt)", "\n}\n")
        self.assertLess(tick.index("TortoiseBots::SystemRun::DecideJoinStep("),
                        tick.index("if (slot.run.state != TortoiseBots::DungeonRun::State::Running || !commander)"))
        self.assertIn("TortoiseBots::SystemRun::kPlayerJoinedNote", tick)
        self.assertIn("slot.systemPausedForPlayer", tick)
        flag = "slot.systemPausedForPlayer = TortoiseBots::SystemRun::JoinFlagAfterScan("
        self.assertIn(flag, tick)
        self.assertLess(tick.index(flag), tick.index("TortoiseBots::SystemRun::DecideJoinStep("))
        scan = "bool realPlayer = false;\n        if (Player* systemLead = FindCharacter(binding->leadGuid))"
        self.assertIn(scan, tick)
        self.assertIn("if (!BotManager::Instance().IsBot(slotMember.guid))", tick)

    def test_bind_entry_point(self):
        bind = body(COMMANDS, "uint64 BindSystemParty(", "\n}\n")
        self.assertLess(bind.index("TortoiseBots::SystemRun::CheckBind(facts)"),
                        bind.index("CommanderBindings().Bind(binding)"))
        self.assertIn("binding.system = true;", bind)
        self.assertIn("binding.commanderGuid = binding.leadGuid;", bind)
        self.assertIn("BindSystemParty(Player* lead, std::vector<Player*> const& joiners, std::string& code, std::string& message)", HANDLERS)

    def test_bind_restores_on_failure(self):
        bind = body(COMMANDS, "uint64 BindSystemParty(", "\n}\n")
        self.assertIn("TortoiseBots::SystemRun::RestorePlan(prior)", bind)
        self.assertIn("priorLeader", bind)
        self.assertEqual(bind.count("restoreMoved();"), 3)
        self.assertEqual(bind.count("restoreLead();"), 2)
        self.assertLess(bind.index("recordPrior(joiner)"), bind.index("JoinLeadGroup(lead, joiner)"))
        self.assertIn("auto restoreLead = [&]", bind)
        self.assertLess(bind.index("auto restoreLead = [&]"), bind.index("restoreLead();"))
        self.assertIn("restoreLead();\n        restoreMoved();", bind)
        self.assertNotIn("priorLeadGroup->", bind)
        self.assertNotIn("Group* priorLeadGroup", bind)
        self.assertIn("now->GetId() == priorLeadGroupId", bind)

    def test_trace_follows_the_anchor_binding(self):
        trace = body(COMMANDS, "static bool TraceContext(", "\n}\n")
        self.assertIn("->system", trace)


class SystemSurfaceWiringTest(unittest.TestCase):
    def test_telemetry_names_the_system_commander(self):
        self.assertIn('ref.commander = "system"', COMMANDS)

    def test_chat_refusal_names_the_system_run(self):
        self.assertIn('"the server\'s dungeon run"', COMMANDS)


class SystemCommandWiringTest(unittest.TestCase):
    def test_run_dispatches_system(self):
        run = body(COMMANDS, "bool HandleDungeonRun(", "\n}\n")
        dispatch = 'if (verb == "system")\n        return HandleSystemRun(handler, rawArg, intent);'
        self.assertIn(dispatch, run)
        self.assertLess(run.index(dispatch), run.index('if (verb != "start" && verb != "pause"'))

    def test_system_run_guards(self):
        cmd = body(COMMANDS, "static bool HandleSystemRun(", "\n}\n")
        self.assertLess(cmd.index("IsBotAdministrator("),
                        cmd.index("RemoteCommander::BindingKindEnabled(RemoteCommander::BindingKind::System,"))
        self.assertLess(cmd.index("RemoteCommander::BindingKindEnabled(RemoteCommander::BindingKind::System,"),
                        cmd.index("TortoiseBots::SystemRun::ParseArgs("))
        self.assertIn("GetSelectionGuid()", cmd)
        self.assertIn("ServerLogChatHandler", cmd)
        self.assertIn("BindSystemParty(", cmd)
        self.assertIn("RemoteCommander::IsKnownOrder(", cmd)

    def test_system_unbind_releases_quietly(self):
        cmd = body(COMMANDS, "static bool HandleSystemRun(", "\n}\n")
        self.assertIn("ReleaseBinding(key, RemoteCommander::ReleaseReason::Unbound, false)", cmd)

    def test_system_cmd_needs_no_running_run(self):
        cmd = body(COMMANDS, "static bool HandleSystemRun(", "\n}\n")
        self.assertLess(cmd.index("State::Running"), cmd.index("IssueRunOrder("))

    def test_system_orders_share_the_player_gate(self):
        cmd = body(COMMANDS, "static bool HandleSystemRun(", "\n}\n")
        self.assertIn('RefuseSystemOrder(lead, key, "cmd", parsed.rest, gateCode, gateMessage)', cmd)
        self.assertIn('RefuseSystemOrder(lead, key, nullptr, parsed.rest, gateCode, gateMessage)', cmd)
        self.assertLess(cmd.index('"cmd", parsed.rest'), cmd.index("IssueRunOrder("))
        self.assertLess(cmd.index("ServerLogChatHandler echo(lead, handler);"),
                        cmd.index("nullptr, parsed.rest") + 400)
        helper = body(COMMANDS, "bool RefuseSystemOrder(", "\n}\n")
        self.assertIn("TortoiseBots::SystemRun::DecideSystemOrder(text, realPlayer, pausedForPlayer)", helper)
        self.assertIn("TortoiseBots::SystemRun::SystemOrderCode(", helper)

    def test_player_gate_allows_the_way_out(self):
        helper = body(COMMANDS, "bool RefuseSystemOrder(", "\n}\n")
        self.assertIn('std::string const text = verb ? verb : order;', helper)
        self.assertIn('TortoiseBots::SystemRun::DecideSystemOrder(text, realPlayer, pausedForPlayer)', helper)
        policy = (pathlib.Path(__file__).resolve().parents[1] / "runtime" / "SystemRunPolicy.h").read_text()
        allow = 'if (verb == "stop" || verb == "status" || verb == "pause" || verb == "unbind")'
        self.assertIn(allow, policy)
        self.assertLess(policy.index(allow), policy.index("return SystemOrder::RefusePlayerInGroup;"))
        cmd = body(COMMANDS, "static bool HandleSystemRun(", "\n}\n")
        self.assertLess(cmd.index('"cmd", parsed.rest'), cmd.index("RemoteCommander::IsKnownOrder("))

if __name__ == "__main__":
    unittest.main()

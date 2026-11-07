"""The Lua API (host/LuaBindings.cpp) is bound through the core's registry, world state only, and writes only
through the `.bot` command queue.

Pins, so a refactor cannot loosen them: the registrar is added through ElunaCustomRegistry behind ENABLE_ELUNA
(a core without Eluna still builds); every TB.* function is METHOD_REG_WORLD (a map state is refused by Eluna's
own wrong-state closure); no binding raises a Lua error (luaL_error longjmps over C++ frames) or pushes a game
object; TB.Command reaches TB's handlers only through BotCommandQueue (the requester's rights, a tick later); the
drain routes a Lua reply to LuaBindings and the world tick flushes the replies.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
BINDINGS = (ROOT / "host/LuaBindings.cpp").read_text()
HEADER = (ROOT / "host/LuaBindings.h").read_text()
CHAT = (ROOT / "host/BotChatAdapter.cpp").read_text()
HOST = (ROOT / "host/BotHostAdapter.cpp").read_text()
MODULE = (ROOT / "host/Module.cpp").read_text()
QUEUE = (ROOT / "commands/BotCommandQueue.h").read_text()
CMAKE = (ROOT / "TortoiseBots.cmake").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


def eluna_block():
    """The text between #ifdef ENABLE_ELUNA and its #endif // ENABLE_ELUNA inside the anonymous namespace."""
    i = BINDINGS.index("#ifdef ENABLE_ELUNA\n\n// Where the world state keeps")
    return BINDINGS[i:BINDINGS.index("#endif // ENABLE_ELUNA", i)]


class LuaApiWiringTest(unittest.TestCase):
    def test_registration_goes_through_the_core_registry_behind_enable_eluna(self):
        block = eluna_block()
        self.assertIn("ElunaCustomRegistry::Add(&RegisterState)", block)
        self.assertIn("ElunaTemplate<>::SetMethods(E, Methods)", block)
        # the includes of the core's Eluna headers sit behind the same guard
        top = BINDINGS[:BINDINGS.index("namespace TortoiseBots")]
        guarded = top[top.index("#ifdef ENABLE_ELUNA"):top.index("#endif")]
        for header in ("ElunaCustomRegistry.h", "LuaEngine.h", "ElunaTemplate.h"):
            self.assertIn(header, guarded)
            self.assertNotIn(f'#include "{header}"', top.replace(guarded, ""))
        # the module hook that keeps the file linked, and the static initializer that makes it early enough
        self.assertIn("LuaBindings::Register();", MODULE)
        self.assertIn("bool const g_registered = (ElunaCustomRegistry::Add(&RegisterState), true);", block)

    def test_every_tb_function_is_world_state_only(self):
        table = body(BINDINGS, "ElunaRegister<> Methods[] =", "};")
        entries = re.findall(r'\{ "(TB\w+)", &\w+, (\w+) \}', table)
        self.assertEqual([n for n, _ in entries], ["TBGetBots", "TBGetBot", "TBGetRun", "TBCommand"])
        for name, state in entries:
            self.assertEqual(state, "METHOD_REG_WORLD", name)
        # the TB table only points at those world-only globals
        tbl = body(BINDINGS, "void RegisterTable(Eluna* E)")
        for short, glob in (("GetBots", "TBGetBots"), ("GetBot", "TBGetBot"), ("GetRun", "TBGetRun"), ("Command", "TBCommand")):
            self.assertIn(f'{{ "{short}", "{glob}" }}', tbl)
        self.assertIn('lua_setglobal(L, "TB")', tbl)

    def test_bindings_never_raise_a_lua_error_or_push_a_game_object(self):
        block = re.sub(r"//[^\n]*", "", eluna_block())  # code only: the comments name what is forbidden
        self.assertNotIn("luaL_error", block)
        self.assertNotIn("luaL_arg", block)
        self.assertNotIn("CHECKVAL", block)      # CHECKVAL raises on a bad argument
        self.assertNotIn("CHECKOBJ", block)
        self.assertNotIn("E->Push(", block)      # Eluna's Push(Object*) hands out userdata
        self.assertNotIn("ElunaTemplate<Player>", block)
        self.assertNotIn("lua_newuserdata", block)
        # every failure is `nil, code`
        fail = body(block, "int Fail(Eluna* E, char const* code)")
        self.assertIn("lua_pushnil(E->L)", fail)
        self.assertIn("return 2;", fail)

    def test_command_reaches_tb_only_through_the_queue(self):
        cmd = body(BINDINGS, "int Command(Eluna* E)")
        self.assertIn("BotCommandQueue::Instance().Enqueue(", cmd)
        self.assertIn("BotCommandReply::Lua", cmd)
        self.assertIn("LuaApi::ParseCommand(", cmd)
        for direct in ("HandleChatCommand", "TryHandleBotCommand", "ExecuteCommand"):
            self.assertNotIn(direct, cmd)
        # the one place a Lua command runs is the world-thread drain, as the requester's own handler
        run = body(BINDINGS, "void ExecuteCommand(Player* requester, PendingBotCommand const& command)")
        self.assertIn("LuaReplyHandler handler(requester, buffer);", run)
        self.assertIn("BotCommands::HandleChatCommand(&handler", run)
        self.assertEqual(BINDINGS.count("HandleChatCommand("), 1)
        # the reply sink is the single virtual the command layer answers through
        self.assertIn("void SendSysMessage(char const* str) override { m_buffer.Append(str); }", BINDINGS)

    def test_requester_is_never_a_bot_and_must_be_in_the_world(self):
        cmd = body(BINDINGS, "int Command(Eluna* E)")
        self.assertIn("LuaApi::RequesterRefusal(BotManager::Instance().IsBot(player->GetObjectGuid()), player->IsInWorld())", cmd)
        run = body(BINDINGS, "void ExecuteCommand(Player* requester, PendingBotCommand const& command)")
        self.assertIn("LuaApi::RequesterRefusal(BotManager::Instance().IsBot(requester->GetObjectGuid())", run)
        # the refusal comes before the handler runs
        self.assertLess(run.index("RequesterRefusal"), run.index("HandleChatCommand"))

    def test_world_state_creation_clears_the_callback_ledger(self):
        reg = body(BINDINGS, "void RegisterState(Eluna* E)")
        self.assertIn("E->GetBoundMapId() == -1", reg)
        self.assertIn("g_ledger.Clear();", reg)

    def test_drain_routes_lua_replies_and_the_tick_flushes_them(self):
        self.assertIn("    Lua,\n", body(QUEUE, "enum class BotCommandReply", "};"))
        self.assertIn("uint64_t luaId = 0;", QUEUE)
        drain = body(CHAT, "uint64_t BotChatAdapter::DrainQueuedCommands()", "\n}\n")
        self.assertIn("if (command.reply == BotCommandReply::Lua)", drain)
        self.assertIn("LuaBindings::ExecuteCommand(requester, command);", drain)
        # the Lua branch sits before the plain ChatHandler a typed command gets
        self.assertLess(drain.index("BotCommandReply::Lua"), drain.index("ChatHandler handler(requester->GetSession());"))
        self.assertIn("LuaBindings::ReportDropped(requester, dropped);", drain)
        update = body(HOST, "void BotHostAdapter::OnUpdate(uint32 diff)", "\n}\n")
        drained = update.index("BotChatAdapter::DrainQueuedCommands()")
        flushed = update.index("LuaBindings::Flush();")
        self.assertLess(drained, flushed)
        # before the AI-disabled early return, so a reply is delivered with the AI off too
        self.assertLess(flushed, update.index("if (!sPlayerbotAIConfig.enabled)\n    {\n        // A run trace"))

    def test_reads_copy_plain_values_and_use_existing_readers(self):
        block = eluna_block()
        self.assertIn("ObservabilityEmitter::Instance().SnapshotBot(bot, snapshot)", block)
        self.assertIn("LuaApi::BotFields(snapshot)", block)
        self.assertIn("BotCommands::ReadRunForLua(", block)
        self.assertIn("BotManager::Instance().IsBot(bot->GetObjectGuid())", block)  # TB.GetBot answers for bots only

    def test_callbacks_live_in_the_lua_registry_and_are_bounded(self):
        block = eluna_block()
        self.assertIn("LUA_REGISTRYINDEX", block)
        self.assertIn("g_ledger.Add(id, WorldTimer::getMSTime())", block)
        self.assertIn('Fail(E, "too_many_callbacks")', block)
        flush = body(BINDINGS, "void Flush()")
        self.assertIn("g_ledger.Expire(", flush)

    def test_the_file_is_built(self):
        self.assertIn('"${TORTOISEBOTS_ROOT}/host/LuaBindings.cpp"', CMAKE)
        self.assertIn("void Flush();", HEADER)


if __name__ == "__main__":
    unittest.main()

-- luacheck config for the ForeverLua test toolkit (the module's Lua is 5.4).
-- Run: luacheck --config forever/lua/.luacheckrc forever/lua/tt/*.lua forever/lua/tests/*.lua
std = "lua54"
max_line_length = false
unused_args = false
-- Engine globals (Cmd/World/Timer/Json) are provided by ForeverLua at runtime; the mock test defines them locally in
-- its own chunk, which is why they are writable here.
globals = { "TT", "Cmd", "World", "Timer", "Json" }

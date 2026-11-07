-- Mock test (plain Lua, no server): the TB API smoke script (`.tbapi`). Run from the toolkit dir:
--   lua tests/mock_tbapi_test.lua
--
-- Pins the command routing, the rank gate, that every check passes against an API that behaves as the module's
-- (nil + code on a bad argument, plain tables), that a reply callback is passed to TB.Command and prints a
-- `reply` line, and that a map state is reported refused.
package.path = "lua_scripts/tt/?.lua;lua_scripts/tt/?.ext;" .. package.path
local handler, mapHandler
_G.RegisterPlayerEvent = function(_, fn) handler = fn end
_G.RegisterServerEvent = function(_, fn) mapHandler = fn end
_G.CreateLuaEvent = function() end
local out = {}
io.write = function(...) for _, s in ipairs({ ... }) do out[#out + 1] = s end end

local callbacks = {}
local bots = { 101, 102 }
local function api()
    return {
        GetBots = function(master)
            if master ~= nil and type(master) ~= "number" then return nil, "bad_guid" end
            return bots
        end,
        GetBot = function(g)
            if type(g) ~= "number" or g < 1 then return nil, "bad_guid" end
            for _, b in ipairs(bots) do
                if b == g then return { guid = g, name = "Dudette", hp = 100, raidMap = false } end
            end
            return nil
        end,
        GetRun = function(g)
            if type(g) ~= "number" or g < 1 then return nil, "bad_guid" end
            return { active = false }
        end,
        Command = function(requester, text, cb)
            if type(requester) ~= "number" or requester < 1 then return nil, "bad_guid" end
            if type(text) ~= "string" then return nil, "bad_text" end
            if text == "" then return nil, "empty" end
            if text:find("\n", 1, true) then return nil, "bad_char" end
            if cb ~= nil and type(cb) ~= "function" then return nil, "bad_callback" end
            if requester == 4294967295 then return nil, "no_such_player" end
            callbacks[#callbacks + 1] = cb
            return #callbacks
        end,
    }
end
_G.TB = api()

require("tt_commands") -- registers its own PLAYER_ON_COMMAND; ours replaces the captured handler below
local tt = handler
require("tt_tbapi")
local ours = handler
assert(ours and ours ~= tt, "tt_tbapi registers a command handler")

local function player(rank)
    local p = { sent = {} }
    p.GetGMRank = function() return rank end
    p.GetGUIDLow = function() return 7 end
    p.SendBroadcastMessage = function(_, s) p.sent[#p.sent + 1] = s end
    return p
end

-- not ours: the core goes on
assert(ours(42, player(3), "tt help") == true, "other commands pass through")
-- rank gate
local low = player(0)
assert(ours(42, low, "tbapi smoke") == false)
assert(low.sent[1]:find("not_allowed", 1, true), "low rank refused")

-- smoke: every check ok
local gm = player(3)
assert(ours(42, gm, "tbapi smoke") == false)
local last = gm.sent[#gm.sent]
assert(last:find('"cmd":"smoke"', 1, true), last)
assert(last:find('"failed":0', 1, true), "all checks pass: " .. last)
assert(last:find('"ok":true', 1, true), last)
assert(#callbacks == 1 and type(callbacks[1]) == "function", "smoke queued one command with a callback")

-- the callback prints a reply line to the caller
callbacks[1](1, 7, { "line one", "line two" }, false)
local replyLine = gm.sent[#gm.sent]
assert(replyLine:find('"what":"reply"', 1, true) and replyLine:find("line two", 1, true), replyLine)

-- cmd passes the caller's guid and the text
gm.sent = {}
assert(ours(42, gm, "tbapi cmd .bot status") == false)
assert(gm.sent[1]:find('"cmd":"cmd"', 1, true) and gm.sent[1]:find('"ok":true', 1, true), gm.sent[1])

-- a failing API makes smoke fail loudly, not silently
_G.TB.GetBots = function() return nil, "boom" end
gm.sent = {}
ours(42, gm, "tbapi smoke")
assert(gm.sent[#gm.sent]:find('"ok":false', 1, true), "a broken API is reported: " .. gm.sent[#gm.sent])

-- no API at all (core without Eluna custom methods)
_G.TB = nil
gm.sent = {}
ours(42, gm, "tbapi bots")
assert(gm.sent[1]:find("no_api", 1, true), gm.sent[1])

-- map state: refused calls are reported once
out = {}
_G.TB = { GetBots = function() error("attempt to call method 'TBGetBots' that does not exist for state: 509") end }
mapHandler()
mapHandler()
local joined = table.concat(out)
local _, count = joined:gsub('"cmd":"mapstate"', "")
assert(count == 1, "one mapstate line, got " .. count)
assert(joined:find('"refused":true', 1, true), joined)
print("mock_tbapi_test ok")

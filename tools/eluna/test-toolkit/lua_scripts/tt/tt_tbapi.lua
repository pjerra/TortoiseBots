--[==[
TortoiseBots Eluna test toolkit: smoke test of the TB Lua API (TEST SERVERS ONLY).

The API (module host/LuaBindings.cpp, docs/developers/lua-api.md) exists only in a core built with Eluna and
it is bound in the WORLD Lua state only: a map state that calls a TB.* function gets Eluna's wrong-state error.

  .tbapi smoke            run every call once; the checks print as `TBAPI {...}` lines (also to the caller)
  .tbapi bots [master]    TB.GetBots(master?)
  .tbapi bot <guid>       TB.GetBot(guid)
  .tbapi run [leader]     TB.GetRun(leader), the caller by default
  .tbapi cmd <text>       TB.Command(caller, text, callback): the reply arrives one tick later as a `reply` line

Needs GM rank >= TT.CFG.min_gm_rank, like `.tt`. A map-state check logs `TBAPI mapstate` once per Lua state
(expected refused=true).
]==]

local TT = require("tt_lib")

local function emit(player, t)
    t.v = 1
    t.ts = os.time()
    local line = "TBAPI " .. TT.json(TT.obj(t))
    if player then
        local ok = pcall(function() player:SendBroadcastMessage(line) end)
        if not ok then TT.log("TBAPI could not reach the caller") end
    end
    TT.writeStdout(line)
end

local function haveApi()
    return type(TB) == "table" and type(TB.GetBots) == "function"
end

local function call(fn, ...)
    local ok, a, b = pcall(fn, ...)
    if not ok then return nil, "error: " .. tostring(a) end
    return a, b
end

local function reply(player, cmd, text)
    return function(id, requester, lines, truncated)
        emit(player, { cmd = cmd, ok = true, what = "reply", id = id, requester = requester, text = text,
            lines = lines, truncated = truncated and true or false })
    end
end

local function smoke(player)
    local me = player and player:GetGUIDLow() or nil
    local checks = {}
    local function check(name, ok, detail)
        checks[#checks + 1] = { name = name, ok = ok and true or false, detail = detail }
    end

    local bots, e1 = call(TB.GetBots)
    check("GetBots", type(bots) == "table", e1 or (bots and #bots))
    local bad, code = call(TB.GetBots, "x")
    check("GetBots(bad master) is nil, bad_guid", bad == nil and code == "bad_guid", code)

    local first = bots and bots[1]
    if first then
        local row = call(TB.GetBot, first)
        check("GetBot(first)", type(row) == "table" and row.guid == first and type(row.name) == "string"
            and type(row.hp) == "number" and type(row.raidMap) == "boolean", row and row.name)
        check("GetBot row holds no userdata", (function()
            for _, v in pairs(row or {}) do
                if type(v) ~= "number" and type(v) ~= "string" and type(v) ~= "boolean" then return false end
            end
            return true
        end)())
    else
        check("GetBot(first)", true, "no bot online, skipped")
    end
    local none, c2 = call(TB.GetBot, 0)
    check("GetBot(0) is nil, bad_guid", none == nil and c2 == "bad_guid", c2)
    local missing = call(TB.GetBot, 4294967295)
    check("GetBot(unknown) is nil", missing == nil)

    if me then
        local run = call(TB.GetRun, me)
        check("GetRun(me)", type(run) == "table" and type(run.active) == "boolean", run and tostring(run.active))
    end

    local id, c3 = call(TB.Command, me or 1, "")
    check("Command(empty) is nil, empty", id == nil and c3 == "empty", c3)
    local id2, c4 = call(TB.Command, me or 1, "x\ny")
    check("Command(newline) is nil, bad_char", id2 == nil and c4 == "bad_char", c4)
    local id3, c5 = call(TB.Command, me or 1, "help", "not a function")
    check("Command(bad callback) is nil, bad_callback", id3 == nil and c5 == "bad_callback", c5)
    local id4, c6 = call(TB.Command, 4294967295, "help")
    check("Command(unknown requester) is nil, no_such_player", id4 == nil and c6 == "no_such_player", c6)
    if me then
        local queued, c7 = call(TB.Command, me, ".bot help", reply(player, "smoke", ".bot help"))
        check("Command(.bot help) queued", type(queued) == "number", c7 or queued)
    end

    local failed = 0
    for _, c in ipairs(checks) do if not c.ok then failed = failed + 1 end end
    emit(player, { cmd = "smoke", ok = failed == 0, failed = failed, checks = checks })
end

local function dispatch(player, args)
    if not haveApi() then
        return emit(player, { cmd = "tbapi", ok = false, err = "no_api",
            msg = "TB is not defined: core without Eluna custom methods, or module without the Lua API" })
    end
    local sub = (args[1] or "smoke"):lower()
    local me = player and player:GetGUIDLow() or nil
    if sub == "smoke" then return smoke(player) end
    if sub == "bots" then
        local list, err = call(TB.GetBots, tonumber(args[2]))
        return emit(player, { cmd = "bots", ok = list ~= nil, err = err, bots = list })
    end
    if sub == "bot" then
        local row, err = call(TB.GetBot, tonumber(args[2]))
        return emit(player, { cmd = "bot", ok = row ~= nil, err = err, bot = row })
    end
    if sub == "run" then
        local row, err = call(TB.GetRun, tonumber(args[2]) or me)
        return emit(player, { cmd = "run", ok = row ~= nil, err = err, run = row })
    end
    if sub == "cmd" then
        local text = table.concat(args, " ", 2)
        local id, err = call(TB.Command, me, text, reply(player, "cmd", text))
        return emit(player, { cmd = "cmd", ok = id ~= nil, err = err, id = id })
    end
    return emit(player, { cmd = "tbapi", ok = false, err = "usage", msg = ".tbapi smoke|bots [master]|bot <guid>|run [leader]|cmd <text>" })
end

local function onCommand(event, player, text)
    local args = {}
    for tok in tostring(text):gmatch("%S+") do args[#args + 1] = tok end
    if (args[1] or ""):lower() ~= "tbapi" then
        return true -- not ours: the core goes on
    end
    table.remove(args, 1)
    if player then
        local rank = player:GetGMRank()
        if rank < TT.CFG.min_gm_rank then
            emit(player, { cmd = "tbapi", ok = false, err = "not_allowed", msg = "GM rank " .. TT.CFG.min_gm_rank .. "+ only" })
            return false
        end
    end
    TT.guard("tbapi", dispatch, player, args)
    return false
end

RegisterPlayerEvent(TT.EV.PLAYER_ON_COMMAND, onCommand)

-- In a MAP state the TB functions must be refused. The world state never gets this event; a dungeon or raid state
-- logs the result once, on its first player.
local mapChecked = false
RegisterServerEvent(TT.EV.MAP_ON_PLAYER_ENTER, function()
    if mapChecked then return end
    mapChecked = true
    if not haveApi() then
        TT.log("TBAPI mapstate api=absent")
        return
    end
    local ok, err = pcall(TB.GetBots)
    TT.writeStdout("TBAPI " .. TT.json(TT.obj({ v = 1, cmd = "mapstate", refused = not ok, err = ok and "" or tostring(err):sub(1, 160),
        ts = os.time() })))
end)

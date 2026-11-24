-- ForeverCore test toolkit: `.tt q ...` read-only queries (TEST SERVERS ONLY). See tt_core.lua.
local TT = require("tt_core")
local CFG = TT.CFG

-- A point given as x y z must be near the caller.
local function checkPoint(player, x, y)
    local px, py = player:pos()
    local d = TT.dist2(px, py, x, y)
    if d > CFG.max_point_dist then
        return false, string.format("point is %.0f yd from you (max %d): .go xyz there first", d, CFG.max_point_dist)
    end
    return true
end

-- The floor under (x, y): searched down from z, like .gps FloorZ. Returns the height or nil.
local function floorAt(map, x, y, z)
    return map:height(x, y, z)
end

TT.floorAt = floorAt

TT.subs["q.floor"] = function(ctx, player, req)
    local map = player:map()
    local px, py, pz = player:pos()
    local x, y = TT.num(req.pos[2]), TT.num(req.pos[3])
    local zWanted = TT.num(req.pos[4])
    if not x or not y then x, y = px, py end
    local d = TT.dist2(px, py, x, y)
    if d > CFG.place_anchor_max then
        return TT.fail(ctx, req, "anchor_far",
            string.format("the floor is searched from your own z: stand within %d yd (now %.0f)", CFG.place_anchor_max, d))
    end
    local fz = floorAt(map, x, y, pz)
    local t = TT.mapInfo(map)
    t.at = { TT.round(x), TT.round(y), zWanted or TT.NULL }
    t.caller = { TT.round(px), TT.round(py), TT.round(pz) }
    t.floor = fz and TT.round(fz) or TT.NULL
    local top = map:height(x, y, pz + 30)   -- the surface above (the roof of a building, the ground outdoors)
    t.top = top and TT.round(top) or TT.NULL
    if fz then
        t.caller_above = TT.round(pz - fz)
        if zWanted then t.dz_wanted = TT.round(fz - zWanted) end
    end
    t.multi = (fz ~= nil and top ~= nil and math.abs(top - fz) > 3)
    TT.ok(ctx, req, t)
end

TT.subs["q.units"] = function(ctx, player, req)
    local map = player:map()
    local r = TT.clampR(TT.num(req.pos[2]), 40)
    local x, y, z = TT.point(req, 3)
    if not x then x, y, z = player:pos() end
    local ok, msg = checkPoint(player, x, y)
    if not ok then return TT.fail(ctx, req, "point_far", msg) end
    local entry = TT.num(req.kv.e)
    local au = TT.idList(req.kv.au)
    local ref = TT.reference(player, req.kv["of"])
    local list = TT.creaturesAt(map, x, y, z, r, { entry = entry, dead = req.flags["dead"] })
    local filtered = {}
    for _, e in ipairs(list) do
        local hostile = e.u:isHostileTo(ref)
        if req.flags["all"] or (req.flags["friendly"] and hostile == false) or (not req.flags["friendly"] and hostile) then
            filtered[#filtered + 1] = e
        end
    end
    local units = {}
    for i = 1, math.min(#filtered, CFG.max_list) do
        units[#units + 1] = TT.unitSnap(filtered[i].u, x, y, z, au)
    end
    local t = TT.mapInfo(map)
    t.at = { TT.round(x), TT.round(y), TT.round(z) }
    t.r = r
    t.filter = req.flags["all"] and "all" or (req.flags["friendly"] and "friendly" or "hostile")
    t.n = #filtered
    t.units = units
    if #filtered > CFG.max_list then t.trunc = true end
    TT.ok(ctx, req, t)
end

TT.subs["q.unit"] = function(ctx, player, req)
    local map = player:map()
    local guid = req.pos[2]
    local o = guid and map:find(guid)
    if not o then return TT.fail(ctx, req, "not_found", "no such unit on your map: " .. tostring(guid)) end
    if o:kind() == "gameobject" then return TT.fail(ctx, req, "not_a_unit", tostring(guid)) end
    local px, py, pz = player:pos()
    local t = TT.mapInfo(map)
    t.unit = TT.unitSnap(o, px, py, pz, TT.idList(req.kv.au))
    -- the reply carries the guid asked for: the client drops a reply whose unit.g differs (wrong-unit protection)
    t.asked = guid
    TT.ok(ctx, req, t)
end

-- q los <x> <y> <z> <guid>... (1-8 guids): sight from an eye 2 yd above the point to each unit.
TT.subs["q.los"] = function(ctx, player, req)
    local map = player:map()
    local x, y, z = TT.point(req, 2)
    local first = 5
    local count = #req.pos - first + 1
    if not x or count < 1 then
        return TT.fail(ctx, req, "usage", "q los <x> <y> <z> <guid>... (1-8 guids)")
    end
    if count > 8 then return TT.fail(ctx, req, "too_many", "q los takes 1-8 guids") end
    local ok, msg = checkPoint(player, x, y)
    if not ok then return TT.fail(ctx, req, "point_far", msg) end
    local eye = z + 2.0
    local los = {}
    for i = first, #req.pos do
        local guid = req.pos[i]
        local u = map:find(guid)
        if not u or u:kind() == "gameobject" then
            los[#los + 1] = { g = guid, err = u and "not_a_unit" or "not_found" }
        else
            local ux, uy, uz = u:pos()
            local seen = map:los(x, y, eye, ux, uy, uz + 1.0, u)
            if seen == nil then
                los[#los + 1] = { g = u:guid(), err = "los_failed" }
            else
                los[#los + 1] = { g = u:guid(), ["in"] = seen and true or false,
                    d = TT.round(TT.dist3(x, y, z, ux, uy, uz), 1) }
            end
        end
    end
    local t = TT.mapInfo(map)
    t.at = { TT.round(x), TT.round(y), TT.round(z) }
    t.n = #los
    t.los = los
    TT.ok(ctx, req, t)
end

TT.subs["q.gos"] = function(ctx, player, req)
    local map = player:map()
    local r = TT.clampR(TT.num(req.pos[2]), 30)
    local x, y, z = TT.point(req, 3)
    if not x then x, y, z = player:pos() end
    local ok, msg = checkPoint(player, x, y)
    if not ok then return TT.fail(ctx, req, "point_far", msg) end
    local entry = TT.num(req.kv.e)
    local R = math.min(1000, math.sqrt(r * r + CFG.z_slack * CFG.z_slack))
    local list = {}
    for _, go in ipairs(map:gameObjects(x, y, z, R, entry and { entry = entry } or nil) or {}) do
        local gx, gy = go:pos()
        if gx then
            local d = TT.dist2(x, y, gx, gy)
            if d <= r then list[#list + 1] = { go = go, d = d } end
        end
    end
    table.sort(list, function(a, b) return a.d < b.d end)
    local gos = {}
    for i = 1, math.min(#list, CFG.max_list) do
        local go = list[i].go
        gos[#gos + 1] = {
            g = go:guid(), e = go:entry(), n = go:name(), p = TT.pos(go), d = TT.round(list[i].d, 1),
            st = go:state(),        -- 0 active/open, 1 ready/closed, 2 alternative
            ls = go:lootState(),
            sp = go:spawned() and true or false,
            db = go:spawnId(),
        }
    end
    local t = TT.mapInfo(map)
    t.at = { TT.round(x), TT.round(y), TT.round(z) }
    t.r = r
    t.n = #list
    t.gos = gos
    if #list > CFG.max_list then t.trunc = true end
    TT.ok(ctx, req, t)
end

TT.subs["q.group"] = function(ctx, player, req)
    local out = {}
    local myMap, myInst = player:mapId(), player:instanceId()
    local px, py = player:pos()
    for _, m in ipairs(TT.groupMembers(player, req.kv["of"])) do
        local mx, my, mz = m:pos()
        if mx then
            local s = {
                g = m:guid(), n = m:name(), map = m:mapId(), inst = m:instanceId(),
                p = { TT.round(mx), TT.round(my), TT.round(mz) }, hp = TT.round(m:healthPct(), 1),
                alive = m:alive() and true or false, cb = m:inCombat() and true or false,
                gm = ((m:security() or 0) >= CFG.min_gm_rank) and true or false,
            }
            if m:mapId() == myMap and m:instanceId() == myInst then
                s.d = TT.round(TT.dist2(px, py, mx, my), 1)
                local fz = floorAt(m:map(), mx, my, mz)
                if fz then s.above_floor = TT.round(mz - fz) end
            end
            out[#out + 1] = s
        end
    end
    local t = TT.mapInfo(player:map())
    t.members = out
    TT.ok(ctx, req, t)
end

return TT

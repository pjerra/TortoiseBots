-- ForeverCore test toolkit: quiet, clearto, place, reset (TEST SERVERS ONLY). See tt_core.lua.
local TT = require("tt_core")
local CFG = TT.CFG

local function checkPoint(player, x, y)
    local px, py = player:pos()
    local d = TT.dist2(px, py, x, y)
    if d > CFG.max_point_dist then
        return false, string.format("point is %.0f yd from you (max %d): .go xyz there first", d, CFG.max_point_dist)
    end
    return true
end

-- dz=<yd> (default CFG.default_dz; 0 = no vertical limit) and skip=<entry,...>: shared by quiet and clearto.
local function windowOf(req)
    local dz = TT.num(req.kv.dz)
    if dz == nil then dz = CFG.default_dz end
    if dz < 0 then dz = -dz end
    local skip, skiplist = {}, {}
    for _, e in ipairs(TT.idList(req.kv.skip)) do
        skip[e] = true
        skiplist[#skiplist + 1] = e
    end
    return { dz = dz, skip = skip, skiplist = skiplist }
end

-- Drops the entries the window excludes.
local function windowFilter(list, window)
    local out = {}
    for _, e in ipairs(list) do
        local entry = e.u:entry()
        if not (entry and window.skip[entry]) and (window.dz == 0 or math.abs(e.dz) <= window.dz) then
            out[#out + 1] = e
        end
    end
    return out
end

-- ---------------------------------------------------------------------------------------------------------------
-- quiet: the owner's quiet rule. No hostile alive within r (2D, within dz of the point's height, entries in skip=
-- never count) now and after w seconds, and no hostile within quiet_roam_r that moved or arrived between the reads.
-- ---------------------------------------------------------------------------------------------------------------
local function quietRead(map, ref, x, y, z, r, window)
    local scope = math.max(r, CFG.quiet_roam_r)
    local list = {}
    for _, e in ipairs(windowFilter(TT.creaturesAt(map, x, y, z, scope), window)) do
        if e.u:isHostileTo(ref) then list[#list + 1] = e end
    end
    local near, seen = {}, {}
    for _, e in ipairs(list) do
        local guid = e.u:guid()
        local cx, cy = e.u:pos()
        seen[guid] = { cx, cy, e.u:entry(), e.d }
        if e.d <= r then near[#near + 1] = TT.unitSnap(e.u, x, y, z) end
    end
    return near, seen, #list
end

TT.subs["quiet"] = function(ctx, player, req)
    local map = player:map()
    local r = TT.clampR(TT.num(req.pos[1]), 50)
    local x, y, z = TT.point(req, 2)
    if not x then x, y, z = player:pos() end
    local ok, msg = checkPoint(player, x, y)
    if not ok then return TT.fail(ctx, req, "point_far", msg) end
    local window = windowOf(req)
    local w = TT.num(req.kv.w) or 20
    if w < 0 then w = 0 end
    if w > CFG.quiet_wait_max_s then w = CFG.quiet_wait_max_s end
    local ref = TT.reference(player, req.kv["of"])
    local near1, seen1, scopeN = quietRead(map, ref, x, y, z, r, window)
    local mapId, inst = player:mapId(), player:instanceId()
    local t = { map = mapId, inst = inst, at = { TT.round(x), TT.round(y), TT.round(z) }, r = r,
        read = 1, final = (w == 0), quiet_now = (#near1 == 0), n_near = #near1, n_scope = scopeN, near = near1,
        dz = window.dz, skip = window.skiplist }
    if w == 0 then t.quiet = (#near1 == 0) end
    TT.ok(ctx, req, t)
    if w == 0 then return end
    local callerGuid, refGuid = player:guid(), ref:guid()
    Timer.after(math.floor(w * 1000), function()
        local p = World.player(callerGuid)
        if not p or p:mapId() ~= mapId or p:instanceId() ~= inst then
            -- the caller left: nobody to tell; the agent's own timeout reports it
            return
        end
        local m2 = p:map()
        local ref2 = World.player(refGuid) or p
        local near2, seen2 = quietRead(m2, ref2, x, y, z, r, window)
        local roamers = {}
        for guid, a in pairs(seen2) do
            local b = seen1[guid]
            if not b then
                roamers[#roamers + 1] = { g = guid, e = a[3], why = "arrived", d = TT.round(a[4], 1) }
            else
                local moved = TT.dist2(a[1], a[2], b[1], b[2])
                if moved > CFG.quiet_roam_moved then
                    roamers[#roamers + 1] = { g = guid, e = a[3], why = "moved", moved = TT.round(moved, 1), d = TT.round(a[4], 1) }
                end
            end
        end
        table.sort(roamers, function(a, b) return a.g < b.g end)
        local quiet = (#near1 == 0) and (#near2 == 0) and (#roamers == 0)
        TT.ok(p, req, { map = mapId, inst = inst, at = { TT.round(x), TT.round(y), TT.round(z) }, r = r,
            read = 2, final = true, wait_s = w, quiet = quiet, n_near = #near2, near = near2, roamers = roamers,
            dz = window.dz, skip = window.skiplist })
    end)
end

-- ---------------------------------------------------------------------------------------------------------------
-- clearto <map> <x> <y> <z> <r> [kill] [boss] [dz=<yd>] [skip=<e,e>] [all=1] [of=<name>]
-- Despawn (default) or kill hostile creatures within r of the point. Bosses (rank 3 / world boss) and owned units
-- (pets, guardians) are skipped (bosses unless `boss`); friendly NPCs are never touched unless all=1 (judged from a
-- non-GM group member). At most max_clear are removed.
-- ---------------------------------------------------------------------------------------------------------------
TT.subs["clearto"] = function(ctx, player, req)
    local map = player:map()
    local mapArg = TT.num(req.pos[1])
    local x, y, z = TT.point(req, 2)
    local r = TT.num(req.pos[5])
    if not mapArg or not x or not r then
        return TT.fail(ctx, req, "usage", ".tt clearto <map> <x> <y> <z> <radius> [kill] [boss]")
    end
    if mapArg ~= player:mapId() then
        return TT.fail(ctx, req, "wrong_map", string.format("you are on map %d, not %d", player:mapId(), mapArg))
    end
    r = TT.clampR(r, r)
    local ok, msg = checkPoint(player, x, y)
    if not ok then return TT.fail(ctx, req, "point_far", msg) end
    local kill = req.flags["kill"] and true or false
    local bosses = req.flags["boss"] and true or false
    local window = windowOf(req)
    local everything = TT.flagOn(req, "all") and true or false
    local ref
    if not everything then
        local judge, isReference = TT.reference(player, req.kv["of"])
        if not isReference then
            return TT.fail(ctx, req, "no_reference",
                "no non-GM group member on this map to judge friendliness: pass of=<lead bot> or all=1")
        end
        ref = judge
    end
    local removed, skipped = {}, {}
    for _, e in ipairs(TT.creaturesAt(map, x, y, z, r)) do
        local c = e.u
        local item = { g = c:guid(), e = c:entry(), n = c:name(), d = TT.round(e.d, 1) }
        if window.skip[item.e] then
            item.why = "skip"; skipped[#skipped + 1] = item
        elseif window.dz > 0 and math.abs(e.dz) > window.dz then
            item.why = "dz"; item.dz = TT.round(e.dz, 1); skipped[#skipped + 1] = item
        elseif ref and not c:isHostileTo(ref) then
            item.why = "friendly"; skipped[#skipped + 1] = item
        elseif c:owner() then
            item.why = "owned"; skipped[#skipped + 1] = item
        elseif (c:rank() >= 3 or c:isBoss()) and not bosses then
            item.why = "boss"; skipped[#skipped + 1] = item
        elseif #removed >= CFG.max_clear then
            item.why = "cap"; skipped[#skipped + 1] = item
        else
            if kill then c:kill() else c:despawn(0) end
            removed[#removed + 1] = item
        end
    end
    TT.ok(ctx, req, { map = mapArg, inst = player:instanceId(), at = { x, y, z }, r = r,
        mode = kill and "kill" or "despawn", n = #removed, removed = removed, skipped = skipped,
        dz = window.dz, skip = window.skiplist, all = everything })
end

-- ---------------------------------------------------------------------------------------------------------------
-- place here|<x y z> [spread=N] [force] [me] [of=<name>]
-- Moves the caller's group members (same map and instance, alive) onto the floor at a point in ONE burst, each on its
-- own ring spot, facing the centre. A second reply (read 2) after place_verify_ms says where everyone landed.
-- ---------------------------------------------------------------------------------------------------------------
TT.subs["place"] = function(ctx, player, req)
    local map = player:map()
    local px, py, pz = player:pos()
    local here = (req.pos[1] == nil) or (req.pos[1]:lower() == "here")
    local ax, ay, fz
    if here then
        local f = TT.floorAt(map, px, py, pz)
        if not f then return TT.fail(ctx, req, "no_floor", "no floor found under you (under the terrain?)") end
        local drop = pz - f
        if drop > CFG.place_max_drop then
            return TT.fail(ctx, req, "caller_high", string.format("you are %.1f yd above the floor found", drop),
                { floor = TT.round(f), caller = TT.pos(player) })
        end
        ax, ay, fz = px, py, f
    else
        local x, y, z = TT.point(req, 1)
        if not x then return TT.fail(ctx, req, "usage", ".tt place here | .tt place <x> <y> <z>") end
        local d = TT.dist2(px, py, x, y)
        if d > CFG.place_anchor_max then
            return TT.fail(ctx, req, "anchor_far", string.format(
                "stand within %d yd of the point (now %.0f): the floor is searched from your z", CFG.place_anchor_max, d))
        end
        if pz < z - 1.5 or pz > z + CFG.place_anchor_dz then
            return TT.fail(ctx, req, "anchor_dz",
                string.format("your z %.1f must be in [%.1f, %.1f]", pz, z - 1.5, z + CFG.place_anchor_dz))
        end
        local f = TT.floorAt(map, x, y, pz)
        if not f then return TT.fail(ctx, req, "no_floor", "no floor found at the point") end
        if math.abs(f - z) > CFG.place_floor_tol then
            return TT.fail(ctx, req, "floor_mismatch", string.format("floor %.2f vs z %.2f: another level?", f, z),
                { floor = TT.round(f) })
        end
        ax, ay, fz = x, y, f
    end
    -- a floor ABOVE the caller means the caller stands under the terrain or under another level
    if fz - pz > 2.5 then
        return TT.fail(ctx, req, "caller_under_floor", string.format(
            "the floor found (%.2f) is above you (%.2f): you are under the ground or a level; .go xyz to the floor first",
            fz, pz), { floor = TT.round(fz), caller = TT.pos(player) })
    end

    local spread = TT.num(req.kv.spread) or CFG.place_spread
    if spread < 0 then spread = 0 end
    if spread > 8 then spread = 8 end
    local includeMe = req.flags["me"] and true or false
    local force = req.flags["force"] and true or false
    local mapId, inst = player:mapId(), player:instanceId()
    local myGuid = player:guid()

    local movers, skipped, fighting = {}, {}, {}
    for _, m in ipairs(TT.groupMembers(player, req.kv["of"])) do
        local isMe = (m:guid() == myGuid)
        if isMe and not includeMe then
            skipped[#skipped + 1] = { n = m:name(), why = "caller" }   -- add `me` to move yourself too
        elseif m:mapId() ~= mapId or m:instanceId() ~= inst then
            skipped[#skipped + 1] = { n = m:name(), why = "other_map" }
        elseif not m:alive() then
            skipped[#skipped + 1] = { n = m:name(), why = "dead" }
        else
            if m:inCombat() then fighting[#fighting + 1] = m:name() end
            movers[#movers + 1] = m
        end
    end
    if #movers == 0 then return TT.fail(ctx, req, "no_members", "no group member to move", { skipped = skipped }) end
    if #fighting > 0 and not force then
        return TT.fail(ctx, req, "in_combat", "members in combat (add `force` to move anyway)", { fighting = fighting })
    end

    local plan, out = {}, {}
    local n = #movers
    for i = 1, n do
        local m = movers[i]
        local ang = 2 * math.pi * (i - 1) / n
        local tx, ty = ax + spread * math.cos(ang), ay + spread * math.sin(ang)
        local sx, sy, sz = tx, ty, nil
        if spread > 0 then
            local f = map:height(tx, ty, fz + 2)
            if f and math.abs(f - fz) <= 2 then sz = f end
        end
        if not sz then sx, sy, sz = ax, ay, fz end   -- fall back to the centre
        local o = math.atan(ay - sy, ax - sx)
        local from = TT.pos(m)
        m:teleport(mapId, sx, sy, sz + 0.1, o)
        plan[#plan + 1] = { g = m:guid(), x = sx, y = sy, z = sz, n = m:name() }
        out[#out + 1] = { g = m:guid(), n = m:name(), from = from,
            to = { TT.round(sx), TT.round(sy), TT.round(sz) }, cb = m:inCombat() and true or false }
    end
    TT.ok(ctx, req, { map = mapId, inst = inst, read = 1, final = false,
        center = { TT.round(ax), TT.round(ay), TT.round(fz) }, floor = TT.round(fz),
        spread = spread, members = out, skipped = skipped })

    Timer.after(CFG.place_verify_ms, function()
        local res, allOk = {}, true
        for _, pl in ipairs(plan) do
            local m = World.player(pl.g)
            if not m then
                res[#res + 1] = { n = pl.n, ok = false, why = "gone" }
                allOk = false
            else
                local mx, my, mz = m:pos()
                local dz = mz - pl.z
                local d = TT.dist2(mx, my, pl.x, pl.y)
                local good = m:mapId() == mapId and math.abs(dz) <= 2 and d <= 5 and m:alive()
                if not good then allOk = false end
                res[#res + 1] = { g = pl.g, n = pl.n, ok = good and true or false, p = TT.pos(m),
                    dz = TT.round(dz), d = TT.round(d, 1), hp = TT.round(m:healthPct(), 1),
                    cb = m:inCombat() and true or false, alive = m:alive() and true or false }
            end
        end
        local caller = World.player(myGuid)
        if caller then
            TT.ok(caller, req, { map = mapId, inst = inst, read = 2, final = true, placed = allOk, members = res })
        end
    end)
end

-- ---------------------------------------------------------------------------------------------------------------
-- reset <mapId>: `.instance unbind <map>` with the caller's own rights (run it from OUTSIDE the instance).
-- ---------------------------------------------------------------------------------------------------------------
TT.subs["reset"] = function(ctx, player, req)
    local mapId = TT.num(req.pos[1])
    if not mapId then
        return TT.fail(ctx, req, "usage", ".tt reset <mapId> (run it from OUTSIDE the instance)")
    end
    mapId = math.floor(mapId)
    if mapId <= 1 then return TT.fail(ctx, req, "not_instance", "maps 0/1 have no binds") end
    if player:mapId() == mapId then
        return TT.fail(ctx, req, "inside", "you are still inside map " .. mapId, { inside = { player:name() } })
    end
    local ok, lines = player:run("instance unbind " .. mapId)
    TT.ok(ctx, req, { map = mapId, unbound = { player:name() }, handled = ok and true or false, lines = lines })
end

return TT

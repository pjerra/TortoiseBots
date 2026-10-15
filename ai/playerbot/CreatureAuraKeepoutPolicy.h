#pragma once

// Pure rules for the creature aura keep-out (AiPlayerbot.AvoidCreatureAuras).
// Some creatures carry a permanent damaging aura that pulses around them
// whether or not they are in combat. A bot must not stand, path or chase
// into that circle. The table lists each such creature by map and entry with
// the aura that marks it live, the aura's pulse radius and the keep-out
// radius the bot steps out from (pulse plus a drift margin).
//
// Behaviour reimplemented from the T-imothy tortoise-wow registry
// (modules/mod-dungeon-clear/.../Data/DcHazardRegistry.cpp, 0fb4d7c): every
// role stays out of the Creeping Sludge, and the keep-out is the 5 yd pulse
// plus 3 yd. No code copied. Turtle data: creature_template 12222 auras
// '22638'; spell 22638 is a 2000 ms periodic trigger of 22595, school damage
// at EffectRadiusIndex 8 (5 yd).
// Callers own game state (aura check, distances, the explicit order); this
// header owns the table and the decisions so tools/test_creature_aura_keepout_policy.cpp
// can test them. No core includes.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ai
{
    struct CreatureAuraKeepout
    {
        uint32_t mapId;
        uint32_t creatureEntry;
        uint32_t auraSpellId;
        float pulseRadius;   // radius of the damaging pulse (yd)
        float keepoutRadius; // bot steps out when this close (yd)
    };

    // Extra yards the movement hazard reaches past the keep-out radius, so a
    // bot that just stepped out is not chased or followed back to the rim.
    constexpr float kCreatureAuraHazardMargin = 1.0f;

    // Seconds a registered movement hazard lives; re-added every trigger tick
    // while the creature is near, so it lapses soon after the creature dies,
    // leaves or the bot moves on.
    constexpr unsigned kCreatureAuraHazardSeconds = 3;

    constexpr CreatureAuraKeepout kCreatureAuraKeepouts[] = {
        // Maraudon, Creeping Sludge, Poison Shock (180-220 nature every 2 s, 5 yd).
        { 349, 12222, 22638, 5.0f, 8.0f },
    };

    inline CreatureAuraKeepout const* FindCreatureAuraKeepout(uint32_t mapId, uint32_t creatureEntry)
    {
        for (CreatureAuraKeepout const& row : kCreatureAuraKeepouts)
            if (row.mapId == mapId && row.creatureEntry == creatureEntry)
                return &row;
        return nullptr;
    }

    // A listed creature is a keep-out for this bot when the toggle is on, the
    // creature is alive and carries the listed aura, and the player has not
    // ordered this bot to attack exactly that creature (the order wins).
    inline bool CreatureAuraKeepoutApplies(bool toggleOn, CreatureAuraKeepout const* row,
        bool alive, bool hasAura, bool isExplicitAttackTarget)
    {
        return toggleOn && row && alive && hasAura && !isExplicitAttackTarget;
    }

    // Step out while inside the keep-out radius. Both this and the movement
    // hazard radius are measured from the creature's centre (no bounding
    // radius), the way MovementAction's hazard checks measure, so an end
    // point the hazard accepts can never trigger a step-out.
    inline bool InsideCreatureAuraKeepout(CreatureAuraKeepout const& row, float distance)
    {
        return distance <= row.keepoutRadius;
    }

    // Radius registered as a movement hazard (end points and paths stay out).
    inline float CreatureAuraHazardRadius(CreatureAuraKeepout const& row)
    {
        return row.keepoutRadius + kCreatureAuraHazardMargin;
    }

    // ---- Follow near a keep-out (HoldFollowOutsideKeepout) ----------------
    //
    // While the follow goal lies inside a keep-out, the follower must never be
    // sent toward it across the hold ring. Rings around the creature, centre
    // measure, Creeping Sludge values in brackets:
    //   keepout   [8.0]  inside: the avoid-aoe step-out owns the bot
    //   hazard    [9.0]  8..9: back off along the creature-bot line to `backoff`
    //   hold      [9.5]  where an approaching follower stops (and the goal test)
    //   backoff   [10.0] where a back-off lands
    //   holdOuter [10.5] 9..10.5: hold (stop a running follow, issue nothing)
    //   release   [10.0] once engaged, the goal must clear this to follow again
    // Every move target lands inside the hold band with at least 0.5 yd to
    // either edge, so a height snap, a short arrival or the creature creeping
    // at 2 yd/s cannot flip hold/follow/step-out from one tick to the next.
    constexpr float kCreatureAuraFollowHoldMargin = 0.5f;    // hold ring past the hazard
    constexpr float kCreatureAuraFollowBackoffMargin = 1.0f; // back-off ring past the hazard
    constexpr float kCreatureAuraFollowOuterMargin = 1.5f;   // hold band edge past the hazard
    constexpr float kCreatureAuraFollowReleaseMargin = 1.0f; // engaged goal release past the hazard

    struct CreatureAuraFollowRings
    {
        float keepout, hazard, hold, backoff, holdOuter, release;
    };

    inline CreatureAuraFollowRings CreatureAuraFollowRingsFor(CreatureAuraKeepout const& row)
    {
        float const hazard = CreatureAuraHazardRadius(row);
        return { row.keepoutRadius, hazard, hazard + kCreatureAuraFollowHoldMargin,
            hazard + kCreatureAuraFollowBackoffMargin, hazard + kCreatureAuraFollowOuterMargin,
            hazard + kCreatureAuraFollowReleaseMargin };
    }

    // First point where the segment from B (outside the circle) to P enters
    // the circle (centre C, radius R). False when B is not outside, or the
    // segment never reaches the circle. 2D.
    inline bool SegmentEntersCircle(float bx, float by, float px, float py,
        float cx, float cy, float R, float& hx, float& hy)
    {
        float const fx = bx - cx, fy = by - cy;
        float const c = fx * fx + fy * fy - R * R;
        if (c <= 0.0f)
            return false; // B not outside
        float const dx = px - bx, dy = py - by;
        float const a = dx * dx + dy * dy;
        if (a <= 0.0f)
            return false;
        float const b = 2.0f * (fx * dx + fy * dy);
        float const disc = b * b - 4.0f * a * c;
        if (disc < 0.0f)
            return false; // the line misses the circle
        float const t = (-b - std::sqrt(disc)) / (2.0f * a);
        if (t < 0.0f || t > 1.0f)
            return false; // circle behind B, or beyond P
        hx = bx + dx * t;
        hy = by + dy * t;
        return true;
    }

    enum class CreatureAuraFollowStep
    {
        Follow,   // goal clear of the keep-out: native follow
        StepOut,  // bot inside the keep-out: stop following, the step-out moves it
        BackOff,  // bot inside the hazard: move to (hx, hy), outward on the creature-bot line
        Hold,     // bot in the hold band: stop a running follow, issue no move
        Approach, // bot outside the band: move to (hx, hy), never inside the hold ring
    };

    // One follow decision against one keep-out creature (centre C) for a bot
    // at B whose follow goal is P. `engaged`: the previous decision against
    // this creature was not Follow (goal hysteresis: an engaged follower
    // resumes only once the goal is past the release ring).
    inline CreatureAuraFollowStep PlanCreatureAuraFollow(CreatureAuraKeepout const& row, bool engaged,
        float bx, float by, float px, float py, float cx, float cy, float& hx, float& hy)
    {
        CreatureAuraFollowRings const rings = CreatureAuraFollowRingsFor(row);
        float const gdx = px - cx, gdy = py - cy;
        float const goalLimit = engaged ? rings.release : rings.hold;
        if (gdx * gdx + gdy * gdy > goalLimit * goalLimit)
            return CreatureAuraFollowStep::Follow;

        float const fx = bx - cx, fy = by - cy;
        float const d = std::sqrt(fx * fx + fy * fy);
        if (d <= rings.keepout)
            return CreatureAuraFollowStep::StepOut;
        if (d < rings.hazard)
        {
            // d > keepout > 0, so the direction is defined.
            hx = cx + fx / d * rings.backoff;
            hy = cy + fy / d * rings.backoff;
            return CreatureAuraFollowStep::BackOff;
        }
        if (d <= rings.holdOuter)
            return CreatureAuraFollowStep::Hold;
        // Outside the band: walk to where the straight line to the goal meets
        // the hold ring. An engaged goal between the hold and release rings
        // may be reachable without touching the ring; then walk to it.
        if (!SegmentEntersCircle(bx, by, px, py, cx, cy, rings.hold, hx, hy))
        {
            hx = px;
            hy = py;
        }
        return CreatureAuraFollowStep::Approach;
    }

    // ---- Chase near a keep-out (HoldChaseOutsideKeepout) ------------------
    //
    // A chase (reach melee, reach spell, ...) is planned with the same bands,
    // the goal being the point the chase wants the bot at: `distance` from the
    // target toward the bot, i.e. the target itself for a melee chase
    // (distance 0). A melee bot whose target is a keep-out creature, or stands
    // within the hold ring of one, so stops outside the ring instead of
    // chasing into the pulse; a ranged chase whose goal lies well outside the
    // ring is left alone.
    inline void CreatureAuraChaseGoal(float bx, float by, float tx, float ty, float distance,
        float& gx, float& gy)
    {
        float const dx = bx - tx, dy = by - ty;
        float const d = std::sqrt(dx * dx + dy * dy);
        if (distance <= 0.0f || d <= 0.0f)
        {
            gx = tx;
            gy = ty;
            return;
        }
        float const k = (distance < d ? distance : d) / d;
        gx = tx + dx * k;
        gy = ty + dy * k;
    }

    // ---- Roles and the bounded chase hold (owner decision 2026-10-13) ------
    //
    // Tanks pick up and hold mobs, so a tank's chase is never held at the
    // ring, and in combat a tank neither steps out of a keep-out nor has it
    // registered as a path hazard. Out of combat (following, walking past an
    // idle creature) a tank keeps out like everyone else.
    inline bool CreatureAuraChaseExempt(bool isTank)
    {
        return isTank;
    }

    inline bool CreatureAuraCombatExempt(bool isTank, bool inCombat)
    {
        return isTank && inCombat;
    }

    // A chase held outside a ring because its goal is inside is no headway:
    // after this long on the same hostile current target the bot sets the
    // target aside and picks another, also in group dungeons where the reach
    // give-up is off. A player's attack order on that target keeps the hold
    // (and an ordered keep-out creature is not held at all).
    constexpr unsigned kCreatureAuraChaseHoldMs = 3000;
    // How long a set-aside target is left alone, even while it hits the bot.
    constexpr unsigned kCreatureAuraSetAsideMs = 30000;

    inline bool CreatureAuraSetAsideTarget(bool hostileCurrentTarget, bool explicitOrder, unsigned heldMs)
    {
        return hostileCurrentTarget && !explicitOrder && heldMs >= kCreatureAuraChaseHoldMs;
    }

    // A chase hold is timed per target. The timer starts over when the hold
    // is on another target, or when the last hold tick was longer ago than
    // this (combat ended mid-hold, or some other action ran in between), so a
    // later hold on the same mob never inherits an old start time and sets it
    // aside on its first tick.
    constexpr unsigned kCreatureAuraChaseHoldGapMs = 2000;

    inline bool CreatureAuraChaseHoldRestarts(bool sameTarget, unsigned sinceLastHoldMs)
    {
        return !sameTarget || sinceLastHoldMs > kCreatureAuraChaseHoldGapMs;
    }

    // A set-aside mob counts as still ringed while it stands within the
    // release ring of a listed creature (centre measure): the ring the chase
    // hold lets go of only past (CreatureAuraFollowRingsFor().release), so a
    // mob just outside it is chased without being held again.
    inline bool CreatureAuraSetAsideRinged(CreatureAuraKeepout const& row, float distance)
    {
        return distance <= CreatureAuraFollowRingsFor(row).release;
    }

    // Target selection (AttackersValue::IsKeepoutSetAside, the bot's own
    // "keepout set aside" book): a set-aside target stays refused until its
    // entry runs out. Every pick path asks this, also the ones that bypass the
    // attackers list (the group tank's victim, raid marks): otherwise the
    // tank's victim came straight back after the set-aside and the bot held,
    // set aside and re-picked the same mob all fight. A keep-out creature
    // stays refused while it is set aside (reachable, but only inside its
    // pulse). Any other mob is refused only while it still stands inside the
    // ring of a listed creature (`stillRinged`): the set-aside ends the moment
    // the tank drags it out, or it walks out to someone else (owner decision
    // 2026-10-13, "lift when it leaves the ring"), and also when it hits the
    // bot in sight. The 30 s entry stays the upper bound. Tanks never hold a
    // set-aside.
    inline bool CreatureAuraSetAsideHolds(bool live, bool isKeepoutCreature, bool isTank, bool hitsBotInSight,
        bool stillRinged)
    {
        return live && !isTank && (isKeepoutCreature || (stillRinged && !hitsBotInSight));
    }

    // ---- Step-out landing near other keep-outs -----------------------------
    //
    // Radius a step-out landing must clear around a listed creature other than
    // the one the bot steps away from (sludges stand in groups): its keep-out
    // on the first pass, only its pulse on the relaxed second pass, so a bot
    // in a cluster with no landing outside every keep-out still never steps
    // into another pulse.
    inline float CreatureAuraLandingClearance(CreatureAuraKeepout const& row, bool relaxed)
    {
        return relaxed ? row.pulseRadius : row.keepoutRadius;
    }

    // ---- End points and gap closers (Maraudon live run 2, 2026-10-13) -----
    //
    // The hazard registry only bends a path; a move whose end point lies in a
    // ring still walks the bot in. An end point (rear flank, set behind, move
    // out of collision, a gap closer's landing) is refused while it lies
    // within the hazard radius of a listed creature, the ring a chase or
    // follow end point already keeps out of.
    inline bool CreatureAuraEndPointBlocked(CreatureAuraKeepout const& row, float creatureToPointDistance)
    {
        return creatureToPointDistance < CreatureAuraHazardRadius(row);
    }

    // Where a gap closer (Charge, Intercept, Feral Charge) puts the bot:
    // `reach` from the target on the bot's side (the chase goal at that
    // distance).
    inline void CreatureAuraGapCloserLanding(float bx, float by, float tx, float ty, float reach,
        float& lx, float& ly)
    {
        CreatureAuraChaseGoal(bx, by, tx, ty, reach, lx, ly);
    }

    // A gap closer jumps the bot past every hold: it never goes through the
    // chase, so neither the ring nor the 3 s set-aside sees it (live: a
    // warrior DPS charged a sludge from 21 yd and died in its pulse). A
    // non-tank bot does not use one onto a listed creature, or onto a target
    // whose landing lies in a ring. Tanks pick up mobs in the ring
    // (CreatureAuraChaseExempt), and a player's attack order on that target
    // keeps today's behaviour.
    // A gap closer fires from 8-25 yd, and set behind or rear flank end next
    // to a target up to 15 yd away, beyond the bot's cached 15 yd list, so
    // those points are also checked against the bot's hazard snapshot below.
    inline float CreatureAuraMaxHazardRadiusOnMap(uint32_t mapId)
    {
        float radius = 0.0f;
        for (CreatureAuraKeepout const& row : kCreatureAuraKeepouts)
            if (row.mapId == mapId && CreatureAuraHazardRadius(row) > radius)
                radius = CreatureAuraHazardRadius(row);
        return radius;
    }

    inline bool CreatureAuraGapCloserVetoed(bool targetIsKeepout, bool landingInRing, bool isTank,
        bool explicitOrder)
    {
        return !isTank && !explicitOrder && (targetIsKeepout || landingInRing);
    }

    // Review #882 round 2: the engine asks isUseful() before the stance
    // prerequisite and before isPossible(), so a gap closer's veto ran on
    // every tick its trigger fired ("enemy out of melee" pushes Charge every
    // tick of a chase, and Fury falls through to Intercept). The landing
    // lookup therefore runs only when the cast could fire this tick. Each
    // "not ready" reason below is one isPossible() also refuses
    // (CastSpellAction::isPossible -> PlayerbotAI::CanCastSpell ->
    // Spell::CheckCast), so skipping the veto then never lets an unvetoed
    // cast through: the engine sees what it sees with the option off (the
    // cast is impossible and its alternatives are pushed).
    // The stance is deliberately not a reason: a wrong stance is fixed by the
    // stance prerequisite, which runs only after isUseful(). Gating on it
    // would switch stance first and veto the charge a tick later.
    struct CreatureAuraGapCloserReadiness
    {
        bool knowsSpell = false;     // "spell id" resolved and the spell entry exists
        bool lostControl = false;    // UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL
        bool onCooldown = false;     // the spell's own cooldown (Player::HasSpellCooldown)
        bool powerShort = false;     // rage below the cost (Intercept 10, Feral Charge 5)
        bool nonCombatSpell = false; // SPELL_ATTR_CANT_USED_IN_COMBAT (Charge)
        bool inCombat = false;
        bool rangeKnown = false;     // isPossible() tests min/max range for this spell
        float distance = 0.0f;       // the distance isPossible() measures
        float minRange = 0.0f;
        float maxRange = 0.0f;       // 25 yd for Charge and Intercept
    };

    inline bool CreatureAuraGapCloserCouldFire(CreatureAuraGapCloserReadiness const& r)
    {
        if (!r.knowsSpell || r.lostControl || r.onCooldown || r.powerShort)
            return false;
        if (r.nonCombatSpell && r.inCombat)
            return false; // Charge never fires in combat on 1.12
        if (r.rangeKnown && !(r.distance < r.maxRange && r.distance >= r.minRange))
            return false; // the same test as CastSpellAction::isPossible
        return true;
    }

    // ---- Hazard snapshot (review of #995) --------------------------------
    //
    // End points and gap-closer landings can lie past the bot's cached 15 yd
    // list, and nothing per bot tick may scan the world (AGENTS.md). Each bot
    // keeps one snapshot of the listed creatures that carry their aura around
    // it: at most one bounded grid visit per bot per kCreatureAuraSnapshotMs,
    // and none on a map without a listed row. Every end-point and landing
    // check in that window reads the snapshot. A creature can move up to
    // kCreatureAuraSnapshotDriftYd (run speed over the window) after the
    // visit, so each snapshot ring is widened by that much: a stale snapshot
    // never lets the bot end inside a ring. No negative cache: a point the
    // snapshot does not cover is not called clear by it.
    constexpr uint32_t kCreatureAuraSnapshotMs = 500;
    constexpr float kCreatureAuraSnapshotDriftYd = 3.5f;  // 7 yd/s run speed x 0.5 s
    constexpr float kCreatureAuraSnapshotReachYd = 30.0f; // 15 yd list + 15 yd end point / landing

    struct CreatureAuraHazardEntry
    {
        uint64_t guidRaw;
        float x, y;
        float hazardRadius; // CreatureAuraHazardRadius(row)
    };

    struct CreatureAuraHazardSnapshot
    {
        bool valid = false;
        uint32_t mapId = 0;
        uint32_t atMs = 0;
        float cx = 0.0f, cy = 0.0f; // bot position at the visit
        std::vector<CreatureAuraHazardEntry> entries;
    };

    // Grid-visit radius around the bot: every creature whose widened ring
    // can hold a point within the reach of the bot.
    inline float CreatureAuraSnapshotRadius(float maxHazardRadius)
    {
        return kCreatureAuraSnapshotReachYd + maxHazardRadius + kCreatureAuraSnapshotDriftYd;
    }

    // Unsigned difference, so the wrap of the millisecond clock does not keep
    // a snapshot alive.
    inline bool CreatureAuraSnapshotStale(CreatureAuraHazardSnapshot const& snap, uint32_t mapId, uint32_t nowMs)
    {
        return !snap.valid || snap.mapId != mapId || uint32_t(nowMs - snap.atMs) >= kCreatureAuraSnapshotMs;
    }

    // Refresh through `fill` (the one grid visit) only when stale. Returns
    // true when it refreshed. `fill(entries)` appends the live entries.
    template <class Fill>
    inline bool CreatureAuraSnapshotEnsure(CreatureAuraHazardSnapshot& snap, uint32_t mapId, float botX,
        float botY, uint32_t nowMs, Fill&& fill)
    {
        if (!CreatureAuraSnapshotStale(snap, mapId, nowMs))
            return false;
        snap.entries.clear();
        fill(snap.entries);
        snap.valid = true;
        snap.mapId = mapId;
        snap.atMs = nowMs;
        snap.cx = botX;
        snap.cy = botY;
        return true;
    }

    // The snapshot holds every creature whose widened ring can reach a point
    // within kCreatureAuraSnapshotReachYd of where the bot stood at the visit.
    inline bool CreatureAuraSnapshotCovers(CreatureAuraHazardSnapshot const& snap, float x, float y)
    {
        float const dx = x - snap.cx, dy = y - snap.cy;
        return snap.valid && dx * dx + dy * dy <= kCreatureAuraSnapshotReachYd * kCreatureAuraSnapshotReachYd;
    }

    // The snapshot creature whose ring, widened by the drift, holds (x, y);
    // `skipA` and `skipB` (the target and the explicit attack target, checked
    // live by the callers) are passed over. 0 when none.
    inline uint64_t CreatureAuraSnapshotRingAt(CreatureAuraHazardSnapshot const& snap, float x, float y,
        uint64_t skipA = 0, uint64_t skipB = 0)
    {
        for (CreatureAuraHazardEntry const& e : snap.entries)
        {
            if (e.guidRaw == 0 || e.guidRaw == skipA || e.guidRaw == skipB)
                continue;
            float const dx = e.x - x, dy = e.y - y;
            if (std::sqrt(dx * dx + dy * dy) < e.hazardRadius + kCreatureAuraSnapshotDriftYd)
                return e.guidRaw;
        }
        return 0;
    }
} // namespace ai

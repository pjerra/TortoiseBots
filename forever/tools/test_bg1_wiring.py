"""Wiring pins for round BG1 (BATTLEGROUNDS-SPEC.md): the queue, the join, the leave and the death of a bot in a battleground.

The core calls cannot run without a server, so these pin them in the source; each assertion fails on the code before BG1.

  1 the module queues only through WorldSession::JoinBattlegroundQueue (never BattlegroundQueue::AddGroup or a queue structure)
  2 an invitation is answered only through HandleBattleFieldPortOpcode (no direct SetBattlegroundId / SendToBattleground)
  3 the three LeaveBattleground calls carry D5's Deserter flag (match over: plain; Bg.MaxMinutes and a bound bot's absent
    human: true, true) and there are no others
  4 no float literal in forever/src/host/bg/ or policy/bg/ except the named constants of BgLimits.h (positions come from the core)
  5 every id in BgIds.h is in the pinned core text it cites (skipped when no core checkout can be read)
  6 Bg.Enable = 0 does no hook work: every hook and the tick return first, and the code default is the template's 0
  7 every `TortoiseBots.Bg.*` code fallback equals the value in forever/conf/tortoisebots.conf.dist
  8 slice_gen_context.py --check is clean (the BG actions are not registered by editing the generated file)
  9 no file under the core's src/ is in the diff (needs the base ref; skipped when git cannot answer)
 10 (BG1 only: the BG2 PvP files stayed out of every slice list. BG2 put them in SLICE-W4.txt; test_bg2_wiring.py pins them there.)
 11 the pool holds a session only for a bot inside a match, never for one waiting in a queue (TbPoolScheduler passes inBg)
 12 W1's ghost rescue is off inside a battleground (TbBotMgr)
 13 the core's creation hook is vetoed until the fill reaches the target, and a bot-only creation stays off (BgVetoCreate)
 14 a queue bucket is re-evaluated every fill, so bots still waiting follow the human into his instance
 15 a module-queued bot leaves a bucket with no human and times out (BgQueuedDrop)
 16 a candidate is checked against the demand bracket, never its own level bracket
 17 the caps count every bot the module holds (queued, invited, inside) once per match: a running match by its instance, a
    queued reservation by its bucket, a leftover entry whose humans are already inside reserves nothing, and only the fill's own
    bucket is exempt (BgCapsOfTracks, ownBucketCounted); an invitation re-checks Bg.MaxBots only, never the instance cap
    (BgAcceptCapsOver)
 18 a bound bot enters only after its human has entered the instance, leaves after the master grace (BgMasterLeave), and follows
    the human out of the queue when the human cancels, logs out, unbinds it or went into another battleground (BgBoundQueueHeld,
    BgHumanInTrackMatch)
 19 a map turns off when a spawn is missing (offWhy is a std::string, checked with empty())
 20 release and spirit-healer waits read the core's own state, not a cached flag
 21 a bot mid-teleport keeps its track (only a real logout erases it)
 22 an invitation is answered on human presence only: a free bot needs a human inside its instance and a bound bot its own
    human, declined at the core's window (BgHumanEntryInviteAnswer) — an invited human is not presence
 23 a queue entry never keeps a bound bot online after its master's grace (TbBotMgr holds only a running match)
 24 a bound bot queued before the module looked is picked up and follows its human (TrackQueuedBots)
 25 a human's join only touches maps the round manages (Bg.Enable stays inert for AV and random queues)

BG_MUTATE=1 runs the same checks against mutated text and expects each to fail.
"""
import os
import pathlib
import re
import subprocess
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
HOST = ROOT / "forever/src/host/bg"
POLICY = ROOT / "ai/playerbot/policy/bg"
CONF = ROOT / "forever/conf/tortoisebots.conf.dist"
SCRIPTS = ROOT / "forever/src/TbScripts.cpp"
SCHED = ROOT / "forever/src/host/pool/TbPoolScheduler.cpp"
SCHED_POLICY = ROOT / "ai/playerbot/policy/world/PoolSchedulePolicy.h"
MGR = ROOT / "forever/src/TbBotMgr.cpp"
PIN = (ROOT / "forever/PINNED_CORE").read_text().strip()


def read(path, override=None):
    return override if override is not None else path.read_text()


def sources(overrides=None):
    overrides = overrides or {}
    out = {}
    for path in sorted(HOST.glob("*.cpp")) + sorted(HOST.glob("*.h")) + sorted(POLICY.glob("*.h")):
        rel = path.relative_to(ROOT).as_posix()
        out[rel] = read(path, overrides.get(rel))
    return out


# ---- the checks, each a function of the source texts so the mutations reuse them ----

def check_queue_only(src):
    text = src["forever/src/host/bg/TbBgQueue.cpp"]
    assert "JoinBattlegroundQueue(" in text, "the fill must queue through WorldSession::JoinBattlegroundQueue"
    for forbidden in ("AddGroup(", "m_QueuedGroups", "m_SelectionPools"):
        assert forbidden not in text, f"the module must not touch a queue structure ({forbidden})"


def check_port_only(src):
    text = src["forever/src/host/bg/TbBgQueue.cpp"]
    assert "HandleBattleFieldPortOpcode(" in text, "an invitation is answered through the core's handler"
    for forbidden in ("SetBattlegroundId(", "SendToBattleground("):
        assert forbidden not in text, f"the module must not enter a battleground itself ({forbidden})"


def check_leave_flags(src):
    text = src["forever/src/host/bg/TbBgQueue.cpp"]
    calls = re.findall(r"LeaveBattleground\(([^)]*)\)", text)
    assert calls, "BG1 leaves a match"
    assert len(calls) == 3, f"only D5's leaves (match over, MaxMinutes, an absent master): {calls}"
    plain = [c for c in calls if c.strip() == ""]
    without = [c for c in calls if "withoutDeserterDebuff" in c and c.strip().startswith("true")]
    assert len(plain) == 1, f"the match-over leave is the plain LeaveBattleground(): {calls}"
    assert len(without) == 2, f"MaxMinutes and master-gone leave with LeaveBattleground(true, true): {calls}"
    # the free bot's own branches in order: the match-over leave, then MaxMinutes
    assert text.index('"match-over"') < text.index('"max-minutes"'), "match-over before max-minutes"


FLOAT = re.compile(r"(?<![\w.])\d+\.\d+(?:[eE][+-]?\d+)?f?")
NAMED_FLOAT = re.compile(r"inline constexpr float k[A-Za-z0-9_]+ = \d+\.\d+f;")


def check_no_float_literal(src):
    for rel, text in src.items():
        if rel.endswith("BgLimits.h"):
            for line in text.splitlines():
                if FLOAT.search(line):
                    assert NAMED_FLOAT.search(line.strip()), f"float literal not a named constant in {rel}: {line.strip()}"
            continue
        for number, line in enumerate(text.splitlines(), 1):
            assert not FLOAT.search(line), f"float literal in {rel}:{number}: {line.strip()}"


def core_text(relative):
    """The file at the pin, from any readable core checkout (the FOREVER_CORE environment variable, or a checkout next to the home directory named after the pin)."""
    candidates = []
    if os.environ.get("FOREVER_CORE"):
        candidates.append(pathlib.Path(os.environ["FOREVER_CORE"]))
    candidates.append(pathlib.Path.home() / "ForeverCore")
    candidates.append(pathlib.Path.home() / f"ForeverCore-{PIN[:9]}")
    for core in candidates:
        if not (core / ".git").exists() and not (core / "src").exists():
            continue
        run = subprocess.run(["git", "-C", str(core), "show", f"{PIN}:src/server/{relative}"],
                             capture_output=True, text=True)
        if run.returncode == 0:
            return run.stdout
    return None


def check_ids(src):
    ids = src["ai/playerbot/policy/bg/BgIds.h"]
    wanted = {
        "kWsgListId": "game/Miscellaneous/SharedDefines.h",
        "kAbListId": "game/Miscellaneous/SharedDefines.h",
        "kWsgMapId": "scripts/Battlegrounds/WarsongGulch/battleground_warsong_gulch.cpp",
        "kAbMapId": "scripts/Battlegrounds/ArathiBasin/battleground_arathi_basin.cpp",
        "kWsgSilverwingFlag": "scripts/Battlegrounds/WarsongGulch/battleground_warsong_gulch.cpp",
        "kWsgWarsongFlag": "scripts/Battlegrounds/WarsongGulch/battleground_warsong_gulch.cpp",
        "kWsgSilverwingFlagDropped": "scripts/Battlegrounds/WarsongGulch/battleground_warsong_gulch.cpp",
        "kWsgWarsongFlagDropped": "scripts/Battlegrounds/WarsongGulch/battleground_warsong_gulch.cpp",
        "kWsgFlagTakenAlliance": "scripts/Battlegrounds/WarsongGulch/battleground_warsong_gulch.cpp",
        "kWsgFlagTakenHorde": "scripts/Battlegrounds/WarsongGulch/battleground_warsong_gulch.cpp",
        "kWsgCapturesAlliance": "scripts/Battlegrounds/WarsongGulch/battleground_warsong_gulch.cpp",
        "kWsgCapturesHorde": "scripts/Battlegrounds/WarsongGulch/battleground_warsong_gulch.cpp",
        "kAbResourcesAlliance": "scripts/Battlegrounds/ArathiBasin/battleground_arathi_basin.cpp",
        "kAbResourcesHorde": "scripts/Battlegrounds/ArathiBasin/battleground_arathi_basin.cpp",
    }
    cache = {}
    for name, relative in wanted.items():
        match = re.search(rf"{name}\s*=\s*(\d+)", ids)
        assert match, f"{name} is not a plain number in BgIds.h"
        number = match.group(1)
        if relative not in cache:
            cache[relative] = core_text(relative)
        text = cache[relative]
        if text is None:
            raise unittest.SkipTest(f"no core checkout at the pin {PIN[:9]} (set FOREVER_CORE)")
        assert re.search(rf"(?<![\w]){number}(?![\w])", text), f"{name} = {number} is not in {relative} at the pin"
    # the arrays carry five banners and five state ids, in the core's NodeData order, and each is in the AB script
    ab_script = cache.get("scripts/Battlegrounds/ArathiBasin/battleground_arathi_basin.cpp")
    if ab_script is None:
        raise unittest.SkipTest(f"no core checkout at the pin {PIN[:9]} (set FOREVER_CORE)")
    # the status numbers the pure leave rule mirrors are the core's enum values
    battleground = core_text("game/Battlegrounds/Battleground.h")
    if battleground is None:
        raise unittest.SkipTest(f"no core checkout at the pin {PIN[:9]} (set FOREVER_CORE)")
    pure = src["ai/playerbot/policy/bg/BgJoinLeavePolicy.h"]
    for name, core_name in (("statusWaitJoin", "STATUS_WAIT_JOIN"), ("statusInProgress", "STATUS_IN_PROGRESS"),
                            ("statusWaitLeave", "STATUS_WAIT_LEAVE")):
        ours = re.search(rf"constexpr std::int32_t {name} = (\d+);", pure)
        assert ours, f"{name} missing from the pure leave rule"
        theirs = re.search(rf"{core_name}\s*=\s*(\d+)", battleground)
        assert theirs, f"{core_name} missing from the core enum"
        assert ours.group(1) == theirs.group(1), f"{name} = {ours.group(1)} but {core_name} = {theirs.group(1)}"
    for array in ("kAbNodeBanners", "kAbNodeStateWorldStates"):
        match = re.search(rf"{array}\[kAbNodeBannerCount\]\s*=\s*\{{([^}}]*)\}}", ids)
        assert match, f"{array} missing"
        values = [v.strip() for v in match.group(1).split(",") if v.strip()]
        assert len(values) == 5, f"{array} must hold five ids"
        for value in values:
            assert re.search(rf"(?<![\w]){re.escape(value)}(?![\w])", ab_script), f"{array}: {value} is not in the AB script"


def check_disabled_is_inert(src, conf):
    text = src["forever/src/host/bg/TbBgQueue.cpp"]
    assert re.search(r"bool Enabled\(\)", text)
    # each hook starts with the enabled test; no hook reaches the core while disabled
    for hook in ("OnBattlegroundQueueJoin", "OnBattlegroundInvite", "OnBattlegroundPortFailed"):
        body = text[text.index(f"void {hook}("):]
        body = body[:body.index("\n    }")]
        assert "if (!Enabled()" in body, f"{hook} must return before doing work while Bg.Enable is 0"
    update = text[text.index("bool Update(std::uint32_t diff)"):]
    update = update[:update.index("\n}")]
    assert "if (!s.enabled)" in update and "return false;" in update, "Update must do nothing while disabled"
    assert re.search(r'GetIntDefault\("TortoiseBots\.Bg\.Enable", 0\)', text), "the code default of Enable is 0"
    assert re.search(r"^TortoiseBots\.Bg\.Enable = 0$", conf, re.M), "the template ships Enable = 0"


def check_config_defaults(src, conf):
    text = src["forever/src/host/bg/TbBgQueue.cpp"]
    keys = re.findall(r'GetIntDefault\("TortoiseBots\.Bg\.([A-Za-z.]+)", (-?\d+)\)', text)
    assert len(keys) >= 7, keys
    for key, default in keys:
        match = re.search(rf"^TortoiseBots\.Bg\.{re.escape(key)} = (-?\d+)$", conf, re.M)
        assert match, f"{key} has no template line"
        assert match.group(1) == default, f"{key}: code fallback {default} != template {match.group(1)}"
        assert f"#    TortoiseBots.Bg.{key}" in conf, f"{key} has no comment block"
    assert re.search(r'GetStringDefault\("TortoiseBots\.Bg\.Maps", "wsg,ab"\)', text)
    assert re.search(r'^TortoiseBots\.Bg\.Maps = "wsg,ab"$', conf, re.M)


def check_slice_gen_clean():
    run = subprocess.run(["python3", str(ROOT / "forever/tools/slice_gen_context.py"), "--check"],
                         capture_output=True, text=True)
    assert run.returncode == 0, run.stdout + run.stderr


def check_core_untouched():
    # The base the PR is read against: tbf/dev when the branch is ported on it, else tbf/main.
    base = "tbf/dev" if subprocess.run(["git", "-C", str(ROOT), "rev-parse", "-q", "--verify", "tbf/dev"], capture_output=True).returncode == 0 else "tbf/main"
    run = subprocess.run(["git", "-C", str(ROOT), "diff", "--name-only", f"{base}...HEAD"], capture_output=True, text=True)
    if run.returncode != 0:
        raise unittest.SkipTest("git cannot name the base ref")
    touched = [p for p in run.stdout.split() if p.startswith(("src/server/", "src/common/"))]
    assert not touched, f"a core file is in the diff: {touched}"


def check_pool_hold(sched, policy):
    assert "facts.inBg = player->InBattleground();" in sched, \
        "only a bot inside a match holds its session: a queue entry leaves with the logout"
    assert "InBattlegroundQueue" not in sched.split("facts.inBg =")[1].split(";")[0], \
        "a queued bot must never hold the pool lease"
    assert "kPoolBgHoldMs" in policy and "bot.sessionEndsMs = now + kPoolBgHoldMs;" in policy, \
        "a session that ended in a battleground is deferred, not spent"
    assert "bot.inBg" in policy.split("if (in.stopping)")[1].split("return out;")[0], \
        "a stop must skip a bot in a battleground"
    assert "bool inBg = false;" in policy


def check_w1_off_in_bg(mgr):
    body = mgr[mgr.index("void BotMgr::GhostRescue"):]
    life = mgr[mgr.index("void BotMgr::TrackLife"):]
    life = life[:life.index("\nvoid BotMgr::")]
    assert "IsBattleground()" in life and "GhostRescue(bot, me);" in life
    assert re.search(r"inBattleground = [^;]*IsBattleground\(\)", life), "the battleground test reads the map"
    assert "if (bot.free && !inBattleground)" in life, "the battleground test must guard the rescue"
    assert life.index("IsBattleground()") < life.index("GhostRescue(bot, me);")
    assert "ghostRescue.Reset()" in life


def check_create_veto(src):
    text = src["forever/src/host/bg/TbBgQueue.cpp"]
    assert "class BgWorldScript final : public WorldScript" in text, "the creation veto needs a WorldScript"
    assert "CanCreateBattleground(uint32 battlemasterListId, uint8 bracketId) override" in text, \
        "the hook must be the core's own CanCreateBattleground"
    assert "new BgWorldScript();" in text, "the world script must be registered"
    assert "BgVetoCreate(" in text, "the decision is the pure BgVetoCreate"
    body = text[text.index("bool CanCreate("):]
    body = body[:body.index("\n}")]
    assert "if (!s.enabled || !s.fillHumanQueues || !s.maxBots || !s.maxInstances)" in body and "return true;" in body, \
        "a disabled module (or fill, or a zero cap) must never veto"
    assert "bool const veto = BgVetoCreate(BucketFacts(" in body, "the veto reads the same bucket view as the fill"
    assert "!s.mapOn[int(map->kind) - 1]" in body, "a map this round does not manage is never held back"


def check_schedule_updates(src):
    text = src["forever/src/host/bg/TbBgQueue.cpp"]
    assert "sBattlegroundMgr->ScheduleQueueUpdate(" in text, "the core only re-evaluates a queue on an event"
    fill = text[text.index("void FillHumanQueues("):]
    fill = fill[:fill.index("\n// `.tb bg status`")]
    assert "ScheduleQueues(demands)" in fill, "every fill pass must schedule its buckets, also with no human demand"
    schedule = text[text.index("void ScheduleQueues("):]
    schedule = schedule[:schedule.index("\n}")]
    assert "for (auto const& [guid, track] : S().bots)" in schedule, \
        "a bucket with a bot still waiting is scheduled too"
    assert "if (!track.inside && !track.invited)" in schedule, \
        "a bot still waiting is scheduled even after its human entered, and a bound bot's too"
    assert "ScheduleQueueUpdate(" in schedule
    remove = text[text.index("void RemoveFromQueue("):]
    remove = remove[:remove.index("\n}")]
    assert "ScheduleQueueUpdate(" in remove, "a removed queue entry must be re-evaluated at once"


def check_queue_drop(src):
    text = src["forever/src/host/bg/TbBgQueue.cpp"]
    queued = text[text.index("void TickQueued("):text.index("struct BucketKey")]
    assert "BgQueuedDrop(" in queued and "orphanSinceMs" in queued, "a humanless bucket must drop the bot"
    assert "RemoveFromQueue(player, track);" in queued and "BgQueueDropWhy(drop)" in queued, \
        "the drop uses the core's remove pair and says why"
    policy = src["ai/playerbot/policy/bg/BgQueuePolicy.h"]
    assert "kBgDeficitMs = 180000" in policy and "kOrphanQueueGraceMs = 10000" in policy
    assert "kQueuedTimeoutMs = 3 * 180000" in policy


def check_demand_bracket(src):
    text = src["forever/src/host/bg/TbBgQueue.cpp"]
    fill = text[text.index("void FillHumanQueues("):]
    fill = fill[:fill.index("\n// `.tb bg status`")]
    assert "GetBattlegroundBracketById(map->mapId, BattlegroundBracketId(demand.bracket))" in fill, \
        "the fill must resolve the demand's bracket entry"
    assert "FillFacts(player, map, demandBracket, caps, facts);" in fill, \
        "the candidate is checked against the demand's bracket"
    assert "FillFacts(player, map, BracketOf(map, player), caps, facts);" not in fill, \
        "the bot's own bracket made inBracket trivially true"


def check_caps_hold_queued(src):
    text = src["forever/src/host/bg/TbBgQueue.cpp"]
    facts = text[text.index("std::vector<BgTrackFact> TrackFacts()"):]
    facts = facts[:facts.index("\n}")]
    assert "for (auto const& [guid, track] : S().bots)" in facts, "every tracked bot is a candidate for the count"
    assert "if (!track.ours)" in facts, "a bound bot's group entry is its human's, not the module's to cap"
    assert "BgTrackState::Queued" in facts and "BgTrackState::Invited" in facts and "BgTrackState::Inside" in facts
    assert "fact.demandPlaced = DemandInInstance(track.serves, track.listId);" in facts, \
        "a queue entry whose humans went into their instance reserves nothing"
    assert "BgQueueCaps CapsOfCurrentWorld(" in text and "BgCapsOfTracks(TrackFacts()" in text
    fill = text[text.index("void FillHumanQueues("):]
    fill = fill[:fill.index("\n// `.tb bg status`")]
    assert "std::vector<BgTrackFact> tracks = TrackFacts();" in fill, "the pass counts from one track view"
    assert "BgCapsOfTracks(tracks, s.maxInstances, s.maxBots, listId, bracket)" in fill, \
        "a candidate's caps say whether its own bucket is already counted"
    assert "tracks.push_back(fact);" in fill, "the pass must count each bot it queues"
    assert "track.serves = demand.humans;" in fill, "a queued bot names the humans whose queue it reserves"
    policy = src["ai/playerbot/policy/bg/BgQueuePolicy.h"]
    assert "bool ownBucketCounted = false;" in policy and \
        "!facts.caps.ownBucketCounted && facts.caps.instancesWithBots >= facts.caps.maxInstances" in policy, \
        "the instance cap must not hold a human's own bucket against him"
    assert "caps.instancesWithBots = std::uint32_t(instances.size() + queued.size());" in policy, \
        "a running match and a queued reservation are counted apart"
    assert "instances.emplace(track.listId, track.instance);" in policy, \
        "a running match is identified by its instance, never by its bracket"
    assert "caps.ownBucketCounted = queued.count({ candidateList, candidateBracket }) != 0;" in policy, \
        "only the bucket this fill reserves is exempt, never a match running in the same bracket"
    assert "bool demandPlaced = false;" in policy and "if (!track.demandPlaced)\n                queued.emplace" in policy, \
        "the instance count skips a leftover entry, keeping the bot count"


def check_bound_follows_human(src):
    text = src["forever/src/host/bg/TbBgQueue.cpp"]
    queued = text[text.index("void TickQueued("):text.index("struct BucketKey")]
    assert "FollowerHoldsQueue(botGuid, track)" in queued, "a bound bot's queue entry follows its human"
    assert "track.masterAtQueue" in queued, "an unbound bot (its master changed) leaves the queue"
    assert 'LogEvent("leave", track, "master-gone"' in queued, "the leave says why"
    policy = src["ai/playerbot/policy/bg/BgQueuePolicy.h"]
    assert "inline bool BgBoundQueueHeld(" in policy, "the follow rule is pure"
    assert "inline bool BgHumanInTrackMatch(" in policy, "the human's battleground check is pure"
    assert "BgHumanInTrackMatch(humanList, humanInstance, track.listId, track.instance)" in text, \
        "only the human's entry into this track's own match holds the queue"
    follow = text[text.index("bool FollowerHoldsQueue("):text.index("bool HumanInInstance(")]
    assert "BgBoundQueueHeld(!human.IsEmpty(), master != nullptr, inQueue, inTrackMatch)" in follow, \
        "the hold is the track match, not any battleground the human is in"
    join = text[text.index("void OnBattlegroundQueueJoin("):text.index("void OnBattlegroundInvite(")]
    assert "group->GetMembers()" in join and "track.follower = player->GetGUID();" in join, \
        "the group join tracks every bot member under its human"
    assert "track.masterAtQueue = BotMgr::Instance().MasterOf(" in join
    assert "BgMapInfo const* map = BgMapByList(bgTypeId);" in join and "if (!map || !map->enabled)" in join, \
        "a human joining a map the round does not manage (AV, random) is left alone"
    tick = text[text.index("void TickBots("):text.index("\nstruct Demand")]
    assert "TrackQueuedBots();" in tick, "a bound bot queued before the module looked is picked up too"
    pickup = text[text.index("void TrackQueuedBots()"):]
    pickup = pickup[:pickup.index("\n}")]
    assert "BotMgr::Instance().MasterOf(guid)" in pickup and "track.follower = master;" in pickup


def check_master_hold(mgr):
    for name in ("void BotMgr::DetachFromMaster", "bool BotMgr::UpdateMasterAway"):
        body = mgr[mgr.index(name):]
        body = body[:body.index("\n}")]
        assert "InBattleground()" in body, f"{name} must hold a bot inside a running match"
        assert "InBattlegroundQueue()" not in body, \
            f"{name} must not keep a bound bot online for a queue entry the module cleans up"


def check_accept_recheck(src):
    text = src["forever/src/host/bg/TbBgQueue.cpp"]
    accept = text[text.index("bool AcceptUsable("):]
    accept = accept[:accept.index("\n}")]
    assert "BgAcceptCapsOver(caps)" in accept, "Bg.MaxBots is enforced again at accept"
    assert "instancesWithBots" not in accept, \
        "an invitation enters an instance that already exists: the instance cap was paid when it was queued"
    assert "BgAcceptEligible(facts)" in accept, "only the refusals that still matter decline the invite"
    assert "BgQueueCaps const caps = CapsOfCurrentWorld();" in accept, "the caps are read from the whole world"
    policy = src["ai/playerbot/policy/bg/BgQueuePolicy.h"]
    accept_caps = policy[policy.index("inline bool BgAcceptCapsOver("):]
    accept_caps = accept_caps[:accept_caps.index("\n}")]
    assert "caps.botsInBattlegrounds > caps.maxBots" in accept_caps and "instancesWithBots" not in accept_caps, \
        "only Bg.MaxBots refuses an accept, and only once it dropped below what is held"
    queued = text[text.index("void TickQueued("):text.index("struct BucketKey")]
    assert "AcceptUsable(player, track)" in queued
    assert "bool const humanInside = free ? HumanInInstance(track.listId, track.instance)" in queued and \
        "MasterEnteredInstance(botGuid, track);" in queued, \
        "an invitation waits for the human who matters to be inside the instance, free bot or bound"
    assert "HumanInvitedToInstance" not in text, \
        "an invited human is not presence: his invite can lapse and leave the bots alone"
    assert "bool const deadlinePassed = BgInviteDeadlinePassed(now, track.inviteDeadlineMs);" in queued, \
        "a human who never enters gets the invitation declined at the core's window"
    assert "BgHumanEntryInviteAnswer(usable, humanInside, acceptDue, deadlinePassed)" in queued, \
        "one presence rule answers both a free and a bound bot's invitation"
    human = text[text.index("bool HumanInInstance("):]
    human = human[:human.index("\n}")]
    assert "player->InBattleground()" in human and "GetInstanceID() == instance" in human, \
        "a human is present as soon as his accepted instance id names the instance (the port ack race)"


def check_bound_master(src):
    text = src["forever/src/host/bg/TbBgQueue.cpp"]
    inside = text[text.index("void TickInside("):text.index("void TickQueued(")]
    assert "MasterEnteredInstance(player->GetGUID(), track)" in inside, "a bound bot tracks its human"
    assert "if (!track.leaving && BgMasterLeave(false, masterHere, away))" in inside, \
        "the bound bot leaves once the master grace ran out"
    assert 'LogEvent("leave", track, "master-gone"' in inside, "the leave says why"
    assert "masterAbsentSinceMs" in inside


def check_map_data_ready(src):
    data = src["forever/src/host/bg/TbBgMapData.cpp"]
    assert "!map.offWhy" not in data, "a pointer test on offWhy was always false: the missing-spawn checks never ran"
    assert "map.offWhy.empty()" in data, "the off reason must be read as a string"
    header = src["forever/src/host/bg/TbBgMapData.h"]
    assert "std::string offWhy;" in header, "a char const* offWhy made !offWhy compile as a null test"


def check_spirit_state(src):
    text = src["forever/src/host/bg/TbBgQueue.cpp"]
    assert "track.released = player->HasPlayerFlag(PLAYER_FLAGS_GHOST);" in text, \
        "a release is only done when the core set the ghost flag"
    assert "player->GetSpiritHealerGUID().IsEmpty()" in text, "the healer queue state is the core's own"
    assert "healerQueued" not in text, "a cached queue flag hid a refused healer request"


def check_teleport_track(src):
    text = src["forever/src/host/bg/TbBgQueue.cpp"]
    bots = text[text.index("void TickBots("):]
    bots = bots[:bots.index("\nstruct Demand")]
    assert "if (BotMgr::Instance().IsBot(guid))" in bots, "a logged-in bot mid-teleport must keep its track"
    assert bots.index("IsBot(guid)") < bots.index('LogEvent("leave", track, "logout");'), \
        "only a real logout writes the leave event"


class Bg1Wiring(unittest.TestCase):
    def test_queue_only_through_the_core(self):
        check_queue_only(sources())

    def test_accept_only_through_the_port_handler(self):
        check_port_only(sources())

    def test_leave_calls_carry_the_deserter_flag(self):
        check_leave_flags(sources())

    def test_no_float_literal(self):
        check_no_float_literal(sources())

    def test_ids_match_the_pinned_core(self):
        check_ids(sources())

    def test_disabled_does_no_hook_work(self):
        check_disabled_is_inert(sources(), CONF.read_text())

    def test_config_defaults_match_the_template(self):
        check_config_defaults(sources(), CONF.read_text())

    def test_slice_context_is_generated(self):
        check_slice_gen_clean()

    def test_no_core_file_touched(self):
        check_core_untouched()

    def test_pool_holds_a_battleground_session(self):
        check_pool_hold(SCHED.read_text(), SCHED_POLICY.read_text())

    def test_the_core_cannot_start_a_premature_match(self):
        check_create_veto(sources())

    def test_every_fill_pass_schedules_the_bucket(self):
        check_schedule_updates(sources())

    def test_a_humanless_bucket_drops_its_bots(self):
        check_queue_drop(sources())

    def test_candidates_use_the_demand_bracket(self):
        check_demand_bracket(sources())

    def test_caps_count_queued_and_invited_bots(self):
        check_caps_hold_queued(sources())

    def test_an_invitation_is_rechecked_at_accept(self):
        check_accept_recheck(sources())

    def test_a_bound_bot_follows_its_human(self):
        check_bound_master(sources())

    def test_a_bound_bot_leaves_the_queue_with_its_human(self):
        check_bound_follows_human(sources())

    def test_a_queue_entry_does_not_hold_a_bound_bot(self):
        check_master_hold(MGR.read_text())

    def test_a_map_without_its_spawns_is_off(self):
        check_map_data_ready(sources())

    def test_death_state_comes_from_the_core(self):
        check_spirit_state(sources())

    def test_a_bot_mid_teleport_keeps_its_track(self):
        check_teleport_track(sources())

    def test_w1_off_inside_a_battleground(self):
        check_w1_off_in_bg(MGR.read_text())

    def test_the_scripts_file_calls_the_round(self):
        text = SCRIPTS.read_text()
        for needle in ("TortoiseBots::bg::LoadConfig();", "TortoiseBots::bg::LoadBgMapData();",
                       "TortoiseBots::bg::Update(diff);", "TortoiseBots::bg::RegisterScripts();"):
            self.assertIn(needle, text)


@unittest.skipUnless(os.environ.get("BG_MUTATE") == "1", "mutations are run on purpose: BG_MUTATE=1")
class Bg1Mutations(unittest.TestCase):
    """Each check must fail on a broken tree: the mutation is the smallest change that removes the guarantee."""

    def assertFails(self, fn, *args):
        with self.assertRaises(AssertionError):
            fn(*args)

    def test_queue_mutation(self):
        mutated = sources()
        rel = "forever/src/host/bg/TbBgQueue.cpp"
        mutated[rel] = mutated[rel].replace("session->JoinBattlegroundQueue(queueId);", "/* queued */")
        self.assertFails(check_queue_only, mutated)

    def test_add_group_mutation(self):
        mutated = sources()
        rel = "forever/src/host/bg/TbBgQueue.cpp"
        mutated[rel] = mutated[rel].replace("session->JoinBattlegroundQueue(queueId);",
                                           "sBattlegroundMgr->GetBattlegroundQueue(queueId).AddGroup(player, nullptr, ALLIANCE, nullptr, false, 0);")
        self.assertFails(check_queue_only, mutated)

    def test_port_mutation(self):
        mutated = sources()
        rel = "forever/src/host/bg/TbBgQueue.cpp"
        mutated[rel] = mutated[rel].replace("session->HandleBattleFieldPortOpcode(packet);", "BattlegroundMgr::SendToBattleground(player, nullptr);")
        self.assertFails(check_port_only, mutated)

    def test_leave_flag_mutation(self):
        mutated = sources()
        rel = "forever/src/host/bg/TbBgQueue.cpp"
        mutated[rel] = mutated[rel].replace("LeaveBattleground(true, /*withoutDeserterDebuff*/ true);", "LeaveBattleground();")
        self.assertFails(check_leave_flags, mutated)

    def test_float_mutation(self):
        mutated = sources()
        rel = "forever/src/host/bg/TbBgMapData.cpp"
        mutated[rel] = mutated[rel].replace("out.x = position.GetPositionX();", "out.x = 1540.4f;")
        self.assertFails(check_no_float_literal, mutated)

    def test_enabled_default_mutation(self):
        mutated = sources()
        rel = "forever/src/host/bg/TbBgQueue.cpp"
        mutated[rel] = mutated[rel].replace('GetIntDefault("TortoiseBots.Bg.Enable", 0)', 'GetIntDefault("TortoiseBots.Bg.Enable", 1)')
        self.assertFails(check_disabled_is_inert, mutated, CONF.read_text())

    def test_config_default_mutation(self):
        mutated = sources()
        rel = "forever/src/host/bg/TbBgQueue.cpp"
        mutated[rel] = mutated[rel].replace('GetIntDefault("TortoiseBots.Bg.MaxBots", 50)', 'GetIntDefault("TortoiseBots.Bg.MaxBots", 40)')
        self.assertFails(check_config_defaults, mutated, CONF.read_text())

    def test_pool_mutation(self):
        policy = SCHED_POLICY.read_text().replace("bot.sessionEndsMs = now + kPoolBgHoldMs;", "")
        self.assertFails(check_pool_hold, SCHED.read_text(), policy)

    def test_w1_mutation(self):
        mgr = MGR.read_text().replace("if (bot.free && !inBattleground)", "if (bot.free)")
        self.assertFails(check_w1_off_in_bg, mgr)

    def mutate(self, rel, old, new):
        mutated = sources()
        assert old in mutated[rel], f"mutation target missing: {old}"
        mutated[rel] = mutated[rel].replace(old, new)
        return mutated

    def test_create_veto_mutation(self):
        self.assertFails(check_create_veto, self.mutate("forever/src/host/bg/TbBgQueue.cpp", "new BgWorldScript();", ""))

    def test_veto_zero_cap_mutation(self):
        self.assertFails(check_create_veto, self.mutate("forever/src/host/bg/TbBgQueue.cpp",
            "if (!s.enabled || !s.fillHumanQueues || !s.maxBots || !s.maxInstances)",
            "if (!s.enabled || !s.fillHumanQueues)"))

    def test_veto_body_mutation(self):
        self.assertFails(check_create_veto, self.mutate("forever/src/host/bg/TbBgQueue.cpp",
            "bool const veto = BgVetoCreate(BucketFacts(", "bool const veto = false && BgVetoCreate(BucketFacts("))

    def test_schedule_mutation(self):
        self.assertFails(check_schedule_updates, self.mutate("forever/src/host/bg/TbBgQueue.cpp",
            """    for (auto const& [guid, track] : S().bots)
        if (!track.inside && !track.invited)
            buckets.insert(BucketKey{ track.listId, track.bracket });
""", ""))

    def test_drop_mutation(self):
        self.assertFails(check_queue_drop, self.mutate("forever/src/host/bg/TbBgQueue.cpp",
            "BgQueueDrop const drop = BgQueuedDrop(humanWaiting, orphanMs, queuedMs);",
            "BgQueueDrop const drop = BgQueueDrop::None;"))

    def test_bracket_mutation(self):
        self.assertFails(check_demand_bracket, self.mutate("forever/src/host/bg/TbBgQueue.cpp",
            "FillFacts(player, map, demandBracket, caps, facts);",
            "FillFacts(player, map, BracketOf(map, player), caps, facts);"))

    def test_caps_mutation(self):
        self.assertFails(check_caps_hold_queued, self.mutate("forever/src/host/bg/TbBgQueue.cpp",
            "        if (!track.ours)\n            continue;", "        if (false)\n            continue;"))

    def test_caps_double_count_mutation(self):
        mutated = sources()
        rel = "ai/playerbot/policy/bg/BgQueuePolicy.h"
        mutated[rel] = mutated[rel].replace("instances.size() + queued.size()", "instances.size()")
        self.assertFails(check_caps_hold_queued, mutated)

    def test_own_bucket_mutation(self):
        mutated = sources()
        rel = "ai/playerbot/policy/bg/BgQueuePolicy.h"
        mutated[rel] = mutated[rel].replace(
            "caps.ownBucketCounted = queued.count({ candidateList, candidateBracket }) != 0;",
            "caps.ownBucketCounted = true;")
        self.assertFails(check_caps_hold_queued, mutated)

    def test_bracket_not_match_mutation(self):
        mutated = sources()
        rel = "ai/playerbot/policy/bg/BgQueuePolicy.h"
        mutated[rel] = mutated[rel].replace(
            "        instances.emplace(track.listId, track.instance);",
            "        instances.emplace(track.listId, track.bracket);")
        self.assertFails(check_caps_hold_queued, mutated)

    def test_accept_caps_mutation(self):
        self.assertFails(check_accept_recheck, self.mutate("forever/src/host/bg/TbBgQueue.cpp",
            "    if (BgAcceptCapsOver(caps))\n        return false;", ""))

    def test_accept_instance_cap_mutation(self):
        self.assertFails(check_accept_recheck, self.mutate("forever/src/host/bg/TbBgQueue.cpp",
            "    if (BgAcceptCapsOver(caps))",
            "    if (BgAcceptCapsOver(caps) || caps.instancesWithBots > caps.maxInstances)"))

    def test_leftover_reservation_mutation(self):
        mutated = sources()
        rel = "ai/playerbot/policy/bg/BgQueuePolicy.h"
        mutated[rel] = mutated[rel].replace(
            "            if (!track.demandPlaced)\n                queued.emplace(track.listId, track.bracket);",
            "            queued.emplace(track.listId, track.bracket);")
        self.assertFails(check_caps_hold_queued, mutated)

    def test_serves_mutation(self):
        self.assertFails(check_caps_hold_queued, self.mutate("forever/src/host/bg/TbBgQueue.cpp",
            "                    track.serves = demand.humans;\n", ""))

    def test_demand_placed_mutation(self):
        self.assertFails(check_caps_hold_queued, self.mutate("forever/src/host/bg/TbBgQueue.cpp",
            "            fact.demandPlaced = DemandInInstance(track.serves, track.listId);\n", ""))

    def test_botonly_accept_mutation(self):
        self.assertFails(check_accept_recheck, self.mutate("forever/src/host/bg/TbBgQueue.cpp",
            """        bool const humanInside = free ? HumanInInstance(track.listId, track.instance)
                                      : MasterEnteredInstance(botGuid, track);""",
            "        bool const humanInside = true;"))

    def test_invited_is_presence_mutation(self):
        self.assertFails(check_accept_recheck, self.mutate("forever/src/host/bg/TbBgQueue.cpp",
            "bool const humanInside = free ? HumanInInstance(track.listId, track.instance)",
            "bool const humanInside = free ? (HumanInInstance(track.listId, track.instance) || "
            "HumanInvitedToInstance(track.listId, track.instance))"))

    def test_bound_track_mutation(self):
        self.assertFails(check_bound_follows_human, self.mutate("forever/src/host/bg/TbBgQueue.cpp",
            """    auto const [humanList, humanInstance] = HumanBattleground(master);
    bool const inTrackMatch = BgHumanInTrackMatch(humanList, humanInstance, track.listId, track.instance);""",
            "    bool const inTrackMatch = master && master->InBattleground();"))

    def test_join_gate_mutation(self):
        self.assertFails(check_bound_follows_human, self.mutate("forever/src/host/bg/TbBgQueue.cpp",
            """        // Bg.Enable stays inert for a map the round does not manage (AV, random): the fill has nothing to queue there, and the
        // tracking below would only take the bots out of a queue whose invitations are never answered.
        BgMapInfo const* map = BgMapByList(bgTypeId);
        if (!map || !map->enabled)
            return;
""", ""))

    def test_master_entered_mutation(self):
        self.assertFails(check_accept_recheck, self.mutate("forever/src/host/bg/TbBgQueue.cpp",
            ": MasterEnteredInstance(botGuid, track);",
            ": true;"))

    def test_bound_follow_mutation(self):
        self.assertFails(check_bound_follows_human, self.mutate("forever/src/host/bg/TbBgQueue.cpp",
            "if ((unbound || (followsHuman && !FollowerHoldsQueue(botGuid, track))))", "if (false)"))

    def test_bound_pickup_mutation(self):
        self.assertFails(check_bound_follows_human, self.mutate("forever/src/host/bg/TbBgQueue.cpp",
            "    TrackQueuedBots();\n", ""))

    def test_master_hold_mutation(self):
        mgr = MGR.read_text().replace("return me && me->IsInWorld() && me->InBattleground();",
                                      "return me && me->IsInWorld() && (me->InBattleground() || me->InBattlegroundQueue());")
        self.assertFails(check_master_hold, mgr)

    def test_master_leave_mutation(self):
        self.assertFails(check_bound_master, self.mutate("forever/src/host/bg/TbBgQueue.cpp",
            "if (!track.leaving && BgMasterLeave(false, masterHere, away))",
            "if (false && BgMasterLeave(false, masterHere, away))"))

    def test_off_why_mutation(self):
        self.assertFails(check_map_data_ready, self.mutate("forever/src/host/bg/TbBgMapData.cpp",
            "if (map.offWhy.empty() && map.kind == BgMapKind::Wsg)", "if (!map.offWhy && map.kind == BgMapKind::Wsg)"))

    def test_release_mutation(self):
        self.assertFails(check_spirit_state, self.mutate("forever/src/host/bg/TbBgQueue.cpp",
            "track.released = player->HasPlayerFlag(PLAYER_FLAGS_GHOST);", "track.released = true;"))

    def test_teleport_mutation(self):
        self.assertFails(check_teleport_track, self.mutate("forever/src/host/bg/TbBgQueue.cpp",
            """            if (BotMgr::Instance().IsBot(guid))
            {
                ++itr;
                continue;
            }
""", ""))

    def test_pool_queue_mutation(self):
        sched = SCHED.read_text().replace(
            "facts.inBg = player->InBattleground();",
            "facts.inBg = player->InBattleground() || player->InBattlegroundQueue();")
        self.assertFails(check_pool_hold, sched, SCHED_POLICY.read_text())


if __name__ == "__main__":
    unittest.main()

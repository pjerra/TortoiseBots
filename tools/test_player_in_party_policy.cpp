// Standalone check: the bot-led run with real players in the party (1 to 4 players, the rest bots; one of them the
// bound commander). The bind gate's `inparty` binding, the binding's release rule, how a player's order is classed,
// and the run leader's player rules: wipe counting, the regroup's wait for players, the pull hold for a dead, far or
// absent player, no run order that names a player, the loot step and the party health.
//   g++ -std=c++17 -Wall -Wextra tools/test_player_in_party_policy.cpp -o /tmp/test_player_in_party_policy && /tmp/test_player_in_party_policy
#include "../runtime/DungeonRunPolicy.h"
#include "../runtime/RemoteCommanderPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace TortoiseBots::DungeonRun;
using ai::DungeonPlan;
using ai::PlanStep;
using ai::PlanStepKind;
namespace RC = TortoiseBots::RemoteCommander;

static PlanStep Step(PlanStepKind kind, float x = 0, float y = 0, float z = 0)
{
    PlanStep s;
    s.kind = kind;
    s.x = x;
    s.y = y;
    s.z = z;
    ai::NormalizePlanStep(s);
    return s;
}

static PlanStep PullStep(float x, std::vector<std::uint32_t> entries)
{
    PlanStep s;
    s.kind = PlanStepKind::Pull;
    s.x = x;
    s.entries = entries;
    ai::NormalizePlanStep(s);
    return s;
}

static MemberFact Mem(const std::string& name, float yd)
{
    MemberFact m;
    m.name = name;
    m.yd = yd;
    m.x = yd;
    return m;
}

static UnitFact Unit(std::uint64_t guid, std::uint32_t entry, float dist)
{
    UnitFact u;
    u.guid = guid;
    u.entry = entry;
    u.distLead = dist;
    return u;
}

static PlayerFact Player(const std::string& name, float yd, bool commander = false)
{
    PlayerFact p;
    p.name = name;
    p.yd = yd;
    p.commander = commander;
    return p;
}

// A party of `bots` bots (the lead bot "Tanky" among them) and the given players.
static Facts Party(std::uint32_t bots, std::vector<PlayerFact> players)
{
    Facts f;
    f.total = bots;
    f.alive = bots;
    f.carrier = "Tanky";
    f.seen = true;
    f.members = {Mem("Tanky", 0)};
    static const char* names[] = {"Healy", "Stabby", "Zappy", "Shooty"};
    for (std::uint32_t i = 1; i < bots && i <= 4; ++i)
        f.members.push_back(Mem(names[i - 1], 3));
    f.maxMemberYd = bots > 1 ? 3.0f : 0.0f;
    f.players = std::move(players);
    return f;
}

static bool Names(const std::string& order, const std::vector<PlayerFact>& players)
{
    for (const PlayerFact& p : players)
        if (order.find(p.name) != std::string::npos)
            return true;
    return false;
}

// ---------------------------------------------------------------------------
// The bind gate
// ---------------------------------------------------------------------------

static RC::BindMember Bot()
{
    RC::BindMember m;
    m.controllable = true;
    m.liveBot = true;
    return m;
}

static RC::BindFacts InPartyFacts(std::uint32_t players, std::uint32_t otherBots)
{
    RC::BindFacts f;
    f.enabled = true;
    f.lead = Bot();
    f.inParty = true;
    f.leadInCommanderParty = true;
    f.commanderLeadsParty = true;
    f.leadGrouped = true;
    f.partyPlayers = players;
    f.leadGroupHasRealPlayer = players > 1;
    for (std::uint32_t i = 0; i < otherBots; ++i)
        f.members.push_back(Bot());
    return f;
}

static void TestBindGate()
{
    // 1 player + 4 bots, 2 players + 3 bots, 4 players + the lead bot alone: bound.
    CHECK(RC::CheckBind(InPartyFacts(1, 3)) == RC::BindRefusal::None);
    CHECK(RC::CheckBind(InPartyFacts(2, 2)) == RC::BindRefusal::None);
    CHECK(RC::CheckBind(InPartyFacts(4, 0)) == RC::BindRefusal::None);
    // Six in all: too large.
    CHECK(RC::CheckBind(InPartyFacts(4, 1)) == RC::BindRefusal::GroupTooLarge);
    CHECK(RC::CheckBind(InPartyFacts(1, 4)) == RC::BindRefusal::GroupTooLarge);
    // The lead bot must be in the commander's own party.
    RC::BindFacts away = InPartyFacts(1, 2);
    away.leadInCommanderParty = false;
    CHECK(RC::CheckBind(away) == RC::BindRefusal::NotInParty);
    CHECK(std::string(RC::BindRefusalCode(RC::BindRefusal::NotInParty)) == "not-in-party");
    // Bob, a plain member of Alice's party, binds with inparty: refused before anything moves (the glue checks the
    // gate before ChangeLeader), so Alice keeps the lead.
    RC::BindFacts bob = InPartyFacts(2, 2);
    bob.commanderLeadsParty = false;
    CHECK(RC::CheckBind(bob) == RC::BindRefusal::NotLeader);
    CHECK(std::string(RC::BindRefusalCode(RC::BindRefusal::NotLeader)) == "not-leader");
    // A classic bind does not ask it (the commander leaves the party anyway).
    RC::BindFacts bobClassic = InPartyFacts(1, 2);
    bobClassic.inParty = false;
    bobClassic.commanderLeadsParty = false;
    CHECK(RC::CheckBind(bobClassic) == RC::BindRefusal::None);
    // A raid is still refused; every bot must still be named and the commander's.
    RC::BindFacts raid = InPartyFacts(2, 1);
    raid.leadGroupIsRaidOrBg = true;
    CHECK(RC::CheckBind(raid) == RC::BindRefusal::LeadGroupRaid);
    RC::BindFacts unnamed = InPartyFacts(2, 1);
    unnamed.keptPartyHasUnnamedBot = true;
    CHECK(RC::CheckBind(unnamed) == RC::BindRefusal::PartyUnnamed);
    RC::BindFacts foreign = InPartyFacts(2, 1);
    foreign.members[0].controllable = false;
    CHECK(RC::CheckBind(foreign) == RC::BindRefusal::MemberNotControllable);
    // Without `inparty` a party with another player is refused as before.
    RC::BindFacts classic = InPartyFacts(2, 1);
    classic.inParty = false;
    CHECK(RC::CheckBind(classic) == RC::BindRefusal::GroupHasPlayer);
    // ... and a classic bind with no named bot still needs one.
    RC::BindFacts lone = InPartyFacts(1, 0);
    lone.inParty = false;
    lone.leadGroupHasRealPlayer = false;
    CHECK(RC::CheckBind(lone) == RC::BindRefusal::NoMembers);

    // The word: last, any case, taken off the names.
    std::vector<std::string> names = {"Tanky", "Healy", "InParty"};
    CHECK(RC::TakeBindInParty(names));
    CHECK(names.size() == 2 && names[1] == "Healy");
    std::vector<std::string> plain = {"Tanky", "Healy"};
    CHECK(!RC::TakeBindInParty(plain));
    CHECK(plain.size() == 2);
    std::vector<std::string> only = {"inparty"};
    CHECK(RC::TakeBindInParty(only));
    CHECK(only.empty());
    // Only as the last argument: anywhere else it stays a name (and the bind finds no such bot).
    std::vector<std::string> middle = {"Tanky", "inparty", "Healy"};
    CHECK(!RC::TakeBindInParty(middle));
    CHECK(middle.size() == 3 && middle[1] == "inparty");
}

static void TestBindingRelease()
{
    RC::Binding classic;
    classic.commanderGuid = 1;
    classic.leadGuid = 2;
    classic.groupId = 7;
    RC::BindingState state;
    state.commanderOnline = true;
    state.leadOnline = true;
    state.leadGroupId = 7;
    state.leadIsLeader = true;
    state.memberCount = 5;
    // The commander in the group is no "player joined" (the glue does not count him).
    CHECK(RC::CheckBinding(classic, state) == RC::ReleaseReason::None);
    // Another player: a classic binding ends, an in-party one stands.
    state.realPlayerInGroup = true;
    CHECK(RC::CheckBinding(classic, state) == RC::ReleaseReason::PlayerJoined);
    RC::Binding inParty = classic;
    inParty.inParty = true;
    state.commanderInGroup = true;
    CHECK(RC::CheckBinding(inParty, state) == RC::ReleaseReason::None);
    // The commander leaves the bound group (he left, was kicked): released at once, the bots taken out; a classic
    // binding has him outside by design.
    state.commanderInGroup = false;
    CHECK(RC::CheckBinding(inParty, state) == RC::ReleaseReason::CommanderLeftParty);
    CHECK(RC::CheckBinding(classic, state) == RC::ReleaseReason::PlayerJoined);
    // ... a replacement player joining changes nothing: the commander is still not in it.
    state.memberCount = 5;
    CHECK(RC::CheckBinding(inParty, state) == RC::ReleaseReason::CommanderLeftParty);
    // ... he logs out or loses his connection (a logout takes him out of the party too).
    state.commanderOnline = false;
    CHECK(RC::CheckBinding(inParty, state) == RC::ReleaseReason::CommanderLeft);
    state.commanderOnline = true;
    // ... the group disbands (the lead bot has no group).
    state.leadGroupId = 0;
    CHECK(RC::CheckBinding(inParty, state) == RC::ReleaseReason::GroupChanged);
    state.leadGroupId = 7;
    state.commanderInGroup = true;
    // What the release does with the bots: out of the party when he is not in it, else they stay.
    // What the release does with the party (DecideReleaseParty, the glue's own call with what it reads).
    using A = RC::ReleasePartyAction;
    RC::ReleaseCommanderFacts here;
    here.inParty = true;
    here.reason = RC::ReleaseReason::Unbound;
    here.found = here.inWorld = here.hasSession = here.isMember = here.leadFound = true;
    CHECK(RC::DecideReleaseParty(here) == A::LeadToCommander); // unbind with him in the party
    RC::ReleaseCommanderFacts off = here;
    off.reason = RC::ReleaseReason::Disabled;
    CHECK(RC::DecideReleaseParty(off) == A::LeadToCommander);
    // Logged out: his guid still in the member slots, no session; the reason says he left.
    RC::ReleaseCommanderFacts loggedOut = here;
    loggedOut.reason = RC::ReleaseReason::CommanderLeft;
    loggedOut.hasSession = false;
    loggedOut.inWorld = false;
    CHECK(loggedOut.isMember);
    CHECK(RC::DecideReleaseParty(loggedOut) == A::BotsLeave);
    // The reason alone decides it, whatever the presence read says (a session read back between the check and here).
    RC::ReleaseCommanderFacts reasonOnly = here;
    reasonOnly.reason = RC::ReleaseReason::CommanderLeft;
    CHECK(RC::DecideReleaseParty(reasonOnly) == A::BotsLeave);
    // ... even with the character gone entirely.
    loggedOut.found = false;
    CHECK(RC::DecideReleaseParty(loggedOut) == A::BotsLeave);
    // Dropped his connection (the character in the world, a member, no network transport), whatever the reason.
    RC::ReleaseCommanderFacts dropped = here;
    dropped.hasSession = false;
    CHECK(RC::DecideReleaseParty(dropped) == A::BotsLeave);
    dropped.reason = RC::ReleaseReason::LeaderChanged;
    CHECK(RC::DecideReleaseParty(dropped) == A::BotsLeave);
    // Out of the world, out of the group, or the reason is his departure: the bots leave.
    RC::ReleaseCommanderFacts away = here;
    away.inWorld = false;
    CHECK(RC::DecideReleaseParty(away) == A::BotsLeave);
    RC::ReleaseCommanderFacts kicked = here;
    kicked.isMember = false;
    CHECK(RC::DecideReleaseParty(kicked) == A::BotsLeave);
    RC::ReleaseCommanderFacts left = here;
    left.reason = RC::ReleaseReason::CommanderLeftParty;
    CHECK(RC::DecideReleaseParty(left) == A::BotsLeave);
    // The lead bot gone (round 3): it logged out or was removed (LeadLeft), or it is not in the bound group any
    // more: the remaining bots leave the players' party, the commander there or not.
    RC::ReleaseCommanderFacts leadGone = here;
    leadGone.reason = RC::ReleaseReason::LeadLeft;
    leadGone.leadFound = false;
    CHECK(RC::DecideReleaseParty(leadGone) == A::BotsLeave);
    RC::ReleaseCommanderFacts leadLeftOnly = here;
    leadLeftOnly.reason = RC::ReleaseReason::LeadLeft;
    CHECK(RC::DecideReleaseParty(leadLeftOnly) == A::BotsLeave);
    RC::ReleaseCommanderFacts leadMissing = here;
    leadMissing.leadFound = false;
    CHECK(RC::DecideReleaseParty(leadMissing) == A::BotsLeave);
    // Which group: the lead's own, else the store by id, else a surviving member's; none: nothing to do.
    using G = RC::ReleaseGroupSource;
    RC::ReleaseGroupFacts groups;
    CHECK(RC::PickReleaseGroup(groups) == G::None);
    groups.groupViaMember = true;
    CHECK(RC::PickReleaseGroup(groups) == G::Member);
    groups.groupById = true;
    CHECK(RC::PickReleaseGroup(groups) == G::ById); // offline lead, online followers and players: found by id
    groups.leadGroupMatches = true;
    CHECK(RC::PickReleaseGroup(groups) == G::Lead);
    RC::ReleaseGroupFacts storeOnly;
    storeOnly.groupById = true;
    CHECK(RC::PickReleaseGroup(storeOnly) == G::ById);
    // A classic binding: the party is left alone, gone commander or not.
    RC::ReleaseCommanderFacts classicGone = loggedOut;
    classicGone.inParty = false;
    CHECK(RC::DecideReleaseParty(classicGone) == A::None);
    RC::ReleaseCommanderFacts classicHere = here;
    classicHere.inParty = false;
    CHECK(RC::DecideReleaseParty(classicHere) == A::None);
    CHECK(std::string(RC::ReleaseReasonText(RC::ReleaseReason::CommanderLeftParty)) == "the commander left the party");
    // A player taking the lead still ends it, and so does a raid.
    state.leadIsLeader = false;
    CHECK(RC::CheckBinding(inParty, state) == RC::ReleaseReason::LeaderChanged);
    state.leadIsLeader = true;
    state.leadGroupIsRaidOrBg = true;
    CHECK(RC::CheckBinding(inParty, state) == RC::ReleaseReason::GroupRaid);
    // The table keeps the flag.
    RC::BindingTable table;
    CHECK(table.Bind(inParty) == RC::BindResult::Bound);
    CHECK(table.FindByCommander(1) && table.FindByCommander(1)->inParty);
}

// ---------------------------------------------------------------------------
// A player's order to a run-bound party
// ---------------------------------------------------------------------------

static void TestOrderClass()
{
    using V = RC::RunOrderVerdict;
    // Not bound: as before.
    CHECK(RC::ClassifyRunBoundOrder(false, false, false, true, true) == V::Obey);
    CHECK(RC::ClassifyRunBoundOrder(false, false, false, false, true) == V::Obey);
    CHECK(RC::ClassifyRunBoundOrder(false, false, false, false, false) == V::Obey);
    // Bound, no run yet: the commander is obeyed; another player is refused from the bind on.
    CHECK(RC::ClassifyRunBoundOrder(true, false, false, true, true) == V::Obey);
    CHECK(RC::ClassifyRunBoundOrder(true, false, false, true, false) == V::Obey);
    CHECK(RC::ClassifyRunBoundOrder(true, false, false, false, true) == V::Refuse);
    CHECK(RC::ClassifyRunBoundOrder(true, false, false, false, false) == V::IgnoreChat);
    // The commander's explicit order (`.bot action`, a whisper): the run pauses, as before.
    CHECK(RC::ClassifyRunBoundOrder(true, true, true, true, true) == V::PauseRun);
    CHECK(RC::ClassifyRunBoundOrder(true, true, false, true, true) == V::PauseRun);
    // The commander's party chat: chatting, never a pause; taken as an order only while the run is paused.
    CHECK(RC::ClassifyRunBoundOrder(true, true, true, true, false) == V::IgnoreChat);
    CHECK(RC::ClassifyRunBoundOrder(true, true, false, true, false) == V::Obey);
    // Another player during a run: an explicit order is refused, chat is chat.
    CHECK(RC::ClassifyRunBoundOrder(true, true, true, false, true) == V::Refuse);
    CHECK(RC::ClassifyRunBoundOrder(true, true, false, false, true) == V::Refuse);
    CHECK(RC::ClassifyRunBoundOrder(true, true, true, false, false) == V::IgnoreChat);
    CHECK(RC::RunBoundRefusalText("Hypeer").find("Hypeer") != std::string::npos);
}

// ---------------------------------------------------------------------------
// The run leader
// ---------------------------------------------------------------------------

// The pull holds while a player is more than 60 yd from the lead bot, and goes out by itself once he is near.
static void TestPullHoldsForAFarPlayer()
{
    for (std::uint32_t players : {1u, 2u, 4u})
    {
        DungeonPlan plan;
        plan.steps = {PullStep(40, {4851})};
        Run r = Start(&plan);
        std::vector<PlayerFact> ps = {Player("Hypeer", 10, true)};
        if (players >= 2)
            ps.push_back(Player("Baerthe", 12));
        if (players >= 4)
        {
            ps.push_back(Player("Daddycool", 8));
            ps.push_back(Player("Sagiroth", 70)); // the far one
        }
        else
            ps.back().yd = 70;
        std::string const far = ps.back().name;
        Facts f = Party(5 - players, ps);
        f.units = {Unit(11, 4851, 38)};
        // Held past the step's idle ceiling (under the hold's own 5 min): no pull, no pause, a plain note naming him.
        for (int i = 0; i < 290; ++i)
        {
            std::string const order = Tick(r, f);
            CHECK(order.find("pullback") == std::string::npos);
            CHECK(!Names(order, f.players));
        }
        CHECK(r.state == State::Running);
        CHECK(r.note.find(far) != std::string::npos);
        CHECK(r.note.find("70") != std::string::npos);
        // Back within 60 yd: the pull goes out by itself.
        f.players.back().yd = 55;
        CHECK(Tick(r, f) == "pullback 11");
    }
}

// Exactly at the line is near (kPlayerPullYd), just past it is far.
static void TestPullHoldLine()
{
    PlanStep s = PullStep(40, {1});
    Run r;
    Facts f = Party(4, {Player("Hypeer", kPlayerPullYd, true)});
    CHECK(PlayerHoldNote(r, f, s).empty());
    f.players[0].yd = kPlayerPullYd + 0.5f;
    CHECK(!PlayerHoldNote(r, f, s).empty());
    // A walk holds only when its next point has a hop pull waiting.
    PlanStep move = Step(PlanStepKind::Move, 50);
    CHECK(PlayerHoldNote(r, f, move).empty());
    f.hopUnits = {Unit(21, 99, 10)};
    CHECK(!PlayerHoldNote(r, f, move).empty());
    f.hopUnits.clear();
    // Mid-pull, in a rest or a task: never held.
    r.phase = Phase::AwaitPull;
    CHECK(PlayerHoldNote(r, f, s).empty());
    r.phase = Phase::Rest;
    CHECK(PlayerHoldNote(r, f, s).empty());
    r.phase = Phase::Begin;
    // No player: never a hold.
    Facts bots = Party(5, {});
    CHECK(PlayerHoldNote(r, bots, s).empty());
}

// Off the plan's map (not in the instance) holds a pull too, whatever the distance reads.
static void TestPullHoldsForAPlayerOutside()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4851})};
    Run r = Start(&plan);
    Facts f = Party(3, {Player("Hypeer", 5, true), Player("Baerthe", 0)});
    f.players[1].onMap = false;
    f.units = {Unit(11, 4851, 38)};
    for (int i = 0; i < 5; ++i)
        CHECK(Tick(r, f).empty());
    CHECK(r.note.find("Baerthe") != std::string::npos);
    CHECK(r.note.find("not in the instance") != std::string::npos);
    f.players[1].onMap = true;
    CHECK(Tick(r, f) == "pullback 11");
}

// A dead player (a body or a ghost) holds every step, a walk too: the party stays where he fell, as for a bot.
// A far but living player does not hold a walk (no drag).
static void TestDeadPlayerHoldsEveryStep()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 50, 0, 0)};
    for (bool ghost : {false, true})
    {
        Run r = Start(&plan);
        Facts f = Party(4, {Player("Hypeer", 30, true)});
        f.players[0].alive = false;
        f.players[0].ghost = ghost;
        for (int i = 0; i < 290; ++i)
        {
            std::string const order = Tick(r, f);
            CHECK(order.empty()); // no walk, no `revive party` for a player, no summon
        }
        CHECK(r.state == State::Running);
        CHECK(r.note.find("Hypeer") != std::string::npos);
        CHECK(r.note.find(ghost ? "ghost" : "dead") != std::string::npos);
        f.players[0].alive = true;
        f.players[0].ghost = false;
        CHECK(Tick(r, f) == "move 50.0 0.0 0.0");
    }
    // Alive, 200 yd behind: the walk goes on.
    Run walk = Start(&plan);
    Facts far = Party(3, {Player("Hypeer", 200, true), Player("Baerthe", 2)});
    CHECK(Tick(walk, far) == "move 50.0 0.0 0.0");
}

// In a fight nothing is held: the party fights on whoever lies dead.
static void TestNoHoldInAFight()
{
    PlanStep s = PullStep(40, {1});
    Run r;
    Facts f = Party(4, {Player("Hypeer", 90, true)});
    f.players[0].alive = false;
    f.anyInCombat = true;
    CHECK(PlayerHoldNote(r, f, s).empty());
}

// A wipe is every member dead, the players too; the near wipe counts dead players.
static void TestWipeCounting()
{
    // 1 player + 4 bots: every bot dead, the player alive: a near wipe, no wipe.
    Facts f = Party(4, {Player("Hypeer", 5, true)});
    f.dead = 4;
    f.alive = 0;
    CHECK(PartyDead(f) == 4);
    CHECK(!PartyAllDead(f));
    f.players[0].alive = false;
    CHECK(PartyDead(f) == 5);
    CHECK(PartyAllDead(f));
    // No player: as before (all bots dead is a wipe).
    Facts bots = Party(5, {});
    bots.dead = 5;
    bots.alive = 0;
    CHECK(PartyAllDead(bots));

    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40), PullStep(60, {1})};
    Run r = Start(&plan);
    Facts p = Party(4, {Player("Hypeer", 5, true)});
    CHECK(Tick(r, p) == "move 40.0 0.0 0.0");
    p.dead = 4;
    p.alive = 0;
    p.members.clear();
    p.leadAlive = false;
    p.deadNames = {"Tanky", "Healy", "Stabby", "Zappy"};
    Tick(r, p);
    CHECK(r.wipes == 0);
    CHECK(r.nearWipes == 1);
    p.players[0].alive = false;
    Tick(r, p);
    CHECK(r.wipes == 1);

    // 4 players + 1 bot: three players dead is a near wipe (the bot counts alone otherwise).
    Run q = Start(&plan);
    Facts four = Party(1, {Player("Hypeer", 5, true), Player("Baerthe", 5), Player("Daddycool", 5), Player("Sagiroth", 5)});
    CHECK(Tick(q, four) == "move 40.0 0.0 0.0");
    for (int i = 1; i < 4; ++i)
        four.players[static_cast<std::size_t>(i)].alive = false;
    CHECK(PartyDead(four) == 3);
    Tick(q, four);
    CHECK(q.nearWipes == 1);
    CHECK(q.wipes == 0);

    // 2 players + 3 bots: one bot and both players dead: three dead, a near wipe.
    Facts two = Party(3, {Player("Hypeer", 5, true), Player("Baerthe", 5)});
    two.dead = 1;
    two.alive = 2;
    two.players[0].alive = false;
    two.players[1].alive = false;
    CHECK(PartyDead(two) == 3);
    CHECK(!PartyAllDead(two));
}

// After a regroup the bots are up and gathered: the regroup waits for every living player within kGatherYd, at
// most kPlayerGatherWaitMs, then pauses naming who and how far.
static Run RegroupedRun(const DungeonPlan& plan, Facts& f, std::vector<PlayerFact> players)
{
    Run r = Start(&plan);
    f = Party(5 - static_cast<std::uint32_t>(players.size()), players);
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    // A pause and `.bot run resume`: the resume regroups (the bots are up and near the lead bot).
    Pause(r, "test");
    CHECK(Resume(r));
    CHECK(r.regroup);
    return r;
}

static void TestRegroupWaitsForPlayers()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40), PullStep(60, {1})};
    for (std::uint32_t players : {1u, 2u, 4u})
    {
        std::vector<PlayerFact> ps = {Player("Hypeer", 3, true)};
        for (std::uint32_t i = 1; i < players; ++i)
            ps.push_back(Player(i == 1 ? "Baerthe" : i == 2 ? "Daddycool" : "Sagiroth", 4));
        ps.back().yd = 40;
        std::string const far = ps.back().name;
        Facts f;
        Run r = RegroupedRun(plan, f, ps);
        // Waited for: no order, the regroup stands.
        for (std::uint32_t ms = 0; ms + kTickMs < kPlayerGatherWaitMs; ms += kTickMs)
        {
            std::string const order = Tick(r, f);
            CHECK(!Names(order, f.players));
            CHECK(r.regroup);
            CHECK(r.state == State::Running);
        }
        CHECK(r.note.find(far) != std::string::npos);
        // The ceiling: a pause that names him and how far.
        for (int i = 0; i < 3 && r.state == State::Running; ++i)
            Tick(r, f);
        CHECK(r.state == State::Paused);
        CHECK(r.note.find(far) != std::string::npos);
        CHECK(r.note.find("40") != std::string::npos);
    }
    // He comes within 20 yd: the regroup ends.
    Facts f;
    Run r = RegroupedRun(plan, f, {Player("Hypeer", 35, true), Player("Baerthe", 4)});
    for (int i = 0; i < 5; ++i)
        Tick(r, f);
    CHECK(r.regroup);
    f.players[0].yd = 12;
    Tick(r, f);
    CHECK(!r.regroup);
    // A dead player is a dead member: the regroup waits for him (a resurrection, his corpse run), nothing ordered to
    // him, at most kPlayerHoldCapMs, then a pause naming him.
    Facts g;
    Run d = RegroupedRun(plan, g, {Player("Hypeer", 300, true)});
    g.players[0].alive = false;
    g.players[0].ghost = true;
    for (std::uint32_t ms = 0; ms + kTickMs < kPlayerHoldCapMs; ms += kTickMs)
    {
        CHECK(Tick(d, g).empty());
        CHECK(d.regroup && d.state == State::Running);
    }
    Tick(d, g);
    CHECK(d.state == State::Paused);
    CHECK(d.note.find("Hypeer") != std::string::npos && d.note.find("ghost") != std::string::npos);
    CHECK(d.note.find("300") != std::string::npos);
    // ... up and back within 20 yd: the regroup ends.
    Run e = RegroupedRun(plan, g, {Player("Hypeer", 3, true)});
    g.players[0].alive = false;
    Tick(e, g);
    CHECK(e.regroup);
    g.players[0].alive = true;
    Tick(e, g);
    CHECK(!e.regroup);
}

// A player far away is never summoned, walked or revived by name, also while bots are summoned.
static void TestNoOrderNamesAPlayer()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40), PullStep(60, {1})};
    Facts f;
    Run r = RegroupedRun(plan, f, {Player("Hypeer", 270, true)});
    // A revived bot far away is summoned; the far player never is.
    f.dead = 1;
    f.alive = 3;
    f.deadNames = {"Healy"};
    f.members = {Mem("Tanky", 0), Mem("Stabby", 2), Mem("Zappy", 2)};
    f.maxMemberYd = 2;
    CHECK(Tick(r, f) == "revive party");
    f.dead = 0;
    f.alive = 4;
    f.deadNames.clear();
    f.members = {Mem("Tanky", 0), Mem("Healy", 270), Mem("Stabby", 2), Mem("Zappy", 2)};
    f.maxMemberYd = 270;
    bool summoned = false;
    for (int i = 0; i < 200 && r.state == State::Running; ++i)
    {
        std::string const order = Tick(r, f);
        CHECK(!Names(order, f.players));
        summoned = summoned || order == "summon Healy";
        if (order == "summon Healy")
        {
            f.members[1].yd = 2;
            f.maxMemberYd = 2;
        }
    }
    CHECK(summoned);
    // A dead player alone never brings a `revive party` (it revives bots only; the player releases himself).
    Run alone = Start(&plan);
    Facts p = Party(4, {Player("Hypeer", 3, true)});
    CHECK(Tick(alone, p) == "move 40.0 0.0 0.0");
    p.players[0].alive = false;
    for (int i = 0; i < 100; ++i)
        CHECK(Tick(alone, p).find("revive") == std::string::npos);
}

// Loot: the group's method is the players' (no free-for-all detour); a player who carries the item ends the step;
// a player's round-robin turn is named in the give-up pause.
static void TestLoot()
{
    PlanStep s = Step(PlanStepKind::Loot, 10, 0, 0);
    s.entry = 7228;
    s.item = 7741;
    Run r;
    Facts f = Party(4, {Player("Hypeer", 3, true)});
    f.lootFfaOn = true;
    f.leadLeadsGroup = true;
    f.lootMethod = RC::kLootRoundRobin;
    CHECK(!LootFfaWanted(r, f, s));
    Facts bots = Party(5, {});
    bots.lootFfaOn = true;
    bots.leadLeadsGroup = true;
    bots.lootMethod = RC::kLootRoundRobin;
    CHECK(LootFfaWanted(r, bots, s)); // the old rule without a player

    DungeonPlan plan;
    plan.steps = {s, Step(PlanStepKind::Move, 20)};
    Run run = Start(&plan);
    Facts held = Party(3, {Player("Hypeer", 3, true), Player("Baerthe", 5)});
    held.itemPlayer = "Baerthe";
    Tick(run, held);
    CHECK(run.step == 1);
    CHECK(run.note.find("Baerthe") != std::string::npos);

    Run given = Start(&plan);
    given.lootRefusal = "refused: not your turn";
    Facts turn = Party(4, {Player("Hypeer", 3, true)});
    turn.lootTurnPlayer = "Hypeer";
    LootGivenUp(given, turn, s);
    CHECK(given.state == State::Paused);
    CHECK(given.note.find("Hypeer") != std::string::npos);
    CHECK(given.note.find("7741") != std::string::npos);
}

static void TestPartyHealth()
{
    Facts f = Party(4, {Player("Hypeer", 3, true), Player("Baerthe", 3)});
    f.minHealthPct = 80;
    f.players[0].healthPct = 35;
    f.players[1].healthPct = 20;
    f.players[1].alive = false; // a dead one is not resting
    ApplyPlayerFacts(f);
    CHECK(f.minHealthPct == 35);
    CHECK(!f.anyInCombat);
    Facts none = Party(5, {});
    none.minHealthPct = 80;
    ApplyPlayerFacts(none);
    CHECK(none.minHealthPct == 80);
}

// One tick as the glue runs it: the players folded into the facts (ApplyPlayerFacts), then Tick.
static std::string PTick(Run& r, const Facts& in)
{
    Facts f = in;
    ApplyPlayerFacts(f);
    return Tick(r, f);
}

// A living player in a fight puts the party in it: no walk, no hold, no regroup while he fights (a dead one does not).
static void TestPlayerInCombat()
{
    Facts f = Party(4, {Player("Hypeer", 30, true)});
    f.players[0].inCombat = true;
    ApplyPlayerFacts(f);
    CHECK(f.anyInCombat);
    Facts dead = Party(4, {Player("Hypeer", 30, true)});
    dead.players[0].inCombat = true;
    dead.players[0].alive = false;
    ApplyPlayerFacts(dead);
    CHECK(!dead.anyInCombat);
    // The hold never fires on a player who fights, even without the fold.
    Facts raw = Party(3, {Player("Hypeer", 90, true), Player("Baerthe", 5)});
    raw.players[1].inCombat = true;
    Run r;
    CHECK(PlayerHoldNote(r, raw, PullStep(40, {1})).empty());

    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 50, 0, 0), Step(PlanStepKind::Move, 90, 0, 0)};
    Run walk = Start(&plan);
    Facts w = Party(3, {Player("Hypeer", 5, true), Player("Baerthe", 5)});
    w.players[1].inCombat = true;
    for (int i = 0; i < 5; ++i)
        CHECK(PTick(walk, w).find("move") == std::string::npos);
    w.players[1].inCombat = false;
    CHECK(PTick(walk, w) == "move 50.0 0.0 0.0");
}

// Mixed deaths, end to end (the party's dead drive the wipe regroup, review round 1).
static void TestMixedDeaths()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40), PullStep(60, {1})};

    // 1 player + 4 bots: two bots and the player die: three dead, the run regroups by itself once, revives the bots,
    // waits for the player and goes on; no second regroup while he was down.
    {
        Run r = Start(&plan);
        Facts f = Party(4, {Player("Hypeer", 5, true)});
        CHECK(PTick(r, f) == "move 40.0 0.0 0.0");
        f.dead = 2;
        f.alive = 2;
        f.deadNames = {"Stabby", "Zappy"};
        f.members = {Mem("Tanky", 0), Mem("Healy", 3)};
        f.players[0].alive = false;
        PTick(r, f);
        CHECK(r.regroup);
        CHECK(r.autoRegroups == 1);
        CHECK(r.note.find("3 dead") != std::string::npos);
        CHECK(PTick(r, f) == "revive party");
        f.dead = 0;
        f.alive = 4;
        f.deadNames.clear();
        f.members = {Mem("Tanky", 0), Mem("Healy", 3), Mem("Stabby", 3), Mem("Zappy", 3)};
        for (int i = 0; i < 30; ++i)
        {
            std::string const order = PTick(r, f);
            CHECK(!Names(order, f.players));
            CHECK(order.find("revive") == std::string::npos);
        }
        CHECK(r.regroup && r.state == State::Running && r.autoRegroups == 1);
        CHECK(r.note.find("Hypeer") != std::string::npos);
        f.players[0].alive = true;
        PTick(r, f);
        CHECK(!r.regroup);
        CHECK(r.autoRegroups == 1);
    }

    // 4 players + 1 bot: the lead bot dies while the players fight: no regroup, no revive until the fight is over.
    {
        Run r = Start(&plan);
        Facts f = Party(1, {Player("Hypeer", 5, true), Player("Baerthe", 5), Player("Daddycool", 5), Player("Sagiroth", 5)});
        CHECK(PTick(r, f) == "move 40.0 0.0 0.0");
        f.dead = 1;
        f.alive = 0;
        f.leadAlive = false;
        f.members.clear();
        f.deadNames = {"Tanky"};
        for (PlayerFact& p : f.players)
            p.inCombat = true;
        for (int i = 0; i < 10; ++i)
            CHECK(PTick(r, f).find("revive") == std::string::npos);
        CHECK(!r.regroup);
        for (PlayerFact& p : f.players)
            p.inCombat = false;
        PTick(r, f);
        CHECK(r.regroup);
        CHECK(PTick(r, f) == "revive party");
    }

    // 2 players + 3 bots: one bot and both players dead: three dead, a regroup; a third death during it starts a
    // new one only when the count grows.
    {
        Run r = Start(&plan);
        Facts f = Party(3, {Player("Hypeer", 5, true), Player("Baerthe", 5)});
        CHECK(PTick(r, f) == "move 40.0 0.0 0.0");
        f.dead = 1;
        f.alive = 2;
        f.deadNames = {"Stabby"};
        f.members = {Mem("Tanky", 0), Mem("Healy", 3)};
        f.players[0].alive = false;
        f.players[1].alive = false;
        PTick(r, f);
        CHECK(r.regroup && r.autoRegroups == 1);
        CHECK(r.regroupDead < 0 || r.regroupDead == 3);
        PTick(r, f);
        CHECK(r.regroupDead == 3);
        CHECK(r.wipes == 0 && r.nearWipes == 1);
    }
}

// pip S5 (2026-10-26): every bot dead and a GameMaster commander still fighting at the hp clamp: his fight never ended,
// the wipe's regroup never revived the bots and the run noted "3 dead: fighting on, a regroup follows" until its
// regroups ran out. With every bot dead and only GameMasters fighting, their fight is not the party's: the regroup
// revives the bots (`revive party`). A real player's fight still holds it, as does any bot alive.
static void TestGmFightOverDeadBots()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40), PullStep(60, {1})};
    // The failing case: 3 bots dead, Pipmira (GM) fighting at the clamp, Pipwren dead.
    {
        Run r = Start(&plan);
        Facts f = Party(3, {Player("Pipmira", 5, true), Player("Pipwren", 5)});
        CHECK(PTick(r, f) == "move 40.0 0.0 0.0");
        f.dead = 3;
        f.alive = 0;
        f.leadAlive = false;
        f.members.clear();
        f.deadNames = {"Tanky", "Healy", "Stabby"};
        f.players[0].gm = true;
        f.players[0].inCombat = true;
        f.players[0].healthPct = 0;
        f.players[1].alive = false;
        PTick(r, f);
        CHECK(r.regroup && r.autoRegroups == 1);
        CHECK(r.note.find("fighting on") == std::string::npos);
        CHECK(PTick(r, f) == "revive party");
    }
    // Control: the same fight by a real player still holds the wipe's regroup and revive.
    {
        Run r = Start(&plan);
        Facts f = Party(3, {Player("Pipmira", 5, true), Player("Pipwren", 5)});
        CHECK(PTick(r, f) == "move 40.0 0.0 0.0");
        f.dead = 3;
        f.alive = 0;
        f.leadAlive = false;
        f.members.clear();
        f.deadNames = {"Tanky", "Healy", "Stabby"};
        f.players[0].gm = false;
        f.players[0].inCombat = true;
        f.players[0].healthPct = 0;
        f.players[1].alive = false;
        for (int i = 0; i < 10; ++i)
            CHECK(PTick(r, f).find("revive") == std::string::npos);
        CHECK(!r.regroup);
    }
    // Mixed: Pipwren alive and fighting too (not a GM) still holds it.
    {
        Run r = Start(&plan);
        Facts f = Party(3, {Player("Pipmira", 5, true), Player("Pipwren", 5)});
        CHECK(PTick(r, f) == "move 40.0 0.0 0.0");
        f.dead = 3;
        f.alive = 0;
        f.leadAlive = false;
        f.members.clear();
        f.deadNames = {"Tanky", "Healy", "Stabby"};
        f.players[0].gm = true;
        f.players[0].inCombat = true;
        f.players[0].healthPct = 0;
        f.players[1].alive = true;
        f.players[1].inCombat = true;
        for (int i = 0; i < 10; ++i)
            CHECK(PTick(r, f).find("revive") == std::string::npos);
        CHECK(!r.regroup);
    }
    // A bot alive: the GM's fight still counts as the party's fight.
    {
        Facts f = Party(3, {Player("Pipmira", 5, true), Player("Pipwren", 5)});
        f.dead = 3;
        f.alive = 1;
        f.players[0].gm = true;
        f.players[0].inCombat = true;
        Facts copy = f;
        ApplyPlayerFacts(copy);
        CHECK(copy.anyInCombat);
    }
    // Direct unit checks: no bots read, and a GM alive but not fighting, are not the override.
    {
        Facts empty = Party(0, {Player("Pipmira", 5, true)});
        empty.players[0].gm = true;
        empty.players[0].inCombat = true;
        CHECK(!OnlyGmFightOverDeadBots(empty));
        Facts quiet = Party(3, {Player("Pipmira", 5, true), Player("Pipwren", 5)});
        quiet.dead = 3;
        quiet.alive = 0;
        quiet.players[0].gm = true;
        CHECK(!OnlyGmFightOverDeadBots(quiet));
    }
}


// Every player hold is bounded: kPlayerHoldCapMs, then a pause naming the player, the reason and the distance.
static void TestHoldCap()
{
    struct Case { const char* what; bool alive; bool ghost; bool onMap; bool online; float yd; const char* word; };
    Case const cases[] = {
        {"dead", false, false, true, true, 25, "dead"},
        {"ghost", false, true, false, true, 1000, "ghost"},
        {"outside", true, false, false, true, 1000, "not in the instance"},
        {"offline", true, false, false, false, 1000, "offline"},
        {"far", true, false, true, true, 75, "75"},
    };
    for (const Case& c : cases)
    {
        DungeonPlan plan;
        plan.steps = {PullStep(40, {4851})};
        Run r = Start(&plan);
        Facts f = Party(3, {Player("Hypeer", 5, true), Player("Baerthe", c.yd)});
        PlayerFact& b = f.players[1];
        b.alive = c.alive;
        b.ghost = c.ghost;
        b.onMap = c.onMap;
        b.online = c.online;
        f.units = {Unit(11, 4851, 38)};
        for (std::uint32_t ms = 0; ms + kTickMs < kPlayerHoldCapMs; ms += kTickMs)
        {
            CHECK(PTick(r, f).empty());
            CHECK(r.state == State::Running);
        }
        PTick(r, f);
        CHECK(r.state == State::Paused);
        CHECK(r.note.find("Baerthe") != std::string::npos);
        CHECK(r.note.find(c.word) != std::string::npos);
        CHECK(r.note.find("5 min") != std::string::npos);
    }
    // The clock starts over once the player is back.
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4851})};
    Run r = Start(&plan);
    Facts f = Party(4, {Player("Hypeer", 75, true)});
    f.units = {Unit(11, 4851, 38)};
    for (int i = 0; i < 200; ++i)
        PTick(r, f);
    CHECK(r.playerHoldMs >= 199000);
    f.players[0].yd = 10;
    CHECK(PTick(r, f) == "pullback 11");
    CHECK(r.playerHoldMs == 0);
}

// The loot step's "a player carries it" end only when no later step needs a bot to hold that item.
static void TestLootItemNeededLater()
{
    PlanStep loot = Step(PlanStepKind::Loot, 10, 0, 0);
    loot.entry = 7228;
    loot.item = 7741;
    PlanStep open = Step(PlanStepKind::Open, 30, 0, 0);
    open.entry = 124367;
    open.item = 7741;
    DungeonPlan plan;
    plan.steps = {loot, Step(PlanStepKind::Move, 20), open};
    Run r = Start(&plan);
    CHECK(LaterStepNeedsItem(r, 7741));
    CHECK(!LaterStepNeedsItem(r, 1234));
    Facts f = Party(3, {Player("Hypeer", 3, true), Player("Baerthe", 5)});
    f.itemPlayer = "Baerthe";
    std::string const order = Tick(r, f);
    CHECK(r.step == 0); // not ended: the bots go on as before
    CHECK(order.compare(0, 5, "loot ") == 0);
    // use_item names the item in its entry, any task step in its param.
    PlanStep use = Step(PlanStepKind::UseItem, 30, 0, 0);
    use.entry = 7741;
    DungeonPlan p2;
    p2.steps = {loot, use};
    CHECK(LaterStepNeedsItem(Start(&p2), 7741));
    PlanStep useOn = Step(PlanStepKind::UseOn, 30, 0, 0);
    useOn.param = 7741;
    DungeonPlan p3;
    p3.steps = {loot, useOn};
    CHECK(LaterStepNeedsItem(Start(&p3), 7741));
    // An earlier step does not count.
    DungeonPlan p4;
    p4.steps = {open, loot};
    Run late = Start(&p4, 1);
    CHECK(!LaterStepNeedsItem(late, 7741));
    // The give-up pause tells him to hand it over.
    Run given = Start(&plan);
    given.lootRefusal = "refused";
    Facts g = Party(4, {Player("Hypeer", 3, true)});
    g.itemPlayer = "Hypeer";
    LootGivenUp(given, g, loot);
    CHECK(given.note.find("trade it to Tanky") != std::string::npos);
    // No carrier read: never "trade it to , then".
    Run blank = Start(&plan);
    blank.lootRefusal = "refused";
    g.carrier.clear();
    LootGivenUp(blank, g, loot);
    CHECK(blank.note.find("to , then") == std::string::npos);
    CHECK(blank.note.find("trade it to the lead bot") != std::string::npos);
    // The bots tried stay listed where they were, also when a player carries the item.
    Run tried = Start(&plan);
    tried.lootRefusal = "refused";
    tried.lootRights = true;
    tried.lootTried = {"Tanky", "Healy"};
    g.carrier = "Tanky";
    LootGivenUp(tried, g, loot);
    std::size_t const list = tried.note.find("(tried Tanky Healy)");
    CHECK(list != std::string::npos);
    CHECK(list < tried.note.find("trade it to Tanky"));
}

int main()
{
    TestBindGate();
    TestBindingRelease();
    TestOrderClass();
    TestPullHoldsForAFarPlayer();
    TestPullHoldLine();
    TestPullHoldsForAPlayerOutside();
    TestDeadPlayerHoldsEveryStep();
    TestNoHoldInAFight();
    TestWipeCounting();
    TestRegroupWaitsForPlayers();
    TestNoOrderNamesAPlayer();
    TestLoot();
    TestPartyHealth();
    TestPlayerInCombat();
    TestMixedDeaths();
    TestGmFightOverDeadBots();
    TestHoldCap();
    TestLootItemNeededLater();
    std::cout << "test_player_in_party_policy: all checks passed\n";
    return 0;
}

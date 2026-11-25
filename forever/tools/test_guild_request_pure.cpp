// Pure tests for the join-by-whisper request rules (TbHostGuildRequestPure.h, TBF #30).
// Build & run:
//   g++ -std=c++17 -I. -Iforever/src forever/tools/test_guild_request_pure.cpp -o /tmp/test_guild_request_pure && /tmp/test_guild_request_pure
#include "host/TbHostGuildInvitePure.h"
#include "host/TbHostGuildJoinPure.h"
#include "host/TbHostGuildRequestPure.h"

#include <cassert>
#include <cstring>
#include <string>
#include <vector>

using namespace tbhost::guildjoin;

namespace
{
// Every check passed: a request from a guildless same-side player to a bot-run guild that has room and an inviter.
RequestFacts Ready()
{
    RequestFacts facts;
    facts.featureOn = true;
    facts.isRequest = true;
    facts.botInGuild = true;
    facts.guildBotRun = true;
    facts.sameTeam = true;
    facts.memberCount = 10;
    facts.inviterFound = true;
    return facts;
}

void RejectsOrdinaryChat()
{
    for (char const* line : { "", " ", "hello", "guild invite", "guild invite Bob", "can i join your raid",
                              "can i join your guild now", "invite me to your guild tomorrow", "ginv b", "inv",
                              "invite", "guild", "can i join you?", "invite me to the raid" })
        assert(!IsJoinRequest(line));

    // The owner's management verbs are never requests (the owner path stays the only answer to them).
    for (char const* line : { "guild invite", "guild invite Bob", "guild join", "guild leave", "guild promote Bob",
                              "guild demote Bob", "guild remove Bob", "guild leader Bob" })
    {
        assert(ParseGuildWhisper(line).command != Command::None);
        assert(!IsJoinRequest(line));
    }

    // A whole line only: a request with words around it is chatter.
    assert(!IsJoinRequest("please ginv"));
    assert(!IsJoinRequest("can i join your guild please now"));
}

void RecognisesPhrases()
{
    // The ten phrases, whole line.
    for (char const* line : { "ginv", "ginvite", "guild inv", "can i join", "can i join your guild",
                              "can i join the guild", "invite me", "invite me to your guild",
                              "invite me to the guild", "inv me to guild" })
        assert(IsJoinRequest(line));

    // Case, spacing, trailing marks and a trailing please/pls.
    assert(IsJoinRequest("GINV"));
    assert(IsJoinRequest("  Ginv?  "));
    assert(IsJoinRequest("Ginvite!"));
    assert(IsJoinRequest("Guild   Inv"));
    assert(IsJoinRequest("Can I Join Your Guild?"));
    assert(IsJoinRequest("invite me."));
    assert(IsJoinRequest("Invite Me To The Guild?!"));
    assert(IsJoinRequest("invite me please"));
    assert(IsJoinRequest("invite me pls"));
    assert(IsJoinRequest("invite me to your guild please?"));
    assert(IsJoinRequest("invite me to your guild pls!"));
    assert(IsJoinRequest("can I join the guild PLEASE."));
    assert(IsJoinRequest("inv me to guild pls"));
}

void VerdictOrder()
{
    assert(DecideRequest(Ready()) == RequestVerdict::Invite);

    // Off, or not a phrase: not ours, the line goes on to the inbox.
    {
        RequestFacts facts = Ready();
        facts.featureOn = false;
        assert(DecideRequest(facts) == RequestVerdict::NotHandled);
    }
    {
        RequestFacts facts = Ready();
        facts.isRequest = false;
        assert(DecideRequest(facts) == RequestVerdict::NotHandled);
    }

    // Rate limited: handled, silent, whatever else the facts say.
    {
        RequestFacts facts = Ready();
        facts.rateLimited = true;
        facts.botInGuild = false;
        assert(DecideRequest(facts) == RequestVerdict::Silent);
    }

    // The bot itself.
    {
        RequestFacts facts = Ready();
        facts.botInGuild = false;
        facts.guildBotRun = false;
        facts.askerInGuild = true;
        facts.memberCount = GuildMemberLimit;
        facts.inviterFound = false;
        assert(DecideRequest(facts) == RequestVerdict::NotInGuild);
    }

    // A guild led by a player: refused, and that beats every later check.
    {
        RequestFacts facts = Ready();
        facts.guildBotRun = false;
        facts.askerInGuild = true;
        facts.sameTeam = false;
        facts.memberCount = GuildMemberLimit;
        facts.inviterFound = false;
        assert(DecideRequest(facts) == RequestVerdict::NotBotRun);
    }
}

void AlreadyInGuildBeatsEverythingButBotRun()
{
    RequestFacts facts = Ready();
    facts.askerInGuild = true;
    facts.sameTeam = false;
    facts.memberCount = GuildMemberLimit;
    facts.inviterFound = false;
    assert(DecideRequest(facts) == RequestVerdict::AlreadyInGuild);

    // Only the bot-run test sits above it.
    facts.guildBotRun = false;
    assert(DecideRequest(facts) == RequestVerdict::NotBotRun);
}

void NonBotRunGuildRefused()
{
    RequestFacts facts = Ready();
    facts.guildBotRun = false;
    assert(DecideRequest(facts) == RequestVerdict::NotBotRun);
}

void WrongFactionUnlessTwoSide()
{
    {
        RequestFacts facts = Ready();
        facts.sameTeam = false;
        assert(DecideRequest(facts) == RequestVerdict::WrongFaction);
    }
    {
        RequestFacts facts = Ready();
        facts.sameTeam = false;
        facts.twoSideGuilds = true;
        assert(DecideRequest(facts) == RequestVerdict::Invite);
    }
    // Facetion sits below already-in-guild and above the member limit.
    {
        RequestFacts facts = Ready();
        facts.sameTeam = false;
        facts.memberCount = GuildMemberLimit;
        assert(DecideRequest(facts) == RequestVerdict::WrongFaction);
    }
}

void FullAtLimit()
{
    {
        RequestFacts facts = Ready();
        facts.memberCount = GuildMemberLimit - 1;
        assert(DecideRequest(facts) == RequestVerdict::Invite);
    }
    {
        RequestFacts facts = Ready();
        facts.memberCount = GuildMemberLimit;
        assert(DecideRequest(facts) == RequestVerdict::Full);
    }
    {
        RequestFacts facts = Ready();
        facts.memberCount = GuildMemberLimit + 1;
        assert(DecideRequest(facts) == RequestVerdict::Full);
    }
}

void NoOfficer()
{
    RequestFacts facts = Ready();
    facts.inviterFound = false;
    assert(DecideRequest(facts) == RequestVerdict::NoOfficer);

    // A full guild is answered as full, not as an officer problem.
    facts.memberCount = GuildMemberLimit;
    assert(DecideRequest(facts) == RequestVerdict::Full);
}

void CooldownSilent()
{
    // Never asked: passes.
    assert(RequestCooldownPassed(0, 0));
    assert(RequestCooldownPassed(0, 12345));

    // 20 s, not 19 999 ms.
    assert(!RequestCooldownPassed(1000, 1000 + 19999));
    assert(RequestCooldownPassed(1000, 1000 + 20000));
    assert(RequestCooldownPassed(1000, 1000 + 20001));

    // The counter wraps: 20 000 ms after 0xFFFFF000 is 0x00003E20.
    assert(!RequestCooldownPassed(0xFFFFF000u, 0x00003E1Fu));
    assert(RequestCooldownPassed(0xFFFFF000u, 0x00003E20u));
    // A stamp from the far future of a wrapped counter is old, not new.
    assert(RequestCooldownPassed(0xFFFFF000u, 0x00000000u) == false);
}

// A player's own alt is in the guild with the best rank and every right: it must never be the one that invites a
// stranger, even when the stranger whispered it. Then the pick is by the lowest rank id (the guild master's is the
// lowest), never the highest.
void InviterPicking()
{
    assert(PickInviterIndex({}) == -1);

    // The whispered bot is the owned alt: the fleet's own bot invites instead, even at a higher rank.
    std::vector<InviterCandidate> candidates = {
        InviterCandidate{ true, false, true, true, 0 },
        InviterCandidate{ false, true, true, true, 5 },
    };
    assert(PickInviterIndex(candidates) == 1);

    // The whispered bot wins over a lower rank when it may invite itself.
    candidates = {
        InviterCandidate{ false, true, true, true, 0 },
        InviterCandidate{ true, true, true, true, 4 },
    };
    assert(PickInviterIndex(candidates) == 1);

    // The highest rank must not win (it is the first one here, rank 4).
    candidates = {
        InviterCandidate{ false, true, true, true, 4 },
        InviterCandidate{ false, true, true, true, 1 },
        InviterCandidate{ false, true, true, true, 0 },
    };
    assert(PickInviterIndex(candidates) == 2);

    // Offline, without the right, or not a bot: never picked.
    candidates = {
        InviterCandidate{ true, true, false, true, 0 },
        InviterCandidate{ false, true, true, false, 1 },
        InviterCandidate{ false, false, true, true, 2 },
        InviterCandidate{ false, true, true, true, 3 },
    };
    assert(PickInviterIndex(candidates) == 3);

    // The first of equal ranks is stable.
    candidates = {
        InviterCandidate{ false, true, true, true, 2 },
        InviterCandidate{ false, true, true, true, 2 },
    };
    assert(PickInviterIndex(candidates) == 0);

    // An owned alt is never a fallback, even when nothing else can invite.
    candidates = { InviterCandidate{ true, false, true, true, 0 } };
    assert(PickInviterIndex(candidates) == -1);
}

// Every refusal the core can report is answered with its own line; only the rest share the generic one.
void InviteRefusalsAnswered()
{
    using tbhost::guild::InviteOutcome;
    std::string const guild = "Bot Guild";
    char const* const side = "Alliance";

    assert(InviteRefusalAnswer(InviteOutcome::Ok, guild, side, "Taurenbot").empty());
    assert(InviteRefusalAnswer(InviteOutcome::TargetIgnoring, guild, side, "Taurenbot")
        == "You are ignoring Taurenbot, so the invite cannot reach you.");
    assert(InviteRefusalAnswer(InviteOutcome::TargetAlreadyInvited, guild, side, "Taurenbot")
        == "You already have a guild invite; accept or decline it first.");
    assert(InviteRefusalAnswer(InviteOutcome::TargetInGuild, guild, side, "Taurenbot")
        == "You are already in a guild.");
    assert(InviteRefusalAnswer(InviteOutcome::WrongFaction, guild, side, "Taurenbot")
        == "We only take Alliance members.");
    assert(InviteRefusalAnswer(InviteOutcome::NoInviteRight, guild, side, "Taurenbot")
        == "No one in Bot Guild can invite you right now, try later.");
    assert(InviteRefusalAnswer(InviteOutcome::TargetNotFound, guild, side, "Taurenbot")
        == "I cannot find you right now.");

    for (InviteOutcome generic : { InviteOutcome::NotInWorld, InviteOutcome::NotInGuild,
                                   InviteOutcome::EmptyTarget, InviteOutcome::CoreRefused })
        assert(InviteRefusalAnswer(generic, guild, side, "Taurenbot") == "I could not invite you right now.");

    // The codes are the seam's `reason=` tokens and every outcome has one.
    for (InviteOutcome outcome : { InviteOutcome::Ok, InviteOutcome::NotInWorld, InviteOutcome::NotInGuild,
                                   InviteOutcome::EmptyTarget, InviteOutcome::TargetNotFound,
                                   InviteOutcome::TargetIgnoring, InviteOutcome::WrongFaction,
                                   InviteOutcome::TargetInGuild, InviteOutcome::TargetAlreadyInvited,
                                   InviteOutcome::NoInviteRight, InviteOutcome::CoreRefused })
        assert(tbhost::guild::InviteOutcomeCode(outcome) && tbhost::guild::InviteOutcomeCode(outcome)[0] != '\0');
    assert(tbhost::guild::InviteSucceeded(InviteOutcome::Ok));
    assert(!tbhost::guild::InviteSucceeded(InviteOutcome::CoreRefused));
}

// The per-asker map admits once per cooldown and drops the entries that can no longer matter.
void RequestCooldownsPruned()
{
    RequestCooldowns cooldowns;
    assert(cooldowns.Size() == 0);

    for (std::uint32_t asker = 1; asker <= 20; ++asker)
        assert(cooldowns.Admit(asker, 5000));
    assert(cooldowns.Size() == 20);

    // Still inside the 20 s cooldown: handled silently, and nothing new is recorded.
    assert(!cooldowns.Admit(7, 5000 + kRequestCooldownMs - 1));
    assert(cooldowns.Size() == 20);

    // A request at the prune age drops all 20 stale entries before recording the new one.
    assert(cooldowns.Admit(100, 5000 + kRequestPruneMs));
    assert(cooldowns.Size() == 1);

    // A pruned asker is admitted again and counted afresh.
    assert(cooldowns.Admit(7, 5000 + kRequestPruneMs));
    assert(cooldowns.Size() == 2);
    assert(!cooldowns.Admit(7, 5000 + kRequestPruneMs + 1));
}

void Codes()
{
    assert(std::strcmp(RequestCode(RequestVerdict::Invite), "invited") == 0);
    assert(std::strcmp(RequestCode(RequestVerdict::NotInGuild), "not-in-guild") == 0);
    assert(std::strcmp(RequestCode(RequestVerdict::NotBotRun), "not-bot-run") == 0);
    assert(std::strcmp(RequestCode(RequestVerdict::AlreadyInGuild), "already-in-guild") == 0);
    assert(std::strcmp(RequestCode(RequestVerdict::WrongFaction), "wrong-faction") == 0);
    assert(std::strcmp(RequestCode(RequestVerdict::Full), "full") == 0);
    assert(std::strcmp(RequestCode(RequestVerdict::NoOfficer), "no-officer") == 0);
    assert(std::strcmp(RequestCode(RequestVerdict::Silent), "silent") == 0);
    assert(std::strcmp(RequestCode(RequestVerdict::NotHandled), "not-handled") == 0);
    for (RequestVerdict verdict : { RequestVerdict::NotHandled, RequestVerdict::Silent, RequestVerdict::NotInGuild,
                                    RequestVerdict::NotBotRun, RequestVerdict::AlreadyInGuild,
                                    RequestVerdict::WrongFaction, RequestVerdict::Full, RequestVerdict::NoOfficer,
                                    RequestVerdict::Invite })
        assert(RequestCode(verdict) && RequestCode(verdict)[0] != '\0');
}
} // namespace

int main()
{
    RejectsOrdinaryChat();
    RecognisesPhrases();
    VerdictOrder();
    AlreadyInGuildBeatsEverythingButBotRun();
    NonBotRunGuildRefused();
    WrongFactionUnlessTwoSide();
    FullAtLimit();
    NoOfficer();
    CooldownSilent();
    InviterPicking();
    InviteRefusalsAnswered();
    RequestCooldownsPruned();
    Codes();
    return 0;
}

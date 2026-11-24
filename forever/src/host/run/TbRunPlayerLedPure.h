// Issue #139, the opt-in player-led run (forever/docs/RUN-KNOBS-139-NOTES.md): the pure half of its start. No game types, so
// forever/tools/test_run_playerled.cpp runs it standalone.
//
// A run is bot-led by default (decision D1: a bot tank leads). `.tb run start <tank> <plan> lead=player` asks for the other mode: the player who types
// it leads his own party and walks, and the run (the tank bot is still its `leader`) pulls the plan's packs near him (runtime/PlayerLedPolicy.h, the
// Turtle human-lead behaviour, unchanged). The bind gate is that header's `CheckBind`; this file only turns the world's answers into its facts and adds
// the one refusal the host needs (`not-commanded`).
#pragma once

#include "runtime/PlayerLedPolicy.h"

#include <string>
#include <vector>

namespace TortoiseBots
{
namespace run
{
namespace playerled
{
// The `lead=` word of `.tb run start`. `lead=player` asks for the player-led run, `lead=bot` is the default said out loud, any other value is refused.
enum class LeadWord : unsigned char { None, Bot, Player, Bad };

// Takes the `lead=` word out of `words` (the words after the verb: leader, plan, ...), wherever it stands after the plan, and says what it asked for.
// A second `lead=` word is Bad as well (no guess); the other words stay in order.
inline LeadWord TakeLeadWord(std::vector<std::string>& words)
{
    LeadWord out = LeadWord::None;
    for (size_t i = 2; i < words.size();)
    {
        std::string const lower = PlayerLed::Lower(words[i]);
        if (lower.compare(0, 5, "lead=") != 0)
        {
            ++i;
            continue;
        }
        LeadWord const now = lower == "lead=player" ? LeadWord::Player : lower == "lead=bot" ? LeadWord::Bot : LeadWord::Bad;
        out = out == LeadWord::None ? now : LeadWord::Bad;
        words.erase(words.begin() + long(i));
    }
    return out;
}

// One member of the starter's party besides himself, as the host read it.
struct MemberObs
{
    std::string name;
    bool bot = false;               // a host bot (a real player otherwise)
    bool inWorld = true;            // in the world and on a map
    bool owned = false;             // the starter's own bot: its host master or its commander is the starter
    bool commanderIsStarter = false;
    bool commanderOther = false;    // commanded by somebody else (a bot-led run's tank, another player)
    bool tankRole = false;          // the tank role is set on it (PlayerbotAI::IsTank)
};

struct StartObs
{
    bool keyOn = true;              // AiPlayerbot.DungeonRunPlayerLed
    bool starterIsBot = false;
    bool grouped = false;
    bool raid = false;
    bool starterLeads = false;      // the starter leads the group
    std::string tankName;           // the bot named on the command line
    std::vector<MemberObs> members; // every member but the starter
};

struct StartVerdict
{
    std::string code;               // empty: the run may start; else the refusal word of StartOutcome
    std::string text;
    int tank = -1;                  // index into StartObs::members
};

// The gate: PlayerLed::CheckBind on the facts below (the starter leads a party of at most five, never a raid, every bot of it is his own and live,
// none is in another binding, at least one bot is there, the named tank is a bot of it), then every bot must already be commanded by the starter:
// the run does not bind (it never rebuilds an engine kit), and the executor's `follow` orders need the commander to be the player.
inline StartVerdict DecideStart(StartObs const& obs)
{
    PlayerLed::BindFacts facts;
    facts.enabled = obs.keyOn;
    facts.commanderIsBot = obs.starterIsBot;
    facts.grouped = obs.grouped;
    facts.raidOrBg = obs.raid;
    facts.commanderLeads = obs.starterLeads;
    facts.tankName = obs.tankName;
    for (MemberObs const& m : obs.members)
    {
        PlayerLed::Member member;
        member.name = m.name;
        member.bot = m.bot;
        member.controllable = m.owned;
        member.liveBot = m.inWorld;
        member.inOtherBinding = m.commanderOther;
        member.tankRole = m.tankRole;
        facts.members.push_back(member);
    }
    PlayerLed::BindVerdict const verdict = PlayerLed::CheckBind(facts);
    StartVerdict out;
    if (verdict.refusal != PlayerLed::Refusal::None)
    {
        out.code = PlayerLed::RefusalCode(verdict.refusal);
        out.text = verdict.refusal == PlayerLed::Refusal::Disabled
            ? "The player-led run is off (AiPlayerbot.DungeonRunPlayerLed = 0)."
            : verdict.refusal == PlayerLed::Refusal::NoTank
                ? "No tank bot in your party: name one on the command (.tb run start <tank> <plan> lead=player)."
                : PlayerLed::RefusalText(verdict.refusal);
        return out;
    }
    for (MemberObs const& m : obs.members)
        if (m.bot && !m.commanderIsStarter)
        {
            out.code = "not-commanded";
            out.text = m.name + " is your bot but not commanded by you yet: bind your party to you first (`.tb master party`, forever/docs/TB-COMMANDS.md)";
            return out;
        }
    out.tank = verdict.tank;
    return out;
}
}
}
}

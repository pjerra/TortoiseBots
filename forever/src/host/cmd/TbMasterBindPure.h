// BG2 live row 5: `.tb master <bot> <human>` inside a battleground, the PURE decision. No core header, no Player, no group.
// Tested standalone by forever/tools/test_master_bind_pure.cpp (g++ -std=c++17 -I. forever/tools/test_master_bind_pure.cpp).
//
// The plain rule (TbH3.cpp MayLeadBots, the N5 rule every party, master and instance command uses) lets a bot be bound to a
// HOST BOT or to the sender's own character; another real player is refused ("neither a host bot nor your own character"). That
// rule is what refused the live row: the operator sends `.tb master <pool bot> <the human he watches>` and is neither of them.
//
// Inside ONE battleground the refusal is lifted, and only there: the bot and the human already stand in the same match, on the
// same team, where nobody outside the instance can reach them and a bind cannot move a bot across sides. Both must be INSIDE
// the match (GetBattlegroundId() != 0, Player.h:2711-2713) and the ids must be EQUAL: a second match instance, a bot still in
// the world, or a human on the other team is refused with its own code. Everything the plain rule refused outside a match it
// still refuses, with the same code and text.
//
// The caller reads the two instance ids and the team pair off the core; everything else is decided here.
#pragma once

#include <cstdint>
#include <string>

namespace TortoiseBots
{
namespace cmd
{
namespace master_bind
{

struct Facts
{
    bool commanderMayLead = false;          // h3::MayLeadBots already said yes (a host bot, or the sender's own character): the plain rule
    std::uint32_t botInstance = 0;          // the bot's Player::GetBattlegroundId(): 0 = not inside a battleground
    std::uint32_t commanderInstance = 0;    // the commander's GetBattlegroundId(): 0 = not inside a battleground
    bool sameTeam = false;                  // Player::GetBGTeam() equal; read only when both are inside one instance
};

enum class Verdict : std::uint8_t
{
    NotAllowed = 0,   // outside a battleground: neither a host bot nor the sender's own character (the plain rule's refusal); the default, so a missing verdict refuses
    Allowed,          // the plain rule, or one battleground instance and one team
    OtherInstance,    // both inside battlegrounds, but not the same match instance
    OtherTeam,        // the same match instance, on the other team
};

inline Verdict Judge(Facts const& facts)
{
    if (facts.commanderMayLead)
        return Verdict::Allowed;
    if (!facts.botInstance || !facts.commanderInstance)
        return Verdict::NotAllowed;
    if (facts.botInstance != facts.commanderInstance)
        return Verdict::OtherInstance;
    if (!facts.sameTeam)
        return Verdict::OtherTeam;
    return Verdict::Allowed;
}

// Is this bind a battleground bind? Both stand in ONE match instance (both ids non-zero and equal) on the same team: the very condition that lets
// an operator bind outside the plain rule, and also the condition under which a bind made through the plain rule is short-lived. It is read PER
// BOT and handed to the binding check as that bot's kind (cmd::NoteMaster's `kinds`): a bg bot is never stored and its only end is the match it
// was bound in (TbCmdBindingLifePure.h, `BgBook` / `CheckBg` / `BgBindEnd`). Ordering the ids as Judge does keeps the two reading the same facts.
inline bool IsBgBind(Facts const& facts)
{
    return facts.botInstance != 0 && facts.botInstance == facts.commanderInstance && facts.sameTeam;
}

// The reply's refusal code (the grammar of forever/docs/TB-COMMANDS.md section 3). Allowed has no code: it is not a refusal.
inline char const* CodeOf(Verdict verdict)
{
    switch (verdict)
    {
        case Verdict::OtherInstance:
            return "other-instance";
        case Verdict::OtherTeam:
            return "other-team";
        case Verdict::Allowed:
            return "allowed";
        case Verdict::NotAllowed:
        default:
            return "not-allowed";
    }
}

// The refusal's one-line text, with the commander's name. Allowed never reaches this.
inline std::string DetailOf(Verdict verdict, std::string const& commanderName)
{
    switch (verdict)
    {
        case Verdict::OtherInstance:
            return commanderName + " is in another battleground instance: a bind stays inside one match, both must be in the same instance";
        case Verdict::OtherTeam:
            return commanderName + " is on the other team in this battleground: a bot is only bound to a player it fights beside";
        case Verdict::Allowed:
            return std::string();
        case Verdict::NotAllowed:
        default:
            return commanderName + " is neither a host bot nor your own character: it cannot be made the commander of bots";
    }
}

} // namespace master_bind
} // namespace cmd
} // namespace TortoiseBots

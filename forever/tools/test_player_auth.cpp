// Standalone test of the player command rule (forever/src/host/player/TbPlayerAuthPure.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -I forever/src forever/tools/test_player_auth.cpp -o /tmp/test_player_auth && /tmp/test_player_auth
#include "host/player/TbPlayerAuthPure.h"

#include <cstdio>
#include <string>
#include <string_view>

namespace p = TortoiseBots::player;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

namespace
{
// A real player on Battle.net account 42, guid 100.
constexpr p::Caller Owner{false, 42, 100};
constexpr p::Caller Stranger{false, 43, 200};
constexpr p::Caller GameMaster{true, 43, 300};

// An owned alt on an account of Battle.net account 42, following guid 100: the caller's own bot.
p::BotFacts OwnerBot()
{
    p::BotFacts bot;
    bot.isBot = true;
    bot.account = 42;
    bot.master = 100;
    return bot;
}

// A bot of another Battle.net account, masterless and in nobody's group.
p::BotFacts OtherBot()
{
    p::BotFacts bot;
    bot.isBot = true;
    bot.account = 99;
    return bot;
}
}

int main()
{
    // THE FAILING CASE: the owner (the bot's own Battle.net account) may order its alt, and follow is a tactical verb.
    // case: owner_same_account_follow_allowed
    {
        CHECK(p::TierOf("follow") == p::Tier::Tactical);
        CHECK(p::Decide(Owner, OwnerBot(), p::Tier::Tactical) == p::Verdict::Allowed);
        // The same Battle.net account without any master binding still owns the bot (D-2).
        p::BotFacts unbound = OwnerBot();
        unbound.master = 0;
        CHECK(p::Decide(Owner, unbound, p::Tier::Tactical) == p::Verdict::Allowed);
        CHECK(p::Decide(Owner, unbound, p::Tier::Owner) == p::Verdict::Allowed);
    }

    // A stranger's follow of another Battle.net account's bot is refused, and the refusal code is the stable word.
    // case: stranger_follow_refused_not_yours
    {
        p::Verdict const v = p::Decide(Stranger, OtherBot(), p::Tier::Tactical);
        CHECK(v == p::Verdict::NotYours);
        CHECK(std::string_view(p::CodeOf(v)) == "not-yours");
    }

    // A master that is not on the bot's Battle.net account may use the tactical verbs, never the owner verbs (logout).
    // case: master_logout_refused
    {
        p::BotFacts bot = OtherBot();
        bot.master = 200;                 // the stranger is the bot's master
        CHECK(p::Decide(Stranger, bot, p::Tier::Tactical) == p::Verdict::Allowed);
        CHECK(p::TierOf("logout") == p::Tier::Owner);
        CHECK(p::Decide(Stranger, bot, p::Tier::Owner) == p::Verdict::NotYours);
        bot.account = 43;                 // on the stranger's Battle.net account it is his bot again
        CHECK(p::Decide(Stranger, bot, p::Tier::Owner) == p::Verdict::Allowed);
    }

    // The real player leading the bot's group may use the tactical verbs.
    // case: leader_real_player_follow_allowed
    {
        p::BotFacts bot = OtherBot();
        bot.groupLeader = 200;
        bot.leaderIsBot = false;
        CHECK(p::Decide(Stranger, bot, p::Tier::Tactical) == p::Verdict::Allowed);
        CHECK(p::Decide(Stranger, bot, p::Tier::Owner) == p::Verdict::NotYours);
    }

    // A bot leading the group is not a real player: the caller gets nothing from the coincidence.
    // case: leader_bot_not_counted
    {
        p::BotFacts bot = OtherBot();
        bot.groupLeader = 200;
        bot.leaderIsBot = true;
        CHECK(p::Decide(Stranger, bot, p::Tier::Tactical) == p::Verdict::NotYours);
    }

    // A commander (cmd::IsCommandedBy) gets the tactical verbs only.
    // case: commander_tactical_only
    {
        p::BotFacts bot = OtherBot();
        bot.commandedByCaller = true;
        CHECK(p::Decide(Stranger, bot, p::Tier::Tactical) == p::Verdict::Allowed);
        CHECK(p::Decide(Stranger, bot, p::Tier::Owner) == p::Verdict::NotYours);
    }

    // case: owner_bnet_across_game_accounts
    // Two game accounts of one Battle.net account own the same bots: the rule sees Battle.net ids, so its owner branch
    // covers the alt the requester logs in on his second game account (D-1 amendment, 2026-11-22).
    {
        p::Caller secondGameAccount{false, 7, 101};        // another character of Battle.net account 7
        p::BotFacts alt = OtherBot();
        alt.account = 7;
        alt.master = 0;
        CHECK(p::Decide(secondGameAccount, alt, p::Tier::Owner) == p::Verdict::Allowed);
        p::Caller otherBnet{false, 8, 102};
        CHECK(p::Decide(otherBnet, alt, p::Tier::Owner) == p::Verdict::NotYours);
    }

    // The GM may use every verb on every bot.
    // case: gm_any_verb_allowed
    {
        CHECK(p::Decide(GameMaster, OtherBot(), p::Tier::Owner) == p::Verdict::Allowed);
        CHECK(p::Decide(GameMaster, OtherBot(), p::Tier::Tactical) == p::Verdict::Allowed);
        p::BotFacts notBot;               // even a character that is not a bot is answered as not-a-bot, not as allowed
        CHECK(p::Decide(GameMaster, notBot, p::Tier::Tactical) == p::Verdict::NotABot);
    }

    // Account 0 (a character without an account in the cache) never makes anyone an owner, on either side.
    // case: zero_account_never_owner
    {
        p::Caller zeroCaller{false, 0, 100};
        p::BotFacts zeroBot = OtherBot();
        zeroBot.account = 0;
        CHECK(p::Decide(zeroCaller, zeroBot, p::Tier::Tactical) == p::Verdict::NotYours);
        CHECK(p::Decide(zeroCaller, zeroBot, p::Tier::Owner) == p::Verdict::NotYours);
        p::Caller realCaller{false, 42, 100};
        CHECK(p::Decide(realCaller, zeroBot, p::Tier::Tactical) == p::Verdict::NotYours);
        p::BotFacts realBot = OtherBot();
        realBot.account = 42;
        CHECK(p::Decide(zeroCaller, realBot, p::Tier::Tactical) == p::Verdict::NotYours);
    }

    // A bot in a running dungeon run refuses every order, GMs included (they use `.tb run pause`).
    // case: in_run_refused_even_for_gm
    {
        p::BotFacts bot = OwnerBot();
        bot.inRun = true;
        CHECK(p::Decide(Owner, bot, p::Tier::Tactical) == p::Verdict::InRun);
        CHECK(p::Decide(GameMaster, bot, p::Tier::Owner) == p::Verdict::InRun);
        CHECK(std::string_view(p::CodeOf(p::Verdict::InRun)) == "in-run");
        p::BotFacts plain = OtherBot();
        plain.inRun = true;
        CHECK(p::Decide(GameMaster, plain, p::Tier::Tactical) == p::Verdict::InRun);
    }

    // Unknown verbs are owner tier (fail closed); help needs no bot, and the tactical/owner words are pinned.
    // case: unknown_verb_is_owner_tier
    {
        CHECK(p::TierOf("dance") == p::Tier::Owner);
        CHECK(p::TierOf("") == p::Tier::None);
        CHECK(p::TierOf("help") == p::Tier::None);
        for (char const* verb : { "follow", "stay", "attack", "flee", "who", "roster", "list", "stats" })
            CHECK(p::TierOf(verb) == p::Tier::Tactical);
        for (char const* verb : { "add", "logout", "remove", "summon" })
            CHECK(p::TierOf(verb) == p::Tier::Owner);
        CHECK(p::TierOf("HELP") == p::Tier::None);
        CHECK(p::TierOf("Logout") == p::Tier::Owner);
        // Not a bot: refused before any tier question, and never Allowed.
        CHECK(p::Decide(Owner, p::BotFacts{}, p::Tier::Tactical) == p::Verdict::NotABot);
        CHECK(std::string_view(p::CodeOf(p::Verdict::NotABot)) == "not-a-bot");
        CHECK(std::string_view(p::CodeOf(p::Verdict::Allowed)).empty());
    }

    // `.bot` claims the word `bot`, never a prefix of another word.
    // case: claims_bot_not_botany
    {
        std::string_view rest;
        CHECK(!p::ClaimsCommand("botany", rest));
        CHECK(!p::ClaimsCommand("bots", rest));
        CHECK(p::ClaimsCommand("bot", rest));             // `bot` alone IS claimed
        CHECK(rest.empty());
        CHECK(p::ClaimsCommand("bot who", rest));
        CHECK(rest == "who");
        CHECK(p::ClaimsCommand("bot who all", rest));
        CHECK(rest == "who all");
        CHECK(p::ClaimsCommand("  BOT   help ", rest));
        CHECK(rest == "help ");
        CHECK(!p::ClaimsCommand("", rest));
        CHECK(!p::ClaimsCommand("bo", rest));
    }

    // The case-insensitive claim, spelled out.
    // case: claims_case_insensitive
    {
        std::string_view rest;
        CHECK(p::ClaimsCommand("BoT help", rest));
        CHECK(rest == "help");
        CHECK(p::ClaimsCommand("bOt help", rest));
        CHECK(rest == "help");
        CHECK(!p::ClaimsCommand("bothelp", rest));        // no space: another word
    }

    // D-3/D-7: an owned login is the owner's account only (unless a GM), and the cap holds.
    // case: may_login_owned_cap
    // case: may_login_owned_other_account_refused
    // case: may_login_owned_gm_any_account
    {
        std::string code;
        CHECK(p::MayLoginOwned(42, 42, false, 0, 4, code));
        CHECK(code.empty());
        CHECK(p::MayLoginOwned(42, 42, false, 3, 4, code));
        CHECK(!p::MayLoginOwned(42, 42, false, 4, 4, code));
        CHECK(code == "too-many");
        CHECK(!p::MayLoginOwned(42, 43, false, 0, 4, code));
        CHECK(code == "not-yours");
        CHECK(!p::MayLoginOwned(0, 0, false, 0, 4, code));
        CHECK(code == "not-yours");
        CHECK(!p::MayLoginOwned(42, 43, false, 9, 4, code));   // the account rule comes first
        CHECK(code == "not-yours");
        CHECK(p::MayLoginOwned(0, 43, true, 99, 4, code));
        CHECK(code.empty());
        CHECK(p::MayLoginOwned(42, 42, true, 99, 0, code));    // a GM is not capped
    }

    // D-8: the summon cooldown, stamped only by a teleport that started, and GMs are exempt.
    // case: cooldown_left_and_gm_exempt
    {
        std::uint32_t left = 0;
        CHECK(!p::SummonCooldownLeft(1000, 0, 300, false, left));   // never summoned: not on cooldown
        CHECK(p::SummonCooldownLeft(1000 + 30 * 1000, 1000, 300, false, left));
        CHECK(left == 270);
        CHECK(!p::SummonCooldownLeft(1000 + 300 * 1000, 1000, 300, false, left));
        CHECK(left == 0);
        CHECK(!p::SummonCooldownLeft(1000 + 30 * 1000, 1000, 300, true, left));
        CHECK(left == 0);
        CHECK(!p::SummonCooldownLeft(1000 + 30 * 1000, 1000, 0, false, left));
        // A clock that went backwards counts as just stamped, never as expired.
        CHECK(p::SummonCooldownLeft(500, 1000, 300, false, left));
        CHECK(left == 300);
    }

    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}

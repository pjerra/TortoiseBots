// Standalone test of the addon-channel passthrough shape (forever/src/host/TbAddonVerbsPure.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -I forever/src forever/tools/test_addon_verbs.cpp -o /tmp/test_addon_verbs && /tmp/test_addon_verbs
#include "host/TbAddonVerbsPure.h"

#include <cstdio>
#include <string>

namespace v = TortoiseBots::addonverbs;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main()
{
    // THE FAILING CASE: a captured summon ok line becomes an Ack for the sent verb, scoped to the bot.
    // case: summon_ok_maps_to_ack
    {
        v::Outcome const out = v::Summarize("summon", {"TortoiseBots: summon Foo: ok started; it arrives next to you"});
        CHECK(out.answered);
        CHECK(out.ok);
        CHECK(out.scope == "bot:Foo");
        CHECK(out.count == 1);
        CHECK(out.detail == "started; it arrives next to you");
    }

    // Exactly the nine verbs the player layer answers ride natively; everything else stays unsupported (f-chan).
    // case: registered_verbs_are_the_player_layer_verbs
    {
        for (char const* verb : {"add", "logout", "remove", "summon", "stats", "who", "maintenance", "autogear", "hire"})
            CHECK(v::IsPassthroughVerb(verb));
        for (char const* verb : {"roster", "list", "alts", "action", "invite", "uninvite", "role", "strategy",
                                 "formation", "downtime", "command", "cc", "run", "features", "config", "help",
                                 "follow", "stay", "attack", "flee"})
            CHECK(!v::IsPassthroughVerb(verb));
        CHECK(v::PassthroughVerbs().size() == 9);
    }

    // `remove` is the alias: the player layer prints `logout` lines, the intent stays the sent word.
    // case: remove_alias_maps_to_logout_lines
    {
        v::Outcome const out = v::Summarize("remove", {"TortoiseBots: logout Foo: ok logged out"});
        CHECK(out.answered && out.ok);
        CHECK(out.scope == "bot:Foo");
        v::Outcome const refused = v::Summarize("remove", {"TortoiseBots: logout: refused usage: .bot logout <bot>"});
        CHECK(refused.answered && !refused.ok);
        CHECK(refused.code == "usage");
    }

    // A refusal carries the player layer's code and text unchanged into the Err.
    // case: refusal_code_passes_through
    {
        v::Outcome const out = v::Summarize("logout", {"TortoiseBots: logout: refused not-yours: you may not log that bot out"});
        CHECK(out.answered && !out.ok);
        CHECK(out.code == "not-yours");
        CHECK(out.message == "you may not log that bot out");
        CHECK(out.scope.empty());
    }

    // `add` joins the party, so the Ack is party-scoped with the bot named by the line.
    // case: add_ok_is_party_scoped
    {
        v::Outcome const out = v::Summarize("add", {"TortoiseBots: add Foo: ok queued for login; it joins your party"});
        CHECK(out.answered && out.ok);
        CHECK(out.scope == "party");
        CHECK(out.count == 1);
    }

    // `who` forwards the count line plus one row per bot and acks the roster scope with the row count.
    // case: who_rows_forward_with_roster_ack
    {
        v::Outcome const out = v::Summarize("who", {"TortoiseBots: who: 2 bot(s)",
                                                   "TortoiseBots: who: Foo level 60 Warrior map 0 follow engine",
                                                   "TortoiseBots: who: Bar level 12 Mage map 1 stay spike"});
        CHECK(out.answered && out.ok);
        CHECK(out.scope == "roster");
        CHECK(out.count == 2);
        CHECK(out.detail == "-");
        v::Outcome const refused = v::Summarize("who", {"TortoiseBots: who: refused usage: .bot who [all]"});
        CHECK(refused.answered && !refused.ok && refused.code == "usage");
    }

    // `stats` and `maintenance` forward their one data line; the Ack detail repeats the ok tail.
    // case: stats_and_maintenance_ok_carry_detail
    {
        v::Outcome const stats = v::Summarize("stats", {"TortoiseBots: stats Foo: ok level 60 Warrior, 100% hp, 42% mana"});
        CHECK(stats.answered && stats.ok);
        CHECK(stats.scope == "bot:Foo");
        CHECK(stats.detail == "level 60 Warrior, 100% hp, 42% mana");
        v::Outcome const maint = v::Summarize("maintenance", {"TortoiseBots: maintenance Foo: ok 3 item(s) granted, repaired, saved"});
        CHECK(maint.answered && maint.ok && maint.scope == "bot:Foo");
    }

    // Autogear answers outside Say/Refuse shapes: its raw lines map onto the same verdict.
    // case: autogear_raw_lines_map_to_verdict
    {
        v::Outcome const ok = v::Summarize("autogear", {"Re-geared Foo within the configured caps."});
        CHECK(ok.answered && ok.ok && ok.scope == "bot:Foo");
        v::Outcome const warn = v::Summarize("autogear", {"Re-geared Foo within the configured caps.",
                                                          "Warning: Foo has a gun equipped but no bullets in the bags."});
        CHECK(warn.answered && warn.ok);
        v::Outcome const usage = v::Summarize("autogear", {"Usage: .bot autogear <online bot name> [quality|ilvl] (same account only)"});
        CHECK(usage.answered && !usage.ok && usage.code == "usage");
        v::Outcome const pool = v::Summarize("autogear", {"'Foo' is a pool bot and keeps progressing organically."});
        CHECK(pool.answered && !pool.ok && pool.code == "pool");
        v::Outcome const disabled = v::Summarize("autogear", {"Owned-bot autogear is disabled (AiPlayerbot.OwnedBotAutogearEnabled = 0)."});
        CHECK(disabled.answered && !disabled.ok && disabled.code == "disabled");
        v::Outcome const dead = v::Summarize("autogear", {"Bot 'Foo' must be alive."});
        CHECK(dead.answered && !dead.ok && dead.code == "dead");
        v::Outcome const unknown = v::Summarize("autogear", {"Unknown autogear option. Use a quality (white, green, blue, purple) or an item level number."});
        CHECK(unknown.answered && !unknown.ok && unknown.code == "usage");
        v::Outcome const shaped = v::Summarize("autogear", {"TortoiseBots: autogear: refused in-run: bot is in an active dungeon run."});
        CHECK(shaped.answered && !shaped.ok && shaped.code == "in-run");
    }

    // No terminal line (the entry declined, the engine is off): the host sends nothing, the core answers over chat.
    // case: no_terminal_line_means_no_reply
    {
        v::Outcome const empty = v::Summarize("summon", {});
        CHECK(!empty.answered);
        v::Outcome const help = v::Summarize("who", {"TortoiseBots: help: .bot who [all] - the bots you may order, one line each"});
        CHECK(!help.answered);
    }

    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}

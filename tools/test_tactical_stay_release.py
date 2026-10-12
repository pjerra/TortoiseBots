"""A tactical order that breaks a stay leaves the bot following again.

Compiles the production EndFlee and RelaxTacticalMovement from
commands/BotCommands.cpp against a stand-in PlayerbotAI (strategy sets per
engine, the real movement-mode list from MovementModePolicy.h), and checks
that every per-bot command that sends a bot at a target goes through it, as
the party-wide `.bot action` forms do. Requires g++ (skipped when unavailable).
"""
import pathlib
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "commands/BotCommands.cpp").read_text()


def extract(signature, next_signature):
    start = SOURCE.index(signature)
    end = SOURCE.index(next_signature, start)
    return SOURCE[start:end]


FIXTURE = r"""
#include "ai/playerbot/MovementModePolicy.h"
#include <cassert>
#include <map>
#include <set>
#include <sstream>
#include <string>

enum class BotState { BOT_STATE_COMBAT, BOT_STATE_NON_COMBAT, BOT_STATE_DEAD, BOT_STATE_REACTION };

struct PlayerbotAI
{
    std::map<BotState, std::set<std::string>> engines;
    bool HasStrategy(std::string const& name, BotState state) { return engines[state].count(name) != 0; }
    void ChangeStrategy(std::string const& names, BotState state)
    {
        std::stringstream in(names);
        std::string entry;
        while (std::getline(in, entry, ','))
        {
            if (entry[0] == '+') engines[state].insert(entry.substr(1));
            else if (entry[0] == '-') engines[state].erase(entry.substr(1));
        }
    }
    bool HasActiveMovementStrategy()
    {
        for (char const* mode : ai::kMovementModeStrategies)
            if (HasStrategy(mode, BotState::BOT_STATE_NON_COMBAT) || HasStrategy(mode, BotState::BOT_STATE_REACTION))
                return true;
        return false;
    }
    std::string GetDefaultMovementStrategy() { return "follow"; }
};

%(code)s

int main()
{
    using S = BotState;
    // Whispered stay (SetMovementStrategy("stay")): stay in all three engines.
    PlayerbotAI stayed;
    for (S s : { S::BOT_STATE_REACTION, S::BOT_STATE_NON_COMBAT, S::BOT_STATE_COMBAT })
        stayed.engines[s] = { "stay" };
    stayed.engines[S::BOT_STATE_NON_COMBAT].insert("follow jump");
    RelaxTacticalMovement(&stayed);
    assert(stayed.HasStrategy("follow", S::BOT_STATE_NON_COMBAT));
    assert(stayed.HasStrategy("follow", S::BOT_STATE_REACTION));
    assert(!stayed.HasStrategy("stay", S::BOT_STATE_NON_COMBAT));
    assert(!stayed.HasStrategy("stay", S::BOT_STATE_COMBAT));
    assert(!stayed.HasStrategy("follow", S::BOT_STATE_COMBAT));

    // Stay plus a passive hold: the passive EndFlee drops is not a mode.
    PlayerbotAI passive;
    passive.engines[S::BOT_STATE_NON_COMBAT] = { "stay", "passive" };
    passive.engines[S::BOT_STATE_COMBAT] = { "stay", "passive" };
    RelaxTacticalMovement(&passive);
    assert(passive.HasStrategy("follow", S::BOT_STATE_NON_COMBAT));
    assert(!passive.HasStrategy("passive", S::BOT_STATE_NON_COMBAT));

    // A guard order is a mode of its own and is kept as it is.
    PlayerbotAI guarded;
    guarded.engines[S::BOT_STATE_NON_COMBAT] = { "guard" };
    RelaxTacticalMovement(&guarded);
    assert(guarded.HasStrategy("guard", S::BOT_STATE_NON_COMBAT));
    assert(!guarded.HasStrategy("follow", S::BOT_STATE_NON_COMBAT));
    return 0;
}
"""


class TacticalStayReleaseTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which("g++"), "g++ unavailable")
    def test_broken_stay_returns_to_follow(self):
        code = extract("static void EndFlee(PlayerbotAI* ai)", "static bool BindMovementMaster(")
        with tempfile.TemporaryDirectory() as tmp:
            src = pathlib.Path(tmp) / "fixture.cpp"
            src.write_text(FIXTURE % {"code": code})
            binary = pathlib.Path(tmp) / "fixture"
            subprocess.run(["g++", "-std=c++17", "-I", str(ROOT), str(src), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_per_bot_attack_breaks_stay(self):
        # `.bot attack <Name>` must end a stay like `.bot action attack`.
        body = extract("static bool HandleAttack(", "static bool IsPublicFormation(")
        self.assertRegex(body, r'HandleNamedAction\([^;]*"attack my target"[^;]*,\s*true\)')
        named = extract("static bool HandleNamedAction(", "// `.bot action flee`")
        relax = named.index("RelaxTacticalMovement(ai)")
        self.assertLess(relax, named.index("DoSpecificAction(action"))


if __name__ == "__main__":
    unittest.main()

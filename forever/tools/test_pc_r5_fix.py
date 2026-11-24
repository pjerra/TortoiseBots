#!/usr/bin/env python3
"""R5 fix round (PR #211, live batch 4): the `quests` crash and the owner gate of the chat words.

No build, no network, no core. Text checks over the sources plus one standalone g++ compile of the pure rule.

Run:
  python3 -m unittest forever/tools/test_pc_r5_fix.py

Bug 1, SIGSEGV on the owner's whisper `quests` (batch 4 rows 16-23: exit 139, the last world log line `slice: missing value focus travel target`):
  ListQuestsAction::ListQuests builds `PlayerTravelInfo info(bot)` on every call (ListQuestsActions.cpp:60), and the constructor reads
  `AI_VALUE(focusQuestTravelList, "focus travel target")` (TravelMgr.cpp:72). The slice did not register the value, and its missing-value
  stand-in (SliceMissingKeyGuard.h) covered `std::set<uint32>&` but not the by-value `std::set<uint32>` (= focusQuestTravelList) this reads,
  so `GetValue<focusQuestTravelList>` cast to null and `->Get()` dereferenced it. Two independent faults, both pinned below:
    * every value an R5 action reads is registered (how the set is found: test_r5_read_set_is_found);
    * every value the slice still does NOT register is covered by a stand-in of the type it is read as (no future unregistered read can
      cast to null).
Bug 2, the owner gate: IsBotOwnerOrGm compared game accounts, so the owner's other game account (same Battle.net account) was refused.
"""
import os
import re
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import slice_registry_check as chk  # noqa: E402
import test_chat_grammar_wiring as grammar  # noqa: E402

ROOT = chk.ROOT
PLAYER_AI = "ai/playerbot/PlayerbotAI.cpp"
OWNER_COMPAT_F = "forever/src/compat/TbCompat_Owner.h"
OWNER_COMPAT_T = "ai/cmangos-compat-owner.h"
OWNER_COMPAT_SWITCH = "ai/playerbot/TbCompatOwner.h"
PLAYER_CMD = "forever/src/host/player/TbPlayerCmd.cpp"
TRAVEL_MGR = "ai/playerbot/TravelMgr.cpp"
TRAVEL_MGR_H = "ai/playerbot/TravelMgr.h"
GUARD = "forever/src/slice/SliceMissingKeyGuard.h"


def read(rel):
    return grammar.read(rel)


def read_or_empty(rel):
    return read(rel) if os.path.isfile(os.path.join(ROOT, rel)) else ""


# ---- bug 1: the values the R5 actions read -------------------------------------------------------------------------------------------

def r5_read_set():
    """The value names the R5 chat words reach: every value-macro / GetValue string in the six R5 action sources and their headers, the
    PlayerTravelInfo constructor (ListQuests builds one on every call) and the name tables that constructor loops over (TravelMgr.h).
    Returns {name: set(types)}; table names carry the table's type."""
    out = {}

    def add(name, typ):
        out.setdefault(name.split("::", 1)[0], set()).add(typ)

    for rel in sorted(set(grammar.R5_ACTION_SOURCES.values())):
        for path in (rel, os.path.splitext(rel)[0] + ".h"):
            if not os.path.isfile(os.path.join(ROOT, path)):
                continue
            text = chk.strip_comments(read(path))
            for kind, name, typ, _has_only in chk.refs_in(path, text):
                if kind == "value":
                    add(name, typ or "")
    # the PlayerTravelInfo constructor body (reached from ListQuests: `PlayerTravelInfo info(bot)`)
    text = chk.strip_comments(read(TRAVEL_MGR))
    start = text.index("PlayerTravelInfo::PlayerTravelInfo(Player* player)")
    body, _ = grammar.brace_body(text, text.index("{", start) + 1, "{", "}")
    for kind, name, typ, _has_only in chk.refs_in(TRAVEL_MGR, body):
        if kind == "value":
            add(name, typ or "")
    # the tables it loops over
    header = chk.strip_comments(read(TRAVEL_MGR_H))
    for table, typ in (("boolValues", "bool"), ("uint8Values", "uint8"), ("uint32Values", "uint32")):
        m = re.search(r"std::unordered_map<std::string, %s> %s =\s*\{(.*?)\n\t\t\};" % (typ, table), header, re.S)
        assert m, "no %s table in TravelMgr.h" % table
        for name in re.findall(r'\{"([^"]+)"\s*,', m.group(1)):
            add(name, typ)
    return out


# Names of the constructor's tables the slice leaves unregistered on purpose: each is a bool/uint8/uint32, which the stand-in covers (its
# answer is false/0, the safe "do not" answer). Everything else an R5 action reads must be a registered value.
TABLE_VALUES_NOT_REGISTERED = {
    # #147/#256 (the free-bot life and quest travel rounds) registered the town and quest values this set used to
    # hold (can sell, should sell, vendor trip needed, free quest log slots, can repair, should repair, can fight
    # equal); only these three are still unregistered.
    "can fight boss", "can ah sell", "should ah sell",
}


def unregistered_r5_values(context_text):
    reg = grammar.registered(context_text)["value"]
    return sorted(n for n in r5_read_set() if n not in reg and n not in TABLE_VALUES_NOT_REGISTERED)


def guard_types_of(text):
    text = chk.strip_comments(text)
    m = re.search(r'using\s+SliceInertValue\s*=\s*InertValues\s*<', text)
    assert m, "no SliceInertValue alias"
    types, i = [], m.end()
    while True:
        t, i = chk.scan_type(text, i)
        types.append(chk.norm_type(t))
        if text[i - 1] == ">":
            break
    return set(types)


def uncovered_stand_in_types(guard_text, context_text):
    """Unregistered values the slice sources read as a type the stand-in lacks: [(name, [types])]."""
    covered = guard_types_of(guard_text)
    reg = grammar.registered(context_text)["value"]
    warrior = chk.warrior_keys()["value"]
    bad = []
    for (kind, name), info in sorted(chk.referenced().items()):
        if kind != "value" or name in reg or name in warrior:
            continue
        lacking = sorted(t for t in info["types"] if chk.norm_type(t) not in covered)
        if lacking:
            bad.append((name, lacking))
    return bad


# ---- bug 2: the owner gate -----------------------------------------------------------------------------------------------------------

def is_owner_body(player_ai_text):
    start = player_ai_text.index("bool PlayerbotAI::IsBotOwnerOrGm(")
    return player_ai_text[start:player_ai_text.index("bool PlayerbotAI::CheckBotCommandAuth", start)]


def check_owner_gate(player_ai, compat_f, player_cmd):
    """Raises AssertionError unless the chat owner gate asks the Battle.net owner check and never compares game account ids."""
    code = chk.strip_comments(is_owner_body(player_ai))
    if "BotOwnedBySenderCompat" not in code:
        raise AssertionError("IsBotOwnerOrGm does not ask the Battle.net owner helper (BotOwnedBySenderCompat)")
    if re.search(r"AccountIdOfCharacterCompat\s*\(.*\)\s*==\s*senderAccount", code):
        raise AssertionError("IsBotOwnerOrGm compares game account ids again (the pre-fix owner gate)")
    f_code = chk.strip_comments(compat_f)
    if "player::CallerOwnsBotForChat" not in f_code.replace("TortoiseBots::", ""):
        raise AssertionError("the ForeverCore helper does not call the host's Battle.net owner check")
    if re.search(r"GetAccountId\s*\(\)\s*==|AccountIdOfCharacterCompat", f_code):
        raise AssertionError("the ForeverCore owner helper compares game accounts")
    cmd = chk.strip_comments(player_cmd)
    m = re.search(r"bool CallerOwnsBotForChat\([^)]*\)\s*\{(.*?)\n\}", cmd, re.S)
    if not m:
        raise AssertionError("TbPlayerCmd.cpp has no CallerOwnsBotForChat")
    fn = m.group(1)
    for needle in ("ScopeOf(", "scope.Owns(", "GetCharacterAccountIdByGuid(", "ChatOwnerAllowed("):
        if needle not in fn:
            raise AssertionError("CallerOwnsBotForChat lacks %s (the .bot ownership read: fresh, Battle.net)" % needle)
    if re.search(r"\bstatic\b|\bmemo\b|thread_local", fn):
        raise AssertionError("CallerOwnsBotForChat caches: ownership is read fresh per message")
    if re.search(r"GetAccountId\s*\(\)\s*==|==\s*\w*[Aa]ccount\b", fn):
        raise AssertionError("CallerOwnsBotForChat compares game account ids")


class R5CrashFix(unittest.TestCase):
    def setUp(self):
        self.context = read(grammar.CONTEXT)

    def test_r5_read_set_is_found(self):
        found = r5_read_set()
        self.assertIn("focus travel target", found)
        self.assertIn("travel target", found)
        self.assertIn("group members", found)
        self.assertIn("death count", found)           # a constructor table value
        self.assertGreaterEqual(len(found), 20, "the read-set scan found only %d values" % len(found))

    def test_every_value_an_r5_action_reads_is_registered(self):
        missing = unregistered_r5_values(self.context)
        self.assertEqual(missing, [], "values the R5 actions read that the slice does not register: %s" % ", ".join(missing))

    def test_unregistered_table_values_are_covered_by_a_stand_in(self):
        found = r5_read_set()
        covered = guard_types_of(read(GUARD))
        reg = grammar.registered(self.context)["value"]
        for name in sorted(TABLE_VALUES_NOT_REGISTERED):
            with self.subTest(value=name):
                self.assertNotIn(name, reg, "%s is registered now: drop it from TABLE_VALUES_NOT_REGISTERED" % name)
                for typ in found[name]:
                    self.assertIn(chk.norm_type(typ), covered, "%s is read as %s, which the stand-in lacks" % (name, typ))

    def test_no_unregistered_value_reads_a_type_the_stand_in_lacks(self):
        # The crash mechanism: GetValue<T> casts the stand-in to Value<T>; a T the stand-in does not derive from gives null and ->Get() faults.
        # slice_registry_check.py lists them as "[stand-in lacks: ...]"; none may remain.
        self.assertEqual(uncovered_stand_in_types(read(GUARD), self.context), [], "unregistered values read as a type the missing-value stand-in lacks")

    def test_the_crash_mutations_are_caught(self):
        # mutation 1: the value gone from the registry
        mutated = re.sub(r'\n[^\n]*creators\["focus travel target"\][^\n]*', "", self.context)
        self.assertTrue(mutated != self.context, "mutation 1 changed nothing (the value is not registered yet)")
        self.assertIn("focus travel target", unregistered_r5_values(mutated))
        # mutation 2: the by-value set type gone from the stand-in AND the value unregistered (the state that crashed)
        guard = read(GUARD)
        dropped = re.sub(r"\n\s*focusQuestTravelList,", "", guard)
        self.assertTrue(dropped != guard, "mutation 2 changed nothing (the type is not in the stand-in yet)")
        self.assertTrue(uncovered_stand_in_types(dropped, mutated), "the stand-in without the set type was not caught")
        # mutation 3: a stand-in type any other unregistered read needs (`current position` / `home bind` read WorldPosition)
        no_pos = re.sub(r"\n\s*WorldPosition>;", ">;", re.sub(r",\n\s*WorldPosition>;", ">;", guard))
        self.assertTrue(no_pos != guard, "mutation 3 changed nothing")
        self.assertTrue(uncovered_stand_in_types(no_pos, self.context), "the stand-in without WorldPosition was not caught")


class R5OwnerGate(unittest.TestCase):
    def setUp(self):
        self.player_ai = read(PLAYER_AI)
        self.compat_f = read_or_empty(OWNER_COMPAT_F)
        self.player_cmd = read(PLAYER_CMD)

    def test_chat_owner_gate_uses_the_battlenet_check(self):
        check_owner_gate(self.player_ai, self.compat_f, self.player_cmd)

    def test_turtle_side_keeps_the_game_account_rule(self):
        # Turtle has no Battle.net account: its helper is the old comparison, moved out of PlayerbotAI.cpp.
        t = chk.strip_comments(read_or_empty(OWNER_COMPAT_T))
        self.assertRegex(t, r"AccountIdOfCharacterCompat\s*\(.*\)\s*==\s*senderAccount")
        sw = read_or_empty(OWNER_COMPAT_SWITCH)
        self.assertIn('__has_include("TbCompat_Owner.h")', sw)
        self.assertIn('#include "cmangos-compat-owner.h"', sw)
        self.assertIn('#include "playerbot/TbCompatOwner.h"', self.player_ai)

    def test_gate_mutations_turn_it_red(self):
        mutations = [
            # the pre-fix line is back: game accounts compared again
            (0, self.player_ai.replace("return BotOwnedBySenderCompat(bot, fromPlayer, senderAccount);",
                                       "return AccountIdOfCharacterCompat(bot->getObjectGuid()) == senderAccount;", 1)),
            # the host compares the sender's game account with the bot's
            (2, self.player_cmd.replace("return ChatOwnerAllowed(", "return sender.GetSession()->GetAccountId() == gameAccount || ChatOwnerAllowed(", 1)),
            # the host drops the fresh Battle.net read
            (2, self.player_cmd.replace("botOwnerBnet = scope.Owns(gameAccount) ? scope.bnet : 0", "botOwnerBnet = scope.bnet", 1)),
        ]
        for i, (slot, mutated) in enumerate(mutations):
            args = [self.player_ai, self.compat_f, self.player_cmd]
            self.assertTrue(mutated != args[slot], "mutation %d changed nothing (the fix is not in yet)" % i)
            args[slot] = mutated
            with self.assertRaises(AssertionError, msg="gate mutation %d was not caught" % i):
                check_owner_gate(*args)

    def test_forever_has_no_durable_owner_record_for_the_fallback_to_override(self):
        # Codex (R5 fix review): a BotRecord whose owner differs would still fall through to the Battle.net helper. On ForeverCore no such
        # record exists: the host's BotManager answers "no record" for every bot, so the helper IS the legacy gate's only owner path there
        # (the same rule as `.bot`). Pin both answers so a later round that adds records must revisit the order.
        text = chk.strip_comments(read("forever/src/host/TbHostBotManager.cpp"))
        self.assertRegex(text, r"BotRecord\* BotManager::FindBot\(ObjectGuid[^)]*\)\s*\{\s*return nullptr;\s*\}")
        self.assertRegex(text, r"bool BotManager::GetOwnedCharacter\([^)]*\)\s*\{\s*return false;")

    def test_pure_rule(self):
        # The rule without the core, compiled and run: the owner's OTHER game account (same Battle.net account) is the owner; a stranger,
        # an unlinked account (bnet 0) and a bot on another Battle.net account are not; a GM always passes.
        src = r'''
#include "host/player/TbPlayerAuthPure.h"
#include <cstdio>
using namespace TortoiseBots::player;
int main()
{
    int bad = 0;
    auto expect = [&](bool got, bool want, char const* what) { if (got != want) { std::printf("FAIL %s\n", what); ++bad; } };
    // callerBnet, botOwnerBnet (the caller's Battle.net account when the bot's game account is one of its, else 0)
    expect(ChatOwnerAllowed(false, 42, 42), true, "same battle.net account (other game account)");
    expect(ChatOwnerAllowed(false, 42, 0), false, "bot not on the caller's account");
    expect(ChatOwnerAllowed(false, 0, 0), false, "no bnet at all");
    expect(ChatOwnerAllowed(false, 0, 42), false, "caller without bnet");
    expect(ChatOwnerAllowed(false, 42, 43), false, "different bnet");
    expect(ChatOwnerAllowed(true, 0, 0), true, "gm");
    return bad;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            cpp = os.path.join(tmp, "t.cpp")
            exe = os.path.join(tmp, "t")
            with open(cpp, "w") as f:
                f.write(src)
            build = subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-I", os.path.join(ROOT, "forever", "src"), cpp, "-o", exe],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr[-600:])
            run = subprocess.run([exe], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout)


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
"""Wiring pins and mutations for `.bot totem` and `.bot behavior` (#133 round 1) and `.bot blessing`, `.bot choice` and
`.bot service` (#133 round 2), all documented in forever/docs/TB-COMMANDS.md section 1a.

The core calls cannot run without a server, so these tests pin where the handlers stand, the order in which they decide,
validate and write, and the refusal codes they may answer; the pure rules are reached through forever/tools/test_choice_cmd.cpp.
Every assertion fails on the pre-round source (no verb, no handler, no table row). With CHOICE_CMD_MUTATE=1 (run on the build
VM) each mutation below is applied to the real sources and must turn the matching check red:
  status-owner            status classified as an owner-tier write
  aoe-not-inverted        `aoe` ("no aoe") read the right way round
  offspec-check-dropped   an off-spec toggle set on a class that has no off-spec strategies
  set-half-applied        a `set` writing one slot before the whole list is validated for that bot
  decide-after-set        a write reached before the ownership decision
  poison-known-dropped    a poison order stored without checking the bot holds a usable poison item for it
  portal-policy-skipped   a portal cast reached without the policy's refusal decision
  non-paladin-stored      a blessing order stored on a bot that is not a paladin
"""
import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
PLAYER = ROOT / "forever" / "src" / "host" / "player"
TOOLS = ROOT / "forever" / "tools"
AUTH_PURE = PLAYER / "TbPlayerAuthPure.h"
CHOICE_PURE = PLAYER / "TbChoiceCmdPure.h"
CHOICE_H = PLAYER / "TbChoiceCmd.h"
CHOICE_CPP = PLAYER / "TbChoiceCmd.cpp"
PLAYER_CMD = PLAYER / "TbPlayerCmd.cpp"
DOC = ROOT / "forever" / "docs" / "TB-COMMANDS.md"
UNIT = TOOLS / "test_choice_cmd.cpp"


def read(path):
    return Path(path).read_text()


def function_body(source, signature_regex):
    m = re.search(signature_regex, source)
    assert m, f"function not found: {signature_regex}"
    start = source.index("{", m.end() - 1 if source[m.end() - 1] == "{" else m.end())
    depth = 0
    for i in range(start, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[start:i + 1]
    raise AssertionError("unbalanced braces")


def branch(body, marker, next_marker=None):
    """The text of one sub-command block: from `marker` to `next_marker` (or the end of the function)."""
    assert marker in body, f"branch marker missing: {marker}"
    start = body.index(marker)
    if next_marker is None:
        return body[start:]
    assert next_marker in body[start:], f"branch end marker missing after {marker}: {next_marker}"
    return body[start:body.index(next_marker, start)]


def first_write(text):
    hits = [i for i in (text.find("PutValue("), text.find("ChangeStrategy(")) if i >= 0]
    assert hits, "no write call in the block"
    return min(hits)


TOTEM_SIG = r"bool HandleTotem\(Player\* player, player::Caller const& caller, player::OwnerScope const& scope, std::string_view tail\)\s*\{"
BEHAVIOR_SIG = r"bool HandleBehavior\(Player\* player, player::Caller const& caller, player::OwnerScope const& scope, std::string_view tail\)\s*\{"
TARGETS_SIG = (r"bool TargetsOf\(Player\* player,\s*player::Caller const& caller,\s*player::OwnerScope const& scope,\s*"
               r"std::string const& verb,\s*ParsedScope const& parsed,\s*player::Tier tier,\s*std::vector<Target>& targets\)\s*\{")


# ---- the checks the mutations must break (plain functions so the mutate runner can call them on mutated text) ----

def check_dispatch(cpp):
    dispatch = function_body(cpp, r"bool Dispatch\(Player\* player, std::string_view rest\)\s*\{")
    assert 'verb == "totem"' in dispatch, "Dispatch does not route totem"
    assert "choice::HandleTotem(player, caller, scope, AfterFirstWord(rest))" in dispatch
    assert 'verb == "behavior" || verb == "behaviour"' in dispatch, "Dispatch does not route behavior/behaviour"
    assert "choice::HandleBehavior(player, caller, scope, AfterFirstWord(rest))" in dispatch
    assert dispatch.index('verb == "totem"') < dispatch.index('"unknown-verb"'), "choice verbs must answer before unknown-verb"


def check_help(cpp):
    body = function_body(cpp, r"bool HandleHelp\(Player\* player, Caller const& caller\)")
    assert ".bot totem" in body, "help row for totem missing"
    assert ".bot behavior" in body, "help row for behavior missing"
    assert "until logout" in body, "the help rows must say the values live until logout (#135)"


def check_targets_decide(cpp):
    body = function_body(cpp, TARGETS_SIG)
    assert "player::Decide(caller, " in body, "TargetsOf must decide with the pure rule"
    assert "player::CodeOf(" in body, "TargetsOf must read the refusal code from the pure rule"
    assert "IsBot(" in body, "only bots become targets"
    assert "kMaxPartyBots" in body, "the party scope must be capped"
    assert "std::sort(" in body, "the party scope must be ordered (the cap must be deterministic)"
    assert "GetMemberSlots()" in body, "the party scope is the caller's group"


def check_totem_rules(cpp):
    body = function_body(cpp, TOTEM_SIG)
    assert '"usage"' in body, "totem refusal code usage missing"
    assert '"not-known"' in body, "totem refusal code not-known missing"
    assert "ShamanTarget(" in body, "the shaman class check is shared by every totem write"
    assert "ai::NormalizeTotemOrder(" in body, "the slot write goes through the pure order parser"
    assert "ai::ParseTotemSetList(" in body, "set is validated by the pure four-slot parser"
    assert "ai::ParseTotemDropMode(" in body, "mode goes through the pure parser"
    assert "ai::TotemOrderKey(" in body, "the slot value key is the policy's"
    assert "ai::TotemModeKey()" in body, "the mode value key is the policy's"
    assert "TierOfChoice(verb, sub)" in body, "the tier comes from the pure choice rule"
    assert "TargetsOf(player, caller, scope, verb, parsed, TierOfChoice(verb, sub), targets)" in body, "every target is decided at the tier of the line"
    assert "player::Decide(caller, player::FactsOf(player, bot, scope), tier)" in cpp, "the decision uses the tier the handler passed"
    shaman = function_body(cpp, r"bool ShamanTarget\(Player\* player, std::string const& verb, Target const& target, PlayerbotAI\*& botAi\)\s*\{")
    assert '"not-shaman"' in shaman, "a non-shaman is refused"
    assert "CLASS_SHAMAN" in shaman, "the refusal is the class"
    assert "sPlayerbotDbStore" not in cpp, "persistence is #135: values live until logout"


def check_totem_set_validated_first(cpp):
    body = function_body(cpp, TOTEM_SIG)
    set_block = branch(body, 'if (sub == "set")', '// `<slot> <totem|auto|none>`')
    assert "ai::ParseTotemSetList(" in set_block, "set must parse the pure list first"
    assert "Pass 2" in set_block, "set must apply only after the validation pass"
    validate = set_block[:set_block.index("Pass 2")]
    assert "PutValue(" not in validate, "no slot may be written before the whole list is validated for that bot"
    assert "->Set(" not in validate, "no slot may be written before the whole list is validated for that bot"
    apply_block = set_block[set_block.index("Pass 2"):]
    assert "for (int slot = 0; slot < 4; ++slot)" in apply_block, "all four slots are written in the apply pass"
    assert apply_block.count("PutValue(") == 1


def check_totem_decide_before_write(cpp):
    body = function_body(cpp, TOTEM_SIG)
    blocks = {
        "mode": branch(body, 'if (sub == "mode")', 'if (sub == "set")'),
        "set": branch(body, 'if (sub == "set")', '// `<slot> <totem|auto|none>`'),
        "slot": branch(body, '// `<slot> <totem|auto|none>`', None),
    }
    for name, block in blocks.items():
        assert "TargetsOf(" in block, f"the {name} branch does not gather its targets"
        assert block.index("TargetsOf(") < first_write(block), f"the {name} branch writes before the decision"
        assert block.index("ShamanTarget(") < first_write(block), f"the {name} branch writes before the class check"


def check_behavior_rules(cpp):
    body = function_body(cpp, BEHAVIOR_SIG)
    assert "FindToggle(" in body, "the toggle key is looked up in the pure table"
    assert "GetStrategy(" in body, "the strategy must be registered in the bot's context"
    assert '"not-in-build"' in body, "an unregistered strategy refuses not-in-build"
    assert "ai::ClassHasOffSpec(" in body, "off-spec toggles need an off-spec class"
    assert '"class"' in body, "the class refusal code is used"
    assert "ToggleAppliesToClass(" in body, "the off-spec rule is the pure one"
    assert '"failed"' in body, "a strategy that does not hold reads back as failed"
    assert "IsToggleOn(" in body, "the status line reads the strategy through the toggle's inversion"
    assert "ToggleChange(" in body, "the write goes through the pure inversion"
    assert "ChangeStrategy(" in body, "the write is a ChangeStrategy"
    assert "HasStrategy(" in body, "the write is read back with HasStrategy"
    assert "sPlayerbotDbStore" not in cpp, "persistence is #135"


def check_offspec_guard(cpp):
    body = function_body(cpp, BEHAVIOR_SIG)
    write = branch(body, "// `<toggle> <on|off>`", None)
    assert "ai::ClassHasOffSpec(" in write, "the write path must read the off-spec class"
    assert "ToggleAppliesToClass(" in write, "the write path must apply the pure off-spec rule"
    assert write.index("ai::ClassHasOffSpec(") < write.index("ChangeStrategy("), "the class is checked before the change"


def check_behavior_order(cpp):
    body = function_body(cpp, BEHAVIOR_SIG)
    write = branch(body, "// `<toggle> <on|off>`", None)
    assert "TargetsOf(" in write, "the write path does not gather its targets"
    assert write.index("TargetsOf(") < first_write(write), "the write path writes before the decision"
    assert write.index("GetStrategy(") < write.index("ChangeStrategy("), "registration is checked before the change"
    assert "ai::ClassHasOffSpec(" in write, "the write path must read the off-spec class"
    assert write.index("ai::ClassHasOffSpec(") < write.index("ChangeStrategy("), "the off-spec class is checked before the change"
    assert write.index("ChangeStrategy(") < write.index("HasStrategy("), "the change is read back afterwards"
    assert body.index('if (sub == "status")') < body.index("// `<toggle> <on|off>`"), "status reads only"


def check_off_build(cpp):
    off = cpp.split("#else", 1)[1]
    assert "bool HandleTotem(Player*, player::Caller const&, player::OwnerScope const&, std::string_view) { return false; }" in off
    assert "bool HandleBehavior(Player*, player::Caller const&, player::OwnerScope const&, std::string_view) { return false; }" in off


def check_pure_core_free(auth, pure):
    for source in (auth, pure):
        for line in source.splitlines():
            stripped = line.strip()
            if stripped.startswith("#include"):
                assert '"' not in stripped or "host/player/TbPlayerAuthPure.h" in stripped, f"pure header includes {stripped}"


def check_headers(hdr, cpp):
    for name in ("HandleTotem", "HandleBehavior"):
        assert name in hdr, f"{name} missing from TbChoiceCmd.h"
    assert "player::Caller const&" in hdr and "player::OwnerScope const&" in hdr
    assert "namespace choice" in hdr, "the handlers live in TortoiseBots::choice"


def check_docs(doc):
    section = doc[doc.index("## 1a. `.bot` (players)"):]
    assert ".bot totem" in section, "TB-COMMANDS.md 1a lacks the totem row"
    assert ".bot behavior" in section, "TB-COMMANDS.md 1a lacks the behavior row"
    assert "until logout" in section, "the doc must say the values live until logout (#135)"
    for code in ("not-shaman", "not-known", "not-in-build"):
        assert code in section, f"the doc must name the refusal code {code}"


def check_unit_names(unit):
    for case in (
        "ToggleTableMatchesTb", "AoeIsInverted", "StatusIsTactical", "SetIsOwner", "UnknownSubIsOwner", "ScopePartyAndName",
    ):
        assert f"case: {case}" in unit, f"missing case {case}"


# ---- round 2: `.bot blessing`, `.bot choice`, `.bot service` ----

BLESSING_SIG = r"bool HandleBlessing\(Player\* player, player::Caller const& caller, player::OwnerScope const& scope, std::string_view tail\)\s*\{"
CHOICE_SIG = r"bool HandleChoice\(Player\* player, player::Caller const& caller, player::OwnerScope const& scope, std::string_view tail\)\s*\{"
SERVICE_SIG = r"bool HandleService\(Player\* player, player::Caller const& caller, player::OwnerScope const& scope, std::string_view tail\)\s*\{"
PALADIN_SIG = r"bool PaladinTarget\(Player\* player, std::string const& verb, Target const& target, PlayerbotAI\*& botAi\)\s*\{"
CHOICE_KNOWN_SIG = r"bool ChoiceKnown\(PlayerbotAI\* botAi, Player\* bot, int kind, std::string const& word\)\s*\{"


def check_dispatch_r2(cpp):
    dispatch = function_body(cpp, r"bool Dispatch\(Player\* player, std::string_view rest\)\s*\{")
    unknown = dispatch.index('"unknown-verb"')
    for verb, call in (
        ("blessing", "choice::HandleBlessing(player, caller, scope, AfterFirstWord(rest))"),
        ("choice", "choice::HandleChoice(player, caller, scope, AfterFirstWord(rest))"),
        ("service", "choice::HandleService(player, caller, scope, AfterFirstWord(rest))"),
    ):
        assert f'verb == "{verb}"' in dispatch, f"Dispatch does not route {verb}"
        assert call in dispatch, f"Dispatch does not call {call.split('(')[0]}"
        assert dispatch.index(f'verb == "{verb}"') < unknown, f"{verb} must answer before unknown-verb"


def check_help_r2(cpp):
    body = function_body(cpp, r"bool HandleHelp\(Player\* player, Caller const& caller\)")
    for verb in (".bot blessing", ".bot choice", ".bot service"):
        assert verb in body, f"help row for {verb} missing"
    assert "until logout" in body, "the help rows must say the values live until logout (#135)"


def check_blessing_rules(cpp):
    body = function_body(cpp, BLESSING_SIG)
    for need in ('"usage"', "ai::BlessingOrderClassIndex(", "ai::IsBlessingOrderWord(", "ai::BlessingOrderKey(",
                 "ai::BlessingOrderPlayersKey()", "ai::SetBlessingPlayerOrder(", "PaladinTarget(", "BlessingKnown(",
                 "TierOfChoice(verb, sub)", "TargetsOf(player, caller, scope, verb, parsed, TierOfChoice(verb, sub), targets)"):
        assert need in body, f"the blessing handler lacks {need}"
    assert '"status"' in body, "the blessing handler has no status branch"
    known = function_body(cpp, r"bool BlessingKnown\(PlayerbotAI\* botAi, std::string const& word\)\s*\{")
    assert "ai::BlessingOrderSpell(" in known, "the blessing check reads the policy's spell"
    assert "HasSpell(" in known, "the blessing check is the bot's own book"
    assert "sPlayerbotDbStore" not in cpp, "persistence is #135"


def check_blessing_order(cpp):
    body = function_body(cpp, BLESSING_SIG)
    write = branch(body, "bool const playerMode", None)
    assert "TargetsOf(" in write, "the blessing write does not gather its targets"
    assert write.index("TargetsOf(") < first_write(write), "the blessing write writes before the decision"
    assert "PaladinTarget(" in write, "the blessing write does not run the paladin check"
    assert write.index("PaladinTarget(") < first_write(write), "the blessing write writes before the class check"
    assert "BlessingKnown(" in write, "the blessing write does not check the spell"
    assert write.index("BlessingKnown(") < first_write(write), "the blessing write stores a spell the bot has not trained"
    assert "PutValue(botAi, ai::BlessingOrderPlayersKey()" in write, "the player form stores the per-character exception"
    assert "PutValue(botAi, ai::BlessingOrderKey(" in write, "the class form stores the class row"


def check_paladin_guard(cpp):
    body = function_body(cpp, PALADIN_SIG)
    assert '"not-paladin"' in body, "a non-paladin is refused"
    assert "if (target.bot->GetClass() != CLASS_PALADIN)" in body, "the refusal is the class"
    assert body.index("CLASS_PALADIN") < body.index("return true;"), "the class is checked before the bot passes"


def check_choice_rules(cpp):
    body = function_body(cpp, CHOICE_SIG)
    for need in ('"usage"', "ai::ChoiceKindIndex(", "ai::IsChoiceWord(", "ai::ChoiceKindsForClass(", '"class"',
                 "ai::ChoiceOrderKey(", "ChoiceKnown(", "TierOfChoice(verb, sub)",
                 "TargetsOf(player, caller, scope, verb, parsed, TierOfChoice(verb, sub), targets)"):
        assert need in body, f"the choice handler lacks {need}"
    assert '"status"' in body, "the choice handler has no status branch"
    known = function_body(cpp, CHOICE_KNOWN_SIG)
    assert "if (ai::ChoiceKindUsesItem(kind))" in known, "a poison kind is a bag item, not a spell"
    assert "ai::HasUsablePoisonItem(" in known, "a poison word is known by a usable poison item"
    assert "ai::ChoicePoisonItemIds(" in known, "the poison coin's item ids are the policy's"
    assert "ai::ChoiceSpell(" in known, "every other kind is a spell of the bot's own book"
    assert "sPlayerbotDbStore" not in cpp, "persistence is #135"


def check_choice_order(cpp):
    body = function_body(cpp, CHOICE_SIG)
    write = branch(body, "// `<kind> <value|auto>`", None)
    assert "TargetsOf(" in write, "the choice write does not gather its targets"
    assert write.index("TargetsOf(") < first_write(write), "the choice write writes before the decision"
    assert "ai::ChoiceKindsForClass(" in write, "the choice write does not read the kinds of the bot's class"
    assert write.index("ai::ChoiceKindsForClass(") < first_write(write), "the choice write stores a kind the class does not have"
    assert "ChoiceKnown(" in write, "the choice write does not check the word"
    assert write.index("ChoiceKnown(") < first_write(write), "the choice write stores a word the bot cannot use"


def check_service_rules(cpp):
    body = function_body(cpp, SERVICE_SIG)
    for need in ('"usage"', "ai::FormatServicePortals(", "ai::ConjureFoodSpellName()", "ai::ConjureWaterSpellName()",
                 "ai::CreateHealthstoneSpellName()", "ai::DecidePortalCast(", "ai::PortalRefusalCode(",
                 "ai::PortalRefusalText(", "ai::PortalCityName(", "ai::PortalRuneItemId()", "AddItemCompat(",
                 '"action-not-registered"', 'ActionRegistered(botAi, "cast")', 'DoSpecificAction("cast"',
                 "HighestSpellOfLine(", "TierOfChoice(verb, sub)", "TargetsOf(player, caller, scope, verb, parsed,"):
        assert need in body, f"the service handler lacks {need}"
    query = branch(body, "if (sub.empty())", 'if (sub == "portal")')
    assert "DoSpecificAction(" not in query, "the bare service query must not cast"
    assert "AddItemCompat(" not in query, "the bare service query must not grant an item"


def check_service_order(cpp):
    body = function_body(cpp, SERVICE_SIG)
    assert body.index("TargetsOf(player, caller, scope, verb, parsed, TierOfChoice(verb, sub), targets)") >= 0
    assert body.index("ai::DecidePortalCast(") < body.index('DoSpecificAction("cast"'), "the policy decides before the cast"
    refusal = "if (decision.refusal != ai::PortalRefusal::None)"
    assert refusal in body, "the portal policy's refusal is not answered"
    assert body.index(refusal) < body.index("AddItemCompat("), \
        "the refusal must answer before the rune is granted"
    assert body.index('ActionRegistered(botAi, "cast")') < body.index('DoSpecificAction("cast"'), \
        "a missing cast action must refuse before the cast"
    assert body.index("HighestSpellOfLine(") < body.index('DoSpecificAction("cast"', body.index("HighestSpellOfLine(")), \
        "conjure picks the rank before the cast"


def check_off_build_r2(cpp):
    off = cpp.split("#else", 1)[1]
    for name in ("HandleBlessing", "HandleChoice", "HandleService"):
        assert f"bool {name}(Player*, player::Caller const&, player::OwnerScope const&, std::string_view) {{ return false; }}" in off, \
            f"the no-engine build does not stub {name}"


def check_headers_r2(hdr):
    for name in ("HandleBlessing", "HandleChoice", "HandleService"):
        assert name in hdr, f"{name} missing from TbChoiceCmd.h"


def check_docs_r2(doc):
    section = doc[doc.index("## 1a. `.bot` (players)"):]
    for verb in (".bot blessing", ".bot choice", ".bot service"):
        assert verb in section, f"TB-COMMANDS.md 1a lacks the {verb} row"
    assert "until logout" in section, "the doc must say the values live until logout (#135)"
    for code in ("not-paladin", "not-known", "class", "action-not-registered", "unknown", "group"):
        assert code in section, f"the doc must name the refusal code {code}"


def check_unit_names_r2(unit):
    for case in ("BlessingClassWords", "ChoiceKindForClass"):
        assert f"case: {case}" in unit, f"missing case {case}"


class Wiring(unittest.TestCase):
    def setUp(self):
        self.cpp = read(CHOICE_CPP)
        self.hdr = read(CHOICE_H)
        self.pure = read(CHOICE_PURE)
        self.auth = read(AUTH_PURE)
        self.cmd = read(PLAYER_CMD)
        self.unit = read(UNIT)
        self.doc = read(DOC)

    def test_dispatch_routes_the_verbs(self):
        check_dispatch(self.cmd)

    def test_help_rows(self):
        check_help(self.cmd)

    def test_targets_decide_with_the_pure_rule(self):
        check_targets_decide(self.cpp)

    def test_totem_rules(self):
        check_totem_rules(self.cpp)

    def test_totem_set_is_validated_before_it_is_applied(self):
        check_totem_set_validated_first(self.cpp)

    def test_totem_decides_before_it_writes(self):
        check_totem_decide_before_write(self.cpp)

    def test_behavior_rules(self):
        check_behavior_rules(self.cpp)

    def test_behavior_order(self):
        check_behavior_order(self.cpp)

    def test_off_build_still_links(self):
        check_off_build(self.cpp)

    def test_the_pure_headers_are_core_free(self):
        check_pure_core_free(self.auth, self.pure)

    def test_the_header_names_the_handlers(self):
        check_headers(self.hdr, self.cpp)

    def test_the_doc_rows(self):
        check_docs(self.doc)

    def test_the_required_cases_are_named(self):
        check_unit_names(self.unit)

    def test_the_tier_rule_is_the_pure_one(self):
        self.assertIn("inline Tier TierOfChoice(std::string_view verb, std::string_view sub)", self.auth)
        self.assertIn("if (subWord == \"status\")", self.auth)
        self.assertIn("return Tier::Tactical;", self.auth)
        self.assertIn("using player::TierOfChoice;", self.pure)
        self.assertIn("using player::Tier;", self.pure)

    # ---- round 2 ----

    def test_dispatch_routes_the_round2_verbs(self):
        check_dispatch_r2(self.cmd)

    def test_round2_help_rows(self):
        check_help_r2(self.cmd)

    def test_blessing_rules(self):
        check_blessing_rules(self.cpp)

    def test_blessing_decides_before_it_writes(self):
        check_blessing_order(self.cpp)

    def test_the_paladin_guard(self):
        check_paladin_guard(self.cpp)

    def test_choice_rules(self):
        check_choice_rules(self.cpp)

    def test_choice_decides_before_it_writes(self):
        check_choice_order(self.cpp)

    def test_service_rules(self):
        check_service_rules(self.cpp)

    def test_service_decides_before_it_casts(self):
        check_service_order(self.cpp)

    def test_off_build_still_links_the_round2_handlers(self):
        check_off_build_r2(self.cpp)

    def test_the_header_names_the_round2_handlers(self):
        check_headers_r2(self.hdr)

    def test_the_round2_doc_rows(self):
        check_docs_r2(self.doc)

    def test_the_round2_cases_are_named(self):
        check_unit_names_r2(self.unit)


# ---- mutations (run on the build VM: CHOICE_CMD_MUTATE=1) ----

PURE_MUTANTS = [
    # name, source path, original, mutant
    ("status-owner", AUTH_PURE,
     '    if (subWord == "status")\n        return Tier::Tactical;',
     '    if (subWord == "status")\n        return Tier::Owner;'),
    ("aoe-not-inverted", CHOICE_PURE,
     '{ "aoe", "no aoe", ToggleState::Combat, true },',
     '{ "aoe", "no aoe", ToggleState::Combat, false },'),
]

TEXT_MUTANTS = [
    # name, original, mutant, check (receives the mutated cpp text)
    ("offspec-check-dropped",
     "if (!ToggleAppliesToClass(*toggle, ai::ClassHasOffSpec(target.bot->GetClass())))",
     "if (false)",
     check_offspec_guard),
    ("set-half-applied",
     "            ready.push_back(target.bot);",
     "            ready.push_back(target.bot);\n            PutValue(botAi, ai::TotemOrderKey(0), stored[0]);",
     check_totem_set_validated_first),
    ("decide-after-set",
     '    if (sub == "set")\n    {\n        std::string stored[4];',
     '    if (sub == "set")\n    {\n        std::string stored[4];\n        for (int slot = 0; slot < 4; ++slot)\n            PutValue(AiOf(targets.empty() ? player : targets.front().bot), ai::TotemOrderKey(slot), stored[slot]);',
     check_totem_decide_before_write),
    # round 2
    ("poison-known-dropped",
     "if (ai::ChoiceKindUsesItem(kind))",
     "if (false)",
     check_choice_rules),
    ("portal-policy-skipped",
     "if (decision.refusal != ai::PortalRefusal::None)",
     "if (false)",
     check_service_order),
    ("non-paladin-stored",
     "if (target.bot->GetClass() != CLASS_PALADIN)",
     "if (false)",
     check_paladin_guard),
]


def run_unit(auth_text, pure_text):
    """Compile the real unit test against the two pure headers; True when every check passes."""
    with tempfile.TemporaryDirectory() as tmp:
        holder = Path(tmp) / "host" / "player"
        holder.mkdir(parents=True)
        (holder / "TbPlayerAuthPure.h").write_text(auth_text)
        (holder / "TbChoiceCmdPure.h").write_text(pure_text)
        exe = Path(tmp) / "test_choice_cmd"
        built = subprocess.run(["g++", "-std=c++17", "-O0", "-I", tmp, str(UNIT), "-o", str(exe)],
                               capture_output=True, text=True)
        if built.returncode != 0:
            return False, built.stderr
        ran = subprocess.run([str(exe)], capture_output=True, text=True)
        return ran.returncode == 0, ran.stdout + ran.stderr


def run_mutations():
    auth = read(AUTH_PURE)
    pure = read(CHOICE_PURE)
    ok, out = run_unit(auth, pure)
    if not ok:
        print(f"ERROR: the unmutated headers fail the unit test:\n{out}", file=sys.stderr)
        return 1
    survivors = []
    for name, path, original, mutant in PURE_MUTANTS:
        text = read(path)
        if text.count(original) != 1:
            print(f"ERROR: {name}: anchor found {text.count(original)} times in {path.name}, expected 1", file=sys.stderr)
            return 1
        changed = text.replace(original, mutant, 1)
        if path is AUTH_PURE:
            passed, _ = run_unit(changed, pure)
        else:
            passed, _ = run_unit(auth, changed)
        if passed:
            survivors.append(name)
        else:
            print(f"MUTATION CAUGHT: {name}")

    cpp = read(CHOICE_CPP)
    for name, original, mutant, check in TEXT_MUTANTS:
        if cpp.count(original) != 1:
            print(f"ERROR: {name}: anchor found {cpp.count(original)} times, expected 1", file=sys.stderr)
            return 1
        try:
            check(cpp)
        except AssertionError as error:
            print(f"ERROR: the unmutated source fails {name}'s check: {error}", file=sys.stderr)
            return 1
        try:
            check(cpp.replace(original, mutant, 1))
        except AssertionError:
            print(f"MUTATION CAUGHT: {name}")
            continue
        survivors.append(name)

    if survivors:
        print(f"ERROR: mutations survived: {survivors}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    if os.environ.get("CHOICE_CMD_MUTATE") == "1":
        sys.exit(run_mutations())
    unittest.main()

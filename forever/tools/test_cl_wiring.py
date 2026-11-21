#!/usr/bin/env python3
"""S1-R1 wiring test (chat links): the parser, the compat pair and the two call sites agree with each other. No build, no network, no core.

Run:
  python3 -m unittest forever/tools/test_cl_wiring.py
  python3 forever/tools/test_cl_wiring.py --mutate   # must FAIL (exit non-zero)

What it proves (each is a bug a green build does not catch):
  * PlayerbotAI.h reads a spell id through the compat helper, and nothing of the unported block is left in that member.
  * ChatHelper::parseItemQualifiers reads ai/playerbot/ChatLinkScan.h (size_t, never an 8-bit position) and inserts the qualifier
    through the compat helper, not the raw text; the bot's ItemQualifier converts fields with the same non-throwing rule.
  * Both sides of the pair define the two helpers; the Turtle side is still the original call, so Turtle behaviour is unchanged.
  * The `.tb link` command is registered, works in the build without the engine (no h3/Reply/RequireBuilt), and writes the grammar-2 markers.
  * The quest branch's ExtractQuestIdCompat is untouched (decision D3 is made by whoever merges second).
  * SLICE-CH.txt is SLICE-W1.txt plus this round's files, every path exists, no duplicates.
"""
import os
import re
import sys
import unittest

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))


def read(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8", errors="replace") as f:
        return f.read()


def code_only(text):
    return re.sub(r"//[^\n]*", "", text)


PLAYERBOT_AI_H = read("ai/playerbot/PlayerbotAI.h")
CHATHELPER_CPP = read("ai/playerbot/ChatHelper.cpp")
PURE_H = read("forever/src/compat/TbChatLinkPure.h")
COMPAT_F = read("forever/src/compat/TbCompat_CL.h")
COMPAT_T = read("ai/cmangos-compat-cl.h")
WRAPPER = read("ai/playerbot/TbCompatCL.h")
SCRIPTS = read("forever/src/TbScripts.cpp")
CMD_CPP = read("forever/src/host/TbChatLinkCmd.cpp")
CMD_H = read("forever/src/host/TbChatLinkCmd.h")
UNPORTED_F = read("forever/src/compat/TbCompat_Unported.h")
UNPORTED_T = read("ai/cmangos-compat-unported.h")
ITEMUSAGE = read("ai/playerbot/strategy/values/ItemUsageValue.cpp")
SCAN = read("ai/playerbot/ChatLinkScan.h")
DOCS = read("forever/docs/TB-COMMANDS.md")


def extract_spell_body(playerbot_ai_h):
    """The member's body as written in the header: from its signature to the closing brace of the class line."""
    m = re.search(r"uint32 extractSpellId\(std::string str\)[^\n]*\n(?:.*?\n)?", playerbot_ai_h)
    return m.group(0) if m else ""


def check_wiring(playerbot_ai_h, chathelper_cpp, pure_h, compat_f, compat_t, wrapper, scripts, cmd_cpp, cmd_h,
                 unported_f, unported_t, itemusage_cpp, scan_h, docs, slice_ch, slice_w1, root):
    # ---- the two call sites ----
    if "ExtractSpellIdCompat(this, str)" not in playerbot_ai_h:
        raise AssertionError("PlayerbotAI.h extractSpellId must call ExtractSpellIdCompat(this, str)")
    body = extract_spell_body(playerbot_ai_h)
    for gone in ("Feature::ChatLinks", "UnportedBlockResultCompat", "ExtractSpellIdFromLink"):
        if gone in body:
            raise AssertionError("PlayerbotAI.h extractSpellId still holds " + gone)
    if '#include "playerbot/TbCompatCL.h"' not in playerbot_ai_h:
        raise AssertionError("PlayerbotAI.h must include playerbot/TbCompatCL.h (the helper is inline in the class)")
    # The core's ExtractSpellIdFromLink is protected: the helper reaches it only as a friend of PlayerbotChatHandler (Turtle build).
    if not re.search(r"template\s*<class Handler>\s*\n\s*friend std::uint32_t ExtractSpellIdCompat\(Handler\*, std::string\);", playerbot_ai_h):
        raise AssertionError("PlayerbotAI.h must declare ExtractSpellIdCompat a friend of PlayerbotChatHandler")

    chat = code_only(chathelper_cpp)
    if '#include "playerbot/TbCompatCL.h"' not in chathelper_cpp:
        raise AssertionError("ChatHelper.cpp must include playerbot/TbCompatCL.h")
    if "ai::chatlink::ItemLinks(text)" not in chat:
        raise AssertionError("ChatHelper::parseItemQualifiers must read ai::chatlink::ItemLinks(text)")
    if "qualifiers.insert(ItemLinkQualifierCompat(link.qualifier));" not in chat:
        raise AssertionError("ChatHelper::parseItemQualifiers must insert ItemLinkQualifierCompat(link.qualifier)")
    if re.search(r"qualifiers\.insert\(\s*link\.qualifier\s*\)", chat):
        raise AssertionError("ChatHelper::parseItemQualifiers still inserts the raw qualifier")
    # A position over chat text is size_t: an 8-bit one wraps at 255 and the loop never ends (511-character messages exist).
    if re.search(r"\buint8\s+pos\b", chat) or re.search(r"\bint\s+(?:i|pos|endPos)\s*=\s*text\.find", chat):
        raise AssertionError("a chat-string position in ChatHelper.cpp is not size_t")
    if "std::string::npos" not in chat:
        raise AssertionError("ChatHelper.cpp must stop a scan on std::string::npos")
    scan = code_only(scan_h)
    for name in ("FieldValue", "ItemLinks"):
        if not re.search(r"\b%s\s*\(" % name, scan):
            raise AssertionError("ChatLinkScan.h lacks " + name)
    if "std::string_view::npos" not in scan or "std::size_t pos" not in scan:
        raise AssertionError("ChatLinkScan.h scans with std::size_t and std::string_view::npos")
    if re.search(r"\b(?:uint8|unsigned char|int)\s+pos\b", scan):
        raise AssertionError("ChatLinkScan.h holds a chat position in a type that can wrap")

    # The bot's qualifier reader converts every field with the same non-throwing rule, never std::stoi on chat text.
    usage = code_only(itemusage_cpp)
    for field in ("numbers[0]", "numbers[1]"):
        if "ai::chatlink::FieldValue(%s)" % field not in usage:
            raise AssertionError("ItemQualifier must read %s through ai::chatlink::FieldValue" % field)
    # the random property is a signed int32 (negative for suffix items): the signed non-throwing rule
    if "ai::chatlink::SignedFieldValue(numbers[2])" not in usage:
        raise AssertionError("ItemQualifier must read numbers[2] through ai::chatlink::SignedFieldValue")
    if "stoi(" in usage:
        raise AssertionError("ItemQualifier still runs std::stoi on a qualifier field")

    # ---- the parser and the pair ----
    pure = code_only(pure_h)
    for name in ("LinkId", "QuestId", "SpellId", "AllIds", "ItemLinks", "ItemQualifiers", "LegacyItemQualifier", "EncodedValue"):
        if not re.search(r"\b%s\s*\(" % name, pure):
            raise AssertionError("TbChatLinkPure.h lacks " + name)
    if "namespace chatlink" not in pure or "namespace TortoiseBots" not in pure:
        raise AssertionError("TbChatLinkPure.h must live in TortoiseBots::chatlink")
    if re.search(r"\bint64\b", pure):
        raise AssertionError("TbChatLinkPure.h names int64: it must be core-free (std::int64_t)")

    for helper, sig in (("ExtractSpellIdCompat", r"inline\s+std::uint32_t\s+ExtractSpellIdCompat\s*\("),
                        ("ItemLinkQualifierCompat", r"inline\s+std::string\s+ItemLinkQualifierCompat\s*\(")):
        if not re.search(sig, code_only(compat_f)):
            raise AssertionError("TbCompat_CL.h lacks " + helper)
        if not re.search(sig, code_only(compat_t)):
            raise AssertionError("ai/cmangos-compat-cl.h lacks " + helper)
    if "handler->ExtractSpellIdFromLink(&source)" not in compat_t:
        raise AssertionError("the Turtle side of ExtractSpellIdCompat must stay handler->ExtractSpellIdFromLink")
    if "TortoiseBots::chatlink::SpellId(str)" not in compat_f:
        raise AssertionError("the ForeverCore side of ExtractSpellIdCompat must answer chatlink::SpellId(str)")
    if "TortoiseBots::chatlink::LegacyItemQualifier(qualifier)" not in compat_f:
        raise AssertionError("the ForeverCore side of ItemLinkQualifierCompat must answer LegacyItemQualifier(qualifier)")
    if "std::string(qualifier)" not in compat_t:
        raise AssertionError("the Turtle side of ItemLinkQualifierCompat must be the identity")
    for base in ("TbCompat_CL.h", "cmangos-compat-cl.h"):
        if base not in wrapper or "__has_include" not in wrapper:
            raise AssertionError("the wrapper ai/playerbot/TbCompatCL.h must pick %s" % base)
    if "#pragma once" not in compat_f or "#pragma once" not in compat_t or "#pragma once" not in pure_h:
        raise AssertionError("every new header needs #pragma once")

    # ---- the command, which must work in the build without the engine ----
    rows = [l for l in scripts.splitlines() if "chatlink::HandleLink" in l]
    if not rows or '"link"' not in rows[0]:
        raise AssertionError('TbScripts.cpp lacks the "link" row -> chatlink::HandleLink')
    if "bool HandleLink(ChatHandler* handler, Trinity::ChatCommands::Tail args);" not in cmd_h:
        raise AssertionError("TbChatLinkCmd.h lacks HandleLink")
    cmd = code_only(cmd_cpp)
    if "bool HandleLink(ChatHandler* handler" not in cmd:
        raise AssertionError("TbChatLinkCmd.cpp lacks the handler")
    for call in (r"\bQuestId\(text\)", r"\bSpellId\(text\)", r"\bItemLinks\(text\)", r"\bLegacyItemQualifier\(link\.qualifier\)", r"\bEncodedValue\(legacy\)"):
        if not re.search(call, cmd):
            raise AssertionError("TbChatLinkCmd.cpp must answer with " + call)
    if cmd.count("ItemLinks(text)") != 1:
        raise AssertionError("TbChatLinkCmd.cpp must build items= and quals= from one ItemLinks scan")
    for forbidden in ("h3::", "Reply(", "RequireBuilt", "engine::"):
        if forbidden in cmd:
            raise AssertionError("TbChatLinkCmd.cpp names %s: .tb link must work in the build without the engine" % forbidden)
    for word in ('"link-begin"', '"link-end count=1 records=1"', "quest=", "spell=", "items=", "quals="):
        if word not in cmd:
            raise AssertionError("TbChatLinkCmd.cpp lacks the reply word " + word)

    # ---- D3 is not done here ----
    if "template <class Handler>\nuint32 ExtractQuestIdCompat" not in unported_f and "ExtractQuestIdCompat(Handler* handler" not in unported_f:
        raise AssertionError("TbCompat_Unported.h lost ExtractQuestIdCompat")
    if 'handler->ExtractKeyFromLink(&source, "Hquest")' not in unported_t:
        raise AssertionError("the Turtle ExtractQuestIdCompat must keep the original link call")

    # ---- the docs and the list ----
    if "`.tb link" not in docs:
        raise AssertionError("TB-COMMANDS.md does not document .tb link")
    w1 = [l.strip() for l in slice_w1.splitlines() if l.strip() and not l.strip().startswith("#")]
    ch = [l.strip() for l in slice_ch.splitlines() if l.strip() and not l.strip().startswith("#")]
    if len(ch) != len(set(ch)):
        raise AssertionError("SLICE-CH.txt has duplicate paths")
    missing = [p for p in w1 if p not in ch]
    if missing:
        raise AssertionError("SLICE-CH.txt lacks SLICE-W1.txt paths: " + ", ".join(missing[:5]))
    for p in ch:
        if not os.path.isfile(os.path.join(root, p)):
            raise AssertionError("SLICE-CH.txt names a missing file: " + p)
    if "forever/src/host/TbChatLinkCmd.cpp" not in ch:
        raise AssertionError("SLICE-CH.txt lacks this round's new file")
    if "ai/playerbot/ChatLinkScan.h" not in ch:
        raise AssertionError("SLICE-CH.txt lacks the shared scan header")


CHATFILTER_CPP = read("ai/playerbot/ChatFilter.cpp")
CHAT_ACTIONS = {rel: read(rel) for rel in ("ai/playerbot/strategy/actions/SendMailAction.cpp", "ai/playerbot/strategy/actions/TradeAction.cpp",
                                          "ai/playerbot/strategy/actions/AhAction.cpp")}


NAMED_H = read("ai/playerbot/strategy/NamedObjectContext.h")


def check_text_parsers(chathelper_cpp, chatfilter_cpp, itemusage_cpp, scan_h, pure_h, actions, named_h):
    """Chat text is parsed with size_t positions and non-throwing numbers: no 8-bit index over a string, no std::sto* on chat text."""
    for name, src in (("ChatHelper.cpp", chathelper_cpp), ("ChatFilter.cpp", chatfilter_cpp)):
        code = code_only(src)
        if re.search(r"\b(?:uint8|uint16|int8|int16|unsigned char|short)\s+\w+\s*=\s*0?\s*;[^\n]*\b\w+\s*<\s*[\w.]*(?:length|size)\(\)", code) or \
                re.search(r"for\s*\(\s*(?:uint8|uint16|int8|int16|unsigned char|short)\s+\w+[^;]*;[^;]*(?:length|size)\(\)", code):
            raise AssertionError(name + " counts a string with a type narrower than size_t")
        if re.search(r"\b(?:std::)?(?:stoi|stol|stoll|stoul|stoull|stof|stod)\s*\(", code):
            raise AssertionError(name + " runs a throwing std::sto* conversion on chat text")
    if "ai::chatlink::ParseMoney(text)" not in code_only(chathelper_cpp):
        raise AssertionError("ChatHelper::parseMoney must read ai::chatlink::ParseMoney(text)")
    for rel, src in actions.items():
        if re.search(r"\b(?:int|uint8|uint16|int8)\s+pos\s*=\s*\w+\.(?:r?find)\(", code_only(src)):
            raise AssertionError(rel + " holds a string position in a type narrower than size_t")
    named = code_only(named_h)
    if re.search(r"\b(?:stoi|stol|stoul)\s*\(", named) or re.search(r"\bint8\s+level\b", named) or "|| level < 0" in named \
            or re.search(r"for\s*\(\s*uint8\s+i\s*=\s*0[^;]*;[^;]*size\(\)", named):
        raise AssertionError("NamedObjectContext.h parses qualifier text with a throwing, 8-bit or unbounded step")
    scan = code_only(scan_h)
    for name in ("ParseUnsigned", "ParseSigned", "SignedFieldValue", "ParseMoney"):
        if not re.search(r"\b%s\s*\(" % name, scan):
            raise AssertionError("ChatLinkScan.h lacks " + name)
    if re.search(r"\b(?:uint8_t|uint16_t|int8_t|int16_t|unsigned char|int|short)\s+i\s*=\s*0", scan):
        raise AssertionError("ChatLinkScan.h counts text with a narrow index")
    if "std::size_t i = 0; i < text.size()" not in scan:
        raise AssertionError("ChatLinkScan.h ParseMoney must loop with std::size_t")
    # the item qualifier's random property keeps its sign on every core
    if "ai::chatlink::SignedFieldValue(numbers[2])" not in code_only(itemusage_cpp):
        raise AssertionError("ItemQualifier must read the random property through SignedFieldValue")
    if "SignedFieldValue(fields[i])" not in code_only(pure_h):
        raise AssertionError("LegacyItemQualifier must keep a negative random property")


class ChatTextParsersWiring(unittest.TestCase):
    def test_intact(self):
        check_text_parsers(CHATHELPER_CPP, CHATFILTER_CPP, ITEMUSAGE, SCAN, PURE_H, CHAT_ACTIONS, NAMED_H)

    def test_mutations_fail(self):
        base = (CHATHELPER_CPP, CHATFILTER_CPP, ITEMUSAGE, SCAN, PURE_H, CHAT_ACTIONS, NAMED_H)
        ah = "ai/playerbot/strategy/actions/AhAction.cpp"
        mutations = [
            (0, CHATHELPER_CPP.replace("return ai::chatlink::ParseMoney(text);",
                                       "for (uint8 i = 0; i < text.length(); i++) {}\n    return 0;")),
            (0, CHATHELPER_CPP.replace("ai::chatlink::ParseUnsigned(itemStr, itemID)", "std::stoi(itemStr)")),
            (1, CHATFILTER_CPP.replace("ai::chatlink::ParseUnsigned(num, percent)", "stoul(num)")),
            (2, ITEMUSAGE.replace("ai::chatlink::SignedFieldValue(numbers[2])", "ai::chatlink::FieldValue(numbers[2])")),
            (3, SCAN.replace("std::size_t i = 0; i < text.size()", "std::uint8_t i = 0; i < text.size()")),
            (4, PURE_H.replace("SignedFieldValue(fields[i])", "FieldValue(fields[i])")),
            (5, dict(CHAT_ACTIONS, **{ah: CHAT_ACTIONS[ah].replace("size_t pos = text.find", "int pos = text.find")})),
            (6, NAMED_H.replace("int level = 0;", "int8 level = 0;")),
            (6, NAMED_H.replace("for (size_t i = 0; i < qualifiers.size()", "for (uint8 i = 0; i < qualifiers.size()")),
            (6, NAMED_H.replace("if (ai::chatlink::ParseSigned(qualifiers[pos], value))", "value = stoi(qualifiers[pos]); if (true)")),
        ]
        for i, (slot, mutation) in enumerate(mutations):
            args = list(base)
            self.assertNotEqual(mutation, args[slot], "mutation %d changed nothing" % i)
            args[slot] = mutation
            with self.assertRaises(AssertionError, msg="mutation %d was not caught" % i):
                check_text_parsers(*args)


class ChatLinkWiring(unittest.TestCase):
    def test_wiring_intact(self):
        check_wiring(PLAYERBOT_AI_H, CHATHELPER_CPP, PURE_H, COMPAT_F, COMPAT_T, WRAPPER, SCRIPTS, CMD_CPP, CMD_H,
                     UNPORTED_F, UNPORTED_T, ITEMUSAGE, SCAN, DOCS, read("forever/SLICE-CH.txt"), read("forever/SLICE-W1.txt"), ROOT)

    def test_mutations_fail(self):
        base = (PLAYERBOT_AI_H, CHATHELPER_CPP, PURE_H, COMPAT_F, COMPAT_T, WRAPPER, SCRIPTS, CMD_CPP, CMD_H,
                UNPORTED_F, UNPORTED_T, ITEMUSAGE, SCAN, DOCS, read("forever/SLICE-CH.txt"), read("forever/SLICE-W1.txt"), ROOT)
        mutations = [
            # the old unported call back in PlayerbotAI.h
            (0, PLAYERBOT_AI_H.replace("return ExtractSpellIdCompat(this, str);",
                                   "char* source = (char*)str.c_str();\n        return UnportedBlockResultCompat(tbcompat::Feature::ChatLinks, uint32(0),\n"
                                   "            [&](auto* self) -> uint32 { return self->ExtractSpellIdFromLink(&source); }, this);")),
            # the friend declaration gone (the Turtle build then cannot reach the protected extractor)
            (0, PLAYERBOT_AI_H.replace("template <class Handler>\n    friend std::uint32_t ExtractSpellIdCompat(Handler*, std::string);", "")),
            # the raw qualifier back in ChatHelper
            (1, CHATHELPER_CPP.replace("qualifiers.insert(ItemLinkQualifierCompat(link.qualifier));", "qualifiers.insert(link.qualifier);")),
            # the 8-bit scan position back in ChatHelper
            (1, CHATHELPER_CPP.replace("    for (ai::chatlink::ItemLink const& link : ai::chatlink::ItemLinks(text))\n"
                                       "        qualifiers.insert(ItemLinkQualifierCompat(link.qualifier));",
                                       "    uint8 pos = 0;\n    while (true) { int i = text.find(\"Hitem:\", pos); if (i == -1) break; pos = i + 6;"
                                       " int endPos = text.find('|', pos); if (endPos == -1) break;"
                                       " qualifiers.insert(ItemLinkQualifierCompat(text.substr(pos, endPos - pos))); }")),
            # the command unregistered
            (6, SCRIPTS.replace("TortoiseBots::chatlink::HandleLink", "TortoiseBots::chatlink::HandleNothing")),
            # a parser that reaches into the engine build's reply type (would not build OFF)
            (7, CMD_CPP.replace("handler->SendSysMessage(line);", "h3::Reply(handler).Line(line);")),
            # the command's two lists from different scans
            (7, CMD_CPP.replace("ItemLinks(text)", "ItemQualifiers(text)")),
            # the command stops encoding the qualifier it writes
            (7, CMD_CPP.replace("EncodedValue(legacy)", "legacy")),
            # the shared scan back to a wrapping position
            (12, SCAN.replace("std::size_t pos = 0;", "unsigned char pos = 0;")),
            # the Turtle side no longer the original call
            (4, COMPAT_T.replace("handler->ExtractSpellIdFromLink(&source)", "00")),
            # a throwing conversion back in the bot's qualifier reader
            (11, ITEMUSAGE.replace("itemId = ai::chatlink::FieldValue(numbers[0]);", "itemId = stoi(numbers[0]);")),
            # the round's file out of the census list
            (14, read("forever/SLICE-CH.txt").replace("forever/src/host/TbChatLinkCmd.cpp\n", "")),
        ]
        for i, (slot, mutation) in enumerate(mutations):
            args = list(base)
            self.assertNotEqual(mutation, args[slot], "mutation %d changed nothing" % i)
            args[slot] = mutation
            with self.assertRaises(AssertionError):
                check_wiring(*args)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        check_wiring(PLAYERBOT_AI_H, CHATHELPER_CPP, PURE_H, COMPAT_F, COMPAT_T, WRAPPER, SCRIPTS,
                     CMD_CPP.replace("std::to_string(QuestId(text))", "std::string()"), CMD_H,
                     UNPORTED_F, UNPORTED_T, ITEMUSAGE, SCAN, DOCS, read("forever/SLICE-CH.txt"), read("forever/SLICE-W1.txt"), ROOT)
        sys.exit(0)
    unittest.main()

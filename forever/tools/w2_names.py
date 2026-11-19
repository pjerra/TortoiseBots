#!/usr/bin/env python3
"""Wide pass 2, family 1: the name map (Turtle name -> ForeverCore name) that TbCompat_W2.h carries, and the list of files
(outside forever/tools/wide2-exclude.txt) that use one of the names and so get `#include "playerbot/TbCompatW2.h"`.

    python3 forever/tools/w2_names.py --check      print the table and the files, write nothing
    python3 forever/tools/w2_names.py --gen        rewrite the name block of TbCompat_W2.h between its markers
    python3 forever/tools/w2_names.py --include    add the wrapper include to every file that uses a name

TEXTEMOTE_* is generated from the two core enums (Turtle SharedDefines.h:394 against ForeverCore SharedDefines.h:3299): a name
is mapped only when ForeverCore has the same name with TEXT_EMOTE_ and the same value; every other name is listed as unported.
"""
import json, os, re, subprocess, sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
T_ENUM = os.path.expanduser("~/playerbots-references/penqle-tortoise-wow-modsys/src/game/SharedDefines.h")
F_ENUM = os.path.expanduser("~/ForeverCore/src/server/game/Miscellaneous/SharedDefines.h")
ALREADY = {"TEXTEMOTE_CHEER", "TEXTEMOTE_NOD", "TEXTEMOTE_NO", "TEXTEMOTE_OPENFIRE"}   # TbCompat_I2.h / TbCompat_Names.h

# Other names, each verified by hand: same meaning, same number in both cores (the table with file:line is in
# forever/docs/WIDE-PASS-R2-NOTES.md). Turtle name -> ForeverCore name.
OTHER = {
    "ITEM_CLASS_JUNK": "ITEM_CLASS_MISCELLANEOUS",
    "TRADE_STATUS_BEGIN_TRADE": "TRADE_STATUS_PROPOSED",
    "TRADE_STATUS_OPEN_WINDOW": "TRADE_STATUS_INITIATED",
    "TRADE_STATUS_TRADE_CANCELED": "TRADE_STATUS_CANCELLED",
    "TRADE_STATUS_TRADE_ACCEPT": "TRADE_STATUS_ACCEPTED",
    "BIND_QUEST_ITEM": "BIND_QUEST",
    "BIND_WHEN_USE": "BIND_ON_USE",
    "BIND_WHEN_PICKED_UP": "BIND_ON_ACQUIRE",
    "BIND_WHEN_EQUIPPED": "BIND_ON_EQUIP",
    "SKILL_2H_MACES": "SKILL_TWO_HANDED_MACES",
    "SKILL_2H_AXES": "SKILL_TWO_HANDED_AXES",
    "SKILL_2H_SWORDS": "SKILL_TWO_HANDED_SWORDS",
    "FORM_BEAR": "FORM_BEAR_FORM",
    "FORM_CAT": "FORM_CAT_FORM",
    "FORM_MOONKIN": "FORM_MOONKIN_FORM",
    "FORM_DIREBEAR": "FORM_DIRE_BEAR_FORM",
    "FORM_DEFENSIVESTANCE": "FORM_DEFENSIVE_STANCE",
    "GAMEOBJECT_TYPE_SUMMONING_RITUAL": "GAMEOBJECT_TYPE_RITUAL",
    "GAMEOBJECT_TYPE_MO_TRANSPORT": "GAMEOBJECT_TYPE_MAP_OBJ_TRANSPORT",
    "QUEST_TYPE_RAID": "QUEST_INFO_RAID",
    "QUEST_TYPE_DUNGEON": "QUEST_INFO_DUNGEON",
    "QUEST_TYPE_PVP": "QUEST_INFO_PVP",
    "UNIT_FLAG_IMMUNE_TO_PLAYER": "UNIT_FLAG_IMMUNE_TO_PC",
    "CHAT_TAG_NONE": "CHAT_FLAG_NONE",
    "TARGET_ICON_COUNT": "TARGET_ICONS_COUNT",
    "CAST_FLAG_AMMO": "CAST_FLAG_PROJECTILE",
    "EQUIP_ERR_ITEM_DOESNT_GO_TO_SLOT": "EQUIP_ERR_WRONG_SLOT",
    "EQUIP_ERR_NONEMPTY_BAG_OVER_OTHER_BAG": "EQUIP_ERR_BAG_IN_BAG",
    "EQUIP_ERR_CANT_TRADE_EQUIP_BAGS": "EQUIP_ERR_TRADE_EQUIPPED_BAG",
    "EQUIP_ERR_ONLY_AMMO_CAN_GO_HERE": "EQUIP_ERR_AMMO_ONLY",
    "EQUIP_ERR_NO_REQUIRED_PROFICIENCY": "EQUIP_ERR_PROFICIENCY_NEEDED",
    "EQUIP_ERR_NO_EQUIPMENT_SLOT_AVAILABLE": "EQUIP_ERR_NO_SLOT_AVAILABLE",
    "EQUIP_ERR_YOU_CAN_NEVER_USE_THAT_ITEM": "EQUIP_ERR_CANT_EQUIP_EVER",
    "EQUIP_ERR_YOU_CAN_NEVER_USE_THAT_ITEM2": "EQUIP_ERR_CANT_EQUIP_EVER_2",
    "EQUIP_ERR_NO_EQUIPMENT_SLOT_AVAILABLE2": "EQUIP_ERR_NO_SLOT_AVAILABLE_2",
    "EQUIP_ERR_CANT_EQUIP_WITH_TWOHANDED": "EQUIP_ERR_2HANDED_EQUIPPED",
    "EQUIP_ERR_ITEM_DOESNT_GO_INTO_BAG": "EQUIP_ERR_WRONG_BAG_TYPE",
    "EQUIP_ERR_ITEM_DOESNT_GO_INTO_BAG2": "EQUIP_ERR_WRONG_BAG_TYPE_2",
    "EQUIP_ERR_CANT_CARRY_MORE_OF_THIS": "EQUIP_ERR_ITEM_MAX_COUNT",
    "EQUIP_ERR_NO_EQUIPMENT_SLOT_AVAILABLE3": "EQUIP_ERR_NO_SLOT_AVAILABLE_3",
    "EQUIP_ERR_ITEM_CANT_STACK": "EQUIP_ERR_CANT_STACK",
    "EQUIP_ERR_ITEM_CANT_BE_EQUIPPED": "EQUIP_ERR_NOT_EQUIPPABLE",
    "EQUIP_ERR_ITEMS_CANT_BE_SWAPPED": "EQUIP_ERR_CANT_SWAP",
    "EQUIP_ERR_SLOT_IS_EMPTY": "EQUIP_ERR_SLOT_EMPTY",
    "EQUIP_ERR_CANT_DROP_SOULBOUND": "EQUIP_ERR_DROP_BOUND_ITEM",
    "EQUIP_ERR_TRIED_TO_SPLIT_MORE_THAN_COUNT": "EQUIP_ERR_TOO_FEW_TO_SPLIT",
    "EQUIP_ERR_COULDNT_SPLIT_ITEMS": "EQUIP_ERR_SPLIT_FAILED",
}


def parse_enum(path, start):
    lines = open(path, errors="ignore").read().split("\n")
    i = next(k for k, l in enumerate(lines) if re.search(start, l))
    out, val = {}, -1
    for j in range(i + 1, len(lines)):
        s = lines[j]
        if s.strip().startswith("}"):
            break
        m = re.match(r"\s*(\w+)\s*(?:=\s*([^,/]+?))?\s*,?\s*(//.*)?$", s)
        if not m or not m.group(1):
            continue
        if m.group(2):
            val = int(m.group(2).strip(), 0)
        else:
            val += 1
        out[m.group(1)] = (val, j + 1)
    return out


def emote_map():
    t = parse_enum(T_ENUM, r"^enum TextEmotes")
    f = parse_enum(F_ENUM, r"^enum TextEmotes")
    mapped, unported = {}, []
    for name, (val, tl) in t.items():
        fname = "TEXT_EMOTE_" + name[len("TEXTEMOTE_"):]
        if fname in f and f[fname][0] == val:
            mapped[name] = (fname, val, tl, f[fname][1])
        else:
            unported.append(name)
    return mapped, unported


def excluded():
    out = set()
    for l in open(os.path.join(ROOT, "forever/tools/wide2-exclude.txt")):
        l = l.strip()
        if l and not l.startswith("#"):
            out.add(os.path.normpath(l))
    return out


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", lambda m: re.sub(r"[^\n]", " ", m.group(0)), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def users(names):
    ex = excluded()
    pat = re.compile(r"\b(" + "|".join(sorted(map(re.escape, names), key=len, reverse=True)) + r")\b")
    found = {}
    for top in ("ai", "commands", "runtime", "host", "behavior"):
        for dp, _, fn in os.walk(os.path.join(ROOT, top)):
            for f in fn:
                if not f.endswith((".cpp", ".h", ".hpp")):
                    continue
                rel = os.path.relpath(os.path.join(dp, f), ROOT)
                if rel in ex or rel.startswith("ai/cmangos-compat"):
                    continue
                text = strip_comments(open(os.path.join(ROOT, rel), errors="ignore").read())
                hits = pat.findall(text)
                if hits:
                    found[rel] = sorted(set(hits))
    return found


INCLUDE = '#include "playerbot/TbCompatW2.h"'
INCLUDE_UP = '#include "../ai/playerbot/TbCompatW2.h"'   # runtime/ files reach ai/ the way their neighbours do


def add_include(rel):
    path = os.path.join(ROOT, rel)
    text = open(path, encoding="utf-8").read()
    inc = INCLUDE_UP if rel.startswith("runtime/") else INCLUDE
    if INCLUDE in text or INCLUDE_UP in text:
        return False
    lines = text.split("\n")
    last = -1
    for i, l in enumerate(lines[:80]):
        if re.match(r'\s*#\s*include\b', l):
            last = i
    if last < 0:
        return False
    # the include goes after the last include of the leading block; a #pragma once header gets it after its includes too
    lines.insert(last + 1, inc)
    open(path, "w", encoding="utf-8").write("\n".join(lines))
    return True


BEGIN, END = "// BEGIN GENERATED NAMES (forever/tools/w2_names.py --gen)", "// END GENERATED NAMES"


def gen(mapped):
    path = os.path.join(ROOT, "forever/src/compat/TbCompat_W2.h")
    text = open(path).read()
    a, b = text.index(BEGIN), text.index(END)
    out = [BEGIN,
           "// TEXTEMOTE_*: Turtle TextEmotes (SharedDefines.h:394+) -> ForeverCore TextEmotes (SharedDefines.h:3299+), same name with",
           "// TEXT_EMOTE_ and the same number, checked row by row by the script. %d names." % len(mapped)]
    for n in sorted(mapped, key=lambda k: mapped[k][1]):
        f, val, tl, fl = mapped[n]
        out.append("#define %-30s %-34s // %d  T:%d F:%d" % (n, f, val, tl, fl))
    out.append("")
    out.append("// Other names: same meaning, same number in both cores (table with file:line in forever/docs/WIDE-PASS-R2-NOTES.md).")
    for n, f in OTHER.items():
        out.append("#define %-44s %s" % (n, f))
    out.append(END)
    open(path, "w").write(text[:a] + "\n".join(out) + text[b + len(END):])


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "--check"
    mapped, unported = emote_map()
    for n in ALREADY:
        mapped.pop(n, None)
    names = set(mapped) | set(OTHER)
    found = users(names)
    if mode == "--check":
        print(len(mapped), "emote names;", len(OTHER), "other names; unported emotes in the Turtle enum:", unported)
        print(len(found), "files use a mapped name")
        for f in sorted(found):
            print(f, len(found[f]))
    elif mode == "--include":
        n = sum(add_include(f) for f in sorted(found))
        print("include added to", n, "files")
    elif mode == "--gen":
        gen(mapped)
    elif mode == "--json":
        json.dump({"emotes": mapped, "other": OTHER, "files": found, "unported": unported}, open(sys.argv[2], "w"), indent=1)


if __name__ == "__main__":
    main()

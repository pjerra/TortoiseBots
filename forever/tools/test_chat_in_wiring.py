#!/usr/bin/env python3
"""S1-R2 wiring test (chat commands in): the hook, the inbox, the drain, the engine kit and the command agree with each other.
No build, no network, no core.

Run:
  python3 -m unittest forever/tools/test_chat_in_wiring.py
  python3 forever/tools/test_chat_in_wiring.py --mutate   # must FAIL (exit non-zero)

What it proves (each is a bug a green build does not catch):
  * the PlayerScript is registered, overrides BOTH OnChat forms (whisper receiver, party group) and never writes the message;
  * the hook only copies guids and text into an Arrival: no Player, Group, BotMgr or binding state is read there, and the world
    thread's DrainArrivals resolves, authorizes and routes (a party line by the sender's own subgroup, as the core broadcasts it);
  * the hook judges through the shared rule for every receiver, and `.tb hear` goes through the same rule (`not-real` the only skip);
  * the drain sits in engine::Tick before UpdateAI, right after the cast results, looks the sender up again, revalidates the
    sender's authority and counts a dropped line;
  * the `chat` strategy is in the reaction kit for every class, and the keys it needs are registered by the generator;
  * every registered action's .cpp is in the build list (SLICE-W1.txt) — a key without its file links nowhere;
  * chat text handling holds no narrow index and no throwing conversion (the S1-R1 rules);
  * SLICE-CH.txt is SLICE-W1.txt plus this round's files, every path exists, no duplicates;
  * the `.tb state` block is appended, no unported row is left for chat, and TB-COMMANDS.md says what the round did.
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


PURE_H = read("forever/src/host/chat/TbChatInPure.h")
HOOK_H = read("forever/src/host/chat/TbChatIn.h")
HOOK_CPP = read("forever/src/host/chat/TbChatIn.cpp")
CMD_H = read("forever/src/host/chat/TbChatCmd.h")
CMD_CPP = read("forever/src/host/chat/TbChatCmd.cpp")
BRAIN = read("forever/src/host/TbEngineBrain.cpp")
FACTORY = read("forever/src/slice/SliceAiFactory.cpp")
CONTEXT = read("forever/src/slice/SliceAiObjectContext.cpp")
GENERATOR = read("forever/tools/slice_gen_context.py")
SCRIPTS = read("forever/src/TbScripts.cpp")
OFF = read("forever/src/host/TbH3Off.cpp")
PARTYSTATE = read("forever/src/host/TbPartyState.cpp")
BOTMGR = read("forever/src/TbBotMgr.cpp")
UNPORTED_MD = read("forever/docs/UNPORTED.md")
UNPORTED_T = read("ai/playerbot/UnportedFeatures.h")
DOCS = read("forever/docs/TB-COMMANDS.md")
CMAKE = read("CMakeLists.txt")
STATS = read("ai/playerbot/strategy/actions/StatsAction.cpp")
LISTSPELLS = read("ai/playerbot/strategy/actions/ListSpellsAction.cpp")
HELP = read("ai/playerbot/strategy/actions/HelpAction.cpp")
HELP_H = read("ai/playerbot/strategy/actions/HelpAction.h")
COMPAT_F = read("forever/src/compat/TbCompat_CH.h")
COMPAT_T = read("ai/cmangos-compat-ch.h")
COMPAT_F_UNPORTED = read("forever/src/compat/TbCompat_Unported.h")
COMPAT_T_UNPORTED = read("ai/cmangos-compat-unported.h")
WRAPPER_CH = read("ai/playerbot/TbCompatCH.h")
RTI = read("ai/playerbot/strategy/actions/RtiAction.cpp")
ITEMVISITORS = read("ai/playerbot/strategy/ItemVisitors.h")

# The trigger/action keys this round registers (the spec's table).
CH_TRIGGERS = ["follow", "stay", "attack", "tank attack", "flee", "max dps", "stats", "help", "spells", "spell", "los", "rti",
               "range", "save mana", "attackers", "possible attack targets", "co", "nc", "formation"]
CH_ACTIONS = ["tank attack chat shortcut", "flee chat shortcut", "max dps chat shortcut", "stats", "help", "spells", "spell", "los",
              "rti", "range", "save mana", "tell attackers", "tell possible attack targets", "co", "nc", "formation"]
# The legacy files those keys live in (each must be built: a registered class with no .cpp links nowhere).
CH_FILES = ["ai/playerbot/strategy/generic/ChatCommandHandlerStrategy.cpp", "ai/playerbot/strategy/actions/StatsAction.cpp",
            "ai/playerbot/strategy/actions/HelpAction.cpp", "ai/playerbot/strategy/actions/ListSpellsAction.cpp",
            "ai/playerbot/strategy/actions/TellCastFailedAction.cpp", "ai/playerbot/strategy/actions/TellTargetAction.cpp",
            "ai/playerbot/strategy/actions/RtiAction.cpp", "ai/playerbot/strategy/actions/RangeAction.cpp",
            "ai/playerbot/strategy/actions/SaveManaAction.cpp", "ai/playerbot/strategy/actions/TellLosAction.cpp"]


def context_body(text, kind):
    """The creators lines of the generated trigger (or strategy/action) contexts only."""
    out = []
    for name, base, body in re.findall(r"class (Slice\w+) : public NamedObjectContext<(\w+)>\s*\{(.*?)\n    \};", text, re.S):
        if base == kind:
            out.append(body)
    return "\n".join(out)


def check(hook_cpp, hook_h, cmd_cpp, cmd_h, brain, factory, context, generator, scripts, off, party_state, bot_mgr,
          unported_md, unported_t, docs, pure_h, cmake, slice_ch, slice_w1, root,
          stats=STATS, listspells=LISTSPELLS, help=HELP, help_h=HELP_H, compat_f=COMPAT_F, compat_t=COMPAT_T, wrapper_ch=WRAPPER_CH,
          rti=RTI, itemvisitors=ITEMVISITORS, compat_f_unported=COMPAT_F_UNPORTED, compat_t_unported=COMPAT_T_UNPORTED):
    code = code_only(hook_cpp)

    # ---- the hook: registered, both forms, the message is never written ----
    if "TortoiseBots::chatin::AddScripts();" not in scripts:
        raise AssertionError("AddSC_tortoisebots must call TortoiseBots::chatin::AddScripts()")
    if "void AddScripts();" not in hook_h:
        raise AssertionError("TbChatIn.h lacks AddScripts")
    if "class tortoisebots_chat : public PlayerScript" not in code:
        raise AssertionError("the hook is not a PlayerScript")
    whisper = re.search(r"void OnChat\(Player\* player, uint32 type, uint32 lang, std::string& (\w+), Player\* receiver\) override(.*?)\n    \}", code, re.S)
    party = re.search(r"void OnChat\(Player\* player, uint32 type, uint32 lang, std::string& (\w+), Group\* group\) override(.*?)\n    \}", code, re.S)
    if not whisper:
        raise AssertionError("the hook does not override OnChat(... Player* receiver)")
    if not party:
        raise AssertionError("the hook does not override OnChat(... Group* group)")
    for m in (whisper, party):
        name = m.group(1)
        for write in ("%s =" % name, "%s +=" % name, "%s.push_back" % name, "%s.erase" % name, "%s.clear" % name,
                      "%s.append" % name, "%s.replace" % name):
            if write in m.group(2):
                raise AssertionError("the hook writes the chat message (%s)" % write)
    # the hook only queues guids and the text: resolving a receiver, reading the group or asking BotMgr/bindings would race the
    # world thread (Player::Whisper is a public script entry) — that work belongs to DrainArrivals.
    for m in (whisper, party):
        for forbidden in ("Consider(", "BotMgr", "IsBot(", "RealPlayer", "HasBrain", "Authorized", "GetMembers()", "GetGroup("):
            if forbidden in m.group(2):
                raise AssertionError("the hook reads world state off-thread (%s)" % forbidden)
    if "Queue(false, player->GetGUID(), receiver->GetGUID(), type, lang, text);" not in whisper.group(2):
        raise AssertionError("the whisper hook must queue one arrival for the receiver's guid")
    if "Queue(true, player->GetGUID(), group->GetGUID(), type, lang, text);" not in party.group(2):
        raise AssertionError("the party hook must queue one arrival for the group's guid")

    # ---- the world thread's drain: resolve, route by subgroup, authorize ----
    if "void DrainArrivals()" not in code:
        raise AssertionError("TbChatIn.cpp lacks DrainArrivals (the world-thread resolution)")
    if "Resolve(line);" not in code:
        raise AssertionError("DrainArrivals must resolve each arrival")
    if "batch.size() < IngressCapacity" not in code:
        raise AssertionError("DrainArrivals must be bounded by the ingress capacity")
    if "ObjectAccessor::FindConnectedPlayer(senderGuid)" not in code:
        raise AssertionError("Resolve must look the sender up by guid")
    if "sGroupMgr->GetGroupByGUID(targetGuid)" not in code:
        raise AssertionError("Resolve must find the party's group by guid")
    if "group->GetMembers()" not in code or "Consider(member, sender, line.kind, line.lang, line.text, false, sameSubGroup);" not in code:
        raise AssertionError("Resolve must judge every bot of the group, with its subgroup verdict")
    if "group->SameSubGroup(senderGuid, member->GetGUID())" not in code:
        raise AssertionError("the party line must be routed by the sender's own subgroup, as the core broadcasts it")
    if "TortoiseBots::chatin::DrainArrivals();" not in bot_mgr:
        raise AssertionError("BotMgr::Update must drain the arrivals on the world thread")
    update = bot_mgr.split("void BotMgr::Update(uint32 diff)", 1)
    if len(update) != 2:
        raise AssertionError("TbBotMgr.cpp lost BotMgr::Update")
    update_body = update[1].split("\nvoid ", 1)[0]
    if "TortoiseBots::chatin::DrainArrivals();" not in update_body:
        raise AssertionError("the arrival drain must be in BotMgr::Update")
    if update_body.index("TortoiseBots::chatin::DrainArrivals();") > update_body.index("for (auto& [guid, bot] : _bots)"):
        raise AssertionError("the arrival drain must run before the bots' engine steps")
    # the subgroup rule itself, in the pure decision, after the ownership rule
    if "if (facts.kind != KindWhisper && !facts.sameSubGroup)" not in pure_h:
        raise AssertionError("Judge must refuse a party line from another subgroup")
    if pure_h.index("if (!facts.authorized)") > pure_h.index("if (facts.kind != KindWhisper && !facts.sameSubGroup)"):
        raise AssertionError("a stranger must be refused as not-authorized before the subgroup rule")
    if 'case Code::OtherSubGroup: return "other-subgroup";' not in pure_h:
        raise AssertionError("the subgroup refusal must be named other-subgroup")

    # ---- the one rule: the pure header, called from the hook and from `.tb hear` ----
    if "void DrainArrivals();" not in hook_h:
        raise AssertionError("TbChatIn.h lacks DrainArrivals")
    if "void Drop(std::uint32_t botKey);" not in hook_h:
        raise AssertionError("TbChatIn.h lacks Drop (the drain's drop counter)")
    if "bool StillAuthorized(Player* bot, Player* sender);" not in hook_h:
        raise AssertionError("TbChatIn.h lacks StillAuthorized (the delivery-time authority check)")
    if "verdict = Judge(facts);" not in code:
        raise AssertionError("TbChatIn.cpp must decide with Judge(facts)")
    if "Verdict Hear(Player* bot, Player* sender, unsigned kind, std::string const& text)" not in code:
        raise AssertionError("TbChatIn.cpp lacks Hear (the `.tb hear` entry)")
    if "return Consider(bot, sender, kind, LANG_UNIVERSAL, text, true);" not in code:
        raise AssertionError("Hear must go through the same Consider with injected=true")
    if "Verdict const verdict = Hear(bot, sender, kind, text);" not in cmd_cpp:
        raise AssertionError("`.tb hear` must call chatin::Hear")
    if "bool HandleHear(ChatHandler* handler, Trinity::ChatCommands::Tail args);" not in cmd_h:
        raise AssertionError("TbChatCmd.h lacks HandleHear")
    if '"hear"' not in scripts or "chatin::HandleHear" not in scripts:
        raise AssertionError('TbScripts.cpp lacks the "hear" row -> chatin::HandleHear')
    if "return h3::NotBuilt(handler, args, \"hear\");" not in off:
        raise AssertionError("the option-OFF build must answer .tb hear with engine-not-built")
    if "chat/TbChatCmd" not in cmake:
        raise AssertionError("CMakeLists.txt must keep TbChatCmd.cpp (an engine reply type) out of the option-OFF build")
    if "h3::Fail(handler, args, \"hear\", \"not-a-bot\"" not in cmd_cpp:
        raise AssertionError("`.tb hear` must refuse a target that is not a host bot")

    # ---- the drain: before UpdateAI, after the cast results, sender looked up again ----
    tick = re.search(r"void Tick\(Brain\* brain, uint32 diff\)(.*?)\n\}\n", brain, re.S)
    if not tick:
        raise AssertionError("TbEngineBrain.cpp lost Tick")
    body = tick.group(1)
    if "DeliverChat(brain);" not in body:
        raise AssertionError("Tick must drain the chat inbox")
    if body.index("DeliverChat(brain);") > body.index("brain->ai->UpdateAI(diff);"):
        raise AssertionError("the chat drain must run BEFORE UpdateAI")
    if body.index("DeliverCastResults(brain);") > body.index("DeliverChat(brain);"):
        raise AssertionError("the chat drain runs after the cast results (spec: as DeliverCastResults)")
    if "ObjectAccessor::FindConnectedPlayer(senderGuid)" not in brain:
        raise AssertionError("the drain must look the sender up again by guid")
    if "AllowActivity()" not in brain:
        raise AssertionError("the drain must drop a line when the bot may not act")
    if "chatin::Drop(brain->guidLow);" not in brain:
        raise AssertionError("a line dropped at the drain must be counted")
    if "chatin::StillAuthorized(brain->bot, sender)" not in brain:
        raise AssertionError("the drain must revalidate the sender's authority before the command runs")
    if "brain->ai->HandleCommand(line.kind, line.text, *sender, line.lang);" not in brain:
        raise AssertionError("the drain must hand the line to PlayerbotAI::HandleCommand")
    if "chatin::InboxCapacity" not in brain:
        raise AssertionError("the drain bound must be the inbox capacity, not a second copy of 16")
    # the batch is bounded: one tick drains at most the box, never an unbounded loop.
    if "taken < chatin::InboxCapacity" not in brain:
        raise AssertionError("the drain loop must be bounded by InboxCapacity")

    # ---- the engine kit: the reaction engine runs "chat", its keys are registered ----
    if 'reactionEngine->addStrategy("chat");' not in code_only(factory):
        raise AssertionError('SliceAiFactory must add the "chat" strategy to the reaction engine')
    if '"strategy": "chat"' not in generator:
        raise AssertionError("slice_gen_context.py CH_KEYS must name the chat strategy")
    strategies = context_body(context, "Strategy")
    if 'creators["chat"] = [](PlayerbotAI* ai) { return new ChatCommandHandlerStrategy(ai); };' not in strategies:
        raise AssertionError("the generated context lacks the chat strategy (rerun slice_gen_context.py)")
    triggers = context_body(context, "Trigger")
    actions = context_body(context, "Action")
    for key in CH_TRIGGERS:
        if 'creators["%s"] = ' % key not in triggers:
            raise AssertionError("the generated context lacks trigger " + key)
    for key in CH_ACTIONS:
        if 'creators["%s"] = ' % key not in actions:
            raise AssertionError("the generated context lacks action " + key)
    ch_keys = generator.split("CH_KEYS = {", 1)[1].split("\n}", 1)[0]
    ch_names = set(n.strip() for chunk in re.findall(r'"([^"]*)"', ch_keys) for n in chunk.split(",") if n.strip())
    for key in CH_TRIGGERS:
        if key not in ch_names:
            raise AssertionError("slice_gen_context.py CH_KEYS lacks trigger " + key)
    for key in CH_ACTIONS:
        if key not in ch_names:
            raise AssertionError("slice_gen_context.py CH_KEYS lacks action " + key)

    # ---- a registered action needs its .cpp in the build list ----
    w1 = [l.strip() for l in slice_w1.splitlines() if l.strip() and not l.strip().startswith("#")]
    for rel in CH_FILES:
        if rel not in w1:
            raise AssertionError("SLICE-W1.txt (the build list) lacks " + rel)

    # ---- the legacy files this round compiles name no 1.12 field the core lacks ----
    stats_code = code_only(stats)
    for gone in ("PLAYER_XP", "PLAYER_NEXT_LEVEL_XP", "PLAYER_REST_STATE_EXPERIENCE", "ITEM_FIELD_MAXDURABILITY",
                 "ITEM_FIELD_DURABILITY", "GetUInt32Value", "quality_mod"):
        if gone in stats_code:
            raise AssertionError("StatsAction.cpp still names " + gone)
    for used in ("RestXPCompat(bot)", "PlayerXPCompat(bot)", "PlayerNextLevelXPCompat(bot)", "ItemRepairCostCompat(item)",
                 "ItemDurabilityCompat(item)", "ItemMaxDurabilityCompat(item)"):
        if used not in stats_code:
            raise AssertionError("StatsAction.cpp must read " + used)
    spells_code = code_only(listspells)
    for gone in ("skillLine->skillId", "skillLine->spellId", "skillLine->min_value", "skillLine->max_value", "FindEquipSlot("):
        if gone in spells_code:
            raise AssertionError("ListSpellsAction.cpp still names " + gone)
    for used in ("SkillLineAbilitySkillCompat(", "SkillLineAbilitySpellCompat(", "SkillLineAbilityMinCompat(",
                 "SkillLineAbilityMaxCompat(", "ItemProtoFitsSlotCompat("):
        if used not in spells_code:
            raise AssertionError("ListSpellsAction.cpp must read " + used)
    if "ChatActionContext" in code_only(help) or "ChatActionContext" in code_only(help_h):
        raise AssertionError("HelpAction must not pull the full ChatActionContext header (it does not compile on the core)")
    if "ChatCommandKeys(ai)" not in help:
        raise AssertionError("HelpAction must list the commands through ChatCommandKeys(ai)")
    for helper in ("SetTargetIconCompat", "RestXPCompat", "ItemRepairCostCompat", "SkillLineAbilityMinCompat",
                   "SkillLineAbilityMaxCompat", "ChatCommandKeys"):
        for name, src in (("TbCompat_CH.h", compat_f), ("ai/cmangos-compat-ch.h", compat_t)):
            if helper + "(" not in code_only(src):
                raise AssertionError(name + " lacks " + helper)
    for helper in ("SetTargetIconCompat", "RestXPCompat", "ItemRepairCostCompat", "SkillLineAbilityMinCompat",
                   "SkillLineAbilityMaxCompat", "ChatCommandKeys"):
        if helper + " (" not in wrapper_ch and helper + "(" not in code_only(compat_f):
            raise AssertionError("the wrapper loses " + helper)

    # ---- the ownership comment: a raid is one group, so a real raid member may whisper any bot in it ----
    if "Not a raid member who is not in the group" in hook_cpp:
        raise AssertionError("the ownership comment still claims a raid stranger is refused")
    if "any real member of the bot's raid" not in hook_cpp:
        raise AssertionError("the ownership comment must say a real raid member may whisper any bot in the raid")

    # ---- the mark: the legacy tree calls getObjectGuid() (the F shim maps it), not the ForeverCore spelling ----
    if "SetTargetIconCompat(group, index, target->getObjectGuid(), bot->getObjectGuid());" not in code_only(rti):
        raise AssertionError("RtiAction must pass both guids through getObjectGuid()")
    if "bot->GetGUID()" in code_only(rti):
        raise AssertionError("RtiAction still names the ForeverCore-only GetGUID()")

    # ---- the slot filter: the hint is the caller's slot (ListSpells), and the first-slot form is the S9 helper (ItemVisitors) ----
    for name, src in (("TbCompat_Unported.h", compat_f_unported), ("ai/cmangos-compat-unported.h", compat_t_unported)):
        if "ItemProtoFitsSlotCompat(" not in code_only(src):
            raise AssertionError(name + " lacks ItemProtoFitsSlotCompat")
    if "bot->FindEquipSlot(proto, slot, true) == slot" not in code_only(compat_t_unported):
        raise AssertionError("the Turtle slot helper must pass the caller's slot as the hint")
    if "slot == EquipSlotOfProtoCompat(bot, proto)" not in code_only(itemvisitors):
        raise AssertionError("ItemVisitors must compare its slot with the first eligible slot")
    if "ItemProtoFitsSlotCompat(" in code_only(itemvisitors):
        raise AssertionError("ItemVisitors must not use the slot-hint helper for its first-slot listing")

    # ---- chat text handling: the S1-R1 parser rules ----
    for name, src in (("TbChatIn.cpp", hook_cpp), ("TbChatCmd.cpp", cmd_cpp), ("TbChatInPure.h", pure_h), ("TbChatCmd.h", cmd_h)):
        body = code_only(src)
        if re.search(r"\b(?:std::)?(?:stoi|stol|stoll|stoul|stoull|stof|stod|atoi|atol)\s*\(", body):
            raise AssertionError(name + " runs a throwing conversion on chat text")
        if re.search(r"\b(?:uint8|uint16|int8|int16|unsigned char|short)\s+\w+\s*=", body):
            raise AssertionError(name + " holds a narrow index")
        if re.search(r"\bfor\s*\(\s*(?:uint8|int8|short|unsigned char)\b", body):
            raise AssertionError(name + " loops on a narrow index")
    # the rule header is core-free: standard headers only
    for inc in re.findall(r"#include\s+([<\"])([^>\"]+)", pure_h):
        if inc[0] == '"':
            raise AssertionError("TbChatInPure.h must stay core-free (it includes %s)" % inc[1])
    # the core's numbers are pinned in the hook, not trusted
    for core_name in ("CHAT_MSG_PARTY", "CHAT_MSG_PARTY_LEADER", "CHAT_MSG_WHISPER", "LANG_ADDON", "LANG_ADDON_LOGGED"):
        if "static_assert(unsigned(%s) == TortoiseBots::chatin::" % core_name not in hook_cpp:
            raise AssertionError("TbChatIn.cpp must static_assert the core's " + core_name)
    # 16 entries and the 1..255 byte rule live in one place
    if "constexpr std::size_t InboxCapacity = 16;" not in pure_h:
        raise AssertionError("the inbox must hold 16 entries (InboxCapacity)")
    if "constexpr std::size_t MaxTextBytes = 255;" not in pure_h:
        raise AssertionError("the length rule must be 1..255 bytes (MaxTextBytes)")
    if "if (facts.text.empty() || facts.text.size() > MaxTextBytes)" not in pure_h:
        raise AssertionError("Judge must apply the length rule")
    # the inbox drops the OLDEST and counts it
    if "head_ = (head_ + 1) % InboxCapacity;" not in pure_h or "++dropped_;" not in pure_h:
        raise AssertionError("the full inbox must drop and count the oldest line")
    # a line thrown away at the drain is counted and said so: never `accepted` for a command that never ran
    if "void Drop()" not in pure_h or "last_ = Code::Dropped;" not in pure_h:
        raise AssertionError("Inbox::Drop must count the line and set the last word to dropped")
    if 'case Code::Dropped: return "dropped";' not in pure_h:
        raise AssertionError("the drain drop must be named dropped")
    # the ingress holds only immutable values and its own bound
    if "constexpr std::size_t IngressCapacity = 64;" not in pure_h:
        raise AssertionError("the ingress bound must live in one place (IngressCapacity)")
    if "struct Arrival" not in pure_h or "class Ingress" not in pure_h:
        raise AssertionError("the pure header must hold the hook's Arrival and its Ingress ring")

    # ---- state, cleanup, rows, docs ----
    if "chatin::AppendState(rec, guid);" not in party_state:
        raise AssertionError("`.tb state` must append the chatin=/chatlast= block")
    if 'rec.P("chatin"' not in hook_cpp or 'rec.S("chatlast"' not in hook_cpp:
        raise AssertionError("the state block must write chatin= and chatlast=")
    if "chatin::Forget(leaving);" not in bot_mgr:
        raise AssertionError("a bot's inbox must go with it on logout")
    if "| incoming: chat message |" in unported_md or "X(IncomingChat," in unported_t:
        raise AssertionError("the chat row is back although the host hook carries every whisper and party line")
    if "`.tb hear" not in docs:
        raise AssertionError("TB-COMMANDS.md does not document .tb hear")
    if "`chatin` `chatlast`" not in docs:
        raise AssertionError("TB-COMMANDS.md does not document the chatin/chatlast keys")

    # ---- the census list ----
    ch = [l.strip() for l in slice_ch.splitlines() if l.strip() and not l.strip().startswith("#")]
    if len(ch) != len(set(ch)):
        raise AssertionError("SLICE-CH.txt has duplicate paths")
    missing = [p for p in w1 if p not in ch]
    if missing:
        raise AssertionError("SLICE-CH.txt lacks SLICE-W1.txt paths: " + ", ".join(missing[:5]))
    for p in ch:
        if not os.path.isfile(os.path.join(root, p)):
            raise AssertionError("SLICE-CH.txt names a missing file: " + p)
    for rel in ("forever/src/host/chat/TbChatIn.cpp", "forever/src/host/chat/TbChatCmd.cpp",
                "forever/src/host/chat/TbChatInPure.h"):
        if rel not in ch:
            raise AssertionError("SLICE-CH.txt lacks this round's file " + rel)


class ChatInWiring(unittest.TestCase):
    def test_wiring_intact(self):
        check(HOOK_CPP, HOOK_H, CMD_CPP, CMD_H, BRAIN, FACTORY, CONTEXT, GENERATOR, SCRIPTS, OFF, PARTYSTATE, BOTMGR,
              UNPORTED_MD, UNPORTED_T, DOCS, PURE_H, CMAKE, read("forever/SLICE-CH.txt"), read("forever/SLICE-W1.txt"), ROOT)

    def test_mutations_fail(self):
        base = (HOOK_CPP, HOOK_H, CMD_CPP, CMD_H, BRAIN, FACTORY, CONTEXT, GENERATOR, SCRIPTS, OFF, PARTYSTATE, BOTMGR,
                UNPORTED_MD, UNPORTED_T, DOCS, PURE_H, CMAKE, read("forever/SLICE-CH.txt"), read("forever/SLICE-W1.txt"), ROOT,
                STATS, LISTSPELLS, HELP, HELP_H, COMPAT_F, COMPAT_T, WRAPPER_CH, RTI, ITEMVISITORS, COMPAT_F_UNPORTED,
                COMPAT_T_UNPORTED)
        mutations = [
            # the hook unregistered: nothing listens again
            (8, SCRIPTS.replace("TortoiseBots::chatin::AddScripts();", "")),
            # the whisper form gone
            (0, HOOK_CPP.replace("void OnChat(Player* player, uint32 type, uint32 lang, std::string& text, Player* receiver) override",
                                 "void OnWhisper(Player* player, uint32 type, uint32 lang, std::string& text, Player* receiver)")),
            # the hook editing the message
            (0, HOOK_CPP.replace("Queue(false, player->GetGUID(), receiver->GetGUID(), type, lang, text);",
                                 "text.erase(0, 1);\n        Queue(false, player->GetGUID(), receiver->GetGUID(), type, lang, text);")),
            # the hook resolving a receiver itself again (world state off-thread)
            (0, HOOK_CPP.replace("Queue(false, player->GetGUID(), receiver->GetGUID(), type, lang, text);",
                                 "Consider(receiver, player, type, lang, text, false);")),
            # the subgroup rule gone: a raid member's /p commands the whole raid again
            (0, HOOK_CPP.replace("group->SameSubGroup(senderGuid, member->GetGUID())", "true")),
            # the ownership comment back to the false claim
            (0, HOOK_CPP.replace("A raid is one Group, so any real member of the bot's raid may whisper it",
                                 "Not a raid member who is not in the group")),
            # the arrivals never drained (nothing is ever judged)
            (11, BOTMGR.replace("    TortoiseBots::chatin::DrainArrivals();\n", "")),
            # DrainArrivals undeclared
            (1, HOOK_H.replace("void DrainArrivals();", "void DrainArrivalsX();")),
            # the subgroup rule removed from the decision
            (15, PURE_H.replace("if (facts.kind != KindWhisper && !facts.sameSubGroup)", "if (false)")),
            # a drain drop back to an uncounted silent continue
            (4, BRAIN.replace("            chatin::Drop(brain->guidLow);\n", "")),
            # the sender's authority not revalidated at delivery
            (4, BRAIN.replace("!chatin::StillAuthorized(brain->bot, sender) || ", "")),
            # the drain after UpdateAI
            (4, BRAIN.replace("        DeliverChat(brain);                                        // S1-R2: the chat lines the hook queued since the last step\n", "")
                    .replace("        brain->ai->UpdateAI(diff);", "        brain->ai->UpdateAI(diff);\n        DeliverChat(brain);")),
            # the strategy out of the reaction kit
            (5, FACTORY.replace('    reactionEngine->addStrategy("chat");\n', "")),
            # a trigger key gone from the generated context
            (6, CONTEXT.replace('            creators["formation"] = [](PlayerbotAI* ai) { return new ChatCommandTrigger(ai, "formation"); };\n', "")),
            # `.tb hear` bypassing the rule
            (2, CMD_CPP.replace("Verdict const verdict = Hear(bot, sender, kind, text);",
                                "Verdict verdict; verdict.code = Code::Accepted; verdict.kind = kind;")),
            # the option-OFF stub gone (the OFF build then links nothing for `.tb hear`)
            (9, OFF.replace('return h3::NotBuilt(handler, args, "hear");', 'return false;')),
            # the state block unappended
            (10, PARTYSTATE.replace("    chatin::AppendState(rec, guid);", "")),
            # an action's .cpp out of the build list: its key would link nowhere
            (18, read("forever/SLICE-W1.txt").replace("ai/playerbot/strategy/actions/RangeAction.cpp\n", "")),
            # a throwing conversion back on chat text
            (2, CMD_CPP.replace("std::string text = RestAfter(std::string_view(tail), 3);",
                                "std::string text = RestAfter(std::string_view(tail), 3); (void)std::stoi(text.c_str());")),
            # a narrow index over the text
            (3, CMD_H.replace("class ChatHandler;", "class ChatHandler;\nstatic_assert(sizeof(unsigned char) == 1, \"x\");\ninline void NarrowIndex(std::string const& text) { for (uint8 i = 0; i < text.size(); ++i) {} }")),
            # the row back to "nothing hears chat"
            (12, UNPORTED_MD.replace("host hook", "no listener")),
            # `.tb hear` undocumented
            (14, DOCS.replace("`.tb hear", "`.tb listen")),
            # the census list trimmed
            (17, read("forever/SLICE-CH.txt").replace("forever/src/host/chat/TbChatIn.cpp\n", "")),
            # a 1.12 update field back in StatsAction
            (20, STATS.replace("uint32 curXP = PlayerXPCompat(bot);", "uint32 curXP = bot->GetUInt32Value(PLAYER_XP);")),
            # the repair estimate back inside StatsAction (the core's stores have other fields)
            (20, STATS.replace("return ItemRepairCostCompat(item);", "return item->GetUInt32Value(ITEM_FIELD_MAXDURABILITY);")),
            # a 1.12 skill line field back
            (21, LISTSPELLS.replace("SkillLineAbilitySkillCompat(skillLine) != skill", "skillLine->skillId != skill")),
            # the full chat context back in HelpAction
            (22, HELP.replace('#include "playerbot/TbCompatCH.h"', '#include "ChatActionContext.h"')),
            # the compat helper gone from the Turtle side
            (25, COMPAT_T.replace("inline uint32 RestXPCompat(Player const* player)", "inline uint32 RestXPCompatX(Player const* player)")),
            # the mark back to the ForeverCore spelling (the Turtle tree has no GetGUID())
            (27, RTI.replace("bot->getObjectGuid());", "bot->GetGUID());")),
            # the listing back to the slot-hint helper (a ring's first slot answers for its second one)
            (28, ITEMVISITORS.replace("slot == EquipSlotOfProtoCompat(bot, proto)",
                                      "ItemProtoFitsSlotCompat(bot, proto, slot)")),
            # the slot hint dropped again: the first eligible slot answers for every slot
            (30, COMPAT_T_UNPORTED.replace("return bot->FindEquipSlot(proto, slot, true) == slot;",
                                           "return bot->FindEquipSlot(proto, NULL_SLOT, true) == slot;")),
            # the first-slot helper gone from the Forever side (ItemVisitors would not compile there)
            (29, COMPAT_F_UNPORTED.replace("ItemProtoFitsSlotCompat(Player* /*bot*/, ItemPrototype const* /*proto*/, uint32 /*slot*/)",
                                           "ItemProtoFitsSlotCompatX(Player* /*bot*/, ItemPrototype const* /*proto*/, uint32 /*slot*/)")),
            # the drain drop uncounted in the state block
            (15, PURE_H.replace("last_ = Code::Dropped;", "last_ = Code::Accepted;")),
        ]
        for i, (slot, mutation) in enumerate(mutations):
            args = list(base)
            self.assertNotEqual(mutation, args[slot], "mutation %d changed nothing" % i)
            args[slot] = mutation
            with self.assertRaises(AssertionError, msg="mutation %d was not caught" % i):
                check(*args)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        check(HOOK_CPP, HOOK_H, CMD_CPP, CMD_H, BRAIN,
              FACTORY.replace('reactionEngine->addStrategy("chat");', ""), CONTEXT, GENERATOR, SCRIPTS, OFF, PARTYSTATE, BOTMGR,
              UNPORTED_MD, UNPORTED_T, DOCS, PURE_H, CMAKE, read("forever/SLICE-CH.txt"), read("forever/SLICE-W1.txt"), ROOT)
        sys.exit(0)
    unittest.main()

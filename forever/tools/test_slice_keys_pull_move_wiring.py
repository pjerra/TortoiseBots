"""Wiring pins for the pull, collision and sit keys the slice left unregistered.

These are the rows whose absence made TB strategies silently do less: they are registered through
forever/tools/slice_gen_context.py, so a regeneration must keep them, the collision value's source must stay on the
build list, and the guards the risky rows rely on must stay in the code (the pull may not start without a tank or
while a party member fights; the collision step may not move a bot that overlaps nobody).

Run: python3 forever/tools/test_slice_keys_pull_move_wiring.py
"""
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[2]

CTX = (ROOT / "forever/src/slice/SliceAiObjectContext.cpp").read_text(encoding="utf-8")
GEN = (ROOT / "forever/tools/slice_gen_context.py").read_text(encoding="utf-8")
LIST = (ROOT / "forever/SLICE-W4.txt").read_text(encoding="utf-8")
PULL_TRIGGERS = (ROOT / "ai/playerbot/strategy/triggers/PullTriggers.cpp").read_text(encoding="utf-8")
MOVEMENT = (ROOT / "ai/playerbot/strategy/actions/MovementActions.cpp").read_text(encoding="utf-8")
NEAREST_NON_BOT = (ROOT / "ai/playerbot/strategy/values/NearestNonBotPlayersValue.cpp").read_text(encoding="utf-8")

# key -> creator, byte for byte the full build's (slice_registry_check.py (a) proves the copy).
CREATORS = {
    "pull back": 'creators["pull back"] = [](PlayerbotAI* ai) { return new PullBackStrategy(ai); };',
    "pull nearest target": 'creators["pull nearest target"] = [](PlayerbotAI* ai) { return new PullNearestTargetAction(ai); };',
    "should pull": 'creators["should pull"] = [](PlayerbotAI* ai) { return new ShouldPullTrigger(ai); };',
    "collision strategy": 'creators["collision"] = [](PlayerbotAI* ai) { return new CollisionStrategy(ai); };',
    "move out of collision": 'creators["move out of collision"] = [](PlayerbotAI* ai) { return new MoveOutOfCollisionAction(ai); };',
    "collision trigger": 'creators["collision"] = [](PlayerbotAI* ai) { return new CollisionTrigger(ai); };',
    "nearest non bot players": 'creators["nearest non bot players"] = [](PlayerbotAI* ai) { return new NearestNonBotPlayersValue(ai); };',
    "sit strategy": 'creators["sit"] = [](PlayerbotAI* ai) { return new SitStrategy(ai); };',
    "sit action": 'creators["sit"] = [](PlayerbotAI* ai) { return new SitAction(ai); };',
    "sit trigger": 'creators["sit"] = [](PlayerbotAI* ai) { return new SitTrigger(ai); };',
}


def func(text, signature):
    i = text.index(signature)
    j = text.index("{", i)
    depth = 0
    for k in range(j, len(text)):
        if text[k] == "{":
            depth += 1
        elif text[k] == "}":
            depth -= 1
            if depth == 0:
                return text[i:k + 1]
    raise AssertionError(signature)


def check():
    failures = []

    def expect(cond, msg):
        if not cond:
            failures.append(msg)

    for name, creator in CREATORS.items():
        expect(creator in CTX, "generated context lacks %s: %s" % (name, creator))

    for block in ("PULL_KEYS", "COLLISION_KEYS", "SIT_KEYS"):
        expect(block in GEN, "slice_gen_context.py lost " + block)

    expect("ai/playerbot/strategy/values/NearestNonBotPlayersValue.cpp" in LIST,
           "the collision value's source is not on the slice build list")
    expect("VisitUnitsCompat(bot, targets, u_check, range)" in NEAREST_NON_BOT,
           "NearestNonBotPlayersValue must search through VisitUnitsCompat (both cores' signature)")
    expect("UnitListSearcher<" not in NEAREST_NON_BOT,
           "NearestNonBotPlayersValue still uses the Turtle-only searcher signature")

    pull = func(PULL_TRIGGERS, "bool ShouldPullTrigger::IsActive()")
    for guard in ("map->IsDungeon()", "PlayerbotAI::IsTank(bot)", "bot->GetGroup()",
                  "member->IsInCombat()", "!member->IsAlive()", "sPlayerbotAIConfig.mediumMana",
                  "FindPullTarget(ai)"):
        expect(guard in pull, "ShouldPullTrigger lost guard: " + guard)

    collision = func(MOVEMENT, "bool MoveOutOfCollisionAction::isUseful()")
    for guard in ('"collision", "self target"', '"nearest non bot players"'):
        expect(guard in collision, "MoveOutOfCollisionAction lost guard: " + guard)

    return failures


def main():
    failures = check()
    if failures:
        print("\n".join(failures))
        return 1
    print("ok: pull, collision and sit wiring")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

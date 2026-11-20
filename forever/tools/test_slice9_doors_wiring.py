"""Wiring pins for slice 9, packages O9, F9 and C9 (the run's object, loot and use tasks, the object and task facts, the start checks).

The decisions are run by standalone tests (test_run_tasks.cpp, test_run_facts_s9.cpp); these pins read the sources and prove the ORDER of the calls and the
places the packages hook into the existing run files stay where the notes say: a refusal changes nothing, the lock is judged before the act, a door that is
open is never used a second time, a run's end drops its tasks, the tasks' own walk orders are not taken for an outside order, the start checks come before
anything is changed, and the only caller of the core's typed packets is the task module.

Run: python3 -m unittest forever/tools/test_slice9_doors_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
RUN = ROOT / "forever/src/host/run"
TASKS = (RUN / "TbRunTasks.cpp").read_text()
PURE = (RUN / "TbRunTasksPure.h").read_text()
RUNCPP = (RUN / "TbRun.cpp").read_text()
ORDERS = (RUN / "TbRunOrders.cpp").read_text()
ORDERS_PURE = (RUN / "TbRunOrdersPure.h").read_text()
FACTS = (RUN / "TbRunFacts.cpp").read_text()
FACTS_PURE = (RUN / "TbRunFactsPure.h").read_text()
STATE = (ROOT / "forever/src/host/TbPartyState.cpp").read_text()


def func(text, signature):
    """The text of the function whose definition line contains `signature` (up to its closing brace)."""
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


class TaskModule(unittest.TestCase):
    def test_the_door_act_judges_the_lock_and_the_state_before_the_packet(self):
        body = func(TASKS, "ActResult BeginObjectAct(")
        lock = body.index("JudgeLock(")
        used = body.index("AlreadyUsed(")
        packet = body.index("HandleGameObjectUseOpcode(")
        self.assertLess(lock, packet)
        self.assertLess(used, packet)
        self.assertEqual(body.count("HandleGameObjectUseOpcode("), 1)

    def test_the_typed_use_packet_has_one_caller(self):
        for path in (ROOT / "forever/src").rglob("*.cpp"):
            if path.name == "TbRunTasks.cpp":
                continue
            self.assertNotIn("HandleGameObjectUseOpcode", path.read_text(), path.name)

    def test_the_order_judges_the_lock_before_it_queues_a_task(self):
        body = func(TASKS, "OrderAnswer Begin(")
        self.assertLess(body.index("JudgeLock("), body.index("rt.byBot[task.bot] = task"))
        self.assertLess(body.index("FindByEntry("), body.index("rt.byBot[task.bot] = task"))
        # every refusal path returns before the table is written: the only write is the line above
        self.assertEqual(body.count("rt.byBot[task.bot] = task"), 1)

    def test_a_target_is_found_within_five_yards_of_the_row_and_never_elsewhere(self):
        self.assertIn("kEntrySearchYd = 5.0f", PURE)
        self.assertIn("RC::PickNearestEntry(list, kEntrySearchYd, key)", PURE)
        self.assertIn("PickEntryTarget(found, key)", func(TASKS, "bool FindByEntry("))

    def test_the_loot_act_watches_the_window_before_it_opens_it(self):
        body = func(TASKS, "ActResult BeginLootAct(")
        self.assertLess(body.index("RC::LootOrders().Watch("), body.index("LootOpenCompat("))
        self.assertLess(body.index("RC::LootOrders().Watch("), body.index('DoSpecificAction("open loot"'))
        self.assertLess(body.index("JudgeLock("), body.index("RC::LootOrders().Watch("))

    def test_every_finish_gives_the_loot_watch_back(self):
        self.assertIn("RC::LootOrders().Forget(GuidKeyCompat(task.bot))", func(TASKS, "void Finish("))
        self.assertIn("RC::LootOrders().Forget(GuidKeyCompat(entry.first))", func(TASKS, "void Cancel("))

    def test_the_walk_orders_are_the_runs_own(self):
        body = func(TASKS, "bool OrderWalk(")
        self.assertIn("spec.fromRun = true", body)
        self.assertIn('spec.verb = "come"', body)
        self.assertIn("++rt.queuedOrders", body)

    def test_the_use_of_an_item_is_refused_not_carried_out(self):
        self.assertIn('Refuse("unsupported", tasks::UseUnsupportedText())', ORDERS)

    def test_the_loot_method_is_read_back_from_the_group(self):
        body = func(TASKS, "OrderAnswer SetLootMethod(")
        self.assertLess(body.index("HandleSetLootMethodOpcode("), body.index("group->GetLootMethod()"))
        self.assertLess(body.index("JudgeLootMethod("), body.index("HandleSetLootMethodOpcode("))


class Hooks(unittest.TestCase):
    def test_the_executor_hands_the_task_verbs_to_the_task_module(self):
        for token in ("tasks::Begin(rec, party.lead, party.bots, p.task, nowMs)", "tasks::SetLootMethod(rec, party.lead, party.bots, p.task)"):
            self.assertIn(token, ORDERS)

    def test_the_four_verbs_are_not_in_the_unsupported_list(self):
        i = ORDERS_PURE.index("for (char const* known : {")
        known = ORDERS_PURE[i:ORDERS_PURE.index("})", i)]
        for verb in ('"loot"', '"lootmethod"', '"open"', '"use"'):
            self.assertNotIn(verb, known)

    def test_a_runs_end_pause_and_restart_drop_its_tasks(self):
        self.assertIn("tasks::Cancel(rec.leader)", func(RUNCPP, "void CancelRunWork("))
        self.assertIn("tasks::Forget(rec.leader)", func(RUNCPP, "uint32 Finish("))
        self.assertIn("tasks::Forget(leader->GetGUID())", func(RUNCPP, "StartOutcome StartRun("))

    def test_the_tasks_tick_before_the_run_tick_and_their_orders_are_baselined(self):
        body = func(RUNCPP, "void Update(uint32 diffMs)")
        self.assertLess(body.index("tasks::Update(diffMs)"), body.index("TickRun(*rec, nowMs)"))
        self.assertIn("tasks::TakeQueuedOrders(entry.first)", body)
        self.assertIn("BaselineOrders(*entry.second)", body)


class StartChecks(unittest.TestCase):
    def test_bags_and_loot_method_are_refused_before_anything_changes(self):
        body = func(RUNCPP, "StartOutcome StartRun(")
        change = body.index("RunRec* rec = Create(")
        self.assertLess(body.index('refuse("bags"'), change)
        self.assertLess(body.index('refuse("loot-method"'), change)
        self.assertIn("tasks::kStartFreeSlots", body)
        self.assertIn("tasks::StartLootMethodOk(", body)

    def test_the_bags_floor_pauses_a_loot_step_before_the_policy_decides(self):
        body = func(RUNCPP, "void TickRun(")
        self.assertLess(body.index("BagsFullForStep("), body.index("DungeonRun::Tick(run, facts, dt)"))
        self.assertLess(body.index("SnapshotStepFacts(rec, facts)"), body.index("DungeonRun::Tick(run, facts, dt)"))

    def test_the_state_record_has_the_bag_and_loot_keys(self):
        self.assertIn('rec.N("bagfree", bot->GetFreeInventorySlotCount())', STATE)
        self.assertIn('end.S("lootmethod"', STATE)


class Facts(unittest.TestCase):
    def test_the_step_read_handles_every_object_and_task_kind(self):
        for kind in ("WaitClear", "WaitObject", "Loot", "Open", "UseItem"):
            self.assertIn("case ai::PlanStepKind::%s:" % kind, FACTS_PURE)

    def test_the_policy_states_are_named_not_numbered(self):
        body = func(FACTS_PURE, "inline int PlanObjectState(")
        self.assertRegex(body, r"case 1:\s*return 0;")
        self.assertRegex(body, r"case 0:\s*return 1;")

    def test_the_loot_method_and_the_doors_are_read_always(self):
        self.assertIn("tasks::PolicyLootMethod(uint32(leadGroup->GetLootMethod()))", FACTS)
        self.assertIn("f.doors = InstanceDoors(rec.run.plan->map)", FACTS)
        self.assertIn("f.lootFfaOn = false", FACTS_PURE)     # the detour stays off (spec D4)

    def test_a_retired_order_drops_the_task_too(self):
        body = func(FACTS, "void RetireTask()")
        self.assertIn("DungeonRun::RetireOrder(_c.rec->run)", body)
        self.assertIn("tasks::Cancel(_c.rec->leader)", body)

    def test_a_not_found_object_is_gone_only_when_the_search_could_have_found_it(self):
        body = func(FACTS, "fx::ObjectObs ObjectAt(")
        for token in ("IsAlive()", "IsBeingTeleported()", "plan->map", "EntryPointInReach", "IsGridLoaded"):
            self.assertIn(token, body)


if __name__ == "__main__":
    unittest.main()

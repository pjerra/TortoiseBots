"""Group ids have ONE identity source per data flow (wide pass 2, review of family 3).

`GroupIdW2` answers ForeverCore's live group guid counter; `Group::GetId()` is the groups-table id the Commander binding table still
stores (`Binding::groupId`, written at Commander.cpp by `binding.groupId = group->GetId()`). Comparing one with the other is a mismatch
on ForeverCore. So: (1) the bound-raid seat check keeps both operands on the binding's own source; (2) a file that uses GroupIdW2 never
touches the binding table; (3) in PatchwerkSafety every key of the `openings` map is built by KeyFor, the one GroupIdW2 site.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
BINDING_TOKENS = ("boundRaid", "GroupBindingKey", "FindByGroup", "CommanderBindings", "Binding const", "BoundRaidOf", "bindingKey")


def code(rel):
    text = (ROOT / rel).read_text(encoding="utf-8", errors="replace")
    return "\n".join(line.split("//", 1)[0] for line in text.splitlines())


class GroupIdIdentityTest(unittest.TestCase):
    def test_bound_raid_seat_check_uses_the_binding_source_on_both_sides(self):
        src = code("runtime/HireProvisionService.cpp")
        self.assertIn("botGroup->GetId() == boundRaid.groupId", src)
        self.assertNotIn("GroupIdW2", src)

    def test_binding_writers_stay_on_group_get_id(self):
        src = code("commands/Commander.cpp")
        self.assertGreaterEqual(len(re.findall(r"binding\.groupId = group->GetId\(\);", src)), 3)
        self.assertNotIn("GroupIdW2", src)

    def test_lfg_group_id_is_not_mixed_with_the_binding_table(self):
        src = code("runtime/BotLfgService.cpp")
        self.assertIn("GroupBindingKey(group.groupId)", src)
        self.assertNotIn("GroupIdW2", src)

    def test_a_file_using_group_id_w2_does_not_touch_the_binding_table(self):
        users = []
        for top in ("ai", "commands", "runtime", "host", "behavior"):
            for path in (ROOT / top).rglob("*"):
                if path.suffix in (".cpp", ".h") and not path.name.startswith("cmangos-compat") and "GroupIdW2" in path.read_text(encoding="utf-8", errors="replace"):
                    users.append(path)
        self.assertEqual([p.name for p in users], ["PatchwerkSafety.cpp"])
        for path in users:
            src = code(str(path.relative_to(ROOT)))
            for token in BINDING_TOKENS:
                self.assertNotIn(token, src, path.name)

    def test_patchwerk_opening_keys_all_come_from_key_for(self):
        src = code("ai/playerbot/strategy/actions/PatchwerkSafety.cpp")
        self.assertEqual(src.count("GroupIdW2("), 1)
        self.assertIn("Key KeyFor(Player* bot) { return {bot->GetInstanceId(), GroupIdW2(bot->GetGroup())}; }", src)
        for use in re.findall(r"openings(?:\.find|\.erase|)[\[(]([^\]\n)]*\)?)", src):
            self.assertTrue(use.startswith("KeyFor("), use)
        self.assertNotIn("GetGroup()->GetId()", src)


if __name__ == "__main__":
    unittest.main()

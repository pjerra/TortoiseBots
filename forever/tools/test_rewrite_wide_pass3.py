#!/usr/bin/env python3
"""The database family of rewrite_wide_pass3.py also rewrites the PQuery shapes the census-clean tree still had.

`std::unique_ptr<QueryResult> x(DB.PQuery(...))` was the shape of family 3; a printf format in `auto x = DB.PQuery(...)`,
`if (auto x = DB.PQuery(...))` or `x = DB.PQuery(...)` still compiled on ForeverCore and silently dropped its arguments
(the review's finding 2), and `PQuery("%s", text)` sent the format itself as the query. Every rewrite keeps one call and
the same arguments: Turtle gets the original `PQuery` through the helper, ForeverCore the printf formatter.

Run: python3 forever/tools/test_rewrite_wide_pass3.py
"""
import importlib.util
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
_spec = importlib.util.spec_from_file_location("rewrite_wide_pass3", ROOT / "forever" / "tools" / "rewrite_wide_pass3.py")
rw3 = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(rw3)


class DbPQueryShapes(unittest.TestCase):
    def rewrite(self, text):
        out, n = rw3.fam_db(text)
        return out, n

    def test_auto_declaration_world_database_goes_through_the_formatting_helper(self):
        text = "auto rows = WorldDatabase.PQuery(\"SELECT a FROM b WHERE c = '%u'\", level);\n"
        out, n = self.rewrite(text)
        self.assertEqual(out, "auto rows = DbPQueryW3(WorldDatabase, \"SELECT a FROM b WHERE c = '%u'\", level);\n")
        self.assertEqual(n, 1)

    def test_auto_declaration_character_database_goes_through_the_character_helper(self):
        text = "auto results = CharacterDatabase.PQuery(\"SELECT `k` FROM `t` WHERE `g` = '%lu'\", guid);\n"
        out, n = self.rewrite(text)
        self.assertEqual(out, "auto results = CharacterPQueryCompat(\"SELECT `k` FROM `t` WHERE `g` = '%lu'\", guid);\n")
        self.assertEqual(n, 1)

    def test_if_declaration_is_rewritten_in_place(self):
        text = "if (auto result = WorldDatabase.PQuery(\"SELECT x FROM y WHERE id = %u\", id))\n{\n}\n"
        out, n = self.rewrite(text)
        self.assertIn("if (auto result = DbPQueryW3(WorldDatabase, \"SELECT x FROM y WHERE id = %u\", id))", out)
        self.assertEqual(n, 1)

    def test_assignment_to_an_existing_result_is_rewritten(self):
        text = "    result = CharacterDatabase.PQuery(\"SELECT p FROM q WHERE a = '%u'\", account);\n"
        out, n = self.rewrite(text)
        self.assertEqual(out, "    result = CharacterPQueryCompat(\"SELECT p FROM q WHERE a = '%u'\", account);\n")
        self.assertEqual(n, 1)

    def test_a_single_percent_s_wrapper_becomes_the_query_itself(self):
        text = "auto result = WorldDatabase.PQuery(\"%s\", query.c_str());\n"
        out, n = self.rewrite(text)
        self.assertEqual(out, "auto result = WorldDatabase.Query(query.c_str());\n")
        self.assertEqual(n, 1)

    def test_a_literal_passed_through_percent_s_becomes_the_query_itself(self):
        text = "if (auto result = WorldDatabase.PQuery(\"%s\", \"SELECT item, entry FROM npc_vendor\"))\n"
        out, n = self.rewrite(text)
        self.assertEqual(out, "if (auto result = WorldDatabase.Query(\"SELECT item, entry FROM npc_vendor\"))\n")
        self.assertEqual(n, 1)

    def test_multi_line_format_and_nested_parentheses_keep_their_arguments(self):
        text = ("auto rows = WorldDatabase.PQuery(\n"
                "    \"select entry from item_template\"\n"
                "    \" where class = '%u' and le\" \"vel <= '%u'\",\n"
                "    PickClass(spec), std::max<uint32>(1, 2 + level));\n")
        out, n = self.rewrite(text)
        self.assertIn("DbPQueryW3(WorldDatabase, \"select entry from item_template\"", out)
        self.assertIn("std::max<uint32>(1, 2 + level));", out)
        self.assertNotIn("Database.PQuery", out)
        self.assertEqual(n, 1)

    def test_a_query_without_a_printf_conversion_is_left_alone(self):
        text = "auto rows = CharacterDatabase.PQuery(\"SELECT COUNT(*) FROM `account`\");\n"
        out, n = self.rewrite(text)
        self.assertEqual(out, text)
        self.assertEqual(n, 0)

    def test_a_double_percent_is_not_a_conversion(self):
        text = "auto rows = WorldDatabase.PQuery(\"SELECT a FROM b WHERE c LIKE '100%%'\", 1);\n"
        out, n = self.rewrite(text)
        self.assertEqual(out, text)
        self.assertEqual(n, 0)

    def test_a_commented_out_call_is_left_alone(self):
        text = ("    // check for same acc id\n"
                "    /*QueryResult* result = CharacterDatabase.PQuery(\"SELECT p FROM q WHERE a = '%u'\", id);\n"
                "    delete result;*/\n")
        out, n = self.rewrite(text)
        self.assertEqual(out, text)
        self.assertEqual(n, 0)


if __name__ == "__main__":
    unittest.main()

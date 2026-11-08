import unittest

from albor_automation import parse_tree


class ParseTreeTest(unittest.TestCase):

    def test_parses_role_id_and_name(self) -> None:
        # the basic dump line carries role, id and name separated by spaces
        nodes = parse_tree("button main_ok OK")
        self.assertEqual(len(nodes), 1)
        self.assertEqual(nodes[0]["role"], "button")
        self.assertEqual(nodes[0]["id"], "main_ok")
        self.assertEqual(nodes[0]["name"], "OK")

    def test_name_may_contain_spaces(self) -> None:
        # the name spans everything between the id and the first flag token
        nodes = parse_tree("label - No netlist loaded")
        self.assertEqual(nodes[0]["name"], "No netlist loaded")

    def test_value_with_spaces_is_read_whole(self) -> None:
        # the server escapes control bytes but not spaces, so a value with a
        # space must be read up to the first trailing flag, not the first space
        nodes = parse_tree("textfield path path value=/Users/foo bar/xyce writable")
        self.assertEqual(nodes[0]["value"], "/Users/foo bar/xyce")
        self.assertTrue(nodes[0]["writable"])

    def test_state_flags_are_parsed(self) -> None:
        # each flag becomes a boolean key on the node
        nodes = parse_tree("textfield path path value=/bin/echo writable disabled selected focused")
        node = nodes[0]
        self.assertTrue(node["writable"])
        self.assertTrue(node["disabled"])
        self.assertTrue(node["selected"])
        self.assertTrue(node["focused"])
        self.assertNotIn("read_only", node)
        self.assertNotIn("multiline", node)

    def test_value_escapes_are_decoded(self) -> None:
        # the server writes backslash escapes for the control bytes a line
        # cannot carry, and the parser must recover the original text
        nodes = parse_tree(r"label - name value=hello\nworld")
        self.assertEqual(nodes[0]["value"], "hello\nworld")

    def test_name_escapes_are_decoded(self) -> None:
        # the name is escaped the same way the value is
        nodes = parse_tree(r"label - tab\there")
        self.assertEqual(nodes[0]["name"], "tab\there")

    def test_literal_backslash_survives_the_round_trip(self) -> None:
        # a value containing a backslash arrives doubled and reads back single
        nodes = parse_tree(r"label - name value=C:\\path\\to\\file")
        self.assertEqual(nodes[0]["value"], "C:\\path\\to\\file")

    def test_selection_range_is_parsed(self) -> None:
        # the half open selection range arrives as start:end
        nodes = parse_tree("textfield path path value=/bin/echo selection=0:4 writable")
        self.assertEqual(nodes[0]["selection"], (0, 4))

    def test_blank_id_and_empty_name(self) -> None:
        # the dump writes a dash for an unidentified node and may carry no name
        nodes = parse_tree("group - ")
        self.assertEqual(nodes[0]["role"], "group")
        self.assertEqual(nodes[0]["id"], "-")
        self.assertEqual(nodes[0]["name"], "")

    def test_indeterminate_progress_state_is_parsed(self) -> None:
        # an indeterminate bar appends its state flag after the name, so the
        # name stops at the flag token and the flag reads as a boolean key
        nodes = parse_tree("progress_bar main_progress_bar main_progress_bar indeterminate indeterminate")
        node = nodes[0]
        self.assertEqual(node["name"], "main_progress_bar")
        self.assertTrue(node["indeterminate"])
        # a determinate bar carries its fraction in the name and no state flag
        nodes = parse_tree("progress_bar bar main_progress_bar 0.400000")
        self.assertEqual(nodes[0]["name"], "main_progress_bar 0.400000")
        self.assertNotIn("indeterminate", nodes[0])

    def test_multiple_nodes_parse_in_order(self) -> None:
        # the dump reports nodes one per line in tree order
        nodes = parse_tree("group - \nbutton ok OK\nlabel - Ready")
        self.assertEqual([n["role"] for n in nodes], ["group", "button", "label"])
        self.assertEqual([n["name"] for n in nodes], ["", "OK", "Ready"])


if __name__ == "__main__":
    unittest.main()

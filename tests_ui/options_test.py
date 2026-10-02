import tempfile
import unittest
from pathlib import Path

from slint_automation import TestSession, expect, launch


class OptionsDialogChecks(unittest.TestCase):

    def test_edit_general_option_regenerates_the_options_line(self) -> None:
        # arrange: write a netlist carrying a DEVICE option into a scratch directory
        netlist = Path(tempfile.mkdtemp(prefix="xyce-options-edit-")) / "options-edit.cir"
        netlist.write_text("* options edit test\nV1 in 0 PULSE(0 5 0 1n 1n 10m 20m)\nR1 in 0 1k\n.OPTIONS DEVICE TEMP=25\n.TRAN 1u 10u\n.END\n")
        # arrange: launch the application with the netlist through the command line
        with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
            # step 1: open the options dialog from the toolbar (the tool sits right after Configure)
            app.get_by_type("ToolbarButton").nth(7).click()
            # arrange: the dialog is open once its value fields exist
            app.get_by_type("LineEdit").nth(0).wait_for_exists(timeout=10.0)
            scroller = app.get_by_type("ScrollView").nth(1)
            # step 2: scroll the DEVICE card until the TEMP field is inside the scroll viewport
            temp = app.get_by_type("LineEdit").with_label("TEMP").nth(0)
            temp.scroll_into_view(scroller)
            # assert: the absent GMIN key shows the reference default as placeholder
            gmin = app.get_by_type("LineEdit").with_label("GMIN").nth(0)
            self.assertEqual(gmin.property("accessiblePlaceholderText"), "1.0E-12")
            # step 3: edit the temperature and accept the dialog
            temp.fill("30")
            app.get_by_type("Button").with_label("OK").nth(0).click()
            # assert: the dialog closed
            app.get_by_type("LineEdit").nth(0).wait_for_gone(timeout=10.0)
            # assert: the netlist editor holds the regenerated option line next to the untouched analysis
            editor_text = app.get_by_id("NetlistEditor::input").text()
            option_lines = [line.strip() for line in editor_text.splitlines() if line.strip().startswith(".OPTIONS")]
            self.assertEqual(option_lines, [".OPTIONS DEVICE TEMP=30"])
            self.assertIn(".TRAN 1u 10u", editor_text)
            # step 4: reopen the dialog to verify the edited value persisted
            app.get_by_type("ToolbarButton").nth(7).click()
            app.get_by_type("LineEdit").nth(0).wait_for_exists(timeout=10.0)
            temp = app.get_by_type("LineEdit").with_label("TEMP").nth(0)
            temp.scroll_into_view(scroller)
            # assert: the TEMP field carries the edited value
            expect(temp).to_have_text("30")

    def test_escape_closes_the_options_dialog_without_changing_the_netlist(self) -> None:
        # arrange: write a netlist carrying a DEVICE option into a scratch directory
        netlist = Path(tempfile.mkdtemp(prefix="xyce-options-escape-")) / "options-escape.cir"
        netlist.write_text("* options escape test\nV1 in 0 PULSE(0 5 0 1n 1n 10m 20m)\nR1 in 0 1k\n.OPTIONS DEVICE TEMP=25\n.TRAN 1u 10u\n.END\n")
        # arrange: launch the application with the netlist through the command line
        with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
            # step 1: open the options dialog from the toolbar
            app.get_by_type("ToolbarButton").nth(7).click()
            # arrange: the dialog is open once its value fields exist
            fields = app.get_by_type("LineEdit")
            fields.nth(0).wait_for_exists(timeout=10.0)
            scroller = app.get_by_type("ScrollView").nth(1)
            # step 2: scroll the DEVICE card until the TEMP field is inside the scroll viewport
            temp = app.get_by_type("LineEdit").with_label("TEMP").nth(0)
            temp.scroll_into_view(scroller)
            # step 3: change the value but dismiss with Escape instead of accepting
            temp.fill("40")
            fields.nth(0).press("\x1b")
            # assert: the dialog closed
            fields.nth(0).wait_for_gone(timeout=10.0)
            # assert: the netlist editor still holds the original option line
            editor_text = app.get_by_id("NetlistEditor::input").text()
            option_lines = [line.strip() for line in editor_text.splitlines() if line.strip().startswith(".OPTIONS")]
            self.assertEqual(option_lines, [".OPTIONS DEVICE TEMP=25"])

    def test_accept_keeps_bare_flag_options_until_they_are_cleared(self) -> None:
        # arrange: write a netlist carrying the bare STRATEGY flag without a value
        netlist = Path(tempfile.mkdtemp(prefix="xyce-options-flag-")) / "options-flag.cir"
        netlist.write_text("* options flag test\nV1 in 0 PULSE(0 5 0 1n 1n 10m 20m)\nR1 in 0 1k\n.OPTIONS DIST STRATEGY\n.TRAN 1u 10u\n.END\n")
        # arrange: launch the application with the netlist through the command line
        with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
            # step 1: open the options dialog from the toolbar
            app.get_by_type("ToolbarButton").nth(7).click()
            # arrange: the dialog is open once its value fields exist
            app.get_by_type("LineEdit").nth(0).wait_for_exists(timeout=10.0)
            scroller = app.get_by_type("ScrollView").nth(1)
            # step 2: scroll the DIST card into the scroll viewport (four cards with 72 rows precede it)
            strategy = app.get_by_type("ComboBox").with_label("STRATEGY").nth(0)
            strategy.scroll_into_view(scroller)
            # assert: the bare flag selects the flag marker entry
            expect(strategy).to_have_text("flag is set")
            # step 3: accept the dialog without editing anything
            app.get_by_type("Button").with_label("OK").nth(0).click()
            # assert: the dialog closed and the bare flag line survived next to the untouched analysis
            app.get_by_type("LineEdit").nth(0).wait_for_gone(timeout=10.0)
            editor_text = app.get_by_id("NetlistEditor::input").text()
            option_lines = [line.strip() for line in editor_text.splitlines() if line.strip().startswith(".OPTIONS")]
            self.assertEqual(option_lines, [".OPTIONS DIST STRATEGY"])
            self.assertIn(".TRAN 1u 10u", editor_text)
            # step 4: reopen the dialog and clear the flag through the <default> entry
            app.get_by_type("ToolbarButton").nth(7).click()
            app.get_by_type("LineEdit").nth(0).wait_for_exists(timeout=10.0)
            strategy = app.get_by_type("ComboBox").with_label("STRATEGY").nth(0)
            strategy.scroll_into_view(scroller)
            strategy.select_option("<default>")
            # step 5: accept the dialog again
            app.get_by_type("Button").with_label("OK").nth(0).click()
            # assert: the dialog closed and the cleared flag no longer reaches the netlist
            app.get_by_type("LineEdit").nth(0).wait_for_gone(timeout=10.0)
            editor_text = app.get_by_id("NetlistEditor::input").text()
            option_lines = [line.strip() for line in editor_text.splitlines() if line.strip().startswith(".OPTIONS")]
            self.assertEqual(option_lines, [])
            self.assertIn(".TRAN 1u 10u", editor_text)

    def test_configure_dialog_edits_timeint_option_and_selects_a_method_choice(self) -> None:
        # arrange: write a transient netlist carrying a TIMEINT option into a scratch directory
        netlist = Path(tempfile.mkdtemp(prefix="xyce-options-timeint-")) / "options-timeint.cir"
        netlist.write_text("* options timeint test\nV1 in 0 PULSE(0 5 0 1n 1n 10m 20m)\nR1 in 0 1k\n.OPTIONS TIMEINT RELTOL=1e-4\n.TRAN 1u 10u\n.END\n")
        # arrange: launch the application with the netlist through the command line
        with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
            # step 1: open the configure dialog from the toolbar; the transient analysis selects the .TRAN tab
            app.get_by_type("ToolbarButton").nth(6).click()
            app.get_by_type("LineEdit").nth(0).wait_for_exists(timeout=10.0)
            scroller = app.get_by_type("ScrollView").nth(1)
            # step 2: scroll the .TRAN tab until the deepest inspected TIMEINT row (MAXORD) is inside the viewport
            maxord = app.get_by_type("LineEdit").with_label("MAXORD").nth(0)
            maxord.scroll_into_view(scroller)
            # assert: the seeded value and the reference default placeholder are shown (the first RELTOL row belongs to the TIMEINT card)
            reltol = app.get_by_type("LineEdit").with_label("RELTOL").nth(0)
            expect(reltol).to_have_text("1e-4")
            self.assertEqual(maxord.property("accessiblePlaceholderText"), "2")
            # step 3: edit the tolerance and accept
            reltol.fill("5e-4")
            # step 4: select trap in the METHOD combobox through the locator action
            method = app.get_by_type("ComboBox").with_label("METHOD").nth(0)
            method.scroll_into_view(scroller)
            method.select_option("trap")
            # assert: the combobox now shows the selected choice
            expect(method).to_have_text("trap")
            # step 5: accept the dialog
            app.get_by_type("Button").with_label("OK").nth(0).click()
            # assert: the dialog closed
            app.get_by_type("LineEdit").nth(0).wait_for_gone(timeout=10.0)
            # assert: the regenerated option line carries both edits next to the untouched analysis
            editor_text = app.get_by_id("NetlistEditor::input").text()
            option_lines = [line.strip() for line in editor_text.splitlines() if line.strip().startswith(".OPTIONS")]
            self.assertEqual(option_lines, [".OPTIONS TIMEINT METHOD=trap RELTOL=5e-4"])
            self.assertIn(".TRAN 1u 10u", editor_text)
            # step 6: reopen the dialog to verify both edits persisted
            app.get_by_type("ToolbarButton").nth(6).click()
            app.get_by_type("LineEdit").nth(0).wait_for_exists(timeout=10.0)
            maxord = app.get_by_type("LineEdit").with_label("MAXORD").nth(0)
            maxord.scroll_into_view(scroller)
            # assert: both edits persisted across the reopen
            reltol = app.get_by_type("LineEdit").with_label("RELTOL").nth(0)
            expect(reltol).to_have_text("5e-4")
            method = app.get_by_type("ComboBox").with_label("METHOD").nth(0)
            expect(method).to_have_text("trap")

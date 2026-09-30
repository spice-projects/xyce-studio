import tempfile
import unittest
from pathlib import Path

from slint_automation import TestSession, launch

# scroll step in logical pixels per wheel tick; a negative delta_y reveals content further down
SCROLL_STEP = -60.0


class OptionsDialogChecks(unittest.TestCase):

    def test_edit_option_value_regenerates_the_options_line(self) -> None:
        # arrange: write a transient netlist carrying a TIMEINT option into a scratch directory
        netlist = Path(tempfile.mkdtemp(prefix="xyce-options-edit-")) / "options-edit.cir"
        netlist.write_text("* options edit test\nV1 in 0 PULSE(0 5 0 1n 1n 10m 20m)\nR1 in 0 1k\n.OPTIONS TIMEINT RELTOL=1e-4\n.TRAN 1u 10u\n.END\n")
        # arrange: launch the application with the netlist through the command line
        with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
            # step 1: open the options dialog from the toolbar (the tool sits right after Configure)
            app.get_by_type("ToolbarButton").nth(7).click()
            # arrange: the dialog is open once its value fields exist
            app.get_by_type("LineEdit").nth(0).wait_for_exists(timeout=10.0)
            root = app.client().get_window_properties()["rootElementHandle"]
            scroller = app.get_by_type("ScrollView").nth(1)
            # step 2: scroll the TIMEINT card into the scroll viewport (the 28-row DEVICE card precedes it; only viewport fields are exposed)
            for _ in range(60):
                fields = [handle for handle in app.client().find_by_type_in(root, "LineEdit") if app.client().get_element_properties(handle).get("accessibleLabel") == "RELTOL"]
                if fields:
                    field_rect = app.client().get_element_properties(fields[0])
                    viewport = scroller.properties()
                    top = field_rect["absolutePosition"]["y"]
                    bottom = top + field_rect["size"]["height"]
                    if top >= viewport["absolutePosition"]["y"] and bottom <= viewport["absolutePosition"]["y"] + viewport["size"]["height"]:
                        break
                scroller.scroll(0.0, SCROLL_STEP)
            else:
                self.fail("expected the RELTOL field to scroll into view")
            # assert: the TIMEINT RELTOL field carries the value parsed from the netlist (the first RELTOL row belongs to the TIMEINT card)
            fields = [handle for handle in app.client().find_by_type_in(root, "LineEdit") if app.client().get_element_properties(handle).get("accessibleLabel") == "RELTOL"]
            self.assertEqual(app.client().get_element_properties(fields[0]).get("accessibleValue"), "1e-4")
            # step 3: edit the value and accept the dialog
            app.client().set_element_value(fields[0], "2e-4")
            ok = [handle for handle in app.client().find_by_type_in(root, "Button") if app.client().get_element_properties(handle).get("accessibleLabel") == "OK"][0]
            app.client().click_element(ok)
            # assert: the dialog closed
            app.get_by_type("LineEdit").nth(0).wait_for_gone(timeout=10.0)
            # assert: the netlist editor holds the regenerated option line next to the untouched analysis
            editor_text = app.get_by_id("NetlistEditor::input").text()
            option_lines = [line.strip() for line in editor_text.splitlines() if line.strip().startswith(".OPTIONS")]
            self.assertEqual(option_lines, [".OPTIONS TIMEINT RELTOL=2e-4"])
            self.assertIn(".TRAN 1u 10u", editor_text)
            # step 4: reopen the dialog to verify the edited value persisted
            app.get_by_type("ToolbarButton").nth(7).click()
            app.get_by_type("LineEdit").nth(0).wait_for_exists(timeout=10.0)
            # step 5: scroll the TIMEINT card into the scroll viewport again
            for _ in range(60):
                fields = [handle for handle in app.client().find_by_type_in(root, "LineEdit") if app.client().get_element_properties(handle).get("accessibleLabel") == "RELTOL"]
                if fields:
                    field_rect = app.client().get_element_properties(fields[0])
                    viewport = scroller.properties()
                    top = field_rect["absolutePosition"]["y"]
                    bottom = top + field_rect["size"]["height"]
                    if top >= viewport["absolutePosition"]["y"] and bottom <= viewport["absolutePosition"]["y"] + viewport["size"]["height"]:
                        break
                scroller.scroll(0.0, SCROLL_STEP)
            else:
                self.fail("expected the RELTOL field to scroll into view")
            # assert: the TIMEINT RELTOL field carries the edited value
            fields = [handle for handle in app.client().find_by_type_in(root, "LineEdit") if app.client().get_element_properties(handle).get("accessibleLabel") == "RELTOL"]
            self.assertEqual(app.client().get_element_properties(fields[0]).get("accessibleValue"), "2e-4")

    def test_escape_closes_the_dialog_without_changing_the_netlist(self) -> None:
        # arrange: write a transient netlist carrying a TIMEINT option into a scratch directory
        netlist = Path(tempfile.mkdtemp(prefix="xyce-options-escape-")) / "options-escape.cir"
        netlist.write_text("* options escape test\nV1 in 0 PULSE(0 5 0 1n 1n 10m 20m)\nR1 in 0 1k\n.OPTIONS TIMEINT RELTOL=1e-4\n.TRAN 1u 10u\n.END\n")
        # arrange: launch the application with the netlist through the command line
        with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
            # step 1: open the options dialog from the toolbar
            app.get_by_type("ToolbarButton").nth(7).click()
            # arrange: the dialog is open once its value fields exist
            fields = app.get_by_type("LineEdit")
            fields.nth(0).wait_for_exists(timeout=10.0)
            root = app.client().get_window_properties()["rootElementHandle"]
            scroller = app.get_by_type("ScrollView").nth(1)
            # step 2: scroll the TIMEINT card into the scroll viewport (only viewport fields are exposed)
            for _ in range(60):
                reltol_fields = [handle for handle in app.client().find_by_type_in(root, "LineEdit") if app.client().get_element_properties(handle).get("accessibleLabel") == "RELTOL"]
                if reltol_fields:
                    field_rect = app.client().get_element_properties(reltol_fields[0])
                    viewport = scroller.properties()
                    top = field_rect["absolutePosition"]["y"]
                    bottom = top + field_rect["size"]["height"]
                    if top >= viewport["absolutePosition"]["y"] and bottom <= viewport["absolutePosition"]["y"] + viewport["size"]["height"]:
                        break
                scroller.scroll(0.0, SCROLL_STEP)
            else:
                self.fail("expected the RELTOL field to scroll into view")
            # step 3: change the value but dismiss with Escape instead of accepting
            reltol_fields = [handle for handle in app.client().find_by_type_in(root, "LineEdit") if app.client().get_element_properties(handle).get("accessibleLabel") == "RELTOL"]
            app.client().set_element_value(reltol_fields[0], "9e-9")
            fields.nth(0).press("\x1b")
            # assert: the dialog closed
            fields.nth(0).wait_for_gone(timeout=10.0)
            # assert: the netlist editor still holds the original option line
            editor_text = app.get_by_id("NetlistEditor::input").text()
            option_lines = [line.strip() for line in editor_text.splitlines() if line.strip().startswith(".OPTIONS")]
            self.assertEqual(option_lines, [".OPTIONS TIMEINT RELTOL=1e-4"])

    def test_accept_keeps_bare_flag_options(self) -> None:
        # arrange: write a transient netlist carrying the bare FFTOUT flag without a value
        netlist = Path(tempfile.mkdtemp(prefix="xyce-options-flag-")) / "options-flag.cir"
        netlist.write_text("* options flag test\nV1 in 0 PULSE(0 5 0 1n 1n 10m 20m)\nR1 in 0 1k\n.OPTIONS FFT FFTOUT\n.TRAN 1u 10u\n.END\n")
        # arrange: launch the application with the netlist through the command line
        with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
            # step 1: open the options dialog from the toolbar
            app.get_by_type("ToolbarButton").nth(7).click()
            # arrange: the dialog is open once its value fields exist
            app.get_by_type("LineEdit").nth(0).wait_for_exists(timeout=10.0)
            root = app.client().get_window_properties()["rootElementHandle"]
            scroller = app.get_by_type("ScrollView").nth(1)
            # step 2: scroll the FFT card into the scroll viewport (eleven cards with 194 rows precede it; only viewport fields are exposed)
            for _ in range(250):
                flag_fields = [handle for handle in app.client().find_by_type_in(root, "LineEdit") if app.client().get_element_properties(handle).get("accessibleLabel") == "FFTOUT"]
                if flag_fields:
                    field_rect = app.client().get_element_properties(flag_fields[0])
                    viewport = scroller.properties()
                    top = field_rect["absolutePosition"]["y"]
                    bottom = top + field_rect["size"]["height"]
                    if top >= viewport["absolutePosition"]["y"] and bottom <= viewport["absolutePosition"]["y"] + viewport["size"]["height"]:
                        break
                scroller.scroll(0.0, SCROLL_STEP)
            else:
                self.fail("expected the FFTOUT field to scroll into view")
            # assert: the FFTOUT row shows the flag marker instead of a value
            flag_fields = [handle for handle in app.client().find_by_type_in(root, "LineEdit") if app.client().get_element_properties(handle).get("accessibleLabel") == "FFTOUT"]
            self.assertEqual(app.client().get_element_properties(flag_fields[0]).get("accessiblePlaceholderText"), "flag is set")
            # step 3: accept the dialog without editing anything
            ok = [handle for handle in app.client().find_by_type_in(root, "Button") if app.client().get_element_properties(handle).get("accessibleLabel") == "OK"][0]
            app.client().click_element(ok)
            # assert: the dialog closed
            app.get_by_type("LineEdit").nth(0).wait_for_gone(timeout=10.0)
            # assert: the bare flag line survives the round trip next to the untouched analysis
            editor_text = app.get_by_id("NetlistEditor::input").text()
            option_lines = [line.strip() for line in editor_text.splitlines() if line.strip().startswith(".OPTIONS")]
            self.assertEqual(option_lines, [".OPTIONS FFT FFTOUT"])
            self.assertIn(".TRAN 1u 10u", editor_text)

import tempfile
import unittest
from pathlib import Path

from slint_automation import TestSession, launch

# scroll step in logical pixels per wheel tick; a negative delta_y reveals content further down
SCROLL_STEP = -60.0


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
            root = app.client().get_window_properties()["rootElementHandle"]
            scroller = app.get_by_type("ScrollView").nth(1)
            # step 2: scroll the DEVICE card until the TEMP field is fully inside the scroll viewport (only viewport fields are exposed)
            for _ in range(60):
                fields = [handle for handle in app.client().find_by_type_in(root, "LineEdit") if app.client().get_element_properties(handle).get("accessibleLabel") == "TEMP"]
                if fields:
                    field_rect = app.client().get_element_properties(fields[0])
                    viewport = scroller.properties()
                    top = field_rect["absolutePosition"]["y"]
                    bottom = top + field_rect["size"]["height"]
                    if top >= viewport["absolutePosition"]["y"] and bottom <= viewport["absolutePosition"]["y"] + viewport["size"]["height"]:
                        break
                scroller.scroll(0.0, SCROLL_STEP)
            else:
                self.fail("expected the TEMP field to scroll into view")
            # assert: the absent GMIN key shows the reference default as placeholder
            gmin = [handle for handle in app.client().find_by_type_in(root, "LineEdit") if app.client().get_element_properties(handle).get("accessibleLabel") == "GMIN"]
            self.assertEqual(app.client().get_element_properties(gmin[0]).get("accessiblePlaceholderText"), "1.0E-12")
            # step 3: edit the temperature and accept the dialog
            fields = [handle for handle in app.client().find_by_type_in(root, "LineEdit") if app.client().get_element_properties(handle).get("accessibleLabel") == "TEMP"]
            app.client().set_element_value(fields[0], "30")
            ok = [handle for handle in app.client().find_by_type_in(root, "Button") if app.client().get_element_properties(handle).get("accessibleLabel") == "OK"][0]
            app.client().click_element(ok)
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
            for _ in range(60):
                fields = [handle for handle in app.client().find_by_type_in(root, "LineEdit") if app.client().get_element_properties(handle).get("accessibleLabel") == "TEMP"]
                if fields:
                    field_rect = app.client().get_element_properties(fields[0])
                    viewport = scroller.properties()
                    top = field_rect["absolutePosition"]["y"]
                    bottom = top + field_rect["size"]["height"]
                    if top >= viewport["absolutePosition"]["y"] and bottom <= viewport["absolutePosition"]["y"] + viewport["size"]["height"]:
                        break
                scroller.scroll(0.0, SCROLL_STEP)
            else:
                self.fail("expected the TEMP field to scroll into view")
            # assert: the TEMP field carries the edited value
            fields = [handle for handle in app.client().find_by_type_in(root, "LineEdit") if app.client().get_element_properties(handle).get("accessibleLabel") == "TEMP"]
            self.assertEqual(app.client().get_element_properties(fields[0]).get("accessibleValue"), "30")

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
            root = app.client().get_window_properties()["rootElementHandle"]
            scroller = app.get_by_type("ScrollView").nth(1)
            # step 2: scroll the DEVICE card until the TEMP field is fully inside the scroll viewport
            for _ in range(60):
                temp_fields = [handle for handle in app.client().find_by_type_in(root, "LineEdit") if app.client().get_element_properties(handle).get("accessibleLabel") == "TEMP"]
                if temp_fields:
                    field_rect = app.client().get_element_properties(temp_fields[0])
                    viewport = scroller.properties()
                    top = field_rect["absolutePosition"]["y"]
                    bottom = top + field_rect["size"]["height"]
                    if top >= viewport["absolutePosition"]["y"] and bottom <= viewport["absolutePosition"]["y"] + viewport["size"]["height"]:
                        break
                scroller.scroll(0.0, SCROLL_STEP)
            else:
                self.fail("expected the TEMP field to scroll into view")
            # step 3: change the value but dismiss with Escape instead of accepting
            temp_fields = [handle for handle in app.client().find_by_type_in(root, "LineEdit") if app.client().get_element_properties(handle).get("accessibleLabel") == "TEMP"]
            app.client().set_element_value(temp_fields[0], "40")
            fields.nth(0).press("\x1b")
            # assert: the dialog closed
            fields.nth(0).wait_for_gone(timeout=10.0)
            # assert: the netlist editor still holds the original option line
            editor_text = app.get_by_id("NetlistEditor::input").text()
            option_lines = [line.strip() for line in editor_text.splitlines() if line.strip().startswith(".OPTIONS")]
            self.assertEqual(option_lines, [".OPTIONS DEVICE TEMP=25"])

    def test_accept_keeps_bare_flag_options(self) -> None:
        # arrange: write a netlist carrying the bare STRATEGY flag without a value
        netlist = Path(tempfile.mkdtemp(prefix="xyce-options-flag-")) / "options-flag.cir"
        netlist.write_text("* options flag test\nV1 in 0 PULSE(0 5 0 1n 1n 10m 20m)\nR1 in 0 1k\n.OPTIONS DIST STRATEGY\n.TRAN 1u 10u\n.END\n")
        # arrange: launch the application with the netlist through the command line
        with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
            # step 1: open the options dialog from the toolbar
            app.get_by_type("ToolbarButton").nth(7).click()
            # arrange: the dialog is open once its value fields exist
            app.get_by_type("LineEdit").nth(0).wait_for_exists(timeout=10.0)
            root = app.client().get_window_properties()["rootElementHandle"]
            scroller = app.get_by_type("ScrollView").nth(1)
            # step 2: scroll the DIST card into the scroll viewport (four cards with 72 rows precede it)
            for _ in range(90):
                flag_fields = [handle for handle in app.client().find_by_type_in(root, "LineEdit") if app.client().get_element_properties(handle).get("accessibleLabel") == "STRATEGY"]
                if flag_fields:
                    field_rect = app.client().get_element_properties(flag_fields[0])
                    viewport = scroller.properties()
                    top = field_rect["absolutePosition"]["y"]
                    bottom = top + field_rect["size"]["height"]
                    if top >= viewport["absolutePosition"]["y"] and bottom <= viewport["absolutePosition"]["y"] + viewport["size"]["height"]:
                        break
                scroller.scroll(0.0, SCROLL_STEP)
            else:
                self.fail("expected the STRATEGY field to scroll into view")
            # assert: the bare flag shows its state message instead of a value editor
            flag_fields = [handle for handle in app.client().find_by_type_in(root, "LineEdit") if app.client().get_element_properties(handle).get("accessibleLabel") == "STRATEGY"]
            self.assertEqual(app.client().get_element_properties(flag_fields[0]).get("accessiblePlaceholderText"), "flag is set")
            # step 3: accept the dialog without editing anything
            ok = [handle for handle in app.client().find_by_type_in(root, "Button") if app.client().get_element_properties(handle).get("accessibleLabel") == "OK"][0]
            app.client().click_element(ok)
            # assert: the dialog closed
            app.get_by_type("LineEdit").nth(0).wait_for_gone(timeout=10.0)
            # assert: the bare flag line survives the round trip next to the untouched analysis
            editor_text = app.get_by_id("NetlistEditor::input").text()
            option_lines = [line.strip() for line in editor_text.splitlines() if line.strip().startswith(".OPTIONS")]
            self.assertEqual(option_lines, [".OPTIONS DIST STRATEGY"])
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
            root = app.client().get_window_properties()["rootElementHandle"]
            scroller = app.get_by_type("ScrollView").nth(1)
            # step 2: scroll the .TRAN tab until the deepest inspected TIMEINT row (MAXORD) is inside the scroll viewport
            for _ in range(90):
                maxord_fields = [handle for handle in app.client().find_by_type_in(root, "LineEdit") if app.client().get_element_properties(handle).get("accessibleLabel") == "MAXORD"]
                if maxord_fields:
                    field_rect = app.client().get_element_properties(maxord_fields[0])
                    viewport = scroller.properties()
                    top = field_rect["absolutePosition"]["y"]
                    bottom = top + field_rect["size"]["height"]
                    if top >= viewport["absolutePosition"]["y"] and bottom <= viewport["absolutePosition"]["y"] + viewport["size"]["height"]:
                        break
                scroller.scroll(0.0, SCROLL_STEP)
            else:
                self.fail("expected the TIMEINT card to scroll into view")
            # assert: the seeded value and the reference default placeholder are shown (the first RELTOL row belongs to the TIMEINT card)
            reltol_fields = [handle for handle in app.client().find_by_type_in(root, "LineEdit") if app.client().get_element_properties(handle).get("accessibleLabel") == "RELTOL"]
            self.assertEqual(app.client().get_element_properties(reltol_fields[0]).get("accessibleValue"), "1e-4")
            maxord = [handle for handle in app.client().find_by_type_in(root, "LineEdit") if app.client().get_element_properties(handle).get("accessibleLabel") == "MAXORD"]
            self.assertEqual(app.client().get_element_properties(maxord[0]).get("accessiblePlaceholderText"), "2")
            # step 3: edit the tolerance and accept
            reltol_fields = [handle for handle in app.client().find_by_type_in(root, "LineEdit") if app.client().get_element_properties(handle).get("accessibleLabel") == "RELTOL"]
            app.client().set_element_value(reltol_fields[0], "5e-4")
            # step 4: select trap in the METHOD combobox (open the popup, move to the first choice, confirm)
            method = [handle for handle in app.client().find_by_type_in(root, "ComboBox") if app.client().get_element_properties(handle).get("accessibleLabel") == "METHOD"][0]
            method_touch = [handle for handle in app.client().find_by_type_in(method, "TouchArea") if (app.client().get_element_properties(handle).get("typeNamesAndIds") or [{}])[0].get("id") == "ComboBoxBase::i-touch-area"]
            app.client().click_element(method_touch[0])
            app.client().dispatch_key_event("\uf701")
            app.client().dispatch_key_event("\n")
            # assert: the combobox now shows the selected choice
            app.wait_for_condition(lambda: (app.client().get_element_properties(method).get("accessibleValue") == "trap"), timeout=5.0, message="expected the METHOD combobox to select trap")
            # step 5: accept the dialog
            ok = [handle for handle in app.client().find_by_type_in(root, "Button") if app.client().get_element_properties(handle).get("accessibleLabel") == "OK"][0]
            app.client().click_element(ok)
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
            for _ in range(90):
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
                self.fail("expected the TIMEINT card to scroll into view")
            # assert: both edits persisted across the reopen
            reltol_fields = [handle for handle in app.client().find_by_type_in(root, "LineEdit") if app.client().get_element_properties(handle).get("accessibleLabel") == "RELTOL"]
            self.assertEqual(app.client().get_element_properties(reltol_fields[0]).get("accessibleValue"), "5e-4")
            method = [handle for handle in app.client().find_by_type_in(root, "ComboBox") if app.client().get_element_properties(handle).get("accessibleLabel") == "METHOD"][0]
            self.assertEqual(app.client().get_element_properties(method).get("accessibleValue"), "trap")

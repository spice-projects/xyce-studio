import unittest
from pathlib import Path

from slint_automation import TestSession, expect, launch

# scroll step in logical pixels per wheel tick; a negative delta_y reveals
# content further down
SCROLL_STEP = -60.0


class ConfigurePceChecks(unittest.TestCase):

    def test_pce_parameters_round_trip(self) -> None:
        # arrange: resolve the sample dc sweep netlist shipped with the repository
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "dc-sweep-01.cir"
        # arrange: launch the application with the netlist through the command line
        with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
            # step 1: open the configure simulation dialog from the toolbar
            app.get_by_type("ToolbarButton").nth(6).click()
            app.get_by_type("LineEdit").nth(0).wait_for_exists()
            # arrange: the dialog scroll view is the second scroll view in the
            # window (the first is the netlist editor scroller)
            root = app.client().get_window_properties()["rootElementHandle"]
            scroller = app.get_by_type("ScrollView").nth(1)
            # step 2: scroll the dc page down until the .PCE enable checkbox
            # appears, then enable the section
            for _ in range(30):
                if any(app.client().get_element_properties(handle).get("accessibleLabel") == "Uncertainty quantification (.PCE)" for handle in app.client().find_by_type_in(root, "CheckBox")):
                    break
                scroller.scroll(0.0, SCROLL_STEP)
            else:
                self.fail("expected the .PCE enable checkbox to scroll into view")
            pce_enable = [handle for handle in app.client().find_by_type_in(root, "CheckBox") if app.client().get_element_properties(handle).get("accessibleLabel") == "Uncertainty quantification (.PCE)"][0]
            app.client().click_element(pce_enable)
            # step 3: scroll until the add action appears, then add a parameter row
            for _ in range(30):
                if any(app.client().get_element_properties(handle).get("accessibleLabel") == "Add Parameter" for handle in app.client().find_by_type_in(root, "Button")):
                    break
                scroller.scroll(0.0, SCROLL_STEP)
            else:
                self.fail("expected the add parameter action to scroll into view after enabling .PCE")
            add_parameter = [handle for handle in app.client().find_by_type_in(root, "Button") if app.client().get_element_properties(handle).get("accessibleLabel") == "Add Parameter"][0]
            app.client().click_element(add_parameter)
            # step 4: scroll until all three parameter row fields are in view, then
            # select the normal distribution through the combo popup (the testing
            # backend reports popup items at popup-relative coordinates, so the
            # selection is driven with the arrow and return keys) and fill the row
            for _ in range(30):
                placeholders = [app.client().get_element_properties(handle).get("accessiblePlaceholderText") for handle in app.client().find_by_type_in(root, "LineEdit")]
                if placeholders.count("e.g. R1") == 1 and placeholders.count("e.g. 1K") == 1 and placeholders.count("e.g. 5K") == 1:
                    break
                scroller.scroll(0.0, SCROLL_STEP)
            else:
                self.fail("expected the parameter row fields to scroll into view after adding a row")
            row_fields = {}
            for handle in app.client().find_by_type_in(root, "LineEdit"):
                props = app.client().get_element_properties(handle)
                if props.get("accessiblePlaceholderText") in ("e.g. R1", "e.g. 1K", "e.g. 5K"):
                    row_fields[props["accessiblePlaceholderText"]] = handle
            combo_touch_areas = [handle for handle in app.client().find_by_type_in(root, "TouchArea") if (app.client().get_element_properties(handle).get("typeNamesAndIds") or [{}])[0].get("id") == "ComboBoxBase::i-touch-area"]
            row_distribution = None
            name_top = app.client().get_element_properties(row_fields["e.g. R1"])["absolutePosition"]["y"]
            first_top = app.client().get_element_properties(row_fields["e.g. 1K"])["absolutePosition"]["y"]
            for handle in combo_touch_areas:
                top = app.client().get_element_properties(handle)["absolutePosition"]["y"]
                if name_top < top < first_top:
                    row_distribution = handle
            app.client().click_element(row_distribution)
            app.client().dispatch_key_event("\uf701")
            app.client().dispatch_key_event("\n")
            app.client().set_element_value(row_fields["e.g. R1"], "R1")
            app.client().set_element_value(row_fields["e.g. 1K"], "3K")
            app.client().set_element_value(row_fields["e.g. 5K"], "1K")
            # step 5: scroll until the .OPTIONS PCES editor is fully inside the
            # scroll viewport (a clipped editor cannot receive the focus click),
            # then type the package entries into the focused editor (a TextEdit
            # carries no accessible value for set_element_value, so type key by key)
            for _ in range(30):
                editors = [handle for handle in app.client().find_by_type_in(root, "TextEdit") if app.client().get_element_properties(handle).get("accessiblePlaceholderText") == "e.g. OUTPUTS={V(1)}"]
                if editors:
                    editor_rect = app.client().get_element_properties(editors[0])
                    viewport = scroller.properties()
                    top = editor_rect["absolutePosition"]["y"]
                    bottom = top + editor_rect["size"]["height"]
                    if top >= viewport["absolutePosition"]["y"] and bottom <= viewport["absolutePosition"]["y"] + viewport["size"]["height"]:
                        break
                scroller.scroll(0.0, SCROLL_STEP)
            else:
                self.fail("expected the .OPTIONS PCES editor to scroll fully into view")
            pces_editor = [handle for handle in app.client().find_by_type_in(root, "TextEdit") if app.client().get_element_properties(handle).get("accessiblePlaceholderText") == "e.g. OUTPUTS={V(1)}"][0]
            app.client().click_element(pces_editor)
            for character in "OUTPUTS={V(1)}":
                app.client().dispatch_key_event(character)
            # step 6: accept the dialog
            ok = [handle for handle in app.client().find_by_type_in(root, "Button") if app.client().get_element_properties(handle).get("accessibleLabel") == "OK"][0]
            app.client().click_element(ok)
            # assert: the dialog closed and the netlist gained the .PCE directives
            app.get_by_type("LineEdit").nth(0).wait_for_gone()
            editor_text = app.get_by_id("NetlistEditor::input").text()
            pce_lines = [line.strip() for line in editor_text.splitlines() if line.strip().startswith(".PCE")]
            self.assertEqual(pce_lines, [".PCE param=R1 type=normal means=3K std_deviations=1K"])
            options_lines = [line.strip() for line in editor_text.splitlines() if line.strip().startswith(".OPTIONS PCES")]
            self.assertEqual(options_lines, [".OPTIONS PCES OUTPUTS={V(1)}"])
            # step 7: reopen the dialog and scroll until the persisted row editor
            # fields are in view
            app.get_by_type("ToolbarButton").nth(6).click()
            app.get_by_type("LineEdit").nth(0).wait_for_exists()
            for _ in range(30):
                placeholders = [app.client().get_element_properties(handle).get("accessiblePlaceholderText") for handle in app.client().find_by_type_in(root, "LineEdit")]
                if placeholders.count("e.g. R1") == 1 and placeholders.count("e.g. 1K") == 1 and placeholders.count("e.g. 5K") == 1:
                    break
                scroller.scroll(0.0, SCROLL_STEP)
            else:
                self.fail("expected the persisted parameter row fields to scroll into view")
            row_fields = {}
            for handle in app.client().find_by_type_in(root, "LineEdit"):
                props = app.client().get_element_properties(handle)
                if props.get("accessiblePlaceholderText") in ("e.g. R1", "e.g. 1K", "e.g. 5K"):
                    row_fields[props["accessiblePlaceholderText"]] = handle
            # assert: the parameter row fields show the accepted values
            self.assertEqual(app.client().get_element_properties(row_fields["e.g. R1"]).get("accessibleValue"), "R1")
            self.assertEqual(app.client().get_element_properties(row_fields["e.g. 1K"]).get("accessibleValue"), "3K")
            self.assertEqual(app.client().get_element_properties(row_fields["e.g. 5K"]).get("accessibleValue"), "1K")
            # step 8: scroll until the .OPTIONS PCES editor appears
            for _ in range(30):
                if any(app.client().get_element_properties(handle).get("accessiblePlaceholderText") == "e.g. OUTPUTS={V(1)}" for handle in app.client().find_by_type_in(root, "TextEdit")):
                    break
                scroller.scroll(0.0, SCROLL_STEP)
            else:
                self.fail("expected the persisted .OPTIONS PCES editor to scroll into view")
            pces_editor = [handle for handle in app.client().find_by_type_in(root, "TextEdit") if app.client().get_element_properties(handle).get("accessiblePlaceholderText") == "e.g. OUTPUTS={V(1)}"][0]
            # assert: the .OPTIONS PCES text shows the accepted entries
            self.assertEqual(app.client().get_element_properties(pces_editor).get("accessibleValue") or app.client().get_element_properties(pces_editor).get("accessibleLabel"), "OUTPUTS={V(1)}")

    def test_pce_seeded_from_netlist_directives(self) -> None:
        # arrange: write a dc netlist with .PCE directives into a scratch directory
        import tempfile
        netlist = Path(tempfile.mkdtemp(prefix="xyce-pce-seed-")) / "pce-seeded.cir"
        netlist.write_text("* PCE seeded netlist\nV1 1 0 DC 0V\nR1 1 2 1k\nR2 2 0 2k\n.DC V1 0 5 0.5\n.PCE param=R1,R2 type=normal,normal means=1k,2k std_deviations=100,200\n.PRINT DC V(*) I(*)\n.END\n")
        # arrange: launch the application with the netlist through the command line
        with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
            # step 1: open the configure simulation dialog from the toolbar
            app.get_by_type("ToolbarButton").nth(6).click()
            app.get_by_type("LineEdit").nth(0).wait_for_exists()
            # arrange: the dialog scroll view is the second scroll view in the
            # window (the first is the netlist editor scroller)
            root = app.client().get_window_properties()["rootElementHandle"]
            scroller = app.get_by_type("ScrollView").nth(1)
            # step 2: scroll the dc page down until both seeded parameter rows are
            # in view (two name fields, two first values, two second values)
            for _ in range(30):
                placeholders = [app.client().get_element_properties(handle).get("accessiblePlaceholderText") for handle in app.client().find_by_type_in(root, "LineEdit")]
                if placeholders.count("e.g. R1") == 2 and placeholders.count("e.g. 1K") == 2 and placeholders.count("e.g. 5K") == 2:
                    break
                scroller.scroll(0.0, SCROLL_STEP)
            else:
                self.fail("expected both seeded parameter rows to scroll into view")
            # assert: the first row shows the first parameter and its normal
            # distribution values (mean, standard deviation)
            name_fields = []
            first_fields = []
            second_fields = []
            for handle in app.client().find_by_type_in(root, "LineEdit"):
                props = app.client().get_element_properties(handle)
                if props.get("accessiblePlaceholderText") == "e.g. R1":
                    name_fields.append(props.get("accessibleValue"))
                elif props.get("accessiblePlaceholderText") == "e.g. 1K":
                    first_fields.append(props.get("accessibleValue"))
                elif props.get("accessiblePlaceholderText") == "e.g. 5K":
                    second_fields.append(props.get("accessibleValue"))
            self.assertEqual(name_fields, ["R1", "R2"])
            self.assertEqual(first_fields, ["1k", "2k"])
            self.assertEqual(second_fields, ["100", "200"])


class PceRunChecks(unittest.TestCase):

    def test_pce_run_produces_print_output_file(self) -> None:
        # arrange: resolve the xyce executable and the sample dc sweep netlist
        import shutil
        import tempfile
        import time
        xyce = shutil.which("Xyce")
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "dc-sweep-01.cir"
        # arrange: skip the scenario when the xyce executable is not available
        if xyce is None:
            self.skipTest("Xyce executable not found")
        # arrange: launch the application with the netlist and the xyce executable
        with TestSession(launch(args=["--netlist", str(netlist), "--xyce", xyce]), self.id()) as app:
            # step 1: open the configure simulation dialog from the toolbar
            app.get_by_type("ToolbarButton").nth(6).click()
            app.get_by_type("LineEdit").nth(0).wait_for_exists()
            root = app.client().get_window_properties()["rootElementHandle"]
            scroller = app.get_by_type("ScrollView").nth(1)
            # step 2: scroll the dc page down until the .PCE enable checkbox
            # appears, then enable the section
            for _ in range(30):
                if any(app.client().get_element_properties(handle).get("accessibleLabel") == "Uncertainty quantification (.PCE)" for handle in app.client().find_by_type_in(root, "CheckBox")):
                    break
                scroller.scroll(0.0, SCROLL_STEP)
            else:
                self.fail("expected the .PCE enable checkbox to scroll into view")
            pce_enable = [handle for handle in app.client().find_by_type_in(root, "CheckBox") if app.client().get_element_properties(handle).get("accessibleLabel") == "Uncertainty quantification (.PCE)"][0]
            app.client().click_element(pce_enable)
            # step 3: scroll until the add action appears, then add a parameter row
            for _ in range(30):
                if any(app.client().get_element_properties(handle).get("accessibleLabel") == "Add Parameter" for handle in app.client().find_by_type_in(root, "Button")):
                    break
                scroller.scroll(0.0, SCROLL_STEP)
            else:
                self.fail("expected the add parameter action to scroll into view after enabling .PCE")
            add_parameter = [handle for handle in app.client().find_by_type_in(root, "Button") if app.client().get_element_properties(handle).get("accessibleLabel") == "Add Parameter"][0]
            app.client().click_element(add_parameter)
            # step 4: scroll until all three parameter row fields are in view, then
            # select the normal distribution through the combo popup (the testing
            # backend reports popup items at popup-relative coordinates, so the
            # selection is driven with the arrow and return keys) and fill the row
            for _ in range(30):
                placeholders = [app.client().get_element_properties(handle).get("accessiblePlaceholderText") for handle in app.client().find_by_type_in(root, "LineEdit")]
                if placeholders.count("e.g. R1") == 1 and placeholders.count("e.g. 1K") == 1 and placeholders.count("e.g. 5K") == 1:
                    break
                scroller.scroll(0.0, SCROLL_STEP)
            else:
                self.fail("expected the parameter row fields to scroll into view after adding a row")
            row_fields = {}
            for handle in app.client().find_by_type_in(root, "LineEdit"):
                props = app.client().get_element_properties(handle)
                if props.get("accessiblePlaceholderText") in ("e.g. R1", "e.g. 1K", "e.g. 5K"):
                    row_fields[props["accessiblePlaceholderText"]] = handle
            combo_touch_areas = [handle for handle in app.client().find_by_type_in(root, "TouchArea") if (app.client().get_element_properties(handle).get("typeNamesAndIds") or [{}])[0].get("id") == "ComboBoxBase::i-touch-area"]
            row_distribution = None
            name_top = app.client().get_element_properties(row_fields["e.g. R1"])["absolutePosition"]["y"]
            first_top = app.client().get_element_properties(row_fields["e.g. 1K"])["absolutePosition"]["y"]
            for handle in combo_touch_areas:
                top = app.client().get_element_properties(handle)["absolutePosition"]["y"]
                if name_top < top < first_top:
                    row_distribution = handle
            app.client().click_element(row_distribution)
            app.client().dispatch_key_event("\uf701")
            app.client().dispatch_key_event("\n")
            app.client().set_element_value(row_fields["e.g. R1"], "R1")
            app.client().set_element_value(row_fields["e.g. 1K"], "3K")
            app.client().set_element_value(row_fields["e.g. 5K"], "1K")
            # step 5: scroll until the .OPTIONS PCES editor is fully inside the
            # scroll viewport, then type the package entries into the focused editor
            for _ in range(30):
                editors = [handle for handle in app.client().find_by_type_in(root, "TextEdit") if app.client().get_element_properties(handle).get("accessiblePlaceholderText") == "e.g. OUTPUTS={V(1)}"]
                if editors:
                    editor_rect = app.client().get_element_properties(editors[0])
                    viewport = scroller.properties()
                    top = editor_rect["absolutePosition"]["y"]
                    bottom = top + editor_rect["size"]["height"]
                    if top >= viewport["absolutePosition"]["y"] and bottom <= viewport["absolutePosition"]["y"] + viewport["size"]["height"]:
                        break
                scroller.scroll(0.0, SCROLL_STEP)
            else:
                self.fail("expected the .OPTIONS PCES editor to scroll fully into view")
            pces_editor = [handle for handle in app.client().find_by_type_in(root, "TextEdit") if app.client().get_element_properties(handle).get("accessiblePlaceholderText") == "e.g. OUTPUTS={V(1)}"][0]
            app.client().click_element(pces_editor)
            for character in "OUTPUTS={V(1)}":
                app.client().dispatch_key_event(character)
            # step 6: scroll until the .PRINT PCE enable checkbox appears, enable
            # the print section and set the additional variables field
            for _ in range(30):
                if any(app.client().get_element_properties(handle).get("accessibleLabel") == "Enable .PRINT PCE output" for handle in app.client().find_by_type_in(root, "CheckBox")):
                    break
                scroller.scroll(0.0, SCROLL_STEP)
            else:
                self.fail("expected the .PRINT PCE enable checkbox to scroll into view")
            print_enable = [handle for handle in app.client().find_by_type_in(root, "CheckBox") if app.client().get_element_properties(handle).get("accessibleLabel") == "Enable .PRINT PCE output"][0]
            app.client().click_element(print_enable)
            for _ in range(30):
                if any(app.client().get_element_properties(handle).get("accessiblePlaceholderText") == "e.g. V(out)" for handle in app.client().find_by_type_in(root, "LineEdit")):
                    break
                scroller.scroll(0.0, SCROLL_STEP)
            else:
                self.fail("expected the print variables field to scroll into view after enabling the print section")
            variables_field = [handle for handle in app.client().find_by_type_in(root, "LineEdit") if app.client().get_element_properties(handle).get("accessiblePlaceholderText") == "e.g. V(out)"][0]
            app.client().set_element_value(variables_field, "V(1)")
            # step 7: accept the dialog and verify the netlist gained the directives
            ok = [handle for handle in app.client().find_by_type_in(root, "Button") if app.client().get_element_properties(handle).get("accessibleLabel") == "OK"][0]
            app.client().click_element(ok)
            app.get_by_type("LineEdit").nth(0).wait_for_gone()
            editor_text = app.get_by_id("NetlistEditor::input").text()
            print_lines = [line.strip() for line in editor_text.splitlines() if line.strip().startswith(".PRINT PCE")]
            self.assertEqual(print_lines, [".PRINT PCE V(1)"])
            # step 8: record the pre-run time, then run the simulation and wait
            # for the final status message
            pre_run = time.time()
            app.get_by_type("ToolbarButton").nth(5).click()
            status = app.get_by_id("MainWindow::statusbar").child("Text")
            expect(status).to_have_property("accessibleLabel", "Simulation finished successfully", timeout=60.0)
            # step 9: find the .PCE.prn file Xyce produced next to the temporary
            # netlist after the run started and show its content
            produced = sorted((path for path in Path(tempfile.gettempdir()).glob("xyce_*.PCE.prn") if path.stat().st_mtime >= pre_run), key=lambda path: path.stat().st_mtime)
            self.assertTrue(produced, "expected a .PCE.prn file next to the temporary netlist")
            content = produced[-1].read_text()
            self.assertIn("V(1)", content)
            print("--- produced file:", produced[-1])
            print(content)

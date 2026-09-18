import unittest
from pathlib import Path

from slint_automation import TestSession, expect, launch


class ConfigureSimulationChecks(unittest.TestCase):

    def test_transient_parameters_round_trip(self) -> None:
        # arrange: resolve the sample netlist shipped with the repository
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "tran-simple-01.cir"
        # arrange: launch the application with the netlist through the command line
        with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
            # step 1: open the configure simulation dialog from the toolbar
            app.get_by_type("ToolbarButton").nth(6).click()
            # arrange: locate the transient analysis form fields
            fields = app.get_by_type("LineEdit")
            fields.nth(0).wait_for_exists()
            # assert: the form fields show the .TRAN directive values
            expect(fields.nth(0)).to_have_text("1u")
            expect(fields.nth(1)).to_have_text("20m")
            expect(fields.nth(2)).to_have_text("0")
            # step 2: edit the simulation end time and accept the dialog
            fields.nth(1).fill("25m")
            root = app.client().get_window_properties()["rootElementHandle"]
            ok = [handle for handle in app.client().find_by_type_in(root, "Button") if app.client().get_element_properties(handle).get("accessibleLabel") == "OK"][0]
            app.client().click_element(ok)
            # assert: the dialog closed and the netlist directive was rewritten
            fields.nth(0).wait_for_gone()
            tran_lines = [line for line in app.get_by_id("NetlistEditor::input").text().splitlines() if line.strip().startswith(".TRAN")]
            self.assertEqual(tran_lines, [".TRAN 1u 25m 0"])
            # step 3: reopen the dialog to verify the accepted values persisted
            app.get_by_type("ToolbarButton").nth(6).click()
            fields.nth(0).wait_for_exists()
            # assert: the end time field shows the accepted value
            expect(fields.nth(1)).to_have_text("25m")

    def test_dc_sweep_validation_rejects_empty_sweep(self) -> None:
        # arrange: resolve the sample netlist (transient only, no .DC directive)
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "tran-simple-01.cir"
        # arrange: launch the application with the netlist through the command line
        with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
            # step 1: open the configure simulation dialog from the toolbar
            app.get_by_type("ToolbarButton").nth(6).click()
            root = app.client().get_window_properties()["rootElementHandle"]
            ok = [handle for handle in app.client().find_by_type_in(root, "Button") if app.client().get_element_properties(handle).get("accessibleLabel") == "OK"][0]
            # step 2: switch to the dc analysis tab (declaration order: .op, .dc, .tran, ...)
            tabs = app.client().find_by_type_in(root, "TabButton")
            app.client().click_element(tabs[1])
            # step 3: accept the dialog with an empty dc sweep (no sweep rows configured)
            app.client().click_element(ok)
            # assert: the dialog stays open for corrections (the footer ok button remains)
            app.wait_for_condition(lambda: any(app.client().get_element_properties(handle).get("accessibleLabel") == "OK" for handle in app.client().find_by_type_in(root, "Button")), timeout=5.0, message="expected the dialog to stay open after the rejected accept")
            # assert: the validation error names the missing sweep variable
            app.wait_for_condition(lambda: any("requires at least one sweep variable" in (app.client().get_element_properties(handle).get("accessibleLabel") or "") for handle in app.client().find_by_type_in(root, "Text")), timeout=5.0, message="expected the dc sweep validation error to appear")

    def test_transient_print_checkboxes_fit_the_panel(self) -> None:
        # arrange: resolve the sample netlist; it carries a .PRINT TRAN so the
        # print section starts expanded
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "tran-simple-01.cir"
        # arrange: launch the application with the netlist through the command line
        with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
            # step 1: open the configure simulation dialog from the toolbar
            app.get_by_type("ToolbarButton").nth(6).click()
            app.get_by_type("LineEdit").nth(0).wait_for_exists(timeout=10.0)
            # arrange: collect the .PRINT checkbox elements from the window root
            root = app.client().get_window_properties()["rootElementHandle"]
            labels = ("All voltages V(*)", "All currents I(*)", "Power P(*)", "BJT leads", "FET leads")
            # step 2: measure the .PRINT checkboxes
            checkboxes = {}
            for handle in app.client().find_by_type_in(root, "CheckBox"):
                props = app.client().get_element_properties(handle)
                if props.get("accessibleLabel") in labels:
                    checkboxes[props["accessibleLabel"]] = props
            # assert: every .PRINT checkbox is rendered
            self.assertEqual(sorted(checkboxes), sorted(labels))
            # assert: the enable checkbox spans the .PRINT card interior; every
            # wildcard and lead checkbox must fit inside that reference width
            enable = [props for handle in app.client().find_by_type_in(root, "CheckBox") if (props := app.client().get_element_properties(handle)).get("accessibleLabel") == "Enable .PRINT output"][0]
            reference_right = enable["absolutePosition"]["x"] + enable["size"]["width"]
            for label in labels:
                props = checkboxes[label]
                left = props["absolutePosition"]["x"]
                right = left + props["size"]["width"]
                self.assertGreaterEqual(left, enable["absolutePosition"]["x"], f"{label} starts outside the .PRINT card")
                self.assertLessEqual(right, reference_right, f"{label} is clipped at the right edge of the .PRINT card")

    def test_toggling_print_checkbox_does_not_reflow_the_row(self) -> None:
        # arrange: resolve the sample netlist; it carries a .PRINT TRAN so the
        # print section starts expanded
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "tran-simple-01.cir"
        # arrange: launch the application with the netlist through the command line
        with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
            # step 1: open the configure simulation dialog from the toolbar
            app.get_by_type("ToolbarButton").nth(6).click()
            app.get_by_type("LineEdit").nth(0).wait_for_exists(timeout=10.0)
            root = app.client().get_window_properties()["rootElementHandle"]
            labels = ("All voltages V(*)", "All currents I(*)", "Power P(*)", "BJT leads", "FET leads")
            # step 2: record the checkbox positions
            before = {props.get("accessibleLabel"): props["absolutePosition"]["x"] for handle in app.client().find_by_type_in(root, "CheckBox") if (props := app.client().get_element_properties(handle)).get("accessibleLabel") in labels}
            # step 3: toggle All voltages V(*)
            voltages = [handle for handle in app.client().find_by_type_in(root, "CheckBox") if app.client().get_element_properties(handle).get("accessibleLabel") == "All voltages V(*)"][0]
            app.client().click_element(voltages)
            # assert: no checkbox moved horizontally
            after = {props.get("accessibleLabel"): props["absolutePosition"]["x"] for handle in app.client().find_by_type_in(root, "CheckBox") if (props := app.client().get_element_properties(handle)).get("accessibleLabel") in labels}
            self.assertEqual(after, before, "toggling a .PRINT checkbox reflowed the row")

    def test_ac_print_checkbox_does_not_reflow_the_row(self) -> None:
        # arrange: resolve the sample netlist; it carries a .PRINT AC so the
        # print section starts expanded on the .AC tab
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "ac-simple-01.cir"
        # arrange: launch the application with the netlist through the command line
        with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
            # step 1: open the configure simulation dialog from the toolbar
            app.get_by_type("ToolbarButton").nth(6).click()
            app.get_by_type("LineEdit").nth(0).wait_for_exists(timeout=10.0)
            root = app.client().get_window_properties()["rootElementHandle"]
            labels = ("All voltages V(*)", "All currents I(*)")
            # step 2: record the checkbox positions
            before = {props.get("accessibleLabel"): props["absolutePosition"]["x"] for handle in app.client().find_by_type_in(root, "CheckBox") if (props := app.client().get_element_properties(handle)).get("accessibleLabel") in labels}
            # step 3: toggle All voltages V(*)
            voltages = [handle for handle in app.client().find_by_type_in(root, "CheckBox") if app.client().get_element_properties(handle).get("accessibleLabel") == "All voltages V(*)"][0]
            app.client().click_element(voltages)
            # assert: no checkbox moved horizontally
            after = {props.get("accessibleLabel"): props["absolutePosition"]["x"] for handle in app.client().find_by_type_in(root, "CheckBox") if (props := app.client().get_element_properties(handle)).get("accessibleLabel") in labels}
            self.assertEqual(after, before, "toggling a .PRINT checkbox reflowed the row")

    def test_lin_print_checkbox_does_not_reflow_the_row(self) -> None:
        # arrange: resolve the sample netlist; it carries a .LIN with a
        # retained .PRINT AC so the print section starts expanded on the .LIN tab
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "lin-simple-01.cir"
        # arrange: launch the application with the netlist through the command line
        with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
            # step 1: open the configure simulation dialog from the toolbar
            app.get_by_type("ToolbarButton").nth(6).click()
            app.get_by_type("LineEdit").nth(0).wait_for_exists(timeout=10.0)
            root = app.client().get_window_properties()["rootElementHandle"]
            labels = ("All voltages V(*)", "All currents I(*)")
            # step 2: record the checkbox positions
            before = {props.get("accessibleLabel"): props["absolutePosition"]["x"] for handle in app.client().find_by_type_in(root, "CheckBox") if (props := app.client().get_element_properties(handle)).get("accessibleLabel") in labels}
            # step 3: toggle All voltages V(*)
            voltages = [handle for handle in app.client().find_by_type_in(root, "CheckBox") if app.client().get_element_properties(handle).get("accessibleLabel") == "All voltages V(*)"][0]
            app.client().click_element(voltages)
            # assert: no checkbox moved horizontally
            after = {props.get("accessibleLabel"): props["absolutePosition"]["x"] for handle in app.client().find_by_type_in(root, "CheckBox") if (props := app.client().get_element_properties(handle)).get("accessibleLabel") in labels}
            self.assertEqual(after, before, "toggling a .PRINT checkbox reflowed the row")

    def test_noise_print_checkbox_does_not_reflow_the_row(self) -> None:
        # arrange: write a noise netlist into a scratch directory; the .PRINT
        # NOISE keeps the print section expanded on the .NOISE tab
        import tempfile
        netlist = Path(tempfile.mkdtemp(prefix="xyce-noise-print-")) / "noise-print.cir"
        netlist.write_text("* Noise print checkbox test\nV1 in 0 DC 0 AC 1\nR1 in out 1k\nC1 out 0 1n\n.NOISE V(out) in DEC 10 1k 10meg\n.PRINT NOISE V(*) I(*)\n.END\n")
        # arrange: launch the application with the netlist through the command line
        with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
            # step 1: open the configure simulation dialog from the toolbar
            app.get_by_type("ToolbarButton").nth(6).click()
            app.get_by_type("LineEdit").nth(0).wait_for_exists(timeout=10.0)
            root = app.client().get_window_properties()["rootElementHandle"]
            labels = ("All voltages V(*)", "All currents I(*)")
            # step 2: record the checkbox positions
            before = {props.get("accessibleLabel"): props["absolutePosition"]["x"] for handle in app.client().find_by_type_in(root, "CheckBox") if (props := app.client().get_element_properties(handle)).get("accessibleLabel") in labels}
            # step 3: toggle All voltages V(*)
            voltages = [handle for handle in app.client().find_by_type_in(root, "CheckBox") if app.client().get_element_properties(handle).get("accessibleLabel") == "All voltages V(*)"][0]
            app.client().click_element(voltages)
            # assert: no checkbox moved horizontally
            after = {props.get("accessibleLabel"): props["absolutePosition"]["x"] for handle in app.client().find_by_type_in(root, "CheckBox") if (props := app.client().get_element_properties(handle)).get("accessibleLabel") in labels}
            self.assertEqual(after, before, "toggling a .PRINT checkbox reflowed the row")

    def test_hb_print_checkbox_does_not_reflow_the_row(self) -> None:
        # arrange: write a harmonic balance netlist into a scratch directory;
        # the .PRINT HB keeps the print section expanded on the .HB tab
        import tempfile
        netlist = Path(tempfile.mkdtemp(prefix="xyce-hb-print-")) / "hb-print.cir"
        netlist.write_text("* Harmonic balance print checkbox test\nV1 in 0 DC 0 AC 1\nR1 in out 1k\nC1 out 0 1n\n.HB 1meg 10\n.PRINT HB V(*) I(*)\n.END\n")
        # arrange: launch the application with the netlist through the command line
        with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
            # step 1: open the configure simulation dialog from the toolbar
            app.get_by_type("ToolbarButton").nth(6).click()
            app.get_by_type("LineEdit").nth(0).wait_for_exists(timeout=10.0)
            root = app.client().get_window_properties()["rootElementHandle"]
            labels = ("All voltages V(*)", "All currents I(*)")
            # step 2: record the checkbox positions
            before = {props.get("accessibleLabel"): props["absolutePosition"]["x"] for handle in app.client().find_by_type_in(root, "CheckBox") if (props := app.client().get_element_properties(handle)).get("accessibleLabel") in labels}
            # step 3: toggle All voltages V(*)
            voltages = [handle for handle in app.client().find_by_type_in(root, "CheckBox") if app.client().get_element_properties(handle).get("accessibleLabel") == "All voltages V(*)"][0]
            app.client().click_element(voltages)
            # assert: no checkbox moved horizontally
            after = {props.get("accessibleLabel"): props["absolutePosition"]["x"] for handle in app.client().find_by_type_in(root, "CheckBox") if (props := app.client().get_element_properties(handle)).get("accessibleLabel") in labels}
            self.assertEqual(after, before, "toggling a .PRINT checkbox reflowed the row")

    def test_cancel_and_escape_keep_original_values(self) -> None:
        # arrange: resolve the sample netlist shipped with the repository
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "tran-simple-01.cir"
        # arrange: launch the application with the netlist through the command line
        with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
            # step 1: open the configure simulation dialog and edit the end time
            app.get_by_type("ToolbarButton").nth(6).click()
            fields = app.get_by_type("LineEdit")
            fields.nth(0).wait_for_exists()
            fields.nth(1).fill("25m")
            # step 2: cancel the dialog from the footer
            root = app.client().get_window_properties()["rootElementHandle"]
            cancel = [handle for handle in app.client().find_by_type_in(root, "Button") if app.client().get_element_properties(handle).get("accessibleLabel") == "Cancel"][0]
            app.client().click_element(cancel)
            fields.nth(0).wait_for_gone()
            # step 3: reopen the dialog
            app.get_by_type("ToolbarButton").nth(6).click()
            fields.nth(0).wait_for_exists()
            # assert: the cancel discarded the edited value
            expect(fields.nth(1)).to_have_text("20m")
            # step 4: edit again and dismiss through the escape key
            fields.nth(1).fill("25m")
            fields.nth(1).press("\x1b")
            fields.nth(0).wait_for_gone()
            # step 5: reopen the dialog
            app.get_by_type("ToolbarButton").nth(6).click()
            fields.nth(0).wait_for_exists()
            # assert: the escape discarded the edited value too
            expect(fields.nth(1)).to_have_text("20m")

import shutil
import unittest
from pathlib import Path

from slint_automation import TestSession, expect, launch


class ReconfigureAfterRunChecks(unittest.TestCase):

    def test_second_run_uses_the_edited_transient_parameters(self) -> None:
        # arrange: resolve the xyce executable and the sample netlist from the repository
        xyce = shutil.which("Xyce")
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "tran-simple-01.cir"
        # arrange: skip the scenario when the xyce executable is not available
        if xyce is None:
            self.skipTest("Xyce executable not found")
        # arrange: launch the application with the netlist and the xyce executable
        with TestSession(launch(args=["--netlist", str(netlist), "--xyce", xyce]), self.id()) as app:
            # arrange: locate the status bar text and the toolbar tools
            status = app.get_by_id("MainWindow::status-message")
            tools = app.get_by_type("ToolbarButton")
            # step 1: run the simulation and wait for the final status message
            tools.nth(5).click()
            expect(status).to_have_property("accessibleLabel", "Simulation finished successfully", timeout=30.0)
            # step 2: open the configure simulation dialog from the toolbar
            tools.nth(6).click()
            # arrange: locate the transient analysis form fields
            fields = app.get_by_type("LineEdit")
            fields.nth(0).wait_for_exists()
            # assert: the dialog shows the netlist directive values before the edit
            expect(fields.nth(0)).to_have_text("1u")
            expect(fields.nth(1)).to_have_text("20m")
            # step 3: edit the simulation end time and accept the dialog
            fields.nth(1).fill("25m")
            root = app.client().get_window_properties()["rootElementHandle"]
            ok = [handle for handle in app.client().find_by_type_in(root, "Button") if app.client().get_element_properties(handle).get("accessibleLabel") == "OK"][0]
            app.client().click_element(ok)
            # assert: the dialog closed
            fields.nth(0).wait_for_gone()
            # step 4: run the simulation again and wait for the final status message
            tools.nth(5).click()
            expect(status).to_have_property("accessibleLabel", "Simulation finished successfully", timeout=30.0)
            # step 5: show the netlist view to inspect the effective directives
            tools.nth(2).click()
            editor = app.get_by_id("NetlistEditor::input")
            editor.wait_for_exists()
            # assert: the netlist shows the edited transient parameters
            tran_lines = [line for line in editor.text().splitlines() if line.strip().upper().startswith(".TRAN")]
            self.assertEqual(tran_lines, [".TRAN 1u 25m 0"])

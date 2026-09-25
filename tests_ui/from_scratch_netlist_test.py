import shutil
import tempfile
import unittest
from pathlib import Path

from slint_automation import TestSession, expect, launch


class FromScratchNetlistChecks(unittest.TestCase):

    def test_run_opens_the_simulation_dialog_and_keeps_the_typed_netlist(self) -> None:
        # arrange: resolve the xyce executable so the run action gets past its config check
        xyce = shutil.which("Xyce")
        # arrange: skip the scenario when the xyce executable is not available
        if xyce is None:
            self.skipTest("Xyce executable not found")
        # arrange: launch the application without any file, as a plain start does
        with TestSession(launch(args=["--xyce", xyce]), self.id()) as app:
            # arrange: locate the blank netlist editor shown at startup
            editor = app.get_by_id("NetlistEditor::input")
            expect(editor).to_exist(timeout=5.0)
            # step 1: type a netlist without simulation directives into the blank editor
            netlist = "* from scratch RLC\nV1 IN 0 DC 0 AC 1\nR1 IN 0 1k\nC1 IN 0 1u\n.END\n"
            editor.click()
            editor.fill(netlist)
            # assert: the editor shows the typed netlist
            expect(editor).to_have_text(netlist, timeout=5.0)
            # step 2: screenshot to validate the netlist text is visible on the screen
            screenshot = Path(__file__).resolve().parents[1] / "artifacts" / "from_scratch_after_typing.png"
            screenshot.parent.mkdir(exist_ok=True)
            app.screenshot(str(screenshot))
            # step 3: press the run tool; a netlist without simulation directives
            # must open the simulation configuration dialog instead of simulating
            app.get_by_type("ToolbarButton").nth(5).click()
            # assert: the simulation configuration dialog opened
            app.get_by_type("LineEdit").nth(0).wait_for_exists(timeout=10.0)
            # assert: the typed netlist is still in the editor
            expect(editor).to_have_text(netlist, timeout=5.0)

    def test_configure_a_typed_netlist_and_simulate_it(self) -> None:
        # arrange: resolve the xyce executable so the simulation can run
        xyce = shutil.which("Xyce")
        # arrange: skip the scenario when the xyce executable is not available
        if xyce is None:
            self.skipTest("Xyce executable not found")
        # arrange: launch the application without any file, as a plain start does
        with TestSession(launch(args=["--xyce", xyce]), self.id()) as app:
            # arrange: locate the blank editor and the status bar
            editor = app.get_by_id("NetlistEditor::input")
            status = app.get_by_id("MainWindow::statusbar").child("Text")
            expect(editor).to_exist(timeout=5.0)
            # step 1: type a netlist without simulation directives into the blank editor
            netlist = "* from scratch RLC\nV1 IN 0 PULSE(0 5 0 1n 1n 10m 20m)\nR1 IN N1 100\nC1 N1 0 1u\n.END\n"
            editor.click()
            editor.fill(netlist)
            expect(editor).to_have_text(netlist, timeout=5.0)
            # step 2: press the run tool, which opens the simulation configuration dialog
            app.get_by_type("ToolbarButton").nth(5).click()
            fields = app.get_by_type("LineEdit")
            fields.nth(0).wait_for_exists(timeout=10.0)
            # step 3: switch to the transient analysis tab (declaration order: .op, .dc, .tran)
            root = app.client().get_window_properties()["rootElementHandle"]
            tabs = app.client().find_by_type_in(root, "TabButton")
            app.client().click_element(tabs[2])
            # step 4: fill the transient analysis parameters
            fields.nth(0).fill("1u")
            fields.nth(1).fill("20m")
            fields.nth(2).fill("0")
            # step 5: enable the .PRINT section with voltage and current outputs
            enable_print = [handle for handle in app.client().find_by_type_in(root, "CheckBox") if app.client().get_element_properties(handle).get("accessibleLabel") == "Enable .PRINT output"][0]
            app.client().click_element(enable_print)
            voltages = [handle for handle in app.client().find_by_type_in(root, "CheckBox") if app.client().get_element_properties(handle).get("accessibleLabel") == "All voltages V(*)"][0]
            app.client().click_element(voltages)
            currents = [handle for handle in app.client().find_by_type_in(root, "CheckBox") if app.client().get_element_properties(handle).get("accessibleLabel") == "All currents I(*)"][0]
            app.client().click_element(currents)
            # step 6: accept the dialog, which starts the pending simulation
            ok = [handle for handle in app.client().find_by_type_in(root, "Button") if app.client().get_element_properties(handle).get("accessibleLabel") == "OK"][0]
            app.client().click_element(ok)
            # assert: the dialog closed
            fields.nth(0).wait_for_gone(timeout=10.0)
            # assert: the simulation ran to success and the charts opened
            expect(status).to_have_property("accessibleLabel", "Simulation finished successfully", timeout=30.0)
            app.get_by_id("MainWindow::charts").wait_for_exists(timeout=10.0)
            # step 7: switch back to the netlist view to inspect the editor
            app.get_by_type("ToolbarButton").nth(2).click()
            editor.wait_for_exists(timeout=10.0)
            # assert: the netlist still carries the typed circuit and the configured analysis
            app.wait_for_condition(lambda: "V1 IN 0 PULSE" in editor.text() and ".TRAN" in editor.text(), timeout=10.0, message="expected the typed circuit to survive with the configured .TRAN directive")

    def test_save_stays_enabled_after_running_a_typed_netlist(self) -> None:
        # arrange: resolve the xyce executable so the simulation can run
        xyce = shutil.which("Xyce")
        # arrange: skip the scenario when the xyce executable is not available
        if xyce is None:
            self.skipTest("Xyce executable not found")
        # arrange: launch the application without any file, as a plain start does
        with TestSession(launch(args=["--xyce", xyce]), self.id()) as app:
            # arrange: locate the blank editor, the status bar and the toolbar
            editor = app.get_by_id("NetlistEditor::input")
            status = app.get_by_id("MainWindow::statusbar").child("Text")
            tools = app.get_by_type("ToolbarButton")
            expect(editor).to_exist(timeout=5.0)
            # step 1: type a netlist carrying its simulation directives
            netlist = "* from scratch RLC\nV1 IN 0 PULSE(0 5 0 1n 1n 10m 20m)\nR1 IN N1 100\nC1 N1 0 1u\n.TRAN 1u 20m 0\n.PRINT TRAN V(*) I(*)\n.END\n"
            editor.click()
            editor.fill(netlist)
            expect(editor).to_have_text(netlist, timeout=5.0)
            # assert: the typed netlist enables the save tool
            expect(tools.nth(1).child("Image")).to_have_opacity(1.0, timeout=5.0)
            # step 2: run the simulation
            tools.nth(5).click()
            expect(status).to_have_property("accessibleLabel", "Simulation finished successfully", timeout=30.0)
            # assert: the netlist was never saved, so the save tool stays enabled
            expect(tools.nth(1).child("Image")).to_have_opacity(1.0, timeout=5.0)

    def test_save_persists_a_typed_netlist_to_a_chosen_file(self) -> None:
        # arrange: launch the application without any file, as a plain start does
        with TestSession(launch(), self.id()) as app:
            # arrange: locate the blank netlist editor and the toolbar tools
            editor = app.get_by_id("NetlistEditor::input")
            tools = app.get_by_type("ToolbarButton")
            expect(editor).to_exist(timeout=5.0)
            # step 1: type a netlist into the blank editor
            netlist = "* from scratch RLC\nV1 IN 0 DC 0 AC 1\nR1 IN 0 1k\n.END\n"
            editor.click()
            editor.fill(netlist)
            expect(editor).to_have_text(netlist, timeout=5.0)
            # assert: the typed netlist enables the save tool
            expect(tools.nth(1).child("Image")).to_have_opacity(1.0, timeout=5.0)
            # step 2: prime the save-as path through the debug trigger; the native
            # save dialog is rendered by the platform and unreachable for the automation
            with tempfile.TemporaryDirectory() as scratch:
                target = Path(scratch) / "typed-from-scratch.cir"
                app.get_by_id("MainWindow::test-save-as").fill(str(target))
                # step 3: press the save tool
                tools.nth(1).click()
                # assert: the netlist was persisted at the chosen location
                app.wait_for_condition(lambda: target.exists() and target.read_text() == netlist, timeout=5.0, message="expected the typed netlist to be written to the chosen file")
                # assert: the save tool dims now that there is nothing left to save
                expect(tools.nth(1).child("Image")).to_have_opacity(0.4, timeout=5.0)
                # step 4: edit again and save; the netlist now has a backing
                # file, so the save writes in place without asking again
                editor.click()
                editor.press("x")
                expect(tools.nth(1).child("Image")).to_have_opacity(1.0, timeout=5.0)
                tools.nth(1).click()
                # assert: the edited content replaced the file content
                app.wait_for_condition(lambda: target.read_text() == editor.text(), timeout=5.0, message="expected the second save to write the edited netlist in place")

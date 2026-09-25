import shutil
import unittest

from slint_automation import TestSession, expect, launch


class TypedNetlistSaveStateChecks(unittest.TestCase):

    def test_save_tool_stays_enabled_after_running_the_typed_netlist(self) -> None:
        # arrange: resolve the xyce executable so the run action gets past its config check
        xyce = shutil.which("Xyce")
        # arrange: skip the scenario when the xyce executable is not available
        if xyce is None:
            self.skipTest("Xyce executable not found")
        # arrange: launch the application without any file, as a plain start does
        with TestSession(launch(args=["--xyce", xyce]), self.id()) as app:
            # arrange: locate the blank netlist editor, the toolbar tools and the status bar
            editor = app.get_by_id("NetlistEditor::input")
            tools = app.get_by_type("ToolbarButton")
            status = app.get_by_id("MainWindow::statusbar").child("Text")
            expect(editor).to_exist(timeout=5.0)
            # step 1: type the AC netlist into the blank editor
            netlist = (
                "* Simple RLC Series Circuit - AC Analysis Test\n"
                "V1 IN 0 AC 1\n"
                "R1 IN N1 100\n"
                "L1 N1 N2 10mH\n"
                "C1 N2 0 1uF\n"
                "\n"
                ".PREPROCESS REPLACEGROUND TRUE\n"
                ".AC LIN 20 1 100k\n"
                ".PRINT AC FORMAT=RAW V(*) I(*)\n"
                "\n"
                ".END\n"
            )
            editor.click()
            editor.fill(netlist)
            # assert: the editor shows the typed netlist
            expect(editor).to_have_text(netlist, timeout=5.0)
            # assert: the typed netlist enables the save tool
            expect(tools.nth(1).child("Image")).to_have_opacity(1.0, timeout=5.0)
            # step 2: run the simulation
            tools.nth(5).click()
            # assert: the simulation ran to success
            expect(status).to_have_property("accessibleLabel", "Simulation finished successfully", timeout=30.0)
            # assert: the netlist was never saved, so the save tool stays enabled
            expect(tools.nth(1).child("Image")).to_have_opacity(1.0, timeout=5.0)


class TypedNetlistEditChecks(unittest.TestCase):

    def test_edited_source_line_survives_the_second_simulation_run(self) -> None:
        # arrange: resolve the xyce executable so the run action gets past its config check
        xyce = shutil.which("Xyce")
        # arrange: skip the scenario when the xyce executable is not available
        if xyce is None:
            self.skipTest("Xyce executable not found")
        # arrange: launch the application without any file, as a plain start does
        with TestSession(launch(args=["--xyce", xyce]), self.id()) as app:
            # arrange: locate the blank netlist editor, the toolbar tools and the status bar
            editor = app.get_by_id("NetlistEditor::input")
            tools = app.get_by_type("ToolbarButton")
            status = app.get_by_id("MainWindow::statusbar").child("Text")
            expect(editor).to_exist(timeout=5.0)
            # step 1: type the AC netlist into the blank editor
            netlist = (
                "* Simple RLC Series Circuit - AC Analysis Test\n"
                "V1 IN 0 AC 1\n"
                "R1 IN N1 100\n"
                "L1 N1 N2 10mH\n"
                "C1 N2 0 1uF\n"
                "\n"
                ".PREPROCESS REPLACEGROUND TRUE\n"
                ".AC LIN 20 1 100k\n"
                ".PRINT AC FORMAT=RAW V(*) I(*)\n"
                "\n"
                ".END\n"
            )
            editor.click()
            editor.fill(netlist)
            # assert: the typed netlist enables the save tool
            expect(tools.nth(1).child("Image")).to_have_opacity(1.0, timeout=5.0)
            # step 2: run the simulation
            tools.nth(5).click()
            # assert: the simulation ran to success
            expect(status).to_have_property("accessibleLabel", "Simulation finished successfully", timeout=30.0)
            # step 3: show the netlist view again to edit the source
            tools.nth(2).click()
            editor.wait_for_exists(timeout=10.0)
            # step 4: replace the source line with the doubled AC amplitude
            edited = netlist.replace("V1 IN 0 AC 1", "V1 IN 0 AC 2")
            editor.click()
            editor.fill(edited)
            # assert: the editor carries the edited source line
            expect(editor).to_have_text(edited, timeout=5.0)
            # step 5: run the simulation a second time
            tools.nth(5).click()
            # assert: the second simulation ran to success
            expect(status).to_have_property("accessibleLabel", "Simulation finished successfully", timeout=30.0)
            # step 6: show the netlist view again to inspect the source
            tools.nth(2).click()
            editor.wait_for_exists(timeout=10.0)
            # assert: the edited source line survived the second run
            expect(editor).to_have_text(edited, timeout=10.0)

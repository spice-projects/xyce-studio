import shutil
import unittest
from pathlib import Path

from slint_automation import expect, TestSession, launch


class ToolbarInitialChecks(unittest.TestCase):

    def test_toolbar_contains_nine_tools_in_expected_state(self) -> None:
        # arrange: launch the application in a session scoped to this test
        with TestSession(launch(), self.id()) as app:
            # arrange: locate the toolbar tools by their slint type in declaration order
            tools = app.get_by_type("ToolbarButton")
            # assert: exactly nine action tools are present
            self.assertEqual(tools.count(), 9)
            # toolbar actions from left to right
            expected_states = [True, False, False, False, False, False, False, True, True]
            # loop expected toolbar tools states
            for index, expected_enabled in enumerate(expected_states):
                # the disabled state is rendered by the dimmed icon inside the tool
                icon = tools.nth(index).child("Image")
                # enabled tools render their icon at full opacity
                if expected_enabled:
                    expect(icon).to_have_opacity(1.0)
                # disabled tools render their icon dimmed
                else:
                    expect(icon).to_have_opacity(0.4)


class ToolbarSimulationChecks(unittest.TestCase):

    def test_run_simulation_updates_toolbar_and_panels(self) -> None:
        # arrange: resolve the xyce executable and the sample netlist from the repository
        xyce = shutil.which("Xyce")
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "tran-simple-01.cir"
        # arrange: skip the scenario when the xyce executable is not available
        if xyce is None:
            self.skipTest("Xyce executable not found")
        # arrange: launch the application with the netlist and the xyce executable
        with TestSession(launch(args=["--netlist", str(netlist), "--xyce", xyce]), self.id()) as app:
            # arrange: locate the toolbar tools by their slint type in declaration order
            tools = app.get_by_type("ToolbarButton")
            # assert: exactly nine action tools are present
            self.assertEqual(tools.count(), 9)
            # step 1: run the simulation from the toolbar run tool
            tools.nth(5).click()
            # step 2: wait for the simulation to finish and the charts view to open
            app.get_by_id("MainWindow::charts").wait_for_exists(timeout=10.0)
            # assert
            expected_states = [True, False, True, False, True, True, True, True, True]
            for index, expected_enabled in enumerate(expected_states):
                icon = tools.nth(index).child("Image")
                if expected_enabled:
                    expect(icon).to_have_opacity(1.0)
                else:
                    expect(icon).to_have_opacity(0.4)
            # assert: the charts panel is visible
            expect(app.get_by_id("MainWindow::charts")).to_exist()
            # assert: the simulation output panel is hidden after a successful run
            expect(app.get_by_id("MainWindow::output")).not_.to_exist()


class ToolbarNetlistFileChecks(unittest.TestCase):

    def test_toolbar_state_after_loading_netlist_file(self) -> None:
        # arrange: resolve the sample netlist shipped with the repository
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "tran-simple-01.cir"
        # arrange: launch the application with the netlist through the command line
        with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
            # arrange: locate the toolbar tools by their slint type in declaration order
            tools = app.get_by_type("ToolbarButton")
            # assert: exactly nine action tools are present
            self.assertEqual(tools.count(), 9)
            # toolbar actions from left to right with a netlist loaded but no results
            expected_states = [True, False, False, False, False, True, True, True, True]
            # loop expected toolbar tools states
            for index, expected_enabled in enumerate(expected_states):
                # the disabled state is rendered by the dimmed icon inside the tool
                icon = tools.nth(index).child("Image")
                # enabled tools render their icon at full opacity
                if expected_enabled:
                    expect(icon).to_have_opacity(1.0)
                # disabled tools render their icon dimmed
                else:
                    expect(icon).to_have_opacity(0.4)

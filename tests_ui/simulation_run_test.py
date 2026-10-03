import shutil
import unittest
from pathlib import Path

from slint_automation import TestSession, expect, launch


class SimulationOutputChecks(unittest.TestCase):

    def test_output_panel_shows_and_retains_simulation_log(self) -> None:
        # arrange: resolve the xyce executable and the sample netlist from the repository
        xyce = shutil.which("Xyce")
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "tran-simple-01.cir"
        # arrange: skip the scenario when the xyce executable is not available
        if xyce is None:
            self.skipTest("Xyce executable not found")
        # arrange: launch the application with the netlist and the xyce executable
        with TestSession(launch(args=["--netlist", str(netlist), "--xyce", xyce]), self.id()) as app:
            # arrange: locate the status bar text
            status = app.get_by_id("MainWindow::status-message")
            # step 1: run the simulation and wait for the final status message
            app.get_by_type("ToolbarButton").nth(5).click()
            expect(status).to_have_property("accessibleLabel", "Simulation finished successfully", timeout=15.0)
            # step 2: wait for the charts view to open
            app.get_by_id("MainWindow::charts").wait_for_exists(timeout=10.0)
            # step 3: show the simulation output panel
            app.get_by_type("ToolbarButton").nth(4).click()
            output = app.get_by_id("MainWindow::output")
            output.wait_for_exists()
            # assert: the log holds the xyce welcome banner
            self.assertTrue(any("Welcome to the Xyce" in (app.client().get_element_properties(handle).get("accessibleLabel") or "") for handle in app.client().find_by_type_in(output._matched_handle(), "Text")))
            # step 4: close the output panel through its close button
            app.get_by_id("SimulationOutputPanel::close-button").click()
            output.wait_for_gone()
            # step 5: re-show the output panel from the toolbar
            app.get_by_type("ToolbarButton").nth(4).click()
            output.wait_for_exists()
            # assert: the log content is retained after the close
            self.assertTrue(any("Welcome to the Xyce" in (app.client().get_element_properties(handle).get("accessibleLabel") or "") for handle in app.client().find_by_type_in(output._matched_handle(), "Text")))


class InvalidXyceChecks(unittest.TestCase):

    def test_run_reports_invalid_configured_executable(self) -> None:
        # arrange: resolve the sample netlist shipped with the repository
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "tran-simple-01.cir"
        # arrange: launch the application with a nonexistent configured executable
        with TestSession(launch(args=["--netlist", str(netlist), "--xyce", "/nonexistent/xyce"]), self.id()) as app:
            # arrange: locate the status bar text and the toolbar tools
            status = app.get_by_id("MainWindow::status-message")
            tools = app.get_by_type("ToolbarButton")
            # step 1: run the simulation from the toolbar run tool
            tools.nth(5).click()
            # assert: the status reports the invalid configured executable
            expect(status).to_have_property("accessibleLabel", "Configured Xyce executable path is invalid")
            # assert: the charts view stays unavailable without results
            expect(tools.nth(3).child("Image")).to_have_opacity(0.4)


class SimulationProgressChecks(unittest.TestCase):

    def test_statusbar_reports_live_progress_and_opens_the_output_panel(self) -> None:
        # arrange: resolve the xyce executable and the slow netlist, whose transient keeps the run in flight long enough to observe
        xyce = shutil.which("Xyce")
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "tran-progress-01.cir"
        # arrange: skip the scenario when the xyce executable is not available
        if xyce is None:
            self.skipTest("Xyce executable not found")
        # arrange: launch the application with the netlist and the xyce executable
        with TestSession(launch(args=["--netlist", str(netlist), "--xyce", xyce]), self.id()) as app:
            # step 1: run the simulation
            app.get_by_type("ToolbarButton").nth(5).click()
            # arrange: the indicator only enters the element tree while a run is in flight
            percentage = app.get_by_id("RunProgress::run-progress-percentage")
            percentage.wait_for_exists(timeout=15.0)
            fill = app.get_by_id("RunProgress::run-progress-fill")
            # assert: the indicator reports a completion percentage with the simulator's own estimate
            app.wait_for_condition(lambda: "%" in (percentage.text() or ""), timeout=15.0, message="expected the run indicator to report a completion percentage")
            self.assertIn("ETA", percentage.text())
            # assert: the bar is filled in step with the reported percentage
            app.wait_for_condition(lambda: fill.properties().get("size", {}).get("width", 0.0) > 6.0, timeout=15.0, message="expected the progress bar to fill while the run advances")
            # step 2: open the simulation output panel from the status bar
            app.get_by_id("MainWindow::statusbar").click()
            output = app.get_by_id("MainWindow::output")
            output.wait_for_exists(timeout=10.0)
            # assert: the panel opened while the run was still in flight, so the status bar strip
            # is a shortcut to the live log and not only to a finished one; the run indicator is
            # still in the element tree for exactly that reason
            expect(app.get_by_id("RunProgress::run-progress-percentage")).to_exist(timeout=10.0)
            # assert: the reported warnings are counted while the run is in flight
            expect(app.get_by_id("RunProgress::run-progress-warnings")).to_have_property("accessibleLabel", "2 warnings", timeout=10.0)

    def test_output_panel_counts_the_warnings_and_navigates_to_the_first(self) -> None:
        # arrange: resolve the xyce executable and the netlist that provokes two netlist warnings
        xyce = shutil.which("Xyce")
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "tran-progress-01.cir"
        # arrange: skip the scenario when the xyce executable is not available
        if xyce is None:
            self.skipTest("Xyce executable not found")
        # arrange: launch the application with the netlist and the xyce executable
        with TestSession(launch(args=["--netlist", str(netlist), "--xyce", xyce]), self.id()) as app:
            # arrange: locate the status bar message
            status = app.get_by_id("MainWindow::status-message")
            # step 1: run the simulation and wait for the final status message
            app.get_by_type("ToolbarButton").nth(5).click()
            expect(status).to_have_property("accessibleLabel", "Simulation finished successfully", timeout=30.0)
            # step 2: show the simulation output panel from the toolbar
            app.get_by_type("ToolbarButton").nth(4).click()
            output = app.get_by_id("MainWindow::output")
            output.wait_for_exists(timeout=10.0)
            # assert: the header carries the count of the warnings the netlist provoked
            expect(app.get_by_id("SimulationOutputPanel::warning-badge")).to_be_visible(timeout=10.0)
            expect(app.get_by_id("SimulationOutputPanel::warning-badge-count")).to_have_property("accessibleLabel", "2", timeout=10.0)
            # step 3: click the warning count to navigate to the first reported warning
            app.get_by_id("SimulationOutputPanel::warning-badge-touch").click()
            # assert: the first warning is scrolled into view inside the log
            app.wait_for_condition(lambda: any("Netlist warning" in (app.client().get_element_properties(handle).get("accessibleLabel") or "") for handle in app.client().find_by_type_in(output._matched_handle(), "Text")), timeout=10.0, message="expected the badge click to scroll the log to the first warning")


class ApplicationExitChecks(unittest.TestCase):

    def test_exit_terminates_the_application(self) -> None:
        # arrange: launch the application without any file
        with TestSession(launch(), self.id()) as app:
            # step 1: exit through the toolbar exit tool
            app.get_by_type("ToolbarButton").nth(9).click()
            # assert: the application process terminated and the session
            # cleanup tolerates the already exited process
            app.wait_for_condition(lambda: not app.is_running(), timeout=10.0, message="expected the application process to exit")
            self.assertFalse(app.is_running())

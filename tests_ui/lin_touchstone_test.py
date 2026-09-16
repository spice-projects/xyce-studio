import shutil
import tempfile
import unittest
from pathlib import Path

from slint_automation import TestSession, expect, launch


class LinTouchstoneTabChecks(unittest.TestCase):

    def test_lin_run_opens_touchstone_tab_with_suggested_charts(self) -> None:
        # arrange: resolve the xyce executable and the sample netlist from the repository
        xyce = shutil.which("Xyce")
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "lin-simple-01.cir"
        # arrange: skip the scenario when the xyce executable is not available
        if xyce is None:
            self.skipTest("Xyce executable not found")
        # arrange: copy the netlist to a scratch directory so the run never
        # touches the repository fixtures; the touchstone output is written
        # next to the netlist working directory
        with tempfile.TemporaryDirectory() as scratch:
            netlist_path = Path(scratch) / "lin-simple-01.cir"
            netlist_path.write_text(netlist.read_text())
            # arrange: launch the application with the netlist and the xyce executable
            with TestSession(launch(args=["--netlist", str(netlist_path), "--xyce", xyce]), self.id()) as app:
                # arrange: locate the status bar text
                status = app.get_by_id("MainWindow::statusbar").child("Text")
                # step 1: run the simulation and wait for the final status message
                app.get_by_type("ToolbarButton").nth(5).click()
                expect(status).to_have_property("accessibleLabel", "Simulation finished successfully", timeout=15.0)
                # step 2: wait for the charts view to open
                app.get_by_id("MainWindow::charts").wait_for_exists(timeout=10.0)
                # assert: the raw output and the touchstone output open as two plot tabs
                tabs = app.get_by_type("PlotTabButton")
                self.assertEqual(tabs.count(), 2)
                self.assertEqual(tabs.nth(0).child("Text").text(), "AC Analysis")
                self.assertEqual(tabs.nth(1).child("Text").text(), "LIN Analysis")
                # assert: the raw tab is the default active tab and opens with
                # an empty chart, like any primary result tab
                charts = app.get_by_type("ChartView")
                self.assertEqual(charts.count(), 1)
                self.assertEqual(charts.nth(0).text(), "")
                # step 3: switch to the LIN Analysis tab
                tabs.nth(1).click()
                # assert: the suggested charts plot the dB magnitudes of the
                # reflection (return loss) and transmission (insertion loss)
                # entries; within a chart the series render in name order
                app.wait_for_condition(lambda: app.get_by_type("ChartView").count() == 2, timeout=5.0, message="expected the LIN Analysis charts")
                charts = app.get_by_type("ChartView")
                self.assertEqual(charts.nth(0).text(), "db(S11)")
                self.assertEqual(charts.nth(1).text(), "db(S12)")


class LinTouchstoneStepChecks(unittest.TestCase):

    def test_lin_run_with_step_opens_touchstone_tab_with_stepped_charts(self) -> None:
        # arrange: resolve the xyce executable and the stepped sample netlist from the repository
        xyce = shutil.which("Xyce")
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "lin-simple-with-step-01.cir"
        # arrange: skip the scenario when the xyce executable is not available
        if xyce is None:
            self.skipTest("Xyce executable not found")
        # arrange: copy the netlist to a scratch directory so the run never
        # touches the repository fixtures
        with tempfile.TemporaryDirectory() as scratch:
            netlist_path = Path(scratch) / "lin-simple-with-step-01.cir"
            netlist_path.write_text(netlist.read_text())
            # arrange: launch the application with the netlist and the xyce executable
            with TestSession(launch(args=["--netlist", str(netlist_path), "--xyce", xyce]), self.id()) as app:
                # arrange: locate the status bar text
                status = app.get_by_id("MainWindow::statusbar").child("Text")
                # step 1: run the simulation and wait for the final status message
                app.get_by_type("ToolbarButton").nth(5).click()
                expect(status).to_have_property("accessibleLabel", "Simulation finished successfully", timeout=15.0)
                # step 2: wait for the charts view to open
                app.get_by_id("MainWindow::charts").wait_for_exists(timeout=10.0)
                # assert: the raw output and the touchstone output open as two plot tabs
                tabs = app.get_by_type("PlotTabButton")
                self.assertEqual(tabs.count(), 2)
                self.assertEqual(tabs.nth(1).child("Text").text(), "LIN Analysis")
                # assert: the raw tab is the default active tab and opens with
                # an empty chart, like any primary result tab
                charts = app.get_by_type("ChartView")
                self.assertEqual(charts.count(), 1)
                self.assertEqual(charts.nth(0).text(), "")
                # step 3: switch to the LIN Analysis tab
                tabs.nth(1).click()
                # assert: the suggested charts carry the reflection and
                # transmission dB series
                app.wait_for_condition(lambda: app.get_by_type("ChartView").count() == 2, timeout=5.0, message="expected the LIN Analysis charts")
                charts = app.get_by_type("ChartView")
                self.assertEqual(charts.nth(0).text(), "db(S11)")
                self.assertEqual(charts.nth(1).text(), "db(S12)")

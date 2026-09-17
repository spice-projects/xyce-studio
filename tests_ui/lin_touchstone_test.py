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
                # assert: the raw output, the touchstone output and the smith
                # chart open as three plot tabs
                tabs = app.get_by_type("PlotTabButton")
                self.assertEqual(tabs.count(), 3)
                self.assertEqual(tabs.nth(0).child("Text").text(), "AC Analysis")
                self.assertEqual(tabs.nth(1).child("Text").text(), "LIN Analysis")
                self.assertEqual(tabs.nth(2).child("Text").text(), "Smith Chart")
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
                # step 4: switch to the Smith Chart tab
                tabs.nth(2).click()
                # assert: the smith chart plots the diagonal entries by default
                app.wait_for_condition(lambda: app.get_by_type("ChartView").count() == 1, timeout=5.0, message="expected the Smith Chart")
                charts = app.get_by_type("ChartView")
                self.assertEqual(charts.nth(0).text(), "S11")


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
                # assert: the raw output, the touchstone output and the smith
                # chart open as three plot tabs
                tabs = app.get_by_type("PlotTabButton")
                self.assertEqual(tabs.count(), 3)
                self.assertEqual(tabs.nth(1).child("Text").text(), "LIN Analysis")
                self.assertEqual(tabs.nth(2).child("Text").text(), "Smith Chart")
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
                # step 4: switch to the Smith Chart tab
                tabs.nth(2).click()
                # assert: the smith chart carries the diagonal entry series
                app.wait_for_condition(lambda: app.get_by_type("ChartView").count() == 1, timeout=5.0, message="expected the Smith Chart")
                charts = app.get_by_type("ChartView")
                self.assertEqual(charts.nth(0).text(), "S11")
                # assert: the cartesian-only context menu tools disappear on the
                # smith tab (the zoom triggers, chart management and new window)
                for action in ("zoom-to-fit", "autorange", "zoom-abscissa-extent", "add-chart", "delete-chart", "new-window"):
                    self.assertFalse(app.get_by_id(f"ChartsPanel::test-{action}").exists(), f"expected the {action} trigger to be hidden on the smith tab")
                # assert: the add/remove plots trigger stays available
                self.assertTrue(app.get_by_id("ChartsPanel::test-add-remove-plots").exists())
                # step 5: switch back to the LIN Analysis tab
                tabs.nth(1).click()
                app.wait_for_condition(lambda: app.get_by_type("ChartView").count() == 2, timeout=5.0, message="expected the LIN Analysis charts")
                # assert: the cartesian tools come back on the xy tab
                self.assertTrue(app.get_by_id("ChartsPanel::test-zoom-to-fit").exists())
                self.assertTrue(app.get_by_id("ChartsPanel::test-add-chart").exists())


class LinSmithStepToolChecks(unittest.TestCase):

    def test_step_tool_opens_from_the_smith_chart_context_menu(self) -> None:
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
                # step 3: switch to the Smith Chart tab
                tabs = app.get_by_type("PlotTabButton")
                self.assertEqual(tabs.count(), 3)
                tabs.nth(2).click()
                # step 4: open the step tool from the smith chart context menu trigger
                app.invoke_chart_action("step-tool", chart_position=0.75)
                # assert: the step tool dialog opens listing the three step rows
                root = app.client().get_window_properties()["rootElementHandle"]
                step_boxes = [handle for handle in app.client().find_by_type_in(root, "CheckBox") if (app.client().get_element_properties(handle).get("accessibleLabel") or "").startswith("Select step")]
                self.assertEqual(len(step_boxes), 3)
                # step 5: dismiss the dialog with the escape key
                app.get_by_type("CheckBox").nth(0).press("\x1b")
                app.wait_for_condition(lambda: not [handle for handle in app.client().find_by_type_in(root, "CheckBox") if (app.client().get_element_properties(handle).get("accessibleLabel") or "").startswith("Select step")], timeout=5.0, message="expected the step tool dialog to close")

import shutil
import unittest
from pathlib import Path

from slint_automation import TestSession, launch


class ExpressionLegendChecks(unittest.TestCase):

    def test_add_plot_legend_lists_dataset_units_and_grows_with_custom_expressions(self) -> None:
        # arrange: resolve the xyce executable and the sample netlist
        xyce = shutil.which("Xyce")
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "tran-simple-01.cir"
        if xyce is None:
            self.skipTest("Xyce executable not found")
        # arrange: launch the application, run the simulation and wait for the
        # initial transient chart
        with TestSession(launch(args=["--netlist", str(netlist), "--xyce", xyce]), self.id()) as app:
            app.get_by_type("ToolbarButton").nth(5).click()
            app.get_by_id("MainWindow::charts").wait_for_exists(timeout=15.0)
            app.wait_for_condition(lambda: app.get_by_type("ChartView").count() == 1, timeout=10.0, message="expected the initial transient chart")
            # step: open the add plot expression selector through the debug
            # trigger; the native context menu is not reachable
            app.invoke_chart_action("add-remove-plots", chart_position=0.5)
            legend = app.get_by_type("Legend")
            legend.nth(0).wait_for_exists(timeout=10.0)
            # assert: the legend lists the dataset units in first-seen order;
            # the flat netlist contributes no scope rows and every expression
            # has a unit, so no misc row appears
            app.wait_for_condition(lambda: [legend.nth(0).child("Text").nth(index).properties().get("accessibleLabel") for index in range(legend.nth(0).child("Text").count())] == ["s", "V", "A", "W"], timeout=10.0, message="expected the legend to list the dataset units")
            # step: type a decibel custom expression and add it
            fields = app.get_by_type("LineEdit")
            self.assertEqual(fields.count(), 2)
            fields.nth(1).fill("db(V(N1))")
            root = app.client().get_window_properties()["rootElementHandle"]
            add = [handle for handle in app.client().find_by_type_in(root, "Button") if app.client().get_element_properties(handle).get("accessibleLabel") == "Add"][0]
            app.client().click_element(add)
            # assert: the legend grows with the decibel unit of the new
            # expression, appended after the dataset units
            app.wait_for_condition(lambda: [legend.nth(0).child("Text").nth(index).properties().get("accessibleLabel") for index in range(legend.nth(0).child("Text").count())] == ["s", "V", "A", "W", "dBV"], timeout=10.0, message="expected the legend to gain the dBV entry")
            # step: close the panel with the cancel button
            cancel = [handle for handle in app.client().find_by_type_in(root, "Button") if app.client().get_element_properties(handle).get("accessibleLabel") == "Cancel"][0]
            app.client().click_element(cancel)
            # assert: the panel closed and the legend is gone
            legend.nth(0).wait_for_gone()

    def test_fft_legend_excludes_the_time_unit(self) -> None:
        # arrange: resolve the xyce executable and the sample netlist
        xyce = shutil.which("Xyce")
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "tran-simple-01.cir"
        if xyce is None:
            self.skipTest("Xyce executable not found")
        # arrange: launch the application, run the simulation and wait for the
        # initial transient chart
        with TestSession(launch(args=["--netlist", str(netlist), "--xyce", xyce]), self.id()) as app:
            app.get_by_type("ToolbarButton").nth(5).click()
            app.get_by_id("MainWindow::charts").wait_for_exists(timeout=15.0)
            app.wait_for_condition(lambda: app.get_by_type("ChartView").count() == 1, timeout=10.0, message="expected the initial transient chart")
            # step: open the fft setup panel through the debug trigger
            app.invoke_chart_action("calculate-fft", chart_position=0.5)
            legend = app.get_by_type("Legend")
            legend.nth(0).wait_for_exists(timeout=10.0)
            # assert: the fft dialog filters out time-domain expressions, so
            # the legend shows the remaining units without the time unit
            app.wait_for_condition(lambda: [legend.nth(0).child("Text").nth(index).properties().get("accessibleLabel") for index in range(legend.nth(0).child("Text").count())] == ["V", "A", "W"], timeout=10.0, message="expected the fft legend to exclude the time unit")
            # step: close the panel with the cancel button
            root = app.client().get_window_properties()["rootElementHandle"]
            cancel = [handle for handle in app.client().find_by_type_in(root, "Button") if app.client().get_element_properties(handle).get("accessibleLabel") == "Cancel"][0]
            app.client().click_element(cancel)
            # assert: the panel closed and the legend is gone
            legend.nth(0).wait_for_gone()

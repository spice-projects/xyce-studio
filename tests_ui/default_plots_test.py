import shutil
import tempfile
import unittest
from pathlib import Path

from slint_automation import TestSession, expect, launch


class PlotDirectiveChartChecks(unittest.TestCase):

    def test_transient_plot_directives_open_one_chart_per_line(self) -> None:
        # arrange: resolve the xyce executable and the sample netlist from the repository
        xyce = shutil.which("Xyce")
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "tran-simple-01.cir"
        # arrange: skip the scenario when the xyce executable is not available
        if xyce is None:
            self.skipTest("Xyce executable not found")
        # arrange: copy the netlist to a scratch directory so the run never touches the repository fixture
        with tempfile.TemporaryDirectory() as scratch:
            netlist_path = Path(scratch) / "tran-simple-01.cir"
            netlist_path.write_text(netlist.read_text())
            # arrange: launch the application with the netlist and the xyce executable
            with TestSession(launch(args=["--netlist", str(netlist_path), "--xyce", xyce]), self.id()) as app:
                # arrange: locate the status bar text
                status = app.get_by_id("MainWindow::status-message")
                # step 1: run the simulation and wait for the final status message
                app.get_by_type("ToolbarButton").nth(5).click()
                expect(status).to_have_property("accessibleLabel", "Simulation finished successfully", timeout=30.0)
                # step 2: wait for the charts view to open
                app.get_by_id("MainWindow::charts").wait_for_exists(timeout=15.0)
                # assert: the two .PLOT lines opened two charts, each carrying the series it declares
                app.wait_for_condition(lambda: app.get_by_type("ChartView").count() == 2, timeout=15.0, message="expected one chart per .PLOT line")
                charts = app.get_by_type("ChartView")
                self.assertEqual(charts.nth(0).text(), "I(L1)")
                self.assertEqual(charts.nth(1).text(), "V(IN)")
                # act: dump the axis labels of the first chart; the abscissa labels sit in the band below the plot (relative y in the last 60 px of the view, together with the legend entry naming the series) and the ordinate labels sit above it
                view = charts.nth(0)
                view_props = view.properties()
                origin_y = view_props.get("absolutePosition", {}).get("y", 0)
                height = view_props.get("size", {}).get("height", 0)
                texts = view.child("Text")
                abscissa_labels: list[str] = []
                ordinate_labels: list[str] = []
                for index in range(texts.count()):
                    props = texts.nth(index).properties()
                    label = props.get("accessibleLabel")
                    relative_y = props.get("absolutePosition", {}).get("y", 0) - origin_y
                    if relative_y > height - 60:
                        abscissa_labels.append(label)
                    else:
                        ordinate_labels.append(label)
                # assert: the declared current is drawn over the full 0..20 ms simulation span
                ticks = [label for label in abscissa_labels if label.endswith(("Hz", "s")) or label.endswith(" ms")]
                self.assertEqual(ticks, ["0s", "2 ms", "4 ms", "6 ms", "8 ms", "10 ms", "12 ms", "14 ms", "16 ms", "18 ms", "20 ms"])
                # assert: the legend of the chart names the declared series and the ordinate carries the current scale
                self.assertIn("I(L1)", abscissa_labels)
                self.assertTrue(any(label.endswith("A") for label in ordinate_labels))
                # assert: the generated fft tabs are untouched by the chart declarations
                tabs = app.get_by_type("PlotTabButton")
                self.assertEqual(tabs.count(), 3)
                self.assertEqual(tabs.nth(0).child("Text").text(), "Transient")
                # step 3: open the simulation output panel
                app.get_by_type("ToolbarButton").nth(4).click()
                output = app.get_by_id("MainWindow::output")
                output.wait_for_exists(timeout=10.0)
                # assert: the simulator never saw the chart declarations, so its log carries no unrecognized dot line warning
                log_lines = [app.client().get_element_properties(handle).get("accessibleLabel") or "" for handle in app.client().find_by_type_in(output._matched_handle(), "Text")]
                self.assertFalse(any("Unrecognized dot line" in line for line in log_lines), f"expected no dot line warning in the simulator log, got: {[line for line in log_lines if 'dot line' in line]}")

    def test_ac_plot_directive_plots_the_computed_series(self) -> None:
        # arrange: resolve the xyce executable and the sample netlist from the repository
        xyce = shutil.which("Xyce")
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "ac-simple-02.cir"
        # arrange: skip the scenario when the xyce executable is not available
        if xyce is None:
            self.skipTest("Xyce executable not found")
        # arrange: copy the netlist to a scratch directory so the run never touches the repository fixture
        with tempfile.TemporaryDirectory() as scratch:
            netlist_path = Path(scratch) / "ac-simple-02.cir"
            netlist_path.write_text(netlist.read_text())
            # arrange: launch the application with the netlist and the xyce executable
            with TestSession(launch(args=["--netlist", str(netlist_path), "--xyce", xyce]), self.id()) as app:
                # arrange: locate the status bar text
                status = app.get_by_id("MainWindow::status-message")
                # step 1: run the simulation and wait for the final status message
                app.get_by_type("ToolbarButton").nth(5).click()
                expect(status).to_have_property("accessibleLabel", "Simulation finished successfully", timeout=30.0)
                # step 2: wait for the charts view to open
                app.get_by_id("MainWindow::charts").wait_for_exists(timeout=15.0)
                # assert: the single .PLOT line opened a single chart carrying the computed decibel series
                app.wait_for_condition(lambda: app.get_by_type("ChartView").count() == 1, timeout=15.0, message="expected the declared chart")
                view = app.get_by_type("ChartView").nth(0)
                self.assertEqual(view.text(), "db(I(L1))")
                # act: dump the axis labels of the chart
                view_props = view.properties()
                origin_y = view_props.get("absolutePosition", {}).get("y", 0)
                height = view_props.get("size", {}).get("height", 0)
                texts = view.child("Text")
                abscissa_labels: list[str] = []
                for index in range(texts.count()):
                    props = texts.nth(index).properties()
                    relative_y = props.get("absolutePosition", {}).get("y", 0) - origin_y
                    if relative_y > height - 60:
                        abscissa_labels.append(props.get("accessibleLabel"))
                # assert: the abscissa is the swept frequency and the legend names the computed series
                ticks = [label for label in abscissa_labels if label.endswith(("Hz", "s")) or label.endswith(" ms")]
                self.assertEqual(ticks, ["1 Hz", "10 Hz", "100 Hz", "1 kHz", "10 kHz"])
                self.assertIn("db(I(L1))", abscissa_labels)
                # assert: a single dataset opens without a plot tab bar
                self.assertEqual(app.get_by_type("PlotTabButton").count(), 0)

    def test_several_expressions_on_one_line_fill_a_single_chart(self) -> None:
        # arrange: resolve the xyce executable so the run action gets past its config check
        xyce = shutil.which("Xyce")
        # arrange: skip the scenario when the xyce executable is not available
        if xyce is None:
            self.skipTest("Xyce executable not found")
        # arrange: write a netlist whose first .PLOT line declares three series and whose second one declares a single series
        with tempfile.TemporaryDirectory() as scratch:
            netlist_path = Path(scratch) / "grouped-plots.cir"
            netlist_path.write_text("* grouped plot directives\nV1 IN 0 PULSE(0 5 0 1n 1n 10m 20m)\nR1 IN N1 100\nL1 N1 N2 10mH\nC1 N2 0 1uF\n.TRAN 1u 20m 0\n.PRINT TRAN FORMAT=RAW V(*) I(*)\n.PLOT V(IN) V(N1) abs(I(L1))\n.PLOT V(N2,N1)\n.END\n")
            # arrange: launch the application with the netlist and the xyce executable
            with TestSession(launch(args=["--netlist", str(netlist_path), "--xyce", xyce]), self.id()) as app:
                # arrange: locate the status bar text
                status = app.get_by_id("MainWindow::status-message")
                # step 1: run the simulation and wait for the final status message
                app.get_by_type("ToolbarButton").nth(5).click()
                expect(status).to_have_property("accessibleLabel", "Simulation finished successfully", timeout=30.0)
                # step 2: wait for the charts view to open
                app.get_by_id("MainWindow::charts").wait_for_exists(timeout=15.0)
                # assert: the two .PLOT lines opened exactly two charts
                app.wait_for_condition(lambda: app.get_by_type("ChartView").count() == 2, timeout=15.0, message="expected one chart per .PLOT line")
                charts = app.get_by_type("ChartView")
                # act: dump the legend entries of the first chart from the band below its plot
                first_view = charts.nth(0)
                first_props = first_view.properties()
                first_origin_y = first_props.get("absolutePosition", {}).get("y", 0)
                first_height = first_props.get("size", {}).get("height", 0)
                first_texts = first_view.child("Text")
                first_legend: list[str] = []
                for index in range(first_texts.count()):
                    props = first_texts.nth(index).properties()
                    relative_y = props.get("absolutePosition", {}).get("y", 0) - first_origin_y
                    if relative_y > first_height - 60:
                        first_legend.append(props.get("accessibleLabel"))
                # act: dump the legend entries of the second chart
                second_view = charts.nth(1)
                second_props = second_view.properties()
                second_origin_y = second_props.get("absolutePosition", {}).get("y", 0)
                second_height = second_props.get("size", {}).get("height", 0)
                second_texts = second_view.child("Text")
                second_legend: list[str] = []
                for index in range(second_texts.count()):
                    props = second_texts.nth(index).properties()
                    relative_y = props.get("absolutePosition", {}).get("y", 0) - second_origin_y
                    if relative_y > second_height - 60:
                        second_legend.append(props.get("accessibleLabel"))
                # assert: the first chart carries the three expressions of its line, the second one the voltage difference of its own line
                self.assertEqual(charts.nth(0).text(), "V(IN)")
                self.assertEqual(charts.nth(1).text(), "V(N2,N1)")
                self.assertTrue({"V(IN)", "V(N1)", "abs(I(L1))"}.issubset(set(first_legend)), f"expected the first chart to legend its three series, got {first_legend}")
                self.assertIn("V(N2,N1)", second_legend)

    def test_compound_voltage_difference_plots_from_the_node_voltages(self) -> None:
        # arrange: resolve the xyce executable so the run action gets past its config check
        xyce = shutil.which("Xyce")
        # arrange: skip the scenario when the xyce executable is not available
        if xyce is None:
            self.skipTest("Xyce executable not found")
        # arrange: write a netlist whose chart reads a voltage difference the print does not carry
        with tempfile.TemporaryDirectory() as scratch:
            netlist_path = Path(scratch) / "differential-plot.cir"
            netlist_path.write_text("* compound differential\nV1 IN 0 PULSE(0 5 0 1n 1n 10m 20m)\nR1 IN N1 100\nL1 N1 N2 10mH\nC1 N2 0 1uF\n.TRAN 1u 20m 0\n.PRINT TRAN FORMAT=RAW V(IN)\n.PLOT abs(V(N2,N1))\n.END\n")
            # arrange: launch the application with the netlist and the xyce executable
            with TestSession(launch(args=["--netlist", str(netlist_path), "--xyce", xyce]), self.id()) as app:
                # arrange: locate the status bar text
                status = app.get_by_id("MainWindow::status-message")
                # step 1: run the simulation and wait for the final status message
                app.get_by_type("ToolbarButton").nth(5).click()
                expect(status).to_have_property("accessibleLabel", "Simulation finished successfully", timeout=30.0)
                # step 2: wait for the charts view to open
                app.get_by_id("MainWindow::charts").wait_for_exists(timeout=15.0)
                # assert: the compound differential resolves, so the chart is plotted instead of skipped
                app.wait_for_condition(lambda: app.get_by_type("ChartView").count() == 1, timeout=15.0, message="expected the declared chart")
                self.assertEqual(app.get_by_type("ChartView").nth(0).text(), "abs(V(N2,N1))")
                # step 3: switch to the netlist view to inspect the rewritten netlist
                app.get_by_type("ToolbarButton").nth(2).click()
                editor = app.get_by_id("NetlistEditor::input")
                app.wait_for_condition(lambda: editor.exists(), timeout=10.0, message="expected the netlist view")
                netlist_text = editor.text() or ""
                # assert: the two node voltages joined the print, because the viewer computes the difference from them
                self.assertIn(".PRINT TRAN FORMAT=RAW V(IN) V(N2) V(N1)", netlist_text)

    def test_plotted_quantity_missing_from_the_print_is_added_to_it(self) -> None:
        # arrange: resolve the xyce executable so the run action gets past its config check
        xyce = shutil.which("Xyce")
        # arrange: skip the scenario when the xyce executable is not available
        if xyce is None:
            self.skipTest("Xyce executable not found")
        # arrange: write a netlist whose chart reads a current the print statement does not carry
        with tempfile.TemporaryDirectory() as scratch:
            netlist_path = Path(scratch) / "augmented-print.cir"
            netlist_path.write_text("* augmented print\nV1 IN 0 PULSE(0 5 0 1n 1n 10m 20m)\nR1 IN N1 100\nL1 N1 N2 10mH\nC1 N2 0 1uF\n.TRAN 1u 20m 0\n.PRINT TRAN FORMAT=RAW V(N1)\n.PLOT abs(I(L1))\n.END\n")
            # arrange: launch the application with the netlist and the xyce executable
            with TestSession(launch(args=["--netlist", str(netlist_path), "--xyce", xyce]), self.id()) as app:
                # arrange: locate the status bar text
                status = app.get_by_id("MainWindow::status-message")
                # step 1: run the simulation and wait for the final status message
                app.get_by_type("ToolbarButton").nth(5).click()
                expect(status).to_have_property("accessibleLabel", "Simulation finished successfully", timeout=30.0)
                # step 2: wait for the charts view to open
                app.get_by_id("MainWindow::charts").wait_for_exists(timeout=15.0)
                # assert: the plotted expression has data and is drawn even though the print never mentioned its quantity
                app.wait_for_condition(lambda: app.get_by_type("ChartView").count() == 1, timeout=15.0, message="expected the declared chart")
                self.assertEqual(app.get_by_type("ChartView").nth(0).text(), "abs(I(L1))")
                # step 3: switch to the netlist view to inspect the rewritten netlist
                app.get_by_type("ToolbarButton").nth(2).click()
                editor = app.get_by_id("NetlistEditor::input")
                app.wait_for_condition(lambda: editor.exists(), timeout=10.0, message="expected the netlist view")
                netlist_text = editor.text() or ""
                # assert: the quantity behind the plotted expression joined the print, keeping the authored format and variables
                self.assertIn(".PRINT TRAN FORMAT=RAW V(N1) I(L1)", netlist_text)
                # assert: the chart declaration is re-emitted with the interpreted directives in the block above .END
                self.assertIn(".PLOT abs(I(L1))\n\n.END", netlist_text)

    def test_charts_follow_the_plot_directives_edited_before_a_rerun(self) -> None:
        # arrange: resolve the xyce executable so the run action gets past its config check
        xyce = shutil.which("Xyce")
        # arrange: skip the scenario when the xyce executable is not available
        if xyce is None:
            self.skipTest("Xyce executable not found")
        # arrange: write a netlist declaring a single chart
        with tempfile.TemporaryDirectory() as scratch:
            netlist_path = Path(scratch) / "edited-plots.cir"
            netlist_path.write_text("* edited plot directives\nV1 IN 0 PULSE(0 5 0 1n 1n 10m 20m)\nR1 IN N1 100\nL1 N1 N2 10mH\nC1 N2 0 1uF\n.TRAN 1u 20m 0\n.PRINT TRAN FORMAT=RAW V(*) I(*)\n.PLOT V(IN)\n.END\n")
            # arrange: launch the application with the netlist and the xyce executable
            with TestSession(launch(args=["--netlist", str(netlist_path), "--xyce", xyce]), self.id()) as app:
                # arrange: locate the status bar text and the netlist editor
                status = app.get_by_id("MainWindow::status-message")
                # step 1: run the simulation and wait for the charts view
                app.get_by_type("ToolbarButton").nth(5).click()
                expect(status).to_have_property("accessibleLabel", "Simulation finished successfully", timeout=30.0)
                app.get_by_id("MainWindow::charts").wait_for_exists(timeout=15.0)
                # assert: the declared chart is the one that opened
                app.wait_for_condition(lambda: app.get_by_type("ChartView").count() == 1, timeout=15.0, message="expected the declared chart")
                self.assertEqual(app.get_by_type("ChartView").nth(0).text(), "V(IN)")
                # step 2: switch to the netlist view and replace the chart declaration with two other charts
                app.get_by_type("ToolbarButton").nth(2).click()
                editor = app.get_by_id("NetlistEditor::input")
                app.wait_for_condition(lambda: editor.exists(), timeout=10.0, message="expected the netlist view")
                edited = "* edited plot directives\nV1 IN 0 PULSE(0 5 0 1n 1n 10m 20m)\nR1 IN N1 100\nL1 N1 N2 10mH\nC1 N2 0 1uF\n.TRAN 1u 20m 0\n.PRINT TRAN FORMAT=RAW V(*) I(*)\n.PLOT V(N1)\n.PLOT abs(I(L1))\n.END\n"
                editor.click()
                editor.fill(edited)
                expect(editor).to_have_text(edited, timeout=10.0)
                # step 3: run the edited netlist again
                app.get_by_type("ToolbarButton").nth(5).click()
                expect(status).to_have_property("accessibleLabel", "Simulation finished successfully", timeout=30.0)
                app.get_by_id("MainWindow::charts").wait_for_exists(timeout=15.0)
                # assert: the charts follow the edited directives, replacing the chart of the previous run
                app.wait_for_condition(lambda: app.get_by_type("ChartView").count() == 2, timeout=15.0, message="expected the charts of the edited directives")
                charts = app.get_by_type("ChartView")
                self.assertEqual(charts.nth(0).text(), "V(N1)")
                self.assertEqual(charts.nth(1).text(), "abs(I(L1))")

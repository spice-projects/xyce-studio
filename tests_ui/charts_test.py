import shutil
import re
import unittest
from pathlib import Path

from slint_automation import TestSession, expect, launch


class ChartPanelChecks(unittest.TestCase):

    def test_run_simulation_shows_the_charts_panel(self) -> None:
        # arrange: resolve the xyce executable and the sample netlist
        xyce = shutil.which("Xyce")
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "tran-simple-01.cir"
        if xyce is None:
            self.skipTest("Xyce executable not found")
        # arrange: launch the application with the netlist and the executable
        with TestSession(launch(args=["--netlist", str(netlist), "--xyce", xyce]), self.id()) as app:
            # act: run the simulation from the toolbar and wait for the charts view
            app.get_by_type("ToolbarButton").nth(5).click()
            app.get_by_id("MainWindow::charts").wait_for_exists(timeout=15.0)
            # assert: the transient tab shows one chart
            self.assertEqual(app.get_by_type("ChartView").count(), 1)

    def test_transient_tab_shows_the_full_abscissa_range(self) -> None:
        # arrange: resolve the xyce executable and the sample netlist
        xyce = shutil.which("Xyce")
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "tran-simple-01.cir"
        if xyce is None:
            self.skipTest("Xyce executable not found")
        with TestSession(launch(args=["--netlist", str(netlist), "--xyce", xyce]), self.id()) as app:
            # act: run the simulation and wait for the charts view
            app.get_by_type("ToolbarButton").nth(5).click()
            app.get_by_id("MainWindow::charts").wait_for_exists(timeout=15.0)
            # act: dump the text labels of the first chart view grouped by axis;
            # the abscissa labels sit in the band below the plot (overlapping
            # the legend) and the ordinate labels sit at the sides
            view = app.get_by_type("ChartView").nth(0)
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
            # assert: the abscissa covers the full 0..20 ms simulation span;
            # the abscissa tick labels end with a time or frequency unit,
            # unlike the legend entries and the ordinate origin label
            ticks = [label for label in abscissa_labels if label.endswith(("Hz", "s")) or label.endswith(" ms")]
            self.assertEqual(ticks, ["0s", "2 ms", "4 ms", "6 ms", "8 ms", "10 ms", "12 ms", "14 ms", "16 ms", "18 ms", "20 ms"])
            # assert: the ordinate axis shows tick labels (default 0..1 axis)
            self.assertTrue(any(label.endswith(("m", " ")) for label in ordinate_labels))

    def test_fft_tab_shows_the_normalized_fft_ranges(self) -> None:
        # arrange: resolve the xyce executable and the sample netlist
        xyce = shutil.which("Xyce")
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "tran-simple-01.cir"
        if xyce is None:
            self.skipTest("Xyce executable not found")
        with TestSession(launch(args=["--netlist", str(netlist), "--xyce", xyce]), self.id()) as app:
            # act: run the simulation and wait for the charts view
            app.get_by_type("ToolbarButton").nth(5).click()
            app.get_by_id("MainWindow::charts").wait_for_exists(timeout=15.0)
            # act: switch to the first generated fft tab
            tabs = app.get_by_type("PlotTabButton")
            self.assertEqual(tabs.count(), 3)
            tabs.nth(1).click()
            expect(app.get_by_type("ChartView").nth(0)).to_exist()
            # assert: two charts, magnitude and phase
            self.assertEqual(app.get_by_type("ChartView").count(), 2)
            # act: dump the axis labels of the magnitude chart
            view = app.get_by_type("ChartView").nth(0)
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
            # assert: the magnitude chart abscissa spans the 0..25.6 kHz span
            ticks = [label for label in abscissa_labels if label.endswith(("Hz", "s")) or label.endswith(" ms")]
            self.assertEqual(ticks, ["0Hz", "5 kHz", "10 kHz", "15 kHz", "20 kHz", "25 kHz"])
            # assert: the ordinate shows current magnitudes
            self.assertTrue(any(label.endswith("A") for label in ordinate_labels))
            # act: dump the ordinate labels of the phase chart
            phase_view = app.get_by_type("ChartView").nth(1)
            phase_view_props = phase_view.properties()
            phase_origin_y = phase_view_props.get("absolutePosition", {}).get("y", 0)
            phase_texts = phase_view.child("Text")
            phase_ordinate_labels: list[str] = []
            for index in range(phase_texts.count()):
                props = phase_texts.nth(index).properties()
                relative_y = props.get("absolutePosition", {}).get("y", 0) - phase_origin_y
                if relative_y <= height - 60:
                    phase_ordinate_labels.append(props.get("accessibleLabel"))
            # assert: the phase chart ordinate shows degrees
            self.assertTrue(any(label.endswith("°") for label in phase_ordinate_labels))

    def test_fft_tab_zoom_applies_full_2d_on_dragged_chart_and_horizontal_only_on_the_other(self) -> None:
        # arrange: resolve the xyce executable and the sample netlist
        xyce = shutil.which("Xyce")
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "tran-simple-01.cir"
        if xyce is None:
            self.skipTest("Xyce executable not found")
        with TestSession(launch(args=["--netlist", str(netlist), "--xyce", xyce]), self.id()) as app:
            # act: run the simulation and switch to the two chart fft tab
            app.get_by_type("ToolbarButton").nth(5).click()
            app.get_by_id("MainWindow::charts").wait_for_exists(timeout=15.0)
            tabs = app.get_by_type("PlotTabButton")
            tabs.nth(1).click()
            expect(app.get_by_type("ChartView").nth(0)).to_exist()
            self.assertEqual(app.get_by_type("ChartView").count(), 2)
            # act: capture the shared pre-zoom axis labels of both charts by
            # splitting the text children of each view by their relative y
            first_view = app.get_by_type("ChartView").nth(0)
            first_props = first_view.properties()
            first_origin_y = first_props.get("absolutePosition", {}).get("y", 0)
            first_height = first_props.get("size", {}).get("height", 0)
            first_texts = first_view.child("Text")
            pre_abscissa_0: list[str] = []
            pre_ordinate_0: list[str] = []
            for index in range(first_texts.count()):
                props = first_texts.nth(index).properties()
                relative_y = props.get("absolutePosition", {}).get("y", 0) - first_origin_y
                if relative_y > first_height - 60:
                    pre_abscissa_0.append(props.get("accessibleLabel"))
                else:
                    pre_ordinate_0.append(props.get("accessibleLabel"))
            second_view = app.get_by_type("ChartView").nth(1)
            second_view_props = second_view.properties()
            second_origin_y = second_view_props.get("absolutePosition", {}).get("y", 0)
            second_texts = second_view.child("Text")
            pre_abscissa_1: list[str] = []
            pre_ordinate_1: list[str] = []
            for index in range(second_texts.count()):
                props = second_texts.nth(index).properties()
                relative_y = props.get("absolutePosition", {}).get("y", 0) - second_origin_y
                if relative_y > first_height - 60:
                    pre_abscissa_1.append(props.get("accessibleLabel"))
                else:
                    pre_ordinate_1.append(props.get("accessibleLabel"))
            pre_ticks_0 = [label for label in pre_abscissa_0 if label.endswith(("Hz", "s")) or label.endswith(" ms")]
            pre_ticks_1 = [label for label in pre_abscissa_1 if label.endswith(("Hz", "s")) or label.endswith(" ms")]
            # act: drag a zoom selection inside the magnitude chart; the chart
            # view center sits inside its plot rect and the drag target stays
            # inside it too
            app.get_by_type("ChartView").nth(0).drag(660.0, 310.0)
            expect(app.get_by_type("ChartView").nth(0)).to_exist()
            # act: read the zoomed axis labels of both charts
            zoomed_view = app.get_by_type("ChartView").nth(0)
            zoomed_view_props = zoomed_view.properties()
            zoomed_origin_y = zoomed_view_props.get("absolutePosition", {}).get("y", 0)
            zoomed_texts = zoomed_view.child("Text")
            abscissa_0: list[str] = []
            ordinate_0: list[str] = []
            for index in range(zoomed_texts.count()):
                props = zoomed_texts.nth(index).properties()
                relative_y = props.get("absolutePosition", {}).get("y", 0) - zoomed_origin_y
                if relative_y > first_height - 60:
                    abscissa_0.append(props.get("accessibleLabel"))
                else:
                    ordinate_0.append(props.get("accessibleLabel"))
            other_view = app.get_by_type("ChartView").nth(1)
            other_view_props = other_view.properties()
            other_origin_y = other_view_props.get("absolutePosition", {}).get("y", 0)
            other_texts = other_view.child("Text")
            abscissa_1: list[str] = []
            ordinate_1: list[str] = []
            for index in range(other_texts.count()):
                props = other_texts.nth(index).properties()
                relative_y = props.get("absolutePosition", {}).get("y", 0) - other_origin_y
                if relative_y > first_height - 60:
                    abscissa_1.append(props.get("accessibleLabel"))
                else:
                    ordinate_1.append(props.get("accessibleLabel"))
            ticks_0 = [label for label in abscissa_0 if label.endswith(("Hz", "s")) or label.endswith(" ms")]
            ticks_1 = [label for label in abscissa_1 if label.endswith(("Hz", "s")) or label.endswith(" ms")]
            # assert: both charts share the zoomed abscissa range
            self.assertEqual(ticks_0, ticks_1)
            self.assertNotEqual(ticks_0, pre_ticks_0)
            self.assertNotEqual(ticks_1, pre_ticks_1)
            # assert: the dragged magnitude chart received the vertical zoom as
            # well: its ordinate narrowed away from the pre-zoom range
            self.assertNotEqual(ordinate_0, pre_ordinate_0)
            # assert: the other chart keeps its full ordinate range
            self.assertEqual(ordinate_1, pre_ordinate_1)

    def test_descending_dc_sweep_shows_the_reversed_abscissa_range(self) -> None:
        # arrange: the netlist sweeps the source from 5 down to 0 in -0.5 steps
        xyce = shutil.which("Xyce")
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "dc-sweep-descending-01.cir"
        if xyce is None:
            self.skipTest("Xyce executable not found")
        with TestSession(launch(args=["--netlist", str(netlist), "--xyce", xyce]), self.id()) as app:
            # act: run the simulation and wait for the charts view
            app.get_by_type("ToolbarButton").nth(5).click()
            app.get_by_id("MainWindow::charts").wait_for_exists(timeout=15.0)
            expect(app.get_by_type("ChartView").nth(0)).to_exist()
            self.assertEqual(app.get_by_type("ChartView").count(), 1)
            # act: dump the abscissa band labels of the bottom band with their x
            # positions; labels left of the panel are ordinate origin text
            view = app.get_by_type("ChartView").nth(0)
            view_props = view.properties()
            origin_y = view_props.get("absolutePosition", {}).get("y", 0)
            height = view_props.get("size", {}).get("height", 0)
            texts = view.child("Text")
            pre_zoom: list[tuple[str, float]] = []
            for index in range(texts.count()):
                props = texts.nth(index).properties()
                relative_y = props.get("absolutePosition", {}).get("y", 0) - origin_y
                x = props.get("absolutePosition", {}).get("x", 0)
                if relative_y > height - 60 and x >= 0:
                    pre_zoom.append((props.get("accessibleLabel"), x))
            # act: parse each abscissa band label like "500 m" into a float;
            # the si prefix scales the number and any unit suffix is ignored
            multipliers = {"m": 1e-3, "u": 1e-6, "µ": 1e-6, "n": 1e-9, "k": 1e3, "M": 1e6, "G": 1e9}
            parsed: dict[str, float] = {}
            for label, _ in pre_zoom:
                match = re.match(r"^(-?\d+(?:\.\d+)?)\s*([munµkMG]?)", label.strip())
                parsed[label] = float(match.group(1)) * multipliers.get(match.group(2), 1.0)
            # assert: the full descending 5..0 sweep is on the axis
            labels = [label for label, _ in pre_zoom]
            values = [parsed[label] for label in labels]
            self.assertEqual(len(labels), 11)
            self.assertEqual(min(values), 0.0)
            self.assertEqual(max(values), 5.0)
            # assert: larger sweep values map toward the panel west edge
            west = next(x for label, x in pre_zoom if parsed[label] == 5.0)
            east = next(x for label, x in pre_zoom if parsed[label] == 0.0)
            self.assertLess(west, east)
            # act: zoom on the chart without plotting any series
            view.drag(660.0, 310.0)
            expect(app.get_by_type("ChartView").nth(0)).to_exist()
            post_texts = view.child("Text")
            post_zoom: list[str] = []
            for index in range(post_texts.count()):
                props = post_texts.nth(index).properties()
                relative_y = props.get("absolutePosition", {}).get("y", 0) - origin_y
                x = props.get("absolutePosition", {}).get("x", 0)
                if relative_y > height - 60 and x >= 0:
                    post_zoom.append(props.get("accessibleLabel"))
            post_values: list[float] = []
            for label in post_zoom:
                match = re.match(r"^(-?\d+(?:\.\d+)?)\s*([munµkMG]?)", label.strip())
                post_values.append(float(match.group(1)) * multipliers.get(match.group(2), 1.0))
            # assert: the zoomed abscissa covers only the interior of the sweep
            # and both sweep boundaries are gone from the axis
            self.assertTrue(post_zoom)
            self.assertNotEqual(post_zoom, labels)
            self.assertTrue(all(0.0 < value < 5.0 for value in post_values))

    def test_fft_tab_2_shows_the_pure_fft_range(self) -> None:
        # arrange: resolve the xyce executable and the sample netlist
        xyce = shutil.which("Xyce")
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "tran-simple-01.cir"
        if xyce is None:
            self.skipTest("Xyce executable not found")
        with TestSession(launch(args=["--netlist", str(netlist), "--xyce", xyce]), self.id()) as app:
            # act: run the simulation and switch to the second generated fft tab
            app.get_by_type("ToolbarButton").nth(5).click()
            app.get_by_id("MainWindow::charts").wait_for_exists(timeout=15.0)
            tabs = app.get_by_type("PlotTabButton")
            tabs.nth(2).click()
            expect(app.get_by_type("ChartView").nth(0)).to_exist()
            # assert: one chart spanning the 0..51.2 kHz span
            self.assertEqual(app.get_by_type("ChartView").count(), 1)
            # act: dump the axis labels of the single chart
            view = app.get_by_type("ChartView").nth(0)
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
            # assert: the abscissa spans the 0..51.2 kHz span
            ticks = [label for label in abscissa_labels if label.endswith(("Hz", "s")) or label.endswith(" ms")]
            self.assertEqual(ticks, ["0Hz", "10 kHz", "20 kHz", "30 kHz", "40 kHz", "50 kHz"])
            # assert: the ordinate shows current magnitudes
            self.assertTrue(any(label.endswith("A") for label in ordinate_labels))

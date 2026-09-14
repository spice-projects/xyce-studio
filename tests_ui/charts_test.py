import shutil
import unittest
from pathlib import Path

from slint_automation import TestSession, expect, launch
from slint_automation.slint_client import SlintClient


def _chart_axis_labels(app, chart_index: int) -> tuple[list[str], list[str]]:
    # dump the text labels of one chart view grouped by axis: the abscissa
    # labels sit in the band below the plot (overlapping the legend) and the
    # ordinate labels sit at the sides of the plot rect
    client: SlintClient = app.client()
    views = client.find_by_type("ChartView")
    view = views[chart_index]
    view_props = client.get_element_properties(view)
    origin_y = view_props.get("absolutePosition", {}).get("y", 0)
    height = view_props.get("size", {}).get("height", 0)
    abscissa_labels: list[str] = []
    ordinate_labels: list[str] = []
    for text_handle in client.find_by_type_in(view, "Text"):
        props = client.get_element_properties(text_handle)
        label = props.get("accessibleLabel")
        relative_y = props.get("absolutePosition", {}).get("y", 0) - origin_y
        if relative_y > height - 60:
            abscissa_labels.append(label)
        else:
            ordinate_labels.append(label)
    return abscissa_labels, ordinate_labels


def _abscissa_tick_labels(labels: list[str]) -> list[str]:
    # keep only the axis tick labels: they end with a time or frequency unit,
    # unlike the legend entries and the ordinate origin label
    return [label for label in labels if label.endswith(("Hz", "s")) or label.endswith(" ms")]


class ChartPanelChecks(unittest.TestCase):

    def _run_simulation(self, app) -> None:
        # run the simulation from the toolbar and wait for the charts view
        tools = app.get_by_type("ToolbarButton")
        tools.nth(5).click()
        app.get_by_id("MainWindow::charts").wait_for_exists(timeout=15.0)
        expect(app.get_by_id("MainWindow::charts")).to_exist()

    def test_run_simulation_shows_the_charts_panel(self) -> None:
        # arrange: resolve the xyce executable and the sample netlist
        xyce = shutil.which("Xyce")
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "tran-simple-01.cir"
        if xyce is None:
            self.skipTest("Xyce executable not found")
        # arrange: launch the application with the netlist and the executable
        with TestSession(launch(args=["--netlist", str(netlist), "--xyce", xyce]), self.id()) as app:
            # act
            self._run_simulation(app)
            # assert: the transient tab shows one chart
            expect(app.get_by_type("ChartView").nth(0)).to_exist()
            self.assertEqual(app.get_by_type("ChartView").count(), 1)

    def test_transient_tab_shows_the_full_abscissa_range(self) -> None:
        # arrange
        xyce = shutil.which("Xyce")
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "tran-simple-01.cir"
        if xyce is None:
            self.skipTest("Xyce executable not found")
        with TestSession(launch(args=["--netlist", str(netlist), "--xyce", xyce]), self.id()) as app:
            # act
            self._run_simulation(app)
            # assert: the abscissa covers the full 0..20 ms simulation span
            abscissa_labels, _ = _chart_axis_labels(app, 0)
            ticks = _abscissa_tick_labels(abscissa_labels)
            self.assertEqual(ticks, ["0s", "2 ms", "4 ms", "6 ms", "8 ms", "10 ms", "12 ms", "14 ms", "16 ms", "18 ms", "20 ms"])
            # assert: the ordinate axis shows tick labels (default 0..1 axis)
            _, ordinate_labels = _chart_axis_labels(app, 0)
            self.assertTrue(any(label.endswith(("m", " ")) for label in ordinate_labels))

    def test_fft_tab_shows_the_normalized_fft_ranges(self) -> None:
        # arrange
        xyce = shutil.which("Xyce")
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "tran-simple-01.cir"
        if xyce is None:
            self.skipTest("Xyce executable not found")
        with TestSession(launch(args=["--netlist", str(netlist), "--xyce", xyce]), self.id()) as app:
            # act: run the simulation and switch to the first generated fft tab
            self._run_simulation(app)
            tabs = app.get_by_type("PlotTabButton")
            self.assertEqual(tabs.count(), 3)
            tabs.nth(1).click()
            expect(app.get_by_type("ChartView").nth(0)).to_exist()
            # assert: two charts, magnitude and phase
            self.assertEqual(app.get_by_type("ChartView").count(), 2)
            # assert: the magnitude chart abscissa spans the 0..25.6 kHz span
            abscissa_labels, ordinate_labels = _chart_axis_labels(app, 0)
            ticks = _abscissa_tick_labels(abscissa_labels)
            self.assertEqual(ticks, ["0Hz", "5 kHz", "10 kHz", "15 kHz", "20 kHz", "25 kHz"])
            # assert: the ordinate shows current magnitudes
            self.assertTrue(any(label.endswith("A") for label in ordinate_labels))
            # assert: the phase chart ordinate shows degrees
            _, phase_labels = _chart_axis_labels(app, 1)
            self.assertTrue(any(label.endswith("°") for label in phase_labels))

    def test_fft_tab_zoom_applies_full_2d_on_dragged_chart_and_horizontal_only_on_the_other(self) -> None:
        # arrange
        xyce = shutil.which("Xyce")
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "tran-simple-01.cir"
        if xyce is None:
            self.skipTest("Xyce executable not found")
        with TestSession(launch(args=["--netlist", str(netlist), "--xyce", xyce]), self.id()) as app:
            # act: run the simulation, switch to the two chart fft tab
            self._run_simulation(app)
            tabs = app.get_by_type("PlotTabButton")
            tabs.nth(1).click()
            expect(app.get_by_type("ChartView").nth(0)).to_exist()
            self.assertEqual(app.get_by_type("ChartView").count(), 2)
            # capture the shared pre-zoom axis labels of both charts
            pre_abscissa_0, pre_ordinate_0 = _chart_axis_labels(app, 0)
            pre_abscissa_1, pre_ordinate_1 = _chart_axis_labels(app, 1)
            pre_ticks_0 = _abscissa_tick_labels(pre_abscissa_0)
            pre_ticks_1 = _abscissa_tick_labels(pre_abscissa_1)
            # act: drag a zoom selection inside the magnitude chart; the chart
            # view center sits inside its plot rect and the drag target stays
            # inside it too
            client = app.client()
            view_handle = client.find_by_type("ChartView")[0]
            client.drag_element(view_handle, 660.0, 310.0)
            expect(app.get_by_type("ChartView").nth(0)).to_exist()
            # assert: both charts share the zoomed abscissa range
            abscissa_0, ordinate_0 = _chart_axis_labels(app, 0)
            abscissa_1, ordinate_1 = _chart_axis_labels(app, 1)
            ticks_0 = _abscissa_tick_labels(abscissa_0)
            ticks_1 = _abscissa_tick_labels(abscissa_1)
            self.assertEqual(ticks_0, ticks_1)
            self.assertNotEqual(ticks_0, pre_ticks_0)
            self.assertNotEqual(ticks_1, pre_ticks_1)
            # assert: the dragged magnitude chart received the vertical zoom as
            # well: its ordinate narrowed away from the pre-zoom range
            self.assertNotEqual(ordinate_0, pre_ordinate_0)
            # assert: the other chart keeps its full ordinate range
            self.assertEqual(ordinate_1, pre_ordinate_1)

    def test_fft_tab_2_shows_the_pure_fft_range(self) -> None:
        # arrange
        xyce = shutil.which("Xyce")
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "tran-simple-01.cir"
        if xyce is None:
            self.skipTest("Xyce executable not found")
        with TestSession(launch(args=["--netlist", str(netlist), "--xyce", xyce]), self.id()) as app:
            # act: run the simulation and switch to the second generated fft tab
            self._run_simulation(app)
            tabs = app.get_by_type("PlotTabButton")
            tabs.nth(2).click()
            expect(app.get_by_type("ChartView").nth(0)).to_exist()
            # assert: one chart spanning the 0..51.2 kHz span
            self.assertEqual(app.get_by_type("ChartView").count(), 1)
            abscissa_labels, _ = _chart_axis_labels(app, 0)
            ticks = _abscissa_tick_labels(abscissa_labels)
            self.assertEqual(ticks, ["0Hz", "10 kHz", "20 kHz", "30 kHz", "40 kHz", "50 kHz"])
            # assert: the ordinate shows current magnitudes
            _, ordinate_labels = _chart_axis_labels(app, 0)
            self.assertTrue(any(label.endswith("A") for label in ordinate_labels))

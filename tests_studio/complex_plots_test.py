import shutil
import tempfile
import unittest
from pathlib import Path

from albor_automation import TestSession, expect, launch


class ComplexPlotChecks(unittest.TestCase):
    # a complex AC source expands into derived series, and the plot selection marks those series

    def test_a_magnitude_only_plot_does_not_select_the_complex_source(self) -> None:
        # arrange: the AC netlist whose .PLOT names the magnitude of a complex current
        xyce = shutil.which("Xyce")
        if xyce is None:
            self.skipTest("Xyce executable not found")
        with tempfile.TemporaryDirectory() as tmp:
            netlist = Path(tmp) / "ac.cir"
            shutil.copy(Path(__file__).resolve().parents[1] / "netlists" / "ac-simple-02.cir", netlist)
            with TestSession(launch(args=["--netlist", str(netlist), "--xyce", xyce]), self.id()) as app:
                # act: run the analysis
                app.get_by_id("main_run").click()
                expect(app.get_by_id("main_status")).to_have_name("Simulation finished", timeout=30.0)
                expect(app.get_by_id("charts_view")).to_have_name("1 line chart")
                # step: open the plot selection
                app.get_by_id("charts_view").click("chart-0-add-remove-plots")
                app.get_by_id("add_plot_filter").wait_for_exists()
                # the cards the dialog lists, keyed by the label they show
                cards = {node["name"]: node["id"] for node in app.client().tree() if node["role"] == "checkbox" and node["id"].startswith("add_plot_card_")}
                # assert: the derived magnitude is selected and the complex source is not
                self.assertEqual(app.client().value(cards["db(I(L1))"]), "yes")
                self.assertEqual(app.client().value(cards["I(L1)"]), "no")
                # step: close the dialog
                app.get_by_id("add_plot_cancel").click()

    def test_selecting_the_complex_source_shows_its_derived_series(self) -> None:
        # arrange: the AC netlist whose .PLOT names the magnitude of a complex current
        xyce = shutil.which("Xyce")
        if xyce is None:
            self.skipTest("Xyce executable not found")
        with tempfile.TemporaryDirectory() as tmp:
            netlist = Path(tmp) / "ac.cir"
            shutil.copy(Path(__file__).resolve().parents[1] / "netlists" / "ac-simple-02.cir", netlist)
            with TestSession(launch(args=["--netlist", str(netlist), "--xyce", xyce]), self.id()) as app:
                # arrange: run the analysis and open the plot selection
                app.get_by_id("main_run").click()
                expect(app.get_by_id("main_status")).to_have_name("Simulation finished", timeout=30.0)
                app.get_by_id("charts_view").click("chart-0-add-remove-plots")
                app.get_by_id("add_plot_filter").wait_for_exists()
                cards = {node["name"]: node["id"] for node in app.client().tree() if node["role"] == "checkbox" and node["id"].startswith("add_plot_card_")}
                # act: select the complex source itself and accept
                app.get_by_id(cards["I(L1)"]).click("toggle")
                app.get_by_id("add_plot_ok").click()
                app.get_by_id("add_plot_ok").wait_for_gone()
                # step: reopen the selection
                app.get_by_id("charts_view").click("chart-0-add-remove-plots")
                app.get_by_id("add_plot_filter").wait_for_exists()
                cards = {node["name"]: node["id"] for node in app.client().tree() if node["role"] == "checkbox" and node["id"].startswith("add_plot_card_")}
                # assert: both derived series are selected and the source is not
                self.assertEqual(app.client().value(cards["db(I(L1))"]), "yes")
                self.assertEqual(app.client().value(cards["phase(I(L1))"]), "yes")
                self.assertEqual(app.client().value(cards["I(L1)"]), "no")
                # step: close the dialog
                app.get_by_id("add_plot_cancel").click()


if __name__ == "__main__":
    unittest.main()

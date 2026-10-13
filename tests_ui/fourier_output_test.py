import shutil
import tempfile
import unittest
from pathlib import Path

from slint_automation import TestSession, expect, launch


class FourierOutputSimulationChecks(unittest.TestCase):

    def test_fourier_results_panel_shows_spectrum_table(self) -> None:
        # arrange: write a transient netlist with a printed curve and a .FOUR directive
        netlist = Path(tempfile.mkdtemp(prefix="xyce-fourier-output-")) / "tran-four.cir"
        netlist.write_text("* Fourier transient netlist\nV1 IN 0 PULSE(0 5 0 1n 1n 10m 20m)\nR1 IN N1 100\nC1 N1 0 1u\n.TRAN 10u 1m 0\n.PRINT TRAN V(IN)\n.FOUR 1k V(IN)\n.END\n")
        # arrange: resolve the xyce executable and skip the scenario when missing
        xyce = shutil.which("Xyce")
        if xyce is None:
            self.skipTest("Xyce executable not found")
        # step 1: launch the application with the netlist and the xyce executable
        with TestSession(launch(args=["--netlist", str(netlist), "--xyce", xyce]), self.id()) as app:
            # arrange: locate the status bar text and the toolbar
            status = app.get_by_id("MainWindow::status-message")
            tools = app.get_by_type("ToolbarButton")
            # step 2: run the simulation from the toolbar run tool
            tools.nth(5).click()
            # step 3: wait for the status bar to report success
            expect(status).to_have_property("accessibleLabel", "Simulation finished successfully", timeout=30.0)
            # step 4: wait for the charts view to open
            app.get_by_id("MainWindow::charts").wait_for_exists(timeout=10.0)
            # assert: the analysis tab and the Fourier tab are rendered side by side
            tabs = app.get_by_type("PlotTabButton")
            self.assertEqual(tabs.count(), 2)
            self.assertEqual(tabs.nth(0).child("Text").text(), "Transient")
            self.assertEqual(tabs.nth(1).child("Text").text(), "FOUR: 1000 Hz")
            # step 5: switch to the Fourier tab
            tabs.nth(1).click()
            # assert: the Fourier results table replaces the chart canvas
            title = app.get_by_id("FourierResultsPanel::title-text")
            expect(title).to_have_text("FOUR: 1000 Hz", timeout=10.0)
            app.wait_for_condition(lambda: app.get_by_type("ChartView").count() == 0, timeout=10.0, message="expected the chart canvas to be replaced by the Fourier results table")
            # assert: the block header band carries the signal name and the THD
            app.wait_for_condition(lambda: any(text.text().startswith("V(IN)") and "THD:" in text.text() for text in app.get_by_type("Text").all()), timeout=10.0, message="expected the signal/THD block header")
            # assert: the harmonic rows carry the numbers Xyce wrote for the spectrum
            cells = [cell.text() for cell in app.get_by_type("FourierDataCell").all()]
            self.assertIn("0.000000e+00", cells)
            # step 6: switch back to the analysis tab
            tabs.nth(0).click()
            # assert: the chart canvas returns in place of the Fourier table
            app.wait_for_condition(lambda: app.get_by_type("ChartView").count() >= 1, timeout=10.0, message="expected the chart canvas to return")
            app.get_by_id("FourierResultsPanel::title-text").wait_for_gone(timeout=10.0)

    def test_fourier_results_panel_labels_each_sweep_step(self) -> None:
        # arrange: write a transient netlist whose .STEP sweep makes Xyce dump one Fourier file per step
        netlist = Path(tempfile.mkdtemp(prefix="xyce-fourier-steps-")) / "tran-four-steps.cir"
        netlist.write_text("* Fourier stepped transient netlist\nV1 IN 0 PULSE(0 5 0 1n 1n 10m 20m)\nR1 IN N1 100\nC1 N1 0 1u\n.TRAN 10u 1m 0\n.STEP R1 100 200 50\n.FOUR 1k V(IN)\n.END\n")
        # arrange: resolve the xyce executable and skip the scenario when missing
        xyce = shutil.which("Xyce")
        if xyce is None:
            self.skipTest("Xyce executable not found")
        # step 1: launch the application with the netlist and the xyce executable
        with TestSession(launch(args=["--netlist", str(netlist), "--xyce", xyce]), self.id()) as app:
            # arrange: locate the status bar text and the toolbar
            status = app.get_by_id("MainWindow::status-message")
            tools = app.get_by_type("ToolbarButton")
            # step 2: run the simulation from the toolbar run tool
            tools.nth(5).click()
            # step 3: wait for the status bar to report success
            expect(status).to_have_property("accessibleLabel", "Simulation finished successfully", timeout=30.0)
            # step 4: wait for the charts view to open; the Fourier dataset is the only one so the table shows directly
            app.get_by_id("MainWindow::charts").wait_for_exists(timeout=10.0)
            # assert: the multi-step table labels the sweep step in each block header
            expect(app.get_by_id("FourierResultsPanel::title-text")).to_have_text("FOUR: 1000 Hz", timeout=10.0)
            app.wait_for_condition(lambda: any("Step 1" in text.text() and "THD:" in text.text() for text in app.get_by_type("Text").all()), timeout=10.0, message="expected the first sweep step label")

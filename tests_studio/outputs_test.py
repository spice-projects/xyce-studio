import shutil
import tempfile
import unittest
from pathlib import Path

from albor_automation import TestSession, expect, launch


class SimulationOutputChecks(unittest.TestCase):
    # the analysis outputs a run produces open as datasets, as the legacy frontend shows them

    def test_a_std_print_run_opens_the_prn_dataset(self) -> None:
        # arrange: resolve Xyce and the sample print-format netlist
        xyce = shutil.which("Xyce")
        if xyce is None:
            self.skipTest("Xyce executable not found")
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "dc-sweep-prn-std-01.cir"
        with TestSession(launch(args=["--netlist", str(netlist), "--xyce", xyce]), self.id()) as app:
            # act: run the simulation
            app.get_by_id("main_run").click()
            expect(app.get_by_id("main_status")).to_have_name("Simulation finished", timeout=30.0)
            # assert: the produced dataset is the active document and it presents a chart
            app.wait_for_condition(lambda: "active: Netlist" not in app.get_by_id("document_tabs").node()["name"], timeout=10.0, message="expected the produced dataset to become the active document")
            chart = app.get_by_id("charts_view")
            chart.wait_for_exists()
            app.wait_for_condition(lambda: "line chart" in chart.node()["name"] or "smith chart" in chart.node()["name"], timeout=10.0, message="expected the produced dataset to present a chart")

    def test_a_pce_print_run_opens_the_pce_dataset(self) -> None:
        # arrange: a transient netlist with the PCE directives the legacy scenario uses
        xyce = shutil.which("Xyce")
        if xyce is None:
            self.skipTest("Xyce executable not found")
        with tempfile.TemporaryDirectory() as tmp:
            netlist = Path(tmp) / "tran-pce.cir"
            netlist.write_text("* PCE transient netlist\nV1 IN 0 PULSE(0 5 0 1n 1n 10m 20m)\nR1 IN N1 100\nC1 N1 0 1u\n.TRAN 10u 1m 0\n.PRINT TRAN V(N1)\n.PCE param=R1 type=normal means=100 std_deviations=10\n.OPTIONS PCES OUTPUTS={V(N1)}\n.PRINT PCE V(N1)\n.END\n")
            with TestSession(launch(args=["--netlist", str(netlist), "--xyce", xyce]), self.id()) as app:
                # act: run the simulation
                app.get_by_id("main_run").click()
                expect(app.get_by_id("main_status")).to_have_name("Simulation finished", timeout=30.0)
                # assert: the analysis and the PCE companion are two dataset tabs, with the analysis active
                tabs = app.get_by_id("document_tabs")
                expect(tabs).to_have_name("Documents, active: tran-pce.cir")
                expect(app.get_by_id("charts_view")).to_have_name("2 datasets, 1 line chart, active: tran-pce.cir")
                # step: select the PCE companion tab
                tabs.click("select-tab-2")
                # assert: the companion opens its own charts
                expect(tabs).to_have_name("Documents, active: netlist.cir.PCE")
                expect(app.get_by_id("charts_view")).to_have_name("2 datasets, 1 line chart, active: netlist.cir.PCE")

    def test_a_stepped_fourier_run_labels_each_step(self) -> None:
        # arrange: a transient netlist whose .STEP sweep makes Xyce write one Fourier file per step
        xyce = shutil.which("Xyce")
        if xyce is None:
            self.skipTest("Xyce executable not found")
        with tempfile.TemporaryDirectory() as tmp:
            netlist = Path(tmp) / "tran-four-steps.cir"
            netlist.write_text("* Fourier stepped transient netlist\nV1 IN 0 PULSE(0 5 0 1n 1n 10m 20m)\nR1 IN N1 100\nC1 N1 0 1u\n.TRAN 10u 1m 0\n.STEP R1 100 200 50\n.FOUR 1k V(IN)\n.END\n")
            with TestSession(launch(args=["--netlist", str(netlist), "--xyce", xyce]), self.id()) as app:
                # act: run the simulation
                app.get_by_id("main_run").click()
                expect(app.get_by_id("main_status")).to_have_name("Simulation finished", timeout=30.0)
                # assert: every Fourier block labels the sweep step it belongs to
                first = app.get_by_id("fourier_block_0")
                first.wait_for_exists()
                app.wait_for_condition(lambda: "Step 1" in first.node()["name"], timeout=10.0, message="expected the first Fourier block to label its step")
                second = app.get_by_id("fourier_block_1")
                second.wait_for_exists()
                app.wait_for_condition(lambda: "Step 2" in second.node()["name"], timeout=10.0, message="expected the second Fourier block to label its step")


if __name__ == "__main__":
    unittest.main()

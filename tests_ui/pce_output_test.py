import shutil
import unittest
from pathlib import Path

from slint_automation import TestSession, expect, launch


class PceOutputSimulationChecks(unittest.TestCase):

    def test_pce_print_loads_pce_tab_next_to_transient(self) -> None:
        # arrange: write a transient netlist with .PCE directives into a scratch directory
        import tempfile
        netlist = Path(tempfile.mkdtemp(prefix="xyce-pce-output-")) / "tran-pce.cir"
        netlist.write_text("* PCE transient netlist\nV1 IN 0 PULSE(0 5 0 1n 1n 10m 20m)\nR1 IN N1 100\nC1 N1 0 1u\n.TRAN 10u 1m 0\n.PRINT TRAN V(N1)\n.PCE param=R1 type=normal means=100 std_deviations=10\n.OPTIONS PCES OUTPUTS={V(N1)}\n.PRINT PCE V(N1)\n.END\n")
        # arrange: resolve the xyce executable and skip the scenario when missing
        xyce = shutil.which("Xyce")
        if xyce is None:
            self.skipTest("Xyce executable not found")
        # step 1: launch the application with the netlist and the xyce executable
        with TestSession(launch(args=["--netlist", str(netlist), "--xyce", xyce]), self.id()) as app:
            # arrange: locate the status bar text and the toolbar
            status = app.get_by_id("MainWindow::statusbar").child("Text")
            tools = app.get_by_type("ToolbarButton")
            # step 2: run the simulation from the toolbar run tool
            tools.nth(5).click()
            # step 3: wait for the status bar to report success
            expect(status).to_have_property("accessibleLabel", "Simulation finished successfully", timeout=30.0)
            # step 4: wait for the charts view to open
            app.get_by_id("MainWindow::charts").wait_for_exists(timeout=10.0)
            # assert: the analysis tab and the pce tab are rendered side by side
            tabs = app.get_by_type("PlotTabButton")
            self.assertEqual(tabs.count(), 2)
            self.assertEqual(tabs.nth(0).child("Text").text(), "Transient")
            self.assertEqual(tabs.nth(1).child("Text").text(), "PCE Analysis")
            # step 5: switch to the pce tab
            tabs.nth(1).click()
            # assert: the pce tab shows its charts
            app.wait_for_condition(lambda: app.get_by_type("ChartView").count() >= 1, timeout=10.0, message="expected the pce charts")

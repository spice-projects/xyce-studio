import shutil
import unittest
from pathlib import Path

from slint_automation import TestSession, expect, launch


class PrnOutputSimulationChecks(unittest.TestCase):

    def test_std_print_format_loads_prn_file(self) -> None:
        # arrange: resolve the xyce executable and the sample netlist
        xyce = shutil.which("Xyce")
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "dc-sweep-prn-std-01.cir"
        # arrange: skip the scenario when the xyce executable is not available
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
            expect(status).to_have_property("accessibleLabel", "Simulation finished successfully", timeout=15.0)
            # step 4: wait for the charts view to open
            app.get_by_id("MainWindow::charts").wait_for_exists(timeout=10.0)
            # assert: at least one chart tab is present, showing the .prn dataset
            self.assertGreaterEqual(app.get_by_type("ChartView").count(), 1)
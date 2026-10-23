import shutil
import tempfile
import unittest
from pathlib import Path

from slint_automation import TestSession, expect, launch


class TempOutputCleanupChecks(unittest.TestCase):

    def test_previous_run_directory_is_removed_and_app_exit_clears_the_latest(self) -> None:
        # arrange: resolve the xyce executable and the sample netlist
        xyce = shutil.which("Xyce")
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "tran-simple-01.cir"
        # arrange: skip the scenario when the xyce executable is not available
        if xyce is None:
            self.skipTest("Xyce executable not found")
        # arrange: isolate the application temporary directory and copy the netlist to a scratch directory
        with tempfile.TemporaryDirectory(prefix="xyce-temp-output-cleanup-") as scratch:
            netlist_path = Path(scratch) / "tran-simple-01.cir"
            netlist_path.write_text(netlist.read_text())
            temp_root = Path(scratch) / "tmp"
            temp_root.mkdir()
            # arrange: pre-create a stale run directory to prove the application performs no startup cleanup
            stale_directory = temp_root / "xyce-studio-stale"
            stale_directory.mkdir()
            # arrange: launch with the isolated temporary directory variables so every run directory lands in the scratch root
            with TestSession(launch(env={"TMPDIR": str(temp_root), "TEMP": str(temp_root), "TMP": str(temp_root)}, args=["--netlist", str(netlist_path), "--xyce", xyce]), self.id()) as app:
                status = app.get_by_id("MainWindow::status-message")
                # act: run the simulation and wait for success
                app.get_by_type("ToolbarButton").nth(5).click()
                expect(status).to_have_property("accessibleLabel", "Simulation finished successfully", timeout=30.0)
                # assert: the run keeps one temporary directory while its dataset is displayed
                app.wait_for_condition(lambda: len([path for path in temp_root.glob("xyce-studio-*") if path != stale_directory]) == 1, timeout=10.0, message="expected one temporary run directory after the first run")
                first_run_directory = next(path for path in temp_root.glob("xyce-studio-*") if path != stale_directory)
                # assert: the run directory retains the netlist handed to Xyce
                self.assertTrue((first_run_directory / "netlist.cir").exists())
                # act: run the simulation again, replacing the displayed dataset
                app.get_by_type("ToolbarButton").nth(5).click()
                # assert: the previous run directory disappears and the new one remains
                app.wait_for_condition(lambda: not first_run_directory.exists() and len([path for path in temp_root.glob("xyce-studio-*") if path != stale_directory]) == 1, timeout=30.0, message="expected the previous run directory to be removed after the second run")
                # act: exit through the toolbar exit tool so the application shutdown path runs
                app.get_by_type("ToolbarButton").nth(9).click()
                app.wait_for_condition(lambda: not app.is_running(), timeout=10.0, message="expected the application process to exit")
            # assert: shutdown released the latest run directory, left the stale directory and kept the user destination
            self.assertEqual(list(temp_root.glob("xyce-studio-*")), [stale_directory])
            self.assertTrue((Path(scratch) / "tran-simple-01.raw").exists())

    def test_exit_while_a_run_is_in_flight_reaps_the_runner_and_removes_its_directory(self) -> None:
        # arrange: resolve the xyce executable and the slow netlist
        xyce = shutil.which("Xyce")
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "tran-progress-01.cir"
        # arrange: skip the scenario when the xyce executable is not available
        if xyce is None:
            self.skipTest("Xyce executable not found")
        # arrange: isolate the application temporary directory
        with tempfile.TemporaryDirectory(prefix="xyce-temp-output-active-run-") as scratch:
            temp_root = Path(scratch) / "tmp"
            temp_root.mkdir()
            # arrange: launch with the isolated temporary directory variables
            with TestSession(launch(env={"TMPDIR": str(temp_root), "TEMP": str(temp_root), "TMP": str(temp_root)}, args=["--netlist", str(netlist), "--xyce", xyce]), self.id()) as app:
                # act: start the simulation and wait until it is in flight
                app.get_by_type("ToolbarButton").nth(5).click()
                app.get_by_id("RunProgress::run-progress-percentage").wait_for_exists(timeout=15.0)
                # act: exit through the toolbar exit tool while the run is still in flight
                app.get_by_type("ToolbarButton").nth(9).click()
                app.wait_for_condition(lambda: not app.is_running(), timeout=10.0, message="expected the application process to exit")
            # assert: shutdown reaped the runner and released the active run directory
            self.assertEqual(list(temp_root.glob("xyce-studio-*")), [])

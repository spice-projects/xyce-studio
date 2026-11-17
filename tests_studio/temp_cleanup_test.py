import shutil
import tempfile
import unittest
from pathlib import Path

from albor_automation import TestSession, expect, launch


class TempOutputCleanupChecks(unittest.TestCase):
    # the private run directories live exactly as long as the datasets that read them

    def test_the_previous_run_directory_is_removed_and_exit_clears_the_latest(self) -> None:
        # arrange: resolve Xyce and isolate the temporary directory the run directories land in
        xyce = shutil.which("Xyce")
        if xyce is None:
            self.skipTest("Xyce executable not found")
        sample = Path(__file__).resolve().parents[1] / "netlists" / "tran-simple-01.cir"
        with tempfile.TemporaryDirectory() as scratch:
            netlist = Path(scratch) / "tran-simple-01.cir"
            shutil.copy(sample, netlist)
            temp_root = Path(scratch) / "tmp"
            temp_root.mkdir()
            # a stale directory the application never created, which no cleanup may remove
            stale = temp_root / "xyce-studio-stale"
            stale.mkdir()
            with TestSession(launch(env={"TMPDIR": str(temp_root), "TEMP": str(temp_root), "TMP": str(temp_root)}, args=["--netlist", str(netlist), "--xyce", xyce]), self.id()) as app:
                # act: run the simulation
                app.get_by_id("main_run").click()
                expect(app.get_by_id("main_status")).to_have_name("Simulation finished", timeout=30.0)
                # assert: the run keeps one private directory that holds the netlist handed to Xyce
                app.wait_for_condition(lambda: len([path for path in temp_root.glob("xyce-studio-*") if path != stale]) == 1, timeout=10.0, message="expected one private run directory after the first run")
                first = next(path for path in temp_root.glob("xyce-studio-*") if path != stale)
                self.assertTrue((first / "netlist.cir").exists())
                # act: run again, which replaces the displayed dataset
                app.get_by_id("main_run").click()
                # assert: the previous directory disappears and the new one remains
                app.wait_for_condition(lambda: not first.exists() and len([path for path in temp_root.glob("xyce-studio-*") if path != stale]) == 1, timeout=30.0, message="expected the previous run directory to be removed")
                # act: exit
                app.get_by_id("main_exit").click()
                app.wait_for_condition(lambda: not app.is_running(), timeout=10.0, message="expected the application to exit")
            # assert: shutdown released the latest run directory and left the stale one alone
            self.assertEqual(list(temp_root.glob("xyce-studio-*")), [stale])


    def test_exit_while_a_run_is_in_flight_removes_its_directory(self) -> None:
        # arrange: a slow simulator and an isolated temporary directory
        with tempfile.TemporaryDirectory() as scratch:
            exe = Path(scratch) / "Xyce"
            exe.write_text("#!/bin/sh\nexec sleep 30\n")
            shutil.os.chmod(exe, 0o755)
            netlist = Path(scratch) / "slow.cir"
            netlist.write_text("R1 1 0 1k\n.TRAN 1m 10m\n.END\n")
            temp_root = Path(scratch) / "tmp"
            temp_root.mkdir()
            # act: start the run and leave the application while it lives
            with TestSession(launch(env={"TMPDIR": str(temp_root), "TEMP": str(temp_root), "TMP": str(temp_root)}, args=["--netlist", str(netlist), "--xyce", str(exe)]), self.id()) as app:
                app.get_by_id("main_run").click()
                expect(app.get_by_id("main_run")).to_have_name("Stop")
                app.get_by_id("main_exit").click()
                app.wait_for_condition(lambda: not app.is_running(), timeout=20.0, message="expected the application to exit")
            # assert: the in-flight run directory was released with the shutdown
            self.assertEqual(list(temp_root.glob("xyce-studio-*")), [])


if __name__ == "__main__":
    unittest.main()

import os
import tempfile
import unittest
from pathlib import Path

from albor_automation import TestSession, expect, launch


class FromScratchChecks(unittest.TestCase):
    # a typed netlist runs, stays dirty and reconfigures like a loaded one

    def test_a_run_without_an_analysis_asks_for_one_and_keeps_the_netlist(self) -> None:
        # arrange: a fake Xyce and a typed netlist that declares no analysis
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "Xyce"
            exe.write_text("#!/bin/sh\nexit 0\n")
            os.chmod(exe, 0o755)
            with TestSession(launch(args=["--xyce", str(exe)]), self.id()) as app:
                # step: type a netlist without an analysis
                editor = app.get_by_id("netlist_editor")
                editor.wait_for_exists()
                netlist = "* typed\nR1 1 0 1k\nV1 1 0 5\n.END\n"
                editor.fill(netlist)
                expect(editor).to_have_value(netlist)
                # act: run, which has no analysis to launch
                app.get_by_id("main_run").click()
                # assert: the parameters dialog opens over the typed netlist
                ok = app.get_by_id("simulation_parameters_ok")
                ok.wait_for_exists()
                # step: cancel the dialog
                app.get_by_id("simulation_parameters_cancel").click()
                ok.wait_for_gone()
                # assert: the typed netlist survived and nothing started
                expect(app.get_by_id("netlist_editor")).to_have_value(netlist)
                expect(app.get_by_id("main_run")).to_have_name("Run")

    def test_a_run_keeps_the_typed_netlist_dirty(self) -> None:
        # arrange: a fake Xyce and a typed transient netlist
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "Xyce"
            exe.write_text("#!/bin/sh\nexit 0\n")
            os.chmod(exe, 0o755)
            with TestSession(launch(args=["--xyce", str(exe)]), self.id()) as app:
                # step: type a netlist carrying its analysis
                editor = app.get_by_id("netlist_editor")
                editor.wait_for_exists()
                netlist = "* typed\nV1 1 0 PULSE(0 5 0 1n 1n 10m 20m)\nR1 1 0 1k\n.TRAN 1m 10m\n.END\n"
                editor.fill(netlist)
                expect(app.get_by_id("main_save")).not_().to_have_state("disabled")
                # act: run the simulation
                app.get_by_id("main_run").click()
                expect(app.get_by_id("main_status")).to_have_name("Simulation finished")
                # assert: the netlist was never saved, so it stays dirty and keeps its text
                expect(app.get_by_id("netlist_editor")).to_have_value(netlist)
                expect(app.get_by_id("main_save")).not_().to_have_state("disabled")
                # act: run the typed netlist a second time
                app.get_by_id("main_run").click()
                expect(app.get_by_id("main_status")).to_have_name("Simulation finished")
                # assert: the typed text survives the rerun and stays dirty
                expect(app.get_by_id("netlist_editor")).to_have_value(netlist)
                expect(app.get_by_id("main_save")).not_().to_have_state("disabled")

    def test_a_second_run_uses_the_edited_transient_parameters(self) -> None:
        # arrange: a fake Xyce that echoes the netlist it is handed and a transient netlist
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "Xyce"
            exe.write_text("#!/bin/sh\ncat \"$1\"\n")
            os.chmod(exe, 0o755)
            netlist = Path(tmp) / "reconfigure.cir"
            netlist.write_text("* typed\nV1 1 0 PULSE(0 5 0 1n 1n 10m 20m)\nR1 1 0 1k\n.TRAN 1m 10m\n.END\n")
            with TestSession(launch(args=["--netlist", str(netlist), "--xyce", str(exe)]), self.id()) as app:
                # step: run the netlist as it is
                app.get_by_id("main_run").click()
                log = app.get_by_id("output_log")
                log.wait_for_exists()
                app.wait_for_condition(lambda: ".TRAN 1m 10m" in log.value(), timeout=10.0, message="expected the first run to hand the configured analysis to the simulator")
                # step: raise the end time in the dialog and accept
                app.get_by_id("main_configure").click()
                final = app.get_by_id("simulation_tran_final_time")
                final.wait_for_exists()
                final.fill("20m")
                app.get_by_id("simulation_parameters_ok").click()
                final.wait_for_gone()
                # act: run again
                app.get_by_id("main_run").click()
                # assert: the run opened its own log, which carries the accepted end time
                log.wait_for_exists()
                app.wait_for_condition(lambda: ".TRAN 1m 20m" in log.value(), timeout=10.0, message="expected the second run to use the accepted end time")
                self.assertNotIn(".TRAN 1m 10m", log.value())

    def test_saving_a_typed_netlist_writes_the_chosen_file(self) -> None:
        # arrange: a typed netlist and a destination the automation build names
        with tempfile.TemporaryDirectory() as tmp:
            target = Path(tmp) / "typed.cir"
            with TestSession(launch(env={"XYCE_STUDIO_SAVE_PATH": str(target)}), self.id()) as app:
                # step: type the netlist and save it without a backing file
                editor = app.get_by_id("netlist_editor")
                editor.wait_for_exists()
                netlist = "* typed\nV1 1 0 DC 0 AC 1\nR1 IN 0 1k\n.END\n"
                editor.fill(netlist)
                expect(app.get_by_id("main_save")).not_().to_have_state("disabled")
                app.get_by_id("main_save").click()
                # assert: the chosen file holds the typed netlist and the save action dims
                app.wait_for_condition(lambda: target.exists() and target.read_text() == netlist, timeout=5.0, message="expected the typed netlist at the chosen destination")
                expect(app.get_by_id("main_save")).to_have_state("disabled")


if __name__ == "__main__":
    unittest.main()

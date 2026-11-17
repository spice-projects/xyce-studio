import os
import tempfile
import unittest
from pathlib import Path

from albor_automation import TestSession, expect, launch


class KiCadPluginChecks(unittest.TestCase):
    # the plugin shell reads the schematic export and locks the file actions

    def test_the_plugin_shell_reads_the_schematic_and_locks_the_file_actions(self) -> None:
        # arrange: an exported schematic netlist and a netlist the command line names
        with tempfile.TemporaryDirectory() as tmp:
            schematic = Path(tmp) / "board.net"
            schematic.write_text("R1 1 0 1k\nV1 1 0 5\n.TRAN 1m 10m\n.END\n")
            netlist = Path(tmp) / "other.cir"
            netlist.write_text("R2 1 0 2k\n.END\n")
            env = {"XYCE_STUDIO_SCHEMATIC": str(schematic)}
            with TestSession(launch(args=["--xyce", "/bin/echo", "--netlist", str(netlist)], env=env), self.id()) as app:
                # assert: the editor shows the schematic export, not the command line netlist
                editor = app.get_by_id("netlist_editor")
                editor.wait_for_exists()
                expect(editor).to_have_value("R1 1 0 1k\nV1 1 0 5\n.TRAN 1m 10m\n.END\n")
                # assert: the plugin mode refuses edits
                expect(editor).not_().to_be_writable()
                # assert: the file actions stay off while the run action stays available
                expect(app.get_by_id("main_open")).to_have_state("disabled")
                expect(app.get_by_id("main_save")).to_have_state("disabled")
                expect(app.get_by_id("main_run")).not_().to_have_state("disabled")

    def test_a_failed_schematic_export_reports_the_status_message(self) -> None:
        # arrange: a schematic export the shell cannot read
        with tempfile.TemporaryDirectory() as tmp:
            missing = Path(tmp) / "missing.net"
            env = {"XYCE_STUDIO_SCHEMATIC": str(missing)}
            with TestSession(launch(env=env), self.id()) as app:
                # assert: the shell reports the failure and leaves nothing to run
                expect(app.get_by_id("main_status")).to_have_name("Schematic netlist export failed")
                expect(app.get_by_id("netlist_editor")).to_have_value("")
                expect(app.get_by_id("main_run")).to_have_state("disabled")

    def test_a_schematic_run_uses_the_directives_the_dialog_accepted(self) -> None:
        # arrange: an exported schematic and a fake Xyce that echoes the netlist it is handed
        with tempfile.TemporaryDirectory() as tmp:
            schematic = Path(tmp) / "board.net"
            schematic.write_text("R1 1 0 1k\nV1 1 0 5\n.TRAN 1m 10m\n.END\n")
            exe = Path(tmp) / "Xyce"
            exe.write_text("#!/bin/sh\ncat \"$1\"\n")
            os.chmod(exe, 0o755)
            env = {"XYCE_STUDIO_SCHEMATIC": str(schematic)}
            with TestSession(launch(args=["--xyce", str(exe)], env=env), self.id()) as app:
                # step: open the simulation dialog and raise the end time
                app.get_by_id("main_configure").click()
                final = app.get_by_id("simulation_tran_final_time")
                final.wait_for_exists()
                expect(final).to_have_value("10m")
                final.fill("20m")
                app.get_by_id("simulation_parameters_ok").click()
                final.wait_for_gone()
                # act: run the simulation
                app.get_by_id("main_run").click()
                # assert: the run netlist carries the accepted end time, not the schematic's
                log = app.get_by_id("output_log")
                log.wait_for_exists()
                app.wait_for_condition(lambda: ".TRAN 1m 20m" in log.value(), timeout=10.0, message="expected the run netlist to carry the accepted end time")
                self.assertNotIn(".TRAN 1m 10m", log.value())


if __name__ == "__main__":
    unittest.main()

import os
import tempfile
import unittest
from pathlib import Path

from albor_automation import AlborTestCase, expect, launch


class SimulationOutputChecks(AlborTestCase):
    # the run action drives the simulation over the loaded netlist, the output
    # panel shows the live log, and the close action hides the panel again

    def test_run_shows_the_output_log_and_close_hides_the_panel(self) -> None:
        # arrange: a fake Xyce executable the configuration points at, which
        # prints the banner, a reported warning and the completion lines
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "Xyce"
            exe.write_text(
                "#!/bin/sh\n"
                "echo '*** Running Xyce simulation ***'\n"
                "echo 'Netlist warning: parallel device count mismatch'\n"
                "echo '***** Percent complete: 100'\n"
                "echo 'Simulation completed.'\n"
            )
            os.chmod(exe, 0o755)
            netlist = Path(tmp) / "tran.cir"
            netlist.write_text(".tran 1m 10m\n.END\n")
            # arrange: launch with the configured executable and the netlist
            self.start_session(launch(args=["--xyce", str(exe), "--netlist", str(netlist)]))
            app = self._app
            # assert: the panel is absent before the first run
            expect(app.get_by_id("output_log")).not_().to_exist()
            # step 1: start the run from the toolbar
            app.get_by_id("main_run").click()
            # assert: the panel mounts and streams the whole log, which the
            # flush coalescing delivers as one value
            log = app.get_by_id("output_log")
            log.wait_for_exists()
            expect(log).to_have_value(
                "*** Running Xyce simulation ***\n"
                "Netlist warning: parallel device count mismatch\n"
                "***** Percent complete: 100\n"
                "Simulation completed.\n"
            )
            # assert: the run finished, so the action reads Run again and the
            # status reports the outcome
            expect(app.get_by_id("main_run")).to_have_name("Run")
            expect(app.get_by_id("main_status")).to_have_name("Simulation finished")
            # step 2: close the panel
            app.get_by_id("output_close").click()
            # assert: the panel unmounts
            log.wait_for_gone()

    def test_the_running_action_becomes_stop_and_cancel_reports_the_outcome(self) -> None:
        # arrange: a fake Xyce executable that keeps running until it is stopped
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "Xyce"
            exe.write_text("#!/bin/sh\nsleep 30\n")
            os.chmod(exe, 0o755)
            netlist = Path(tmp) / "tran.cir"
            netlist.write_text(".tran 1m 10m\n.END\n")
            # arrange: launch with the configured executable and the netlist
            self.start_session(launch(args=["--xyce", str(exe), "--netlist", str(netlist)]))
            app = self._app
            # step 1: start the run
            app.get_by_id("main_run").click()
            # assert: the action reads Stop while the simulation lives
            expect(app.get_by_id("main_run")).to_have_name("Stop")
            # step 2: stop the run through the same action
            app.get_by_id("main_run").click()
            # assert: the run canceled, the action reads Run again and the
            # status reports the cancel
            expect(app.get_by_id("main_run")).to_have_name("Run")
            expect(app.get_by_id("main_status")).to_have_name("Simulation canceled")


if __name__ == "__main__":
    unittest.main()

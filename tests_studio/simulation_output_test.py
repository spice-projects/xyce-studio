import os
import tempfile
import unittest
from pathlib import Path

from albor_automation import TestSession, expect, launch


class SimulationOutputChecks(unittest.TestCase):
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
            # arrange: launch the application in a session scoped to this test
            with TestSession(launch(args=["--xyce", str(exe), "--netlist", str(netlist)]), self.id()) as app:
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
            # arrange: launch the application in a session scoped to this test
            with TestSession(launch(args=["--xyce", str(exe), "--netlist", str(netlist)]), self.id()) as app:
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

    def test_status_reports_the_run_progress_while_it_is_live(self) -> None:
        # arrange: a fake Xyce executable that reports progress and then keeps
        # running, so the live state can be observed before the run ends
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "Xyce"
            exe.write_text(
                "#!/bin/sh\n"
                "echo '***** Percent complete: 40 %'\n"
                "echo '***** Estimated time to completion: 12 sec.'\n"
                "echo 'Netlist warning: parallel device count mismatch'\n"
                "exec sleep 30\n"
            )
            os.chmod(exe, 0o755)
            netlist = Path(tmp) / "tran.cir"
            netlist.write_text(".tran 1m 10m\n.END\n")
            # arrange: launch the application in a session scoped to this test
            with TestSession(launch(args=["--xyce", str(exe), "--netlist", str(netlist)]), self.id()) as app:
                # assert: the output action has nothing to reveal before the first run
                expect(app.get_by_id("main_output")).to_have_state("disabled")
                # step 1: start the run from the toolbar
                app.get_by_id("main_run").click()
                # assert: the status bar carries the reported progress and counts
                progress = app.get_by_id("main_progress")
                progress.wait_for_exists()
                expect(progress).to_have_name("40% · ETA 12 sec. · 1 warning")
                # assert: the output header shows the warning count as its own badge
                expect(app.get_by_id("output_warning_badge")).to_have_name("1 warning")
                # assert: no error was reported, so its badge stays out of the tree
                expect(app.get_by_id("output_error_badge")).not_().to_exist()
                # assert: the log is now reachable from the toolbar again
                expect(app.get_by_id("main_output")).not_().to_have_state("disabled")
                # step 2: stop the run, which folds the live indicator away
                app.get_by_id("main_run").click()
                progress.wait_for_gone()

    def test_status_reports_an_indeterminate_bar_without_a_percentage(self) -> None:
        # arrange: a fake Xyce executable whose analysis reports no completion
        # percentage, which AC, DC sweep, HB and operating-point runs do
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "Xyce"
            exe.write_text(
                "#!/bin/sh\n"
                "echo '***** Beginning DC Analysis'\n"
                "exec sleep 30\n"
            )
            os.chmod(exe, 0o755)
            netlist = Path(tmp) / "tran.cir"
            netlist.write_text(".tran 1m 10m\n.END\n")
            # arrange: launch the application in a session scoped to this test
            with TestSession(launch(args=["--xyce", str(exe), "--netlist", str(netlist)]), self.id()) as app:
                # step 1: start the run from the toolbar
                app.get_by_id("main_run").click()
                # assert: the bar sweeps because the run reports no fraction
                bar = app.get_by_id("main_progress_bar")
                bar.wait_for_exists()
                expect(bar).to_have_name("main_progress_bar")
                expect(bar).to_have_state("indeterminate")
                # assert: the status readout carries the announced analysis phase
                expect(app.get_by_id("main_progress")).to_have_name("DC Analysis")
                # step 2: stop the run, which folds the live indicator away
                app.get_by_id("main_run").click()
                bar.wait_for_gone()

    def test_sim_output_action_reopens_the_closed_panel(self) -> None:
        # arrange: a fake Xyce executable that finishes a complete log quickly
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "Xyce"
            exe.write_text(
                "#!/bin/sh\n"
                "echo '*** Running Xyce simulation ***'\n"
                "echo 'Simulation completed.'\n"
            )
            os.chmod(exe, 0o755)
            netlist = Path(tmp) / "tran.cir"
            netlist.write_text(".tran 1m 10m\n.END\n")
            # arrange: launch the application in a session scoped to this test
            with TestSession(launch(args=["--xyce", str(exe), "--netlist", str(netlist)]), self.id()) as app:
                # step 1: run to completion, which streams the whole log
                app.get_by_id("main_run").click()
                log = app.get_by_id("output_log")
                log.wait_for_exists()
                expect(log).to_have_value(
                    "*** Running Xyce simulation ***\n"
                    "Simulation completed.\n"
                )
                # step 2: close the panel, which keeps the log in the shell
                app.get_by_id("output_close").click()
                log.wait_for_gone()
                # step 3: reveal the same log from the toolbar output action
                app.get_by_id("main_output").click()
                log.wait_for_exists()
                expect(log).to_have_value(
                    "*** Running Xyce simulation ***\n"
                    "Simulation completed.\n"
                )
                # step 4: copy the whole log, which must leave the panel and its
                # content untouched
                app.get_by_id("output_copy").click()
                expect(log).to_have_value(
                    "*** Running Xyce simulation ***\n"
                    "Simulation completed.\n"
                )


if __name__ == "__main__":
    unittest.main()

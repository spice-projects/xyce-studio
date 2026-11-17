import os
import tempfile
import unittest
from pathlib import Path

from albor_automation import TestSession, expect, launch


class SimulationOutputChecks(unittest.TestCase):
    # the run action drives the simulation over the loaded netlist, the output panel surfaces on a diagnostic and stays hidden otherwise, and the close action hides the panel again

    def test_run_shows_the_output_log_and_close_hides_the_panel(self) -> None:
        # arrange: a fake Xyce executable the configuration points at, which prints the banner, a reported warning and the completion lines
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
            netlist.write_text("* run\n.tran 1m 10m\n.END\n")
            # arrange: launch the application in a session scoped to this test
            with TestSession(launch(args=["--xyce", str(exe), "--netlist", str(netlist)]), self.id()) as app:
                # assert: the panel is absent before the first run
                expect(app.get_by_id("output_log")).not_().to_exist()
                # step 1: start the run from the toolbar
                app.get_by_id("main_run").click()
                # assert: the warning surfaced the panel, which streams the whole log the flush coalescing delivers as one value
                log = app.get_by_id("output_log")
                log.wait_for_exists()
                expect(log).to_have_value(
                    "*** Running Xyce simulation ***\n"
                    "Netlist warning: parallel device count mismatch\n"
                    "***** Percent complete: 100\n"
                    "Simulation completed.\n",
                    timeout=10.0,
                )
                # assert: the run finished, so the action reads Run again and the status reports the outcome
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
            netlist.write_text("* run\n.tran 1m 10m\n.END\n")
            # arrange: launch the application in a session scoped to this test
            with TestSession(launch(args=["--xyce", str(exe), "--netlist", str(netlist)]), self.id()) as app:
                # step 1: start the run
                app.get_by_id("main_run").click()
                # assert: the action reads Stop while the simulation lives
                expect(app.get_by_id("main_run")).to_have_name("Stop")
                # step 2: stop the run through the same action
                app.get_by_id("main_run").click()
                # assert: the run canceled, the action reads Run again and the status reports the cancel
                expect(app.get_by_id("main_run")).to_have_name("Run")
                expect(app.get_by_id("main_status")).to_have_name("Simulation canceled")

    def test_status_reports_the_run_progress_while_it_is_live(self) -> None:
        # arrange: a fake Xyce executable that reports progress and then keeps running, so the live state can be observed before the run ends
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
            netlist.write_text("* run\n.tran 1m 10m\n.END\n")
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
                # assert: the warning opened the log by itself, which leaves the toolbar action disabled
                expect(app.get_by_id("output_log")).to_exist()
                expect(app.get_by_id("main_output")).to_have_state("disabled")
                # step 2: stop the run, which folds the live indicator away
                app.get_by_id("main_run").click()
                progress.wait_for_gone()

    def test_status_reports_an_indeterminate_bar_without_a_percentage(self) -> None:
        # arrange: a fake Xyce executable whose analysis reports no completion percentage, which AC, DC sweep, HB and operating-point runs do
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "Xyce"
            exe.write_text(
                "#!/bin/sh\n"
                "echo '***** Beginning DC Analysis'\n"
                "exec sleep 30\n"
            )
            os.chmod(exe, 0o755)
            netlist = Path(tmp) / "tran.cir"
            netlist.write_text("* run\n.tran 1m 10m\n.END\n")
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
            netlist.write_text("* run\n.tran 1m 10m\n.END\n")
            # arrange: launch the application in a session scoped to this test
            with TestSession(launch(args=["--xyce", str(exe), "--netlist", str(netlist)]), self.id()) as app:
                # step 1: run to completion, which streams the whole log
                app.get_by_id("main_run").click()
                log = app.get_by_id("output_log")
                log.wait_for_exists()
                expect(log).to_have_value(
                    "*** Running Xyce simulation ***\n"
                    "Simulation completed.\n",
                    timeout=10.0,
                )
                # step 2: close the panel, which keeps the log in the shell
                app.get_by_id("output_close").click()
                log.wait_for_gone()
                # step 3: reveal the same log from the toolbar output action
                app.get_by_id("main_output").click()
                log.wait_for_exists()
                expect(log).to_have_value(
                    "*** Running Xyce simulation ***\n"
                    "Simulation completed.\n",
                    timeout=10.0,
                )
                # step 4: copy the whole log, which must leave the panel and its content untouched
                app.get_by_id("output_copy").click()
                expect(log).to_have_value(
                    "*** Running Xyce simulation ***\n"
                    "Simulation completed.\n",
                    timeout=10.0,
                )

    def test_a_quiet_live_run_keeps_the_panel_hidden_and_the_action_available(self) -> None:
        # arrange: a fake Xyce that reports progress and then keeps running without a diagnostic
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "Xyce"
            exe.write_text(
                "#!/bin/sh\n"
                "echo '*** Running Xyce simulation ***'\n"
                "echo '***** Percent complete: 40 %'\n"
                "exec sleep 30\n"
            )
            os.chmod(exe, 0o755)
            netlist = Path(tmp) / "tran.cir"
            netlist.write_text("* run\n.tran 1m 10m\n.END\n")
            # arrange: launch the application in a session scoped to this test
            with TestSession(launch(args=["--xyce", str(exe), "--netlist", str(netlist)]), self.id()) as app:
                # assert: the panel is absent and its action has nothing to offer before the run
                expect(app.get_by_id("output_log")).not_().to_exist()
                expect(app.get_by_id("main_output")).to_have_state("disabled")
                # step 1: start the run
                app.get_by_id("main_run").click()
                # assert: the quiet log arrives without opening the panel
                expect(app.get_by_id("main_progress")).to_have_name("40%", timeout=10.0)
                expect(app.get_by_id("output_log")).not_().to_exist()
                # assert: the hidden log is reachable through the toolbar action
                expect(app.get_by_id("main_output")).not_().to_have_state("disabled")
                # step 2: reveal the log from the toolbar
                app.get_by_id("main_output").click()
                log = app.get_by_id("output_log")
                log.wait_for_exists()
                expect(log).to_have_value(
                    "*** Running Xyce simulation ***\n"
                    "***** Percent complete: 40 %\n",
                    timeout=10.0,
                )
                # assert: a log already on screen leaves the action disabled
                expect(app.get_by_id("main_output")).to_have_state("disabled")
                # step 3: stop the run
                app.get_by_id("main_run").click()
                expect(app.get_by_id("main_status")).to_have_name("Simulation canceled")

    def test_a_warning_during_the_run_opens_the_output_panel(self) -> None:
        # arrange: a fake Xyce that reports a warning and then keeps running
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "Xyce"
            exe.write_text(
                "#!/bin/sh\n"
                "echo 'Netlist warning: parallel device count mismatch'\n"
                "exec sleep 30\n"
            )
            os.chmod(exe, 0o755)
            netlist = Path(tmp) / "tran.cir"
            netlist.write_text("* run\n.tran 1m 10m\n.END\n")
            # arrange: launch the application in a session scoped to this test
            with TestSession(launch(args=["--xyce", str(exe), "--netlist", str(netlist)]), self.id()) as app:
                # step 1: start the run
                app.get_by_id("main_run").click()
                # assert: the warning opened the panel without a click and its log carries the line
                log = app.get_by_id("output_log")
                log.wait_for_exists()
                expect(log).to_have_value("Netlist warning: parallel device count mismatch\n", timeout=10.0)
                # assert: the log on screen leaves the toolbar action disabled
                expect(app.get_by_id("main_output")).to_have_state("disabled")
                # step 2: close the panel while the run lives, which keeps the log reachable from the toolbar
                app.get_by_id("output_close").click()
                log.wait_for_gone()
                expect(app.get_by_id("main_output")).not_().to_have_state("disabled")
                # step 3: stop the run
                app.get_by_id("main_run").click()
                expect(app.get_by_id("main_status")).to_have_name("Simulation canceled")

    def test_a_successful_run_with_results_hides_the_output_panel(self) -> None:
        # arrange: a fake Xyce that reports a warning and writes the table the charts parse, so the run produces a dataset
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "Xyce"
            exe.write_text(
                "#!/bin/sh\n"
                "echo 'Netlist warning: parallel device count mismatch'\n"
                "DIR=$(dirname \"$1\")\n"
                "cat > \"$DIR/netlist.cir.csv\" <<'EOF'\n"
                "TIME,V(1)\n"
                "0,0\n"
                "1,1\n"
                "EOF\n"
                "sleep 3\n"
                "echo 'Simulation completed.'\n"
            )
            os.chmod(exe, 0o755)
            netlist = Path(tmp) / "tran.cir"
            netlist.write_text("* plot\nR1 1 0 1k\n.TRAN 1m 10m\n.PRINT TRAN FORMAT=CSV V(1)\n.END\n")
            # arrange: launch the application in a session scoped to this test
            with TestSession(launch(args=["--xyce", str(exe), "--netlist", str(netlist)]), self.id()) as app:
                # step 1: start the run
                app.get_by_id("main_run").click()
                # assert: the warning surfaced the log during the run
                log = app.get_by_id("output_log")
                log.wait_for_exists()
                # step 2: wait for the run to deliver its dataset
                expect(app.get_by_id("main_status")).to_have_name("Simulation finished", timeout=15.0)
                # assert: the results replace the log, so the panel folds away
                log.wait_for_gone()
                # assert: the hidden log stays reachable from the toolbar action
                expect(app.get_by_id("main_output")).not_().to_have_state("disabled")


    def test_closing_a_window_mid_run_keeps_the_application_alive(self) -> None:
        # arrange: a fake Xyce that sleeps, a netlist and a dataset to spawn a window from
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "Xyce"
            exe.write_text("#!/bin/sh\nexec sleep 30\n")
            os.chmod(exe, 0o755)
            netlist = Path(tmp) / "tran.cir"
            netlist.write_text("* run\n.tran 1m 10m\n.END\n")
            dataset = Path(__file__).resolve().parent / "fixtures" / "tran-simple-01.csv"
            with TestSession(launch(args=["--xyce", str(exe), "--netlist", str(netlist), "--dataset", str(dataset)]), self.id()) as app:
                # step: start the run in the shell that owns the netlist
                app.get_by_id("charts_view").wait_for_exists()
                app.get_by_id("main_run").click()
                expect(app.get_by_id("main_run")).to_have_name("Stop")
                # step: spawn a window of its own over the dataset
                app.get_by_id("charts_view").click("chart-0-new-window")
                expect(app.get_by_id("document_tabs")).to_have_name("Documents, active: tran-simple-01")
                # step: close the spawned window, which returns the run to the shell that started it
                app.get_by_id("main_exit").click()
                expect(app.get_by_id("document_tabs")).to_have_name("Documents, active: tran-simple-01")
                # act: close the window that owns the live run
                app.get_by_id("main_exit").click()
                # assert: the run stops and the application exits cleanly instead of aborting
                app.wait_for_condition(lambda: app.is_running() is False, timeout=20.0, message="expected closing the running window to end the application")


if __name__ == "__main__":
    unittest.main()

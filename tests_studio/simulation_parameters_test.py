import tempfile
import unittest
from pathlib import Path

from albor_automation import TestSession, expect, launch


class SimulationParametersDialogChecks(unittest.TestCase):
    # the native simulation parameters dialog is seeded from the netlist, rewrites
    # the accepted analysis, and refuses an invalid analysis without closing

    def test_transient_netlist_seeds_the_dialog(self) -> None:
        # arrange: a netlist carrying one transient analysis
        with tempfile.TemporaryDirectory() as tmp:
            netlist = Path(tmp) / "tran.cir"
            netlist.write_text("R1 1 0 1k\n.TRAN 1m 10m\n.END\n")
            # arrange: launch the application in a session scoped to this test
            with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
                # step 1: open the simulation parameters dialog
                app.get_by_id("main_configure").click()
                # assert: the transient tab is selected and carries the netlist values
                initial = app.get_by_id("simulation_tran_initial_step")
                initial.wait_for_exists()
                expect(initial).to_have_value("1m")
                expect(app.get_by_id("simulation_tran_final_time")).to_have_value("10m")
                expect(app.get_by_id("simulation_tab_transient")).to_have_state("selected")

    def test_accept_rewrites_the_transient_directive(self) -> None:
        # arrange: a netlist carrying the first timing pair
        with tempfile.TemporaryDirectory() as tmp:
            netlist = Path(tmp) / "tran.cir"
            netlist.write_text("R1 1 0 1k\n.TRAN 1m 10m\n.END\n")
            # arrange: launch the application in a session scoped to this test
            with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
                # step 1: open the dialog and write the accepted timing
                app.get_by_id("main_configure").click()
                initial = app.get_by_id("simulation_tran_initial_step")
                initial.wait_for_exists()
                initial.fill("2m")
                app.get_by_id("simulation_tran_final_time").fill("20m")
                # step 2: accept the dialog
                app.get_by_id("simulation_parameters_ok").click()
                initial.wait_for_gone()
                # assert: the editor carries the accepted directive and the save action is live
                editor = app.get_by_id("netlist_editor")
                app.wait_for_condition(lambda: ".TRAN 2m 20m" in editor.value(), timeout=5.0, message="expected the editor to carry the accepted directive")
                expect(app.get_by_id("main_save")).not_().to_have_state("disabled")

    def test_invalid_final_time_reports_the_error_and_keeps_the_dialog_open(self) -> None:
        # arrange: a netlist carrying one transient analysis
        with tempfile.TemporaryDirectory() as tmp:
            netlist = Path(tmp) / "tran.cir"
            netlist.write_text("R1 1 0 1k\n.TRAN 1m 10m\n.END\n")
            # arrange: launch the application in a session scoped to this test
            with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
                # step 1: clear the final time and accept
                app.get_by_id("main_configure").click()
                final = app.get_by_id("simulation_tran_final_time")
                final.wait_for_exists()
                final.fill("")
                app.get_by_id("simulation_parameters_ok").click()
                # assert: the validation message appears and the dialog stays open
                error = app.get_by_id("simulation_parameters_error")
                error.wait_for_exists()
                expect(error).to_have_name("Final time is required")
                self.assertTrue(app.get_by_id("simulation_parameters_ok").exists())

    def test_cancel_discards_the_edit(self) -> None:
        # arrange: a netlist carrying the original timing
        with tempfile.TemporaryDirectory() as tmp:
            netlist = Path(tmp) / "tran.cir"
            original = "R1 1 0 1k\n.TRAN 1m 10m\n.END\n"
            netlist.write_text(original)
            # arrange: launch the application in a session scoped to this test
            with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
                # step 1: edit the timing and cancel the dialog
                app.get_by_id("main_configure").click()
                initial = app.get_by_id("simulation_tran_initial_step")
                initial.wait_for_exists()
                initial.fill("9m")
                app.get_by_id("simulation_parameters_cancel").click()
                initial.wait_for_gone()
                # assert: the editor still carries the original netlist
                expect(app.get_by_id("netlist_editor")).to_have_value(original)

    def test_dc_tab_reports_the_missing_sweep_variable(self) -> None:
        # arrange: a netlist with no analysis, which opens on the transient tab
        with tempfile.TemporaryDirectory() as tmp:
            netlist = Path(tmp) / "bare.cir"
            netlist.write_text("R1 1 0 1k\n.END\n")
            # arrange: launch the application in a session scoped to this test
            with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
                # step 1: open the dialog and switch to the DC tab
                app.get_by_id("main_configure").click()
                app.get_by_id("simulation_parameters_tabs").wait_for_exists()
                app.get_by_id("simulation_parameters_tabs").click("select_next")
                # step 2: accept the empty linear sweep
                variable = app.get_by_id("simulation_dc_variable")
                variable.wait_for_exists()
                app.get_by_id("simulation_parameters_ok").click()
                # assert: the core sweep validation is the message the dialog shows
                error = app.get_by_id("simulation_parameters_error")
                error.wait_for_exists()
                expect(error).to_have_name("DC LIN sweep: sweep variable is required")

    def test_ac_netlist_seeds_the_ac_tab_and_validates_the_points(self) -> None:
        # arrange: a netlist carrying one AC analysis
        with tempfile.TemporaryDirectory() as tmp:
            netlist = Path(tmp) / "ac.cir"
            netlist.write_text("V1 1 0 AC 1\n.AC DEC 10 1 1e6\n.END\n")
            # arrange: launch the application in a session scoped to this test
            with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
                # step 1: open the dialog, which starts on the AC tab
                app.get_by_id("main_configure").click()
                points = app.get_by_id("simulation_ac_points")
                points.wait_for_exists()
                expect(app.get_by_id("simulation_tab_ac")).to_have_state("selected")
                expect(points).to_have_value("10")
                expect(app.get_by_id("simulation_ac_start")).to_have_value("1")
                expect(app.get_by_id("simulation_ac_end")).to_have_value("1e6")
                # step 2: clear the points and accept
                points.fill("")
                app.get_by_id("simulation_parameters_ok").click()
                # assert: the dialog reports the missing value and stays open
                error = app.get_by_id("simulation_parameters_error")
                error.wait_for_exists()
                expect(error).to_have_name("Points is required")


if __name__ == "__main__":
    unittest.main()

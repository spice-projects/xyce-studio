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



class SimulationParametersPanelsChecks(unittest.TestCase):
    # the dialog carries one panel per analysis, plus the print, measure, IC,
    # PCE and .OPTIONS sections the reference dialog exposes

    def test_op_panel_seeds_and_round_trips(self) -> None:
        # arrange: a netlist with an operating point, a nodeset and a save
        with tempfile.TemporaryDirectory() as tmp:
            netlist = Path(tmp) / "op.cir"
            netlist.write_text("op test\nR1 1 0 1k\n.OP\n.NODESET V(1)=5\n.SAVE TYPE=NODESET FILE=op.ns LEVEL=all\n.END\n")
            # arrange: launch the application in a session scoped to this test
            with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
                # step 1: open the dialog, which starts on the OP tab
                app.get_by_id("main_configure").click()
                nodeset = app.get_by_id("simulation_op_nodeset")
                nodeset.wait_for_exists()
                expect(app.get_by_id("simulation_tab_op")).to_have_state("selected")
                expect(nodeset).to_have_value("V(1)=5")
                # step 2: accept the untouched panel
                app.get_by_id("simulation_parameters_ok").click()
                nodeset.wait_for_gone()
                # assert: the OP, nodeset and save directives survive
                lines = app.get_by_id("netlist_editor").value().splitlines()
                expected = [".OP", ".NODESET V(1)=5", ".SAVE TYPE=NODESET FILE=op.ns LEVEL=all"]
                for directive in expected:
                    self.assertIn(directive, lines)

    def test_noise_panel_seeds_and_round_trips(self) -> None:
        # arrange: a netlist carrying one noise analysis
        with tempfile.TemporaryDirectory() as tmp:
            netlist = Path(tmp) / "noise.cir"
            netlist.write_text("noise test\nV1 1 0 AC 1\nR1 1 0 1K\n.NOISE V(1) V1 DEC 10 1 100MEG\n.END\n")
            # arrange: launch the application in a session scoped to this test
            with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
                # step 1: open the dialog, which starts on the NOISE tab
                app.get_by_id("main_configure").click()
                output = app.get_by_id("simulation_noise_output_node")
                output.wait_for_exists()
                expect(app.get_by_id("simulation_tab_noise")).to_have_state("selected")
                expect(app.get_by_id("simulation_noise_start_freq")).to_have_value("1")
                expect(app.get_by_id("simulation_noise_num_points")).to_have_value("10")
                # step 2: accept the untouched panel
                app.get_by_id("simulation_parameters_ok").click()
                output.wait_for_gone()
                # assert: the noise directive survives
                lines = app.get_by_id("netlist_editor").value().splitlines()
                self.assertIn(".NOISE V(1) V1 DEC 10 1 100MEG", lines)

    def test_hb_panel_seeds_and_round_trips(self) -> None:
        # arrange: a netlist carrying one harmonic balance analysis
        with tempfile.TemporaryDirectory() as tmp:
            netlist = Path(tmp) / "hb.cir"
            netlist.write_text("hb test\nV1 1 0 SIN(0 1 1MEG)\nR1 1 0 1K\n.HB 1MEG\n.END\n")
            # arrange: launch the application in a session scoped to this test
            with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
                # step 1: open the dialog, which starts on the HB tab
                app.get_by_id("main_configure").click()
                frequencies = app.get_by_id("simulation_hb_frequencies")
                frequencies.wait_for_exists()
                expect(app.get_by_id("simulation_tab_hb")).to_have_state("selected")
                expect(frequencies).to_have_value("1MEG")
                # step 2: accept the untouched panel
                app.get_by_id("simulation_parameters_ok").click()
                frequencies.wait_for_gone()
                # assert: the harmonic balance directive survives
                lines = app.get_by_id("netlist_editor").value().splitlines()
                self.assertIn(".HB 1MEG", lines)

    def test_lin_panel_seeds_and_round_trips(self) -> None:
        # arrange: a netlist carrying one linear analysis
        with tempfile.TemporaryDirectory() as tmp:
            netlist = Path(tmp) / "lin.cir"
            netlist.write_text("lin test\nP1 port1 0 port=1 z0=50 AC 1\nL1 port1 port2 25nH\nC1 port2 0 10pF\nP2 port2 0 port=2 z0=100\n.LIN SPARCALC=1 FORMAT=TOUCHSTONE2 LINTYPE=S DATAFORMAT=RI FILE=t.s2p WIDTH=16 PRECISION=8\n.END\n")
            # arrange: launch the application in a session scoped to this test
            with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
                # step 1: open the dialog, which starts on the LIN tab
                app.get_by_id("main_configure").click()
                file = app.get_by_id("simulation_lin_file")
                file.wait_for_exists()
                expect(app.get_by_id("simulation_tab_lin")).to_have_state("selected")
                expect(file).to_have_value("t.s2p")
                expect(app.get_by_id("simulation_lin_width")).to_have_value("16")
                expect(app.get_by_id("simulation_lin_precision")).to_have_value("8")
                # step 2: accept the untouched panel
                app.get_by_id("simulation_parameters_ok").click()
                file.wait_for_gone()
                # assert: the linear directive survives
                lines = app.get_by_id("netlist_editor").value().splitlines()
                self.assertIn(".LIN SPARCALC=1 FORMAT=TOUCHSTONE2 LINTYPE=S DATAFORMAT=RI FILE=t.s2p WIDTH=16 PRECISION=8", lines)

    def test_pce_section_round_trips(self) -> None:
        # arrange: a transient netlist with a PCE parameter and its print
        with tempfile.TemporaryDirectory() as tmp:
            netlist = Path(tmp) / "pce.cir"
            netlist.write_text("pce test\nR1 1 0 1k\n.TRAN 1m 10m\n.PCE param=R1 type=normal means=3K std_deviations=1K\n.PRINT PCE V(1)\n.END\n")
            # arrange: launch the application in a session scoped to this test
            with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
                # step 1: open the dialog and read the PCE text
                app.get_by_id("main_configure").click()
                parameters = app.get_by_id("simulation_pce_parameters")
                parameters.wait_for_exists()
                expect(parameters).to_have_value("R1, normal, 3K, 1K")
                # step 2: accept the untouched panel
                app.get_by_id("simulation_parameters_ok").click()
                parameters.wait_for_gone()
                # assert: the PCE parameter and its print survive
                lines = app.get_by_id("netlist_editor").value().splitlines()
                self.assertIn(".PCE param=R1 type=normal means=3K std_deviations=1K", lines)
                self.assertIn(".PRINT PCE V(1)", lines)

    def test_option_packages_and_companion_sections_round_trip(self) -> None:
        # arrange: a transient netlist carrying a print, a measure, IC and TIMEINT
        with tempfile.TemporaryDirectory() as tmp:
            netlist = Path(tmp) / "companions.cir"
            netlist.write_text("companions test\nV1 1 0 1\nR1 1 0 1k\n.TRAN 1m 10m\n.PRINT TRAN V(1)\n.MEASURE TRAN vout_max MAX V(1)\n.IC V(1)=0\n.OPTIONS TIMEINT RELTOL=1e-3\n.END\n")
            # arrange: launch the application in a session scoped to this test
            with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
                # step 1: open the dialog and accept the untouched panel
                app.get_by_id("main_configure").click()
                initial = app.get_by_id("simulation_tran_initial_step")
                initial.wait_for_exists()
                app.get_by_id("simulation_parameters_ok").click()
                initial.wait_for_gone()
                # assert: the companion directives survive the round trip
                lines = app.get_by_id("netlist_editor").value().splitlines()
                self.assertIn(".PRINT TRAN V(1)", lines)
                self.assertIn(".MEASURE TRAN vout_max MAX V(1)", lines)
                self.assertIn(".IC V(1)=0", lines)
                self.assertIn(".OPTIONS TIMEINT RELTOL=1e-3", lines)

if __name__ == "__main__":
    unittest.main()

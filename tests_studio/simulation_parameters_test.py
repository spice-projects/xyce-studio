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
                app.get_by_id("simulation_tab_dc").click()
                # step 2: accept the empty sweep table
                app.get_by_id("simulation_dc_add_sweep").wait_for_exists()
                app.get_by_id("simulation_parameters_ok").click()
                # assert: the empty table names the missing sweep and the dialog stays open
                error = app.get_by_id("simulation_parameters_error")
                expect(error).to_have_name("A DC sweep requires at least one sweep variable")
                self.assertTrue(app.get_by_id("simulation_parameters_ok").exists())

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
                expect(points).to_have_value("10")
                expect(app.get_by_id("simulation_ac_start")).to_have_value("1")
                expect(app.get_by_id("simulation_ac_end")).to_have_value("1e6")
                # step 2: clear the points and accept
                points.fill("")
                app.get_by_id("simulation_parameters_ok").click()
                # assert: the dialog reports the missing value and stays open
                error = app.get_by_id("simulation_parameters_error")
                expect(error).to_have_name("Points is required")
                self.assertTrue(app.get_by_id("simulation_parameters_ok").exists())



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
                # step 1: open the dialog and check the table seeded from the netlist
                app.get_by_id("main_configure").click()
                add = app.get_by_id("simulation_pce_add_parameter")
                add.wait_for_exists()
                app.wait_for_condition(lambda: "Parameter 1" in app.tree_text(), timeout=5.0, message="expected the PCE table to seed a row")
                # step 2: add a row and accept the panel
                add.click()
                app.wait_for_condition(lambda: "Parameter 2" in app.tree_text(), timeout=5.0, message="expected the PCE table to add a row")
                app.get_by_id("simulation_parameters_ok").click()
                add.wait_for_gone()
                # assert: the stored PCE parameter and its print survive
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
                app.wait_for_condition(lambda: "RELTOL" in app.tree_text(), timeout=5.0, message="expected the TIMEINT card to show the RELTOL row")
                app.get_by_id("simulation_parameters_ok").click()
                initial.wait_for_gone()
                # assert: the companion directives survive the round trip
                lines = app.get_by_id("netlist_editor").value().splitlines()
                self.assertIn(".PRINT TRAN V(1)", lines)
                self.assertIn(".MEASURE TRAN vout_max MAX V(1)", lines)
                self.assertIn(".IC V(1)=0", lines)
                self.assertIn(".OPTIONS TIMEINT RELTOL=1e-3", lines)
    def test_dc_sweep_table_seeds_and_adds_a_row(self) -> None:
        # arrange: a netlist carrying one linear DC sweep
        with tempfile.TemporaryDirectory() as tmp:
            netlist = Path(tmp) / "dc.cir"
            netlist.write_text("dc test\nR1 1 0 1k\nV1 1 0 1\n.DC LIN V1 0 5 0.1\n.END\n")
            # arrange: launch the application in a session scoped to this test
            with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
                # step 1: open the dialog and switch to the DC tab
                app.get_by_id("main_configure").click()
                app.get_by_id("simulation_tab_dc").click()
                app.get_by_id("simulation_dc_add_sweep").wait_for_exists()
                # assert: the seeded sweep is one editable row card
                app.wait_for_condition(lambda: "Sweep 1" in app.tree_text() and "V1" in app.tree_text(), timeout=5.0, message="expected the DC sweep row to seed")
                # step 2: add another row
                app.get_by_id("simulation_dc_add_sweep").click()
                # assert: the table materializes the second row
                app.wait_for_condition(lambda: "Sweep 2" in app.tree_text(), timeout=5.0, message="expected the DC sweep table to add a row")
                # step 3: cancel, which leaves the editor untouched
                app.get_by_id("simulation_parameters_cancel").click()
                app.get_by_id("simulation_dc_add_sweep").wait_for_gone()
                self.assertEqual([line for line in app.get_by_id("netlist_editor").value().splitlines() if line.strip().startswith(".DC")], [".DC LIN V1 0 5 0.1"])

    def test_dialog_keeps_one_scroll_page_and_no_inline_close(self) -> None:
        # arrange: a netlist carrying one transient analysis
        with tempfile.TemporaryDirectory() as tmp:
            netlist = Path(tmp) / "tran.cir"
            netlist.write_text("R1 1 0 1k\n.TRAN 1m 10m\n.END\n")
            # arrange: launch the application in a session scoped to this test
            with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
                # step 1: open the dialog
                app.get_by_id("main_configure").click()
                page = app.get_by_id("simulation_parameters_page")
                page.wait_for_exists()
                # assert: the page moves twice, which a one-off repaint cannot explain
                before = app.client().screenshot()
                page.click("scroll_down")
                app.wait_for_condition(lambda: app.client().screenshot() != before, timeout=5.0, message="expected the page to scroll")
                scrolled = app.client().screenshot()
                page.click("scroll_down")
                app.wait_for_condition(lambda: app.client().screenshot() != scrolled, timeout=5.0, message="expected the page to scroll again")
                # assert: the window chrome owns the close, so the form carries no second one
                self.assertFalse(app.get_by_id("simulation_parameters_close").exists())
                # step 2: close through the actions the dialog keeps
                app.get_by_id("simulation_parameters_cancel").click()
                page.wait_for_gone()


if __name__ == "__main__":
    unittest.main()

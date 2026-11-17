import tempfile
import unittest
from pathlib import Path

from albor_automation import TestSession, expect, launch


class InitialConditionsChecks(unittest.TestCase):
    # the shared initial conditions field merges and validates the .IC lines

    def test_two_ic_statements_merge_into_one_line(self) -> None:
        # arrange: a transient netlist carrying two .IC statements
        with tempfile.TemporaryDirectory() as tmp:
            netlist = Path(tmp) / "ic-transient.cir"
            netlist.write_text("* IC transient test\nR1 a 0 1k\nR2 b 0 1k\nR3 a b 1k\n.IC V(a)=2\n.IC V(b)=10\n.TRAN 1u 10u\n.END\n")
            with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
                # step: open the transient page of the parameters dialog
                app.get_by_id("main_configure").click()
                ic = app.get_by_id("simulation_ic")
                ic.wait_for_exists()
                # assert: both statements appear on one line without the .IC prefix
                expect(ic).to_have_value("V(a)=2 V(b)=10")
                # act: edit the merged line and accept
                ic.fill("V(a)=3 V(b)=10")
                app.get_by_id("simulation_parameters_ok").click()
                ic.wait_for_gone()
                # assert: the editor holds one rewritten .IC line
                editor = app.get_by_id("netlist_editor")
                editor.wait_for_exists()
                app.wait_for_condition(lambda: ".IC V(a)=3 V(b)=10" in editor.value(), timeout=5.0, message="expected the accepted initial conditions")
                ic_lines = [line.strip() for line in editor.value().splitlines() if line.strip().startswith(".IC")]
                self.assertEqual(ic_lines, [".IC V(a)=3 V(b)=10"])

    def test_unparseable_ic_text_keeps_the_dialog_open(self) -> None:
        # arrange: a transient netlist carrying one .IC statement
        with tempfile.TemporaryDirectory() as tmp:
            netlist = Path(tmp) / "ic-invalid.cir"
            netlist.write_text("* IC validation test\nR1 a 0 1k\n.IC V(a)=2\n.TRAN 1u 10u\n.END\n")
            with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
                # step: replace the initial conditions with text that parses to nothing
                app.get_by_id("main_configure").click()
                ic = app.get_by_id("simulation_ic")
                ic.wait_for_exists()
                ic.fill("garbage")
                # act: accept
                app.get_by_id("simulation_parameters_ok").click()
                # assert: the dialog stays open and names the problem
                app.get_by_id("simulation_parameters_ok").wait_for_exists()
                expect(app.get_by_id("simulation_parameters_error")).to_have_name("Invalid initial conditions")
                # step: dismiss the dialog
                app.get_by_id("simulation_parameters_cancel").click()
                app.get_by_id("simulation_parameters_ok").wait_for_gone()

    def test_the_ac_page_keeps_the_ic_line(self) -> None:
        # arrange: an AC netlist carrying an .IC line, which the AC page does not edit
        with tempfile.TemporaryDirectory() as tmp:
            netlist = Path(tmp) / "ac-ic.cir"
            netlist.write_text("* AC IC test\nR1 1 0 1k\nV1 1 0 AC 1\n.IC V(a)=2\n.AC DEC 10 1 1e6\n.END\n")
            with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
                # act: accept the untouched AC page
                app.get_by_id("main_configure").click()
                app.get_by_id("simulation_parameters_ok").click()
                app.get_by_id("simulation_parameters_ok").wait_for_gone()
                # assert: the initial conditions line survives the rewrite
                editor = app.get_by_id("netlist_editor")
                editor.wait_for_exists()
                ic_lines = [line.strip() for line in editor.value().splitlines() if line.strip().startswith(".IC")]
                self.assertEqual(ic_lines, [".IC V(a)=2"])

    def test_the_dc_page_round_trips_the_ic_line(self) -> None:
        # arrange: a DC sweep netlist carrying an .IC line
        with tempfile.TemporaryDirectory() as tmp:
            netlist = Path(tmp) / "dc-ic.cir"
            netlist.write_text("* DC IC test\nR1 1 0 1k\nV1 1 0 1\n.IC V(1)=2\n.DC LIN V1 0 5 0.1\n.END\n")
            with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
                # step: open the DC page, which seeds the shared initial conditions field
                app.get_by_id("main_configure").click()
                ic = app.get_by_id("simulation_ic")
                ic.wait_for_exists()
                expect(ic).to_have_value("V(1)=2")
                # act: edit the line and accept
                ic.fill("V(1)=3")
                app.get_by_id("simulation_parameters_ok").click()
                ic.wait_for_gone()
                # assert: the editor holds the accepted line
                editor = app.get_by_id("netlist_editor")
                editor.wait_for_exists()
                app.wait_for_condition(lambda: ".IC V(1)=3" in editor.value(), timeout=5.0, message="expected the accepted DC initial conditions")
                ic_lines = [line.strip() for line in editor.value().splitlines() if line.strip().startswith(".IC")]
                self.assertEqual(ic_lines, [".IC V(1)=3"])


if __name__ == "__main__":
    unittest.main()

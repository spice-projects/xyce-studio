import tempfile
import unittest
from pathlib import Path

from albor_automation import TestSession, expect, launch


class OptionsDialogChecks(unittest.TestCase):
    # the options editor writes only the simulation-agnostic .OPTIONS values it accepted

    def test_the_editor_writes_an_accepted_option_and_keeps_the_analysis(self) -> None:
        # arrange: a netlist carrying one device option and a transient analysis
        with tempfile.TemporaryDirectory() as tmp:
            netlist = Path(tmp) / "options.cir"
            netlist.write_text("R1 1 0 1k\nV1 1 0 5\n.TRAN 1m 10m\n.PRINT TRAN V(1)\n.PLOT TRAN V(1)\n.OPTIONS DEVICE GMIN=1e-12\n.END\n")
            with TestSession(launch(args=["--xyce", "/bin/echo", "--netlist", str(netlist)]), self.id()) as app:
                # step: open the options editor from the toolbar
                app.get_by_id("main_options").click()
                field = app.get_by_id("options_row_DEVICE_GMIN")
                field.wait_for_exists()
                # assert: the row opens on the value the netlist carries
                expect(field).to_have_value("1e-12")
                # act: raise the device conductance and accept
                field.fill("1e-9")
                app.get_by_id("options_ok").click()
                app.get_by_id("options_ok").wait_for_gone()
                # assert: the editor carries the accepted option and keeps the analysis
                editor = app.get_by_id("netlist_editor")
                editor.wait_for_exists()
                app.wait_for_condition(lambda: "GMIN=1e-9" in editor.value(), timeout=5.0, message="expected the editor to carry the accepted option")
                self.assertNotIn("GMIN=1e-12", editor.value())
                # assert: the directives the editor does not own survive untouched
                self.assertIn(".TRAN 1m 10m", editor.value())
                self.assertIn(".PRINT TRAN V(1)", editor.value())
                self.assertIn(".PLOT TRAN V(1)", editor.value())
                self.assertNotIn(".PREPROCESS", editor.value())

    def test_cancel_leaves_the_netlist_untouched(self) -> None:
        # arrange: a netlist carrying one device option
        with tempfile.TemporaryDirectory() as tmp:
            netlist = Path(tmp) / "options.cir"
            netlist.write_text("R1 1 0 1k\n.OPTIONS DEVICE GMIN=1e-12\n.END\n")
            with TestSession(launch(args=["--xyce", "/bin/echo", "--netlist", str(netlist)]), self.id()) as app:
                # step: open the editor and change the value
                app.get_by_id("main_options").click()
                field = app.get_by_id("options_row_DEVICE_GMIN")
                field.wait_for_exists()
                field.fill("1e-9")
                # act: dismiss the dialog without accepting
                app.get_by_id("options_cancel").click()
                app.get_by_id("options_cancel").wait_for_gone()
                # assert: the netlist keeps the value the file carries
                editor = app.get_by_id("netlist_editor")
                editor.wait_for_exists()
                self.assertIn("GMIN=1e-12", editor.value())
                self.assertNotIn("GMIN=1e-9", editor.value())
                # step: reopen the editor, which seeds the row from the netlist again
                app.get_by_id("main_options").click()
                expect(app.get_by_id("options_row_DEVICE_GMIN")).to_have_value("1e-12")
                app.get_by_id("options_cancel").click()
                app.get_by_id("options_cancel").wait_for_gone()

    def test_a_documented_choice_replaces_a_bare_flag(self) -> None:
        # arrange: a netlist carrying a documented choice key as a bare flag
        with tempfile.TemporaryDirectory() as tmp:
            netlist = Path(tmp) / "options.cir"
            netlist.write_text("R1 1 0 1k\n.OPTIONS DEVICE VOLTLIM\n.END\n")
            with TestSession(launch(args=["--xyce", "/bin/echo", "--netlist", str(netlist)]), self.id()) as app:
                # step: open the editor and move the choice off the flag marker
                app.get_by_id("main_options").click()
                combo = app.get_by_id("options_row_DEVICE_VOLTLIM")
                combo.wait_for_exists()
                expect(combo).to_have_name("VOLTLIM")
                combo.click("select_next")
                combo.click("activate")
                # act: accept the chosen value
                app.get_by_id("options_ok").click()
                app.get_by_id("options_ok").wait_for_gone()
                # assert: the editor writes the documented value over the bare flag
                editor = app.get_by_id("netlist_editor")
                editor.wait_for_exists()
                app.wait_for_condition(lambda: "VOLTLIM=0" in editor.value(), timeout=5.0, message="expected the editor to carry the chosen value")
                self.assertNotIn("VOLTLIM\n", editor.value())


if __name__ == "__main__":
    unittest.main()

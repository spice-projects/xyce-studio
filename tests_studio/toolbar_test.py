import unittest
from pathlib import Path

from albor_automation import TestSession, expect, launch


class ToolbarChecks(unittest.TestCase):
    # the toolbar follows the document state, as the legacy toolbar does

    def test_loading_a_netlist_enables_the_document_actions(self) -> None:
        # arrange: a netlist the command line opens
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "tran-simple-01.cir"
        with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
            editor = app.get_by_id("netlist_editor")
            editor.wait_for_exists()
            # assert: a loaded document accepts open, run and configure and has nothing to save
            expect(app.get_by_id("main_open")).not_().to_have_state("disabled")
            expect(app.get_by_id("main_save")).to_have_state("disabled")
            expect(app.get_by_id("main_run")).not_().to_have_state("disabled")
            expect(app.get_by_id("main_configure")).not_().to_have_state("disabled")
            # act: edit the document
            editor.fill(netlist.read_text() + "* toolbar scenario\n")
            # assert: the edit enables the save action
            expect(app.get_by_id("main_save")).not_().to_have_state("disabled")


if __name__ == "__main__":
    unittest.main()

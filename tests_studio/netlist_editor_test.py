import shutil
import tempfile
import unittest
from pathlib import Path

from albor_automation import AlborTestCase, expect, launch


class NetlistEditorChecks(AlborTestCase):

    def test_untitled_editor_starts_clean_and_empty(self) -> None:
        # arrange: launch the application with no netlist requested
        self.start_session(launch())
        app = self._app
        # arrange: locate the editor and the save tool
        editor = app.get_by_id("netlist_editor")
        editor.wait_for_exists()
        save = app.get_by_id("main_save")
        save.wait_for_exists()
        # assert: the untitled editor holds nothing and save is disabled
        expect(editor).to_have_value("")
        expect(save).to_have_state("disabled")

    def test_edit_and_save_netlist_round_trip(self) -> None:
        # arrange: resolve the sample netlist shipped with the repository
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "tran-simple-01.cir"
        # arrange: work on a scratch copy so the scenario never mutates the fixture
        with tempfile.TemporaryDirectory() as scratch:
            working_copy = Path(scratch) / "tran-simple-01.cir"
            shutil.copy(netlist, working_copy)
            # arrange: launch the application with the working copy
            self.start_session(launch(args=["--netlist", str(working_copy)]))
            app = self._app
            # arrange: locate the netlist editor and the save tool
            editor = app.get_by_id("netlist_editor")
            editor.wait_for_exists()
            save = app.get_by_id("main_save")
            # assert: the editor shows the netlist file content and the state is clean
            expect(editor).to_have_value(netlist.read_text())
            expect(save).to_have_state("disabled")
            # step 1: type an edit into the editor
            edited = netlist.read_text() + "* typed by the scenario\n"
            editor.fill(edited)
            # assert: the edit marks the netlist dirty and enables save
            expect(editor).to_have_value(edited)
            expect(save).not_().to_have_state("disabled")
            # step 2: save the netlist from the toolbar
            save.click()
            # assert: the file on disk received the typed line
            app.wait_for_condition(lambda: working_copy.read_text() == edited, timeout=5.0, message="expected the save to write the edited netlist")
            # assert: the save tool is disabled again after the save
            expect(save).to_have_state("disabled")

    def test_reopened_netlist_starts_clean(self) -> None:
        # arrange: resolve the sample netlist shipped with the repository
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "tran-simple-01.cir"
        # arrange: work on a scratch copy so the scenario never mutates the fixture
        with tempfile.TemporaryDirectory() as scratch:
            working_copy = Path(scratch) / "tran-simple-01.cir"
            shutil.copy(netlist, working_copy)
            # arrange: launch reading the working copy
            self.start_session(launch(args=["--netlist", str(working_copy)]))
            app = self._app
            editor = app.get_by_id("netlist_editor")
            editor.wait_for_exists()
            # step 1: edit and save, which leaves a dirty marker until it is written
            edited = netlist.read_text() + "* saved by the scenario\n"
            editor.fill(edited)
            app.get_by_id("main_save").click()
            app.wait_for_condition(lambda: working_copy.read_text() == edited, timeout=5.0, message="expected the save to write the edited netlist")
            # step 2: relaunch with the same file
            self.start_session(launch(args=["--netlist", str(working_copy)]))
            app = self._app
            # assert: the relaunched editor holds the saved content and starts clean
            editor = app.get_by_id("netlist_editor")
            editor.wait_for_exists()
            expect(editor).to_have_value(edited)
            expect(app.get_by_id("main_save")).to_have_state("disabled")


if __name__ == "__main__":
    unittest.main()

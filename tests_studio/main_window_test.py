import unittest

from albor_automation import TestSession, expect, launch


class MainWindowShellChecks(unittest.TestCase):
    # the shell toolbar owns the window lifecycle actions, which the presenter
    # does not mediate

    def test_toolbar_actions_announce_their_purpose(self) -> None:
        # arrange: launch the application in a session scoped to this test
        with TestSession(launch(), self.id()) as app:
            # assert: every icon-only action announces its tooltip as its name
            expect(app.get_by_id("main_open")).to_have_name("Open")
            expect(app.get_by_id("main_save")).to_have_name("Save")
            expect(app.get_by_id("main_charts")).to_have_name("Charts")
            expect(app.get_by_id("main_output")).to_have_name("Sim Output")
            expect(app.get_by_id("main_plugin_settings")).to_have_name("Plugin Settings")
            expect(app.get_by_id("main_exit")).to_have_name("Exit")
            # assert: the run action keeps its caption, which swaps to Stop live
            expect(app.get_by_id("main_run")).to_have_name("Run")

    def test_exit_action_closes_the_shell(self) -> None:
        # arrange: launch the application in a session scoped to this test
        with TestSession(launch(), self.id()) as app:
            # arrange: locate the exit action on the toolbar
            exit_action = app.get_by_id("main_exit")
            expect(exit_action).to_exist()
            # step 1: press the exit action
            exit_action.click()
            # assert: the shell window closes, which ends the process
            app.wait_for_condition(lambda: app.is_running() is False, timeout=10.0, message="expected the exit action to close the shell")


if __name__ == "__main__":
    unittest.main()

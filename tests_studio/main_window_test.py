import unittest

from albor_automation import TestSession, expect, launch


class MainWindowShellChecks(unittest.TestCase):
    # the shell toolbar owns the window lifecycle actions, which the presenter
    # does not mediate

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

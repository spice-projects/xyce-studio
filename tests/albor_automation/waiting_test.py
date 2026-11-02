import os
import unittest

import albor_automation
from albor_automation.errors import AlborAssertionError
from albor_automation.waiting import reports_test_frames, wait_for

# the framework package root, which is the only directory the trimmed
# traceback must never keep frames from
_FRAMEWORK_ROOT = os.path.dirname(os.path.abspath(albor_automation.__file__))


class WaitingChecks(unittest.TestCase):

    def test_wait_for_returns_when_condition_holds(self) -> None:
        # the first poll already satisfies the condition, so the wait returns
        wait_for(lambda: True, timeout=0.2, timeout_message="should not fail")

    def test_wait_for_times_out_with_message_and_state(self) -> None:
        # the condition never holds, so the wait fails with the caller message,
        # the last observed state and the waited duration
        with self.assertRaises(AlborAssertionError) as context:
            wait_for(lambda: False, timeout=0.2, poll_interval=0.05, timeout_message="waiting for the dialog")
        # assert
        self.assertIn("waiting for the dialog", str(context.exception))
        self.assertIn("last observed: unknown", str(context.exception))
        self.assertIn("waited: 0.2 seconds", str(context.exception))

    def test_wait_for_reports_the_observed_state(self) -> None:
        # the observer supplies the state the failure message shows
        with self.assertRaises(AlborAssertionError) as context:
            wait_for(lambda: False, timeout=0.2, poll_interval=0.05, timeout_message="waiting for ready state", observe=lambda: "state ready")
        self.assertIn("last observed: state ready", str(context.exception))

    def test_wait_for_reports_unknown_when_the_observer_fails(self) -> None:
        # an observer that raises must never mask the timeout failure
        with self.assertRaises(AlborAssertionError) as context:
            wait_for(lambda: False, timeout=0.2, poll_interval=0.05, timeout_message="waiting for the panel", observe=lambda: 1 / 0)
        self.assertIn("last observed: unknown", str(context.exception))

    def test_wait_for_failure_reports_only_test_frames(self) -> None:
        # the raised error carries a traceback whose frames outside the
        # framework are kept, which is what keeps the report readable
        # assertRaises stores the exception without a traceback, so the
        # frames are inspected through a direct catch
        try:
            wait_for(lambda: False, timeout=0.2, poll_interval=0.05, timeout_message="frame check")
            self.fail("wait_for must raise on timeout")
        except AlborAssertionError as error:
            # collect every frame filename in the traceback chain
            filenames = []
            entry = error.__traceback__
            while entry is not None:
                filenames.append(entry.tb_frame.f_code.co_filename)
                entry = entry.tb_next
            # assert: the chain keeps the test frames and none from the framework package
            self.assertTrue(filenames)
            for filename in filenames:
                self.assertFalse(filename.startswith(_FRAMEWORK_ROOT), filename)

    def test_reports_test_frames_preserves_non_assertion_errors(self) -> None:
        # errors the framework does not own propagate untouched, which keeps
        # unexpected failures visible as runner errors rather than trimmed ones
        @reports_test_frames
        def raising_function() -> None:
            raise ValueError("unexpected")

        with self.assertRaises(ValueError) as context:
            raising_function()
        self.assertEqual(str(context.exception), "unexpected")

    def test_reports_test_frames_trims_assertion_frames(self) -> None:
        # a decorated function that raises an assertion error propagates the
        # error untouched, because its frames already belong to the test file
        @reports_test_frames
        def raising_function() -> None:
            raise AlborAssertionError("decorated failure")

        with self.assertRaises(AlborAssertionError) as context:
            raising_function()
        self.assertEqual(str(context.exception), "decorated failure")


if __name__ == "__main__":
    unittest.main()

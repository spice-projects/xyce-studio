import os
import tempfile
import unittest
from pathlib import Path

from slint_automation.slint_application import SlintApplication
from slint_automation.test_case import SlintTestCase


class FakeProcess:

    def __init__(self) -> None:
        # pid is read by the correlation label when present
        self.pid = 4242
        # _exit_code flips to a final value once the process terminates
        self._exit_code: int | None = None

    def poll(self) -> int | None:
        # report the exit code once the process finished
        return self._exit_code

    def terminate(self) -> None:
        # accept the graceful termination request and finish the process
        self._exit_code = 0

    def wait(self, timeout: float | None = None) -> int:
        # report a clean exit
        return 0


class FakeArtifactClient:

    def take_screenshot(self) -> bytes:
        # serve the canned png payload
        return b"png-bytes"

    def get_element_tree(self) -> dict:
        # serve the canned element tree
        return {"totalCount": 3, "elements": []}


class SessionCase(SlintTestCase):

    def setUp(self) -> None:
        # arrange: initialize the base hooks and start the session
        super().setUp()
        self.start_session(SlintApplication(FakeProcess(), FakeArtifactClient(), 1))

    def test_passing_run(self) -> None:
        # act: touch the exposed application so the session is used
        self.assertEqual(self._app.port(), 1)


class TestCaseSessionChecks(unittest.TestCase):

    def test_start_session_exposes_the_application_and_session(self) -> None:
        # arrange: route the artifact root into a temporary directory
        with tempfile.TemporaryDirectory() as tmp:
            os.environ["SLINT_TEST_ARTIFACTS"] = tmp
            os.environ.pop("SLINT_TEST_DEBUG", None)
            self.addCleanup(os.environ.pop, "SLINT_TEST_ARTIFACTS", None)
            case = SessionCase("test_passing_run")
            # act: run the inner case through a real test result
            result = unittest.TestResult()
            case.run(result)
            # assert: the inner test passed and the application was exposed
            self.assertTrue(result.wasSuccessful())
            # the exposed application is the session owned instance
            self.assertEqual(case._app.port(), 1)
            # the session reference is dropped after the test
            self.assertIsNone(case._session)

    def test_clean_run_closes_the_application_without_artifacts(self) -> None:
        # arrange: route the artifact root into a temporary directory
        with tempfile.TemporaryDirectory() as tmp:
            os.environ["SLINT_TEST_ARTIFACTS"] = tmp
            self.addCleanup(os.environ.pop, "SLINT_TEST_ARTIFACTS", None)
            case = SessionCase("test_passing_run")
            # act: run the inner case through a real test result
            result = unittest.TestResult()
            case.run(result)
            # assert: the test passed, the application was closed by the
            # teardown and no failure artifacts were collected
            self.assertTrue(result.wasSuccessful())
            self.assertFalse(case._app.is_running())
            self.assertEqual(os.listdir(tmp), [])

    def test_failed_run_collects_artifacts_named_after_the_case(self) -> None:
        # arrange: route the artifact root into a temporary directory
        with tempfile.TemporaryDirectory() as tmp:
            os.environ["SLINT_TEST_ARTIFACTS"] = tmp
            self.addCleanup(os.environ.pop, "SLINT_TEST_ARTIFACTS", None)

            class DeliberatelyFailingCase(SlintTestCase):

                def setUp(self) -> None:
                    # arrange: initialize the base hooks and start the session
                    super().setUp()
                    self.start_session(SlintApplication(FakeProcess(), FakeArtifactClient(), 1))

                def test_failing_run(self) -> None:
                    # act: fail the inner test so the teardown collects artifacts
                    self.fail("deliberate failure")

            case = DeliberatelyFailingCase("test_failing_run")
            # act: run the inner case through a real test result
            result = unittest.TestResult()
            case.run(result)
            # assert: the failure was recorded and the artifacts landed in the
            # directory named after the fully qualified case id
            self.assertEqual(len(result.failures), 1)
            directory = Path(tmp) / case.id()
            self.assertTrue((directory / "screenshot.png").exists())
            self.assertEqual((directory / "screenshot.png").read_bytes(), b"png-bytes")

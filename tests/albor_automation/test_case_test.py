import os
import tempfile
import unittest
from pathlib import Path

from albor_automation.test_case import AlborTestCase


class FakeSessionApp:
    # the application surface the adapter session owns, recording what finish does

    def __init__(self) -> None:
        # artifacts directories collected through collect_artifacts
        self.artifacts: list[str] = []
        # closed records whether the app was closed
        self.closed = False

    def is_running(self) -> bool:
        # report the application as running until the session closes it
        return not self.closed

    def collect_artifacts(self, directory: str) -> str:
        # the real app writes its artifacts, the fake records the directory
        self.artifacts.append(directory)
        return directory

    def close(self) -> None:
        # the real app terminates the process, the fake records the call
        self.closed = True


class SessionCase(AlborTestCase):

    def setUp(self) -> None:
        # arrange: initialize the base hooks and start the session
        super().setUp()
        self.app = FakeSessionApp()
        self.start_session(self.app)

    def test_passing_run(self) -> None:
        # act: touch the exposed application so the session is used
        self.assertTrue(self._app.is_running())


class TestCaseSessionChecks(unittest.TestCase):

    def test_start_session_exposes_the_application_and_session(self) -> None:
        # arrange: route the artifact root into a temporary directory
        with tempfile.TemporaryDirectory() as tmp:
            os.environ["ALBOR_TEST_ARTIFACTS"] = tmp
            self.addCleanup(os.environ.pop, "ALBOR_TEST_ARTIFACTS", None)
            case = SessionCase("test_passing_run")
            # act: run the inner case through a real test result
            result = unittest.TestResult()
            case.run(result)
            # assert: the inner test passed and the application was exposed
            self.assertTrue(result.wasSuccessful())
            # the exposed application is the session owned instance
            self.assertIs(case._app, case.app)
            # the session reference is dropped after the test
            self.assertIsNone(case._session)

    def test_clean_run_closes_the_application_without_artifacts(self) -> None:
        # arrange: route the artifact root into a temporary directory
        with tempfile.TemporaryDirectory() as tmp:
            os.environ["ALBOR_TEST_ARTIFACTS"] = tmp
            self.addCleanup(os.environ.pop, "ALBOR_TEST_ARTIFACTS", None)
            case = SessionCase("test_passing_run")
            # act: run the inner case through a real test result
            result = unittest.TestResult()
            case.run(result)
            # assert: the test passed, the application was closed by the
            # teardown and no failure artifacts were collected
            self.assertTrue(result.wasSuccessful())
            self.assertTrue(case._app.closed)
            self.assertEqual(case._app.artifacts, [])
            self.assertEqual(os.listdir(tmp), [])

    def test_failed_run_collects_artifacts_named_after_the_case(self) -> None:
        # arrange: route the artifact root into a temporary directory
        with tempfile.TemporaryDirectory() as tmp:
            os.environ["ALBOR_TEST_ARTIFACTS"] = tmp
            self.addCleanup(os.environ.pop, "ALBOR_TEST_ARTIFACTS", None)

            class DeliberatelyFailingCase(AlborTestCase):

                def setUp(self) -> None:
                    # arrange: initialize the base hooks and start the session
                    super().setUp()
                    self.start_session(FakeSessionApp())

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
            self.assertEqual(case._app.artifacts, [str(Path(tmp) / case.id())])
            self.assertTrue(case._app.closed)


if __name__ == "__main__":
    unittest.main()

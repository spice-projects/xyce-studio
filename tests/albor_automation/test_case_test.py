import os
import tempfile
import unittest
from pathlib import Path

from albor_automation.albor_application import AlborApplication
from albor_automation.test_case import AlborTestCase


class FakeProcess:
    # a live process handle the fake application reports running
    def poll(self) -> int | None:
        return None


class FakeMcp:
    def call_tool(self, name: str, arguments: dict | None = None):
        return "tree dump"

    def close(self) -> None:
        pass


class FakeClient:
    def __init__(self) -> None:
        self._mcp = FakeMcp()

    def screenshot(self) -> bytes:
        return b"png-bytes"


class FakeSessionApp:
    # a fake application recording the session teardown the base case drives

    def __init__(self) -> None:
        self.closed = False
        self.artifacts: list[str] = []

    def is_running(self) -> bool:
        return not self.closed

    def collect_artifacts(self, directory: str) -> str:
        self.artifacts.append(directory)
        return directory

    def close(self) -> None:
        self.closed = True


class SessionCase(AlborTestCase):

    def setUp(self) -> None:
        # arrange: the base hooks run first
        super().setUp()
        # arrange: the session owns a fake application
        self.start_session(FakeSessionApp())

    def test_passing_run(self) -> None:
        # act: the test body touches the exposed application
        self.assertTrue(self._app.is_running())


class RelaunchCase(AlborTestCase):

    def setUp(self) -> None:
        super().setUp()
        self.start_session(FakeSessionApp())

    def test_relaunch_finishes_the_first_session(self) -> None:
        # arrange: the first app is exposed through the session
        first = self._app
        # act: a relaunch starts a second session
        self.start_session(FakeSessionApp())
        # assert: the first app was closed by the finished session
        self.assertTrue(first.closed)
        self.assertFalse(self._app.closed)


class AlborTestCaseSessionChecks(unittest.TestCase):

    def test_passing_run_closes_without_artifacts(self) -> None:
        # arrange: the artifact root routes into a temporary directory
        with tempfile.TemporaryDirectory() as tmp:
            os.environ["ALBOR_TEST_ARTIFACTS"] = tmp
            self.addCleanup(os.environ.pop, "ALBOR_TEST_ARTIFACTS", None)
            case = SessionCase("test_passing_run")
            # act: run the inner case through a real test result
            result = unittest.TestResult()
            case.run(result)
            # assert: the inner test passed and the app was closed
            self.assertTrue(result.wasSuccessful())
            self.assertTrue(case._app.closed)
            self.assertEqual(os.listdir(tmp), [])

    def test_failed_run_collects_artifacts_named_after_the_case(self) -> None:
        # arrange: the failing case is defined here so discovery never runs it
        class FailingSessionCase(AlborTestCase):

            def setUp(self) -> None:
                super().setUp()
                self.start_session(FakeSessionApp())

            def test_deliberate_failure(self) -> None:
                # act: the test body fails on purpose
                self.fail("deliberate")

        with tempfile.TemporaryDirectory() as tmp:
            os.environ["ALBOR_TEST_ARTIFACTS"] = tmp
            self.addCleanup(os.environ.pop, "ALBOR_TEST_ARTIFACTS", None)
            case = FailingSessionCase("test_deliberate_failure")
            # act
            result = unittest.TestResult()
            case.run(result)
            # assert: the failure was recorded and the artifacts collected
            self.assertEqual(len(result.failures), 1)
            self.assertTrue(case._app.closed)
            self.assertEqual(case._app.artifacts, [str(Path(tmp) / case.id())])

    def test_passing_run_after_a_failed_one_collects_no_artifacts(self) -> None:
        # arrange: the failing case runs first; the passing case then shares
        # the same result object, which mimics the runner accumulating every
        # failure of the run
        class FailingSessionCase(AlborTestCase):

            def setUp(self) -> None:
                super().setUp()
                self.start_session(FakeSessionApp())

            def test_deliberate_failure(self) -> None:
                self.fail("deliberate")

        class PassingSessionCase(AlborTestCase):

            def setUp(self) -> None:
                super().setUp()
                self.start_session(FakeSessionApp())

            def test_deliberate_pass(self) -> None:
                self.assertTrue(True)

        with tempfile.TemporaryDirectory() as tmp:
            os.environ["ALBOR_TEST_ARTIFACTS"] = tmp
            self.addCleanup(os.environ.pop, "ALBOR_TEST_ARTIFACTS", None)
            result = unittest.TestResult()
            FailingSessionCase("test_deliberate_failure").run(result)
            passing = PassingSessionCase("test_deliberate_pass")
            passing.run(result)
            # assert: the failure was recorded once and the passing case was
            # not blamed for it
            self.assertEqual(len(result.failures), 1)
            self.assertEqual(passing._app.artifacts, [])


if __name__ == "__main__":
    unittest.main()

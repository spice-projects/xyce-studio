import os
import tempfile
import unittest
from pathlib import Path

from albor_automation.session import TestSession


class FakeApp:
    # the application surface the session owns, recording what finish does

    def __init__(self) -> None:
        # artifacts directories collected through collect_artifacts
        self.collected: list[str] = []
        # closed records whether the app was closed
        self.closed = False

    def collect_artifacts(self, directory: str) -> str:
        # the real app writes its artifacts, the fake records the directory
        self.collected.append(directory)
        return directory

    def close(self) -> None:
        # the real app terminates the process, the fake records the call
        self.closed = True


class SessionFinishChecks(unittest.TestCase):

    def test_app_returns_the_owned_application(self) -> None:
        # arrange
        app = FakeApp()
        session = TestSession(app, "some-test", artifacts_root="unused")
        # act / assert
        self.assertIs(session.app(), app)

    def test_clean_finish_closes_without_artifacts(self) -> None:
        # arrange
        with tempfile.TemporaryDirectory() as tmp:
            app = FakeApp()
            session = TestSession(app, "my-test", artifacts_root=tmp)
            # act: a passing test closes the app without collecting anything
            session.finish(False)
            # assert
            self.assertFalse(app.collected)
            self.assertTrue(app.closed)
            self.assertFalse((Path(tmp) / "my-test").exists())

    def test_failed_finish_collects_artifacts_and_closes(self) -> None:
        # arrange
        with tempfile.TemporaryDirectory() as tmp:
            app = FakeApp()
            session = TestSession(app, "failing-test", artifacts_root=tmp)
            # act: a failing test collects artifacts then closes
            session.finish(True)
            # assert
            self.assertEqual(app.collected, [str(Path(tmp) / "failing-test")])
            self.assertTrue(app.closed)

    def test_finish_is_idempotent(self) -> None:
        # arrange
        with tempfile.TemporaryDirectory() as tmp:
            app = FakeApp()
            session = TestSession(app, "once-only", artifacts_root=tmp)
            # act: the second finish call is a no-op
            session.finish(True)
            session.finish(True)
            # assert: the artifacts were collected exactly once and the app closed once
            self.assertEqual(len(app.collected), 1)
            self.assertTrue(app.closed)

    def test_artifacts_root_defaults_to_the_environment(self) -> None:
        # arrange: the environment names the artifacts root
        with tempfile.TemporaryDirectory() as tmp:
            os.environ["ALBOR_TEST_ARTIFACTS"] = tmp
            self.addCleanup(os.environ.pop, "ALBOR_TEST_ARTIFACTS", None)
            app = FakeApp()
            session = TestSession(app, "env-test")
            # act
            session.finish(True)
            # assert: the collection directory came from the environment
            self.assertEqual(app.collected, [str(Path(tmp) / "env-test")])


class SessionContextChecks(unittest.TestCase):

    def test_context_manager_closes_on_clean_exit(self) -> None:
        # arrange
        with tempfile.TemporaryDirectory() as tmp:
            app = FakeApp()
            session = TestSession(app, "clean-run", artifacts_root=tmp)
            # act: the block completes without raising
            with session as session_app:
                # the yielded app is the owned instance
                self.assertIs(session_app, app)
            # assert
            self.assertTrue(app.closed)
            self.assertFalse(app.collected)

    def test_context_manager_collects_and_propagates_errors(self) -> None:
        # arrange
        with tempfile.TemporaryDirectory() as tmp:
            app = FakeApp()
            session = TestSession(app, "error-run", artifacts_root=tmp)
            # act / assert: the exception propagates after the session finishes
            with self.assertRaises(RuntimeError) as context:
                with session:
                    raise RuntimeError("test body failed")
            self.assertEqual(str(context.exception), "test body failed")
            # assert: the failure collected artifacts and closed the app
            self.assertEqual(app.collected, [str(Path(tmp) / "error-run")])
            self.assertTrue(app.closed)


if __name__ == "__main__":
    unittest.main()

import os
import tempfile
import unittest
from pathlib import Path

from slint_automation.session import TestSession
from slint_automation.slint_application import SlintApplication


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


class SessionFinishChecks(unittest.TestCase):

    def test_app_returns_the_owned_application(self) -> None:
        # arrange
        app = SlintApplication(FakeProcess(), FakeArtifactClient(), 1)
        session = TestSession(app, "app-check", artifacts_root="unused")
        # act
        owned = session.app()
        # assert
        self.assertIs(owned, app)

    def test_clean_finish_closes_without_artifacts(self) -> None:
        # arrange
        with tempfile.TemporaryDirectory() as tmp:
            app = SlintApplication(FakeProcess(), FakeArtifactClient(), 1)
            session = TestSession(app, "my-test", artifacts_root=tmp)
            # act
            session.finish(False)
            # assert
            self.assertFalse((Path(tmp) / "my-test").exists())
            self.assertFalse(app.is_running())

    def test_failed_finish_collects_artifacts(self) -> None:
        # arrange
        with tempfile.TemporaryDirectory() as tmp:
            app = SlintApplication(FakeProcess(), FakeArtifactClient(), 1)
            session = TestSession(app, "failing-test", artifacts_root=tmp)
            # act
            session.finish(True)
            # assert
            directory = Path(tmp) / "failing-test"
            self.assertEqual((directory / "screenshot.png").read_bytes(), b"png-bytes")
            self.assertTrue((directory / "ui-tree.json").exists())
            self.assertFalse(app.is_running())

    def test_artifacts_root_defaults_to_environment(self) -> None:
        # arrange
        with tempfile.TemporaryDirectory() as tmp:
            os.environ["SLINT_TEST_ARTIFACTS"] = tmp
            self.addCleanup(os.environ.pop, "SLINT_TEST_ARTIFACTS", None)
            app = SlintApplication(FakeProcess(), FakeArtifactClient(), 1)
            session = TestSession(app, "env-test")
            # act
            session.finish(True)
            # assert
            self.assertTrue((Path(tmp) / "env-test" / "screenshot.png").exists())


class SessionContextChecks(unittest.TestCase):

    def test_context_manager_closes_on_clean_exit(self) -> None:
        # arrange
        with tempfile.TemporaryDirectory() as tmp:
            app = SlintApplication(FakeProcess(), FakeArtifactClient(), 1)
            # act
            with TestSession(app, "clean-run", artifacts_root=tmp) as session_app:
                # assert: the session yields the application instance
                self.assertIs(session_app, app)
            # assert
            self.assertFalse(app.is_running())
            self.assertFalse((Path(tmp) / "clean-run").exists())

    def test_context_manager_collects_on_error_and_propagates(self) -> None:
        # arrange
        with tempfile.TemporaryDirectory() as tmp:
            app = SlintApplication(FakeProcess(), FakeArtifactClient(), 1)
            # act / assert
            with self.assertRaises(RuntimeError) as context:
                with TestSession(app, "error-run", artifacts_root=tmp):
                    raise RuntimeError("test body failed")
            self.assertIn("test body failed", str(context.exception))
            # assert
            self.assertTrue((Path(tmp) / "error-run" / "screenshot.png").exists())
            self.assertFalse(app.is_running())

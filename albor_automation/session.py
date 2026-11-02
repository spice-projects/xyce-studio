import os
from pathlib import Path
from types import TracebackType

from .albor_application import AlborApplication
from .log import logger

DEFAULT_ARTIFACTS_DIRECTORY = "artifacts"
ARTIFACTS_ENVIRONMENT_VARIABLE = "ALBOR_TEST_ARTIFACTS"
_LOGGER = logger()


class TestSession:
    # owns one application for the lifetime of one test, collecting failure
    # artifacts before the process is closed

    def __init__(self, app: AlborApplication, name: str, artifacts_root: str | None = None) -> None:
        # app is the launched application instance this session owns
        self._app = app
        # name scopes the artifact directory per test
        self._name = name
        # artifacts_root comes from the argument or the environment override
        self._artifacts_root = artifacts_root or os.environ.get(ARTIFACTS_ENVIRONMENT_VARIABLE, DEFAULT_ARTIFACTS_DIRECTORY)
        # the session finishes once, so repeated finishes are ignored
        self._finished = False

    def app(self) -> AlborApplication:
        # return the application this session owns
        return self._app

    def finish(self, failed: bool) -> None:
        # collect the failure artifacts and close the application once
        if self._finished:
            return
        self._finished = True
        if failed:
            # artifacts only matter when the test failed
            directory = str(Path(self._artifacts_root) / self._name)
            self._app.collect_artifacts(directory)
        # the process is closed after the artifacts are written
        self._app.close()

    def __enter__(self) -> AlborApplication:
        # support for 'with' statement context manager
        return self._app

    def __exit__(self, exc_type: type[BaseException] | None, exc_val: BaseException | None, exc_tb: TracebackType | None) -> bool:
        # finish the session with the failure state the exception carries
        self.finish(exc_type is not None)
        # return False to propagate any exceptions
        return False

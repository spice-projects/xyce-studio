import unittest

from .albor_application import AlborApplication
from .log import configure_from_environment
from .session import TestSession


# the optional unittest adapter over the runner neutral TestSession; suites use
# the session directly (see albor_automation/session.py) and never inherit this
class AlborTestCase(unittest.TestCase):

    def setUp(self) -> None:
        # enable debug protocol logging when the environment requests it
        configure_from_environment()
        # subclasses must start the app session in their setUp
        self._session: TestSession | None = None

    def tearDown(self) -> None:
        # finish the session, collecting failure artifacts on test failures
        if self._session is not None:
            self._session.finish(self._test_failed())
        # drop the session reference after the test
        self._session = None

    def start_session(self, app: AlborApplication) -> None:
        # expose the app for the test body
        self._app = app
        # wrap the app in a session scoped by the test id
        self._session = TestSession(app, self.id())

    def _test_failed(self) -> bool:
        # read the runner outcome recorded for this test
        outcome = getattr(self, "_outcome", None)
        # report no failure when the runner outcome is unavailable
        if outcome is None:
            return False
        # read the result object carrying the recorded failures
        result = getattr(outcome, "result", None)
        # report no failure when the runner result is unavailable
        if result is None:
            return False
        # report failure when this test was recorded with a failure or error
        for entry in list(result.failures) + list(result.errors):
            # match the recorded entry to this test instance
            if entry[0] is self:
                return True
        # report no failure when nothing was recorded for this test
        return False

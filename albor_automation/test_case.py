import unittest

from .albor_application import AlborApplication
from .log import configure_from_environment
from .session import TestSession


class AlborTestCase(unittest.TestCase):
    # the unittest adapter over the runner neutral TestSession; the base class
    # closes the application and collects artifacts when a test fails

    def setUp(self) -> None:
        # arrange: initialize the base test case hooks
        super().setUp()
        # arrange: enable debug protocol logging when the environment asks for it
        configure_from_environment()
        # subclasses must start the app session in their setUp
        self._session: TestSession | None = None

    def start_session(self, app: AlborApplication) -> None:
        # the test asks the framework to own the application lifecycle
        if self._session is not None:
            # a prior session is finished first, which closes its application;
            # a scenario that relaunches (persistence checks) uses this
            self._session.finish(self._test_failed())
        self._app = app
        self._session = TestSession(app, self.id())
        # the session is finished through the cleanup hook, which unittest
        # runs even when the test method failed; the tearDown hook is skipped
        # in that case on python 3.11+, which would leak the application
        self.addCleanup(self._finish_session)
        # baseline the failures the run has already recorded, which is how the
        # cleanup decides whether this test failed
        self._failure_baseline = self._recorded_failures()

    def _finish_session(self) -> None:
        # the session collects failure artifacts and closes the application
        if self._session is not None:
            self._session.finish(self._test_failed())
            self._session = None

    def _test_failed(self) -> bool:
        # a test failed when the run recorded more failures than the baseline
        # taken when the session started; the outcome success flag cannot be
        # read from a cleanup because unittest resets it before every cleanup
        # part, and the result object accumulates every failure of the whole
        # run, so the baseline is what keeps other tests' failures out
        return self._recorded_failures() > self._failure_baseline

    def _recorded_failures(self) -> int:
        # count the failures and errors the current run has recorded so far
        outcome = getattr(self, "_outcome", None)
        result = getattr(outcome, "result", None) if outcome is not None else None
        return len(getattr(result, "failures", ())) + len(getattr(result, "errors", ()))

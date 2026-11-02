import functools
import time
from pathlib import Path
from types import TracebackType

from .errors import AlborAssertionError
from .log import logger

DEFAULT_WAIT_TIMEOUT = 5.0
DEFAULT_POLL_INTERVAL = 0.1

# framework package directory used to hide internals from reported tracebacks
_FRAMEWORK_PATH = str(Path(__file__).resolve().parent)


def reports_test_frames(function):
    # wrap a public entry point so assertion failures report only the test frames
    @functools.wraps(function)
    def wrapper(*args, **kwargs):
        try:
            # run the entry point and let failures propagate to the caller
            return function(*args, **kwargs)
        except AlborAssertionError as error:
            # trim the framework frames before the failure leaves the framework
            _trim_framework_frames(error)
            raise
    return wrapper


def wait_for(condition, *, timeout=DEFAULT_WAIT_TIMEOUT, poll_interval=DEFAULT_POLL_INTERVAL, timeout_message, observe=None) -> None:
    # poll the condition until it holds or the timeout expires
    deadline = time.monotonic() + timeout
    while True:
        # evaluate the polled condition
        if condition():
            return
        # stop polling once the deadline expires
        if time.monotonic() >= deadline:
            break
        # sleep between polls
        time.sleep(poll_interval)
    # read the last observed state for the failure message
    state = _safe_observe(observe)
    # raise the final failure with the last observed state and the framework internals hidden
    try:
        raise AlborAssertionError(f"{timeout_message}\nlast observed: {state}\nwaited: {timeout} seconds")
    except AlborAssertionError as error:
        # trim the framework frames so the reported traceback points at the test
        _trim_framework_frames(error)
        raise


def _trim_framework_frames(error: AlborAssertionError) -> None:
    # collect the frames captured between the caller and the raise site
    frames = []
    entry = error.__traceback__
    while entry is not None:
        frames.append(entry)
        entry = entry.tb_next
    # keep only the frames outside the framework package
    kept = [entry for entry in frames if not entry.tb_frame.f_code.co_filename.startswith(_FRAMEWORK_PATH)]
    # rebuild the frame chain without the framework internals
    previous = None
    for entry in reversed(kept):
        previous = TracebackType(previous, entry.tb_frame, entry.tb_lasti, entry.tb_lineno)
    # attach the cleaned chain back to the exception
    error.__traceback__ = previous


def _safe_observe(observe) -> str:
    # fall back to an unknown state when observation fails
    if observe is None:
        # report an unknown state without an observer
        return "unknown"
    try:
        # read the last observed state
        return observe()
    except Exception:
        # observation must never mask the timeout failure
        return "unknown"

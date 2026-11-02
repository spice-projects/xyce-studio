from typing import Callable

from .errors import AlborAssertionError, LocatorError
from .locator import Locator
from .log import logger
from .waiting import DEFAULT_POLL_INTERVAL, DEFAULT_WAIT_TIMEOUT, wait_for

_LOGGER = logger()


def expect(locator: Locator) -> "Expect":
    # build the assertion builder over one locator
    return Expect(locator)


class Expect:
    # polls node state until the assertion holds or the timeout expires

    def __init__(self, locator: Locator, negate: bool = False) -> None:
        # locator selects the node the assertion reads
        self._locator = locator
        # negate flips every assertion through not_
        self._negate = negate

    def not_(self) -> "Expect":
        # return the negated expectation builder
        return Expect(self._locator, not self._negate)

    def to_exist(self, *, timeout: float = DEFAULT_WAIT_TIMEOUT, poll_interval: float = DEFAULT_POLL_INTERVAL) -> None:
        # poll until the node is present or absent as the polarity demands
        self._poll(lambda: self._locator.exists(), "exist", timeout, poll_interval)

    def to_have_name(self, name: str, *, timeout: float = DEFAULT_WAIT_TIMEOUT, poll_interval: float = DEFAULT_POLL_INTERVAL) -> None:
        # poll until the node name matches, which is the announced label
        self._poll(lambda: self._locator.text() == name, f"have name {name!r}", timeout, poll_interval)

    def to_have_value(self, value: str, *, timeout: float = DEFAULT_WAIT_TIMEOUT, poll_interval: float = DEFAULT_POLL_INTERVAL) -> None:
        # poll until the editable value matches
        self._poll(lambda: self._locator.value() == value, f"have value {value!r}", timeout, poll_interval)

    def to_be_writable(self, *, timeout: float = DEFAULT_WAIT_TIMEOUT, poll_interval: float = DEFAULT_POLL_INTERVAL) -> None:
        # poll until the node accepts a value write
        self._poll(lambda: self._locator.writable(), "be writable", timeout, poll_interval)

    def to_have_state(self, flag: str, *, timeout: float = DEFAULT_WAIT_TIMEOUT, poll_interval: float = DEFAULT_POLL_INTERVAL) -> None:
        # poll until the named tree flag is present, which covers disabled,
        # focused, selected, read_only, multiline and expanded in one method
        self._poll(lambda: self._flag(flag), f"have state {flag}", timeout, poll_interval)

    def _flag(self, flag: str) -> bool:
        # read one state flag from the live node
        return bool(self._locator.node().get(flag, False))

    def _poll(self, positive: Callable[[], bool], description: str, timeout: float, poll_interval: float) -> None:
        # a node that disappears mid-wait simply does not match yet
        def matches() -> bool:
            try:
                return positive()
            except LocatorError:
                return False

        # build the condition the polarity demands and wait for it
        if self._negate:
            condition: Callable[[], bool] = lambda: not matches()
            failure = f"expected node {self._locator.describe()} to NOT {description}"
        else:
            condition = matches
            failure = f"expected node {self._locator.describe()} to {description}"
        # the wait reports the last observed state, which makes the mismatch visible
        wait_for(condition, timeout=timeout, poll_interval=poll_interval, timeout_message=failure, observe=self._locator.describe)

import unittest

from slint_automation.errors import SlintAssertionError
from slint_automation.locator import Locator
from slint_automation.mcp_client import McpClient
from slint_automation.slint_application import SlintApplication
from slint_automation.slint_client import SlintClient
from slint_automation.waiting import wait_for


class FakeProcess:

    def poll(self) -> int | None:
        # report a running process
        return None


class StaticClient:

    def __init__(self, present: bool) -> None:
        # present decides whether find returns a handle
        self._present = present

    def find_elements_by_id(self, elements_id: str) -> list[dict]:
        # return one handle when the element is present
        return [{"index": "1", "generation": "1"}] if self._present else []


class WaitHelperChecks(unittest.TestCase):

    def test_wait_for_returns_when_condition_holds(self) -> None:
        # act / assert
        wait_for(lambda: True, timeout=0.2, timeout_message="should not fail")

    def test_wait_for_raises_with_message_and_state(self) -> None:
        # act
        with self.assertRaises(SlintAssertionError) as context:
            wait_for(lambda: False, timeout=0.2, poll_interval=0.05, timeout_message="waiting for charts panel")
        # assert
        self.assertIn("waiting for charts panel", str(context.exception))
        self.assertIn("last observed: unknown", str(context.exception))
        self.assertIn("waited: 0.2 seconds", str(context.exception))

    def test_wait_for_reports_observed_state(self) -> None:
        # act
        with self.assertRaises(SlintAssertionError) as context:
            wait_for(lambda: False, timeout=0.2, poll_interval=0.05, timeout_message="waiting for ready state", observe=lambda: "state ready")
        # assert
        self.assertIn("last observed: state ready", str(context.exception))

    def test_wait_for_reports_unknown_when_the_observer_fails(self) -> None:
        # act: an observer that raises must never mask the timeout failure
        with self.assertRaises(SlintAssertionError) as context:
            wait_for(lambda: False, timeout=0.2, poll_interval=0.05, timeout_message="waiting for charts", observe=lambda: 1 / 0)
        # assert
        self.assertIn("last observed: unknown", str(context.exception))


class LocatorWaitChecks(unittest.TestCase):

    def test_wait_for_exists_passes_when_present(self) -> None:
        # arrange
        locator = Locator(StaticClient(True), "App::button")
        # act / assert
        locator.wait_for_exists(timeout=0.2)

    def test_wait_for_exists_times_out_when_missing(self) -> None:
        # arrange
        locator = Locator(StaticClient(False), "App::missing")
        # act
        with self.assertRaises(SlintAssertionError) as context:
            locator.wait_for_exists(timeout=0.2, poll_interval=0.05)
        # assert
        self.assertIn("timed out waiting for element 'App::missing' to exist", str(context.exception))
        self.assertIn("last observed: missing", str(context.exception))

    def test_wait_for_gone_passes_when_missing(self) -> None:
        # arrange
        locator = Locator(StaticClient(False), "App::missing")
        # act / assert
        locator.wait_for_gone(timeout=0.2)

    def test_wait_for_gone_times_out_when_present(self) -> None:
        # arrange
        locator = Locator(StaticClient(True), "App::dialog")
        # act
        with self.assertRaises(SlintAssertionError) as context:
            locator.wait_for_gone(timeout=0.2, poll_interval=0.05)
        # assert
        self.assertIn("timed out waiting for element 'App::dialog' to be removed", str(context.exception))
        self.assertIn("last observed: exists", str(context.exception))


class ApplicationWaitChecks(unittest.TestCase):

    def test_wait_for_condition_passes_when_condition_holds(self) -> None:
        # arrange
        app = SlintApplication(FakeProcess(), SlintClient(McpClient(1)), 1)
        # act / assert
        app.wait_for_condition(lambda: True, timeout=0.2, message="waiting for charts")

    def test_wait_for_condition_raises_with_message(self) -> None:
        # arrange
        app = SlintApplication(FakeProcess(), SlintClient(McpClient(1)), 1)
        # act
        with self.assertRaises(SlintAssertionError) as context:
            app.wait_for_condition(lambda: False, timeout=0.2, poll_interval=0.05, message="waiting for charts")
        # assert
        self.assertIn("waiting for charts", str(context.exception))

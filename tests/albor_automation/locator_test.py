import unittest

from albor_automation.errors import AlborAssertionError
from albor_automation.locator import Locator, LocatorError


class FakeClient:
    # canned tree the locator resolves against, which keeps the checks free
    # of any running application

    def __init__(self, nodes: list[dict]) -> None:
        # nodes are the parsed dump entries the find call walks
        self._nodes = nodes
        # invokes records every (id, action) click requested
        self.invokes: list[tuple[str, str]] = []
        # values records every (id, text) fill requested
        self.values: list[tuple[str, str]] = []

    def find(self, node_id: str) -> dict | None:
        # the lookup matches the full id exactly, which mirrors the facade
        for node in self._nodes:
            if node["id"] == node_id:
                return dict(node)
        return None

    def invoke(self, node_id: str, action: str = "activate") -> None:
        self.invokes.append((node_id, action))

    def set_value(self, node_id: str, value: str) -> None:
        self.values.append((node_id, value))


class FakeApp:
    # the application surface the locator touches: the client and the tree text

    def __init__(self, nodes: list[dict]) -> None:
        self._client = FakeClient(nodes)
        # tree_calls counts the observe reads the waits triggered
        self.tree_calls = 0

    def client(self) -> FakeClient:
        return self._client

    def tree_text(self) -> str:
        # the observe hook the wait failure messages read
        self.tree_calls += 1
        return "tree dump"


class LocatorResolutionChecks(unittest.TestCase):

    def test_exists_reports_presence(self) -> None:
        # arrange: the tree carries the node the locator targets
        app = FakeApp([{"role": "button", "id": "ok", "name": "OK"}])
        locator = Locator(app, "ok")
        # act / assert
        self.assertTrue(locator.exists())

    def test_exists_reports_absence(self) -> None:
        # arrange: the tree carries a different node
        app = FakeApp([{"role": "button", "id": "ok", "name": "OK"}])
        locator = Locator(app, "missing")
        # act / assert
        self.assertFalse(locator.exists())

    def test_node_returns_the_parsed_entry(self) -> None:
        # arrange
        app = FakeApp([{"role": "textfield", "id": "path", "name": "path", "value": "/bin/echo", "writable": True}])
        locator = Locator(app, "path")
        # act
        node = locator.node()
        # assert
        self.assertEqual(node["role"], "textfield")
        self.assertEqual(node["value"], "/bin/echo")

    def test_node_raises_when_absent(self) -> None:
        # arrange
        app = FakeApp([])
        locator = Locator(app, "missing")
        # act / assert
        with self.assertRaises(LocatorError) as context:
            locator.node()
        self.assertIn("no node with id 'missing'", str(context.exception))

    def test_text_returns_the_node_name(self) -> None:
        # arrange
        app = FakeApp([{"role": "button", "id": "ok", "name": "OK"}])
        locator = Locator(app, "ok")
        # act / assert
        self.assertEqual(locator.text(), "OK")

    def test_value_returns_the_editable_value(self) -> None:
        # arrange
        app = FakeApp([{"role": "textfield", "id": "path", "name": "path", "value": "/bin/echo"}])
        locator = Locator(app, "path")
        # act / assert
        self.assertEqual(locator.value(), "/bin/echo")

    def test_value_defaults_to_empty(self) -> None:
        # arrange: a button carries no editable value
        app = FakeApp([{"role": "button", "id": "ok", "name": "OK"}])
        locator = Locator(app, "ok")
        # act / assert
        self.assertEqual(locator.value(), "")

    def test_writable_reflects_the_flag(self) -> None:
        # arrange: the writable flag lands on the parsed node
        app = FakeApp([{"role": "textfield", "id": "path", "name": "path", "writable": True}])
        locator = Locator(app, "path")
        # act / assert
        self.assertTrue(locator.writable())

    def test_writable_defaults_to_false(self) -> None:
        # arrange: a button carries no writable flag
        app = FakeApp([{"role": "button", "id": "ok", "name": "OK"}])
        locator = Locator(app, "ok")
        # act / assert
        self.assertFalse(locator.writable())

    def test_click_invokes_the_default_action(self) -> None:
        # arrange
        app = FakeApp([{"role": "button", "id": "ok", "name": "OK"}])
        locator = Locator(app, "ok")
        # act
        locator.click()
        # assert: the invoke reached the client with the default action
        self.assertEqual(app.client().invokes, [("ok", "activate")])

    def test_click_forwards_a_custom_action(self) -> None:
        # arrange
        app = FakeApp([{"role": "textfield", "id": "path", "name": "path"}])
        locator = Locator(app, "path")
        # act
        locator.click(action="set_focused")
        # assert
        self.assertEqual(app.client().invokes, [("path", "set_focused")])

    def test_fill_writes_the_value(self) -> None:
        # arrange
        app = FakeApp([{"role": "textfield", "id": "path", "name": "path"}])
        locator = Locator(app, "path")
        # act
        locator.fill("/bin/echo")
        # assert
        self.assertEqual(app.client().values, [("path", "/bin/echo")])

    def test_node_id_returns_the_identity(self) -> None:
        # arrange
        app = FakeApp([])
        locator = Locator(app, "ok")
        # act / assert
        self.assertEqual(locator.node_id(), "ok")


class LocatorDescribeChecks(unittest.TestCase):

    def test_describe_includes_role_id_and_name(self) -> None:
        # arrange
        app = FakeApp([{"role": "button", "id": "ok", "name": "OK"}])
        locator = Locator(app, "ok")
        # act / assert
        self.assertEqual(locator.describe(), "button id=ok name='OK'")

    def test_describe_reports_absence(self) -> None:
        # arrange
        app = FakeApp([])
        locator = Locator(app, "missing")
        # act / assert
        self.assertEqual(locator.describe(), "id=missing (not found)")


class LocatorWaitChecks(unittest.TestCase):

    def test_wait_for_exists_passes_when_present(self) -> None:
        # arrange
        app = FakeApp([{"role": "button", "id": "ok", "name": "OK"}])
        locator = Locator(app, "ok")
        # act / assert
        locator.wait_for_exists(timeout=0.2)

    def test_wait_for_exists_times_out_when_missing(self) -> None:
        # arrange
        app = FakeApp([])
        locator = Locator(app, "missing")
        # act / assert: the failure message names the node and the observed tree
        with self.assertRaises(AlborAssertionError) as context:
            locator.wait_for_exists(timeout=0.2, poll_interval=0.05)
        self.assertIn("node 'missing' did not appear", str(context.exception))
        self.assertIn("last observed: tree dump", str(context.exception))

    def test_wait_for_gone_passes_when_absent(self) -> None:
        # arrange
        app = FakeApp([])
        locator = Locator(app, "missing")
        # act / assert
        locator.wait_for_gone(timeout=0.2)

    def test_wait_for_gone_times_out_when_present(self) -> None:
        # arrange
        app = FakeApp([{"role": "button", "id": "ok", "name": "OK"}])
        locator = Locator(app, "ok")
        # act / assert: the failure message names the node
        with self.assertRaises(AlborAssertionError) as context:
            locator.wait_for_gone(timeout=0.2, poll_interval=0.05)
        self.assertIn("node 'ok' did not disappear", str(context.exception))


if __name__ == "__main__":
    unittest.main()

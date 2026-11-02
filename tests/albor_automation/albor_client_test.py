import unittest

from albor_automation.albor_client import AlborClient


class FakeMcpClient:
    # in-process replacement for the transport so the facade runs against
    # canned tool results without any endpoint

    def __init__(self) -> None:
        # calls records every (tool, arguments) pair the facade requested
        self.calls: list[tuple[str, dict]] = []
        # replies maps the tool name to the outcome: a result payload or an
        # exception to raise
        self.replies: dict[str, object] = {}
        # info is the handshake result server_info returns
        self.info: dict = {"name": "fake-albor"}
        # tools are the definitions list_tools returns
        self.tools: list[dict] = [{"name": "semantic.tree"}, {"name": "semantic.invoke"}]

    def server_info(self) -> dict:
        return self.info

    def list_tools(self) -> list[dict]:
        return self.tools

    def call_tool(self, name: str, arguments: dict | None = None) -> object:
        # record the call and serve the canned outcome
        self.calls.append((name, dict(arguments or {})))
        outcome = self.replies.get(name)
        if isinstance(outcome, Exception):
            raise outcome
        return outcome


class AlborClientFacadeChecks(unittest.TestCase):

    def setUp(self) -> None:
        # every check runs against the same fake transport
        self._mcp = FakeMcpClient()
        self._client = AlborClient(self._mcp)

    def test_server_info_is_forwarded(self) -> None:
        # act / assert
        self.assertEqual(self._client.server_info()["name"], "fake-albor")

    def test_list_tools_is_forwarded(self) -> None:
        # act / assert
        self.assertEqual([tool["name"] for tool in self._client.list_tools()], ["semantic.tree", "semantic.invoke"])

    def test_tree_parses_the_dump(self) -> None:
        # arrange: the dump carries two nodes and a blank separator line
        self._mcp.replies["semantic.tree"] = "group - \nbutton ok OK\n"
        # act
        nodes = self._client.tree()
        # assert
        self.assertEqual(len(nodes), 2)
        self.assertEqual(nodes[0]["role"], "group")
        self.assertEqual(nodes[1]["id"], "ok")

    def test_find_resolves_by_exact_id(self) -> None:
        # arrange: the tree carries a node whose id the lookup targets
        self._mcp.replies["semantic.tree"] = "button plugin_config_ok OK\nlabel - Error"
        # act
        node = self._client.find("plugin_config_ok")
        # assert
        self.assertEqual(node["role"], "button")
        self.assertEqual(node["name"], "OK")

    def test_find_returns_none_when_absent(self) -> None:
        # arrange
        self._mcp.replies["semantic.tree"] = "button ok OK"
        # act
        node = self._client.find("missing")
        # assert
        self.assertIsNone(node)

    def test_find_requires_the_full_id(self) -> None:
        # arrange: the tree carries a node whose id merely starts with the
        # query, which must not match a lookup for the longer id
        self._mcp.replies["semantic.tree"] = "button ok OK"
        # act / assert
        self.assertIsNone(self._client.find("ok_extra"))

    def test_invoke_defaults_to_activate(self) -> None:
        # arrange
        self._mcp.replies["semantic.invoke"] = ""
        # act
        self._client.invoke("plugin_config_ok")
        # assert: the tool call carries the node id and the default action
        self.assertEqual(self._mcp.calls, [("semantic.invoke", {"id": "plugin_config_ok", "action": "activate"})])

    def test_invoke_forwards_the_action(self) -> None:
        # arrange
        self._mcp.replies["semantic.invoke"] = ""
        # act
        self._client.invoke("ok", action="press")
        # assert
        self.assertEqual(self._mcp.calls, [("semantic.invoke", {"id": "ok", "action": "press"})])

    def test_set_value_writes_the_text(self) -> None:
        # arrange
        self._mcp.replies["semantic.set_value"] = ""
        # act
        self._client.set_value("path", "/bin/echo")
        # assert
        self.assertEqual(self._mcp.calls, [("semantic.set_value", {"id": "path", "value": "/bin/echo"})])

    def test_set_focused_writes_the_state(self) -> None:
        # arrange: both focus transitions are queued
        self._mcp.replies["semantic.set_focused"] = ""
        # act
        self._client.set_focused("ok", True)
        self._client.set_focused("ok", False)
        # assert: the flag arrives as the protocol's 1/0 values
        self.assertEqual(self._mcp.calls, [
            ("semantic.set_focused", {"id": "ok", "value": "1"}),
            ("semantic.set_focused", {"id": "ok", "value": "0"}),
        ])

    def test_screenshot_requests_a_png(self) -> None:
        # arrange: the capture tool answers with the png payload
        self._mcp.replies["capture.screenshot"] = b"png-bytes"
        # act
        png = self._client.screenshot()
        # assert
        self.assertEqual(png, b"png-bytes")
        self.assertEqual(self._mcp.calls, [("capture.screenshot", {"format": "png"})])

    def test_transport_errors_propagate(self) -> None:
        # arrange: the tool refuses, which the facade must not swallow
        from albor_automation.errors import McpError

        self._mcp.replies["semantic.tree"] = McpError("the conversation was closed by the server")
        # act / assert
        with self.assertRaises(McpError):
            self._client.tree()


if __name__ == "__main__":
    unittest.main()

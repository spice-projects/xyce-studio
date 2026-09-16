import json
import unittest
import urllib.error
from unittest import mock

from slint_automation.errors import McpError
from slint_automation.mcp_client import McpClient


class FakeResponse:

    def __init__(self, data: bytes) -> None:
        # data is the canned response body returned by read
        self._data = data

    def read(self) -> bytes:
        # serve the canned body like a real http response
        return self._data

    def __enter__(self) -> "FakeResponse":
        # support the with statement of the transport
        return self

    def __exit__(self, exc_type: object, exc_val: object, exc_tb: object) -> bool:
        # nothing to release for the fake response
        return False


class FakeUrlopen:
    # in-process replacement for urllib.request.urlopen so the transport is
    # exercised without any socket, loopback server or thread

    def __init__(self, responder) -> None:
        # responder maps a decoded request body to a result dict, an
        # exception to raise or raw bytes for a malformed body
        self._responder = responder
        # requests records every decoded request body for assertions
        self.requests: list[dict] = []
        # raw_requests records every urllib request object for endpoint checks
        self.raw_requests: list[object] = []

    def __call__(self, request: object, timeout: float | None = None) -> FakeResponse:
        # record the raw urllib request for endpoint assertions
        self.raw_requests.append(request)
        # decode and record the request like the real transport would
        body = json.loads(request.data.decode("utf-8"))
        self.requests.append(body)
        # serve the canned outcome for this request
        outcome = self._responder(body)
        # exceptions model transport level failures
        if isinstance(outcome, Exception):
            raise outcome
        # raw bytes model a malformed non-json response body
        if isinstance(outcome, bytes):
            return FakeResponse(outcome)
        return FakeResponse(json.dumps(outcome).encode("utf-8"))


def mock_responder(body: dict) -> dict:
    # dispatch on the json-rpc method like the real embedded server
    method = body.get("method")
    if method == "initialize":
        result = {"serverInfo": {"name": "mock-mcp"}}
        return {"jsonrpc": "2.0", "id": body.get("id"), "result": result}
    if method == "tools/list":
        result = {"tools": [{"name": "list_windows"}, {"name": "click_element"}]}
        return {"jsonrpc": "2.0", "id": body.get("id"), "result": result}
    if method == "tools/call":
        # echo the tool arguments back as a nested json text block
        text = json.dumps({"echo": body.get("params", {})})
        result = {"content": [{"type": "text", "text": text}]}
        # flag the boom tool as a tool-level failure
        if body.get("params", {}).get("name") == "boom":
            result = {"content": [{"type": "text", "text": "tool exploded"}], "isError": True}
        return {"jsonrpc": "2.0", "id": body.get("id"), "result": result}
    # every other method returns a json-rpc error
    return {"jsonrpc": "2.0", "id": body.get("id"), "error": {"code": -32601, "message": f"method not found: {method}"}}


class McpClientTransportChecks(unittest.TestCase):

    def setUp(self) -> None:
        # arrange: replace the http transport with the in-process fake
        self._urlopen = FakeUrlopen(mock_responder)
        patcher = mock.patch("urllib.request.urlopen", self._urlopen)
        patcher.start()
        self.addCleanup(patcher.stop)
        self._client = McpClient(8080)

    def test_initialize_returns_server_info(self) -> None:
        # act
        info = self._client.initialize()
        # assert
        self.assertEqual(info["serverInfo"]["name"], "mock-mcp")

    def test_call_builds_json_rpc_envelope(self) -> None:
        # act
        self._client.initialize()
        # assert: the envelope carries the protocol, sequential id, method and version
        request = self._urlopen.requests[0]
        self.assertEqual(request["jsonrpc"], "2.0")
        self.assertEqual(request["id"], 1)
        self.assertEqual(request["method"], "initialize")
        self.assertEqual(request["params"]["protocolVersion"], "2025-06-18")
        # a second call consumes the next sequential id
        self._client.call("initialize")
        self.assertEqual(self._urlopen.requests[1]["id"], 2)

    def test_call_posts_to_the_mcp_endpoint(self) -> None:
        # act
        self._client.call("initialize")
        # assert: the request targets the local mcp endpoint
        request = self._urlopen.raw_requests[0]
        self.assertEqual(request.full_url, "http://127.0.0.1:8080/mcp")
        self.assertEqual(request.get_header("Content-type"), "application/json")

    def test_call_raises_on_rpc_error(self) -> None:
        # act
        with self.assertRaises(McpError) as context:
            self._client.call("resources/list")
        # assert
        self.assertIn("-32601", str(context.exception))

    def test_call_raises_on_malformed_response(self) -> None:
        # arrange: make the transport serve a non-json body
        self._urlopen._responder = lambda body: b"not json"
        # act
        with self.assertRaises(McpError) as context:
            self._client.call("initialize")
        # assert
        self.assertIn("malformed json response", str(context.exception))

    def test_call_tool_decodes_nested_json(self) -> None:
        # act
        result = self._client.call_tool("echo", {"x": 1})
        # assert
        self.assertEqual(result["echo"]["name"], "echo")
        self.assertEqual(result["echo"]["arguments"], {"x": 1})

    def test_call_tool_raises_on_is_error(self) -> None:
        # act
        with self.assertRaises(McpError) as context:
            self._client.call_tool("boom")
        # assert
        self.assertIn("tool exploded", str(context.exception))

    def test_connection_failure_raises_mcp_error(self) -> None:
        # arrange: make the transport fail like an unreachable server would
        self._urlopen._responder = lambda body: urllib.error.URLError("connection refused")
        # act
        with self.assertRaises(McpError):
            self._client.call("initialize")

    def test_server_info_returns_the_stored_handshake_result(self) -> None:
        # arrange: no handshake yet reports no server info
        self.assertIsNone(self._client.server_info())
        # act
        self._client.initialize()
        # assert
        self.assertEqual(self._client.server_info()["serverInfo"]["name"], "mock-mcp")

    def test_connect_stores_server_info_and_sends_the_initialized_notification(self) -> None:
        # act
        self._client.connect()
        # assert: the handshake result is stored for inspection
        self.assertEqual(self._client.server_info()["serverInfo"]["name"], "mock-mcp")
        # the initialized notification carries no request id and expects no response
        notification = self._urlopen.requests[1]
        self.assertEqual(notification["method"], "notifications/initialized")
        self.assertNotIn("id", notification)

    def test_list_tools_returns_the_advertised_tool_definitions(self) -> None:
        # act
        tools = self._client.list_tools()
        # assert
        self.assertEqual([tool["name"] for tool in tools], ["list_windows", "click_element"])

    def test_call_tool_without_arguments_sends_an_empty_arguments_object(self) -> None:
        # act
        self._client.call_tool("echo")
        # assert
        request = self._urlopen.requests[-1]
        self.assertEqual(request["params"], {"name": "echo", "arguments": {}})

    def test_http_error_raises_mcp_error(self) -> None:
        # arrange: make the transport respond with an http 500
        self._urlopen._responder = lambda body: urllib.error.HTTPError("http://127.0.0.1:8080/mcp", 500, "server error", None, None)
        # act
        with self.assertRaises(McpError) as context:
            self._client.call("initialize")
        # assert
        self.assertIn("http error 500", str(context.exception))

    def test_close_is_a_noop(self) -> None:
        # act / assert: the stateless http transport releases nothing
        self._client.close()
        self.assertIsNone(self._client.server_info())

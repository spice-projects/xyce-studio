import base64
import json
import select
import unittest
from unittest import mock

from albor_automation.errors import McpError
from albor_automation.mcp_client import McpClient


class FakeSocket:
    # in-process replacement for the unix socket so the framing logic runs
    # without any real endpoint, thread or blocking read

    def __init__(self, chunks=None) -> None:
        # chunks are the byte fragments the server delivers, one per recv call
        self._chunks = list(chunks or [])
        # sent records every frame the client wrote, for envelope assertions
        self.sent: list[bytes] = []
        # closed records whether the client released the socket
        self.closed = False
        # connect_error makes the connect call fail like an unreachable endpoint
        self.connect_error: OSError | None = None
        # send_error makes sendall fail like a dropped connection
        self.send_error: OSError | None = None
        # recv_error makes recv fail like a reset connection
        self.recv_error: OSError | None = None

    def connect(self, path: str) -> None:
        # a configured failure models an endpoint that refuses the connection
        if self.connect_error is not None:
            raise self.connect_error

    def sendall(self, data: bytes) -> None:
        # a configured failure models a connection that dropped mid write
        if self.send_error is not None:
            raise self.send_error
        self.sent.append(bytes(data))

    def recv(self, size: int) -> bytes:
        # a configured failure models a reset while reading the reply
        if self.recv_error is not None:
            raise self.recv_error
        # an exhausted chunk list reads as the server closing the conversation
        if not self._chunks:
            return b""
        return self._chunks.pop(0)

    def close(self) -> None:
        self.closed = True


def rpc_result(request_id: int, result: dict) -> bytes:
    # build the json line one real reply carries
    return json.dumps({"jsonrpc": "2.0", "id": request_id, "result": result}).encode("utf-8") + b"\n"


def rpc_error(request_id: int, code: int, message: str) -> bytes:
    return json.dumps({"jsonrpc": "2.0", "id": request_id, "error": {"code": code, "message": message}}).encode("utf-8") + b"\n"


def rpc_notification(method: str) -> bytes:
    # a notification carries no id, which the client must skip
    return json.dumps({"jsonrpc": "2.0", "method": method}).encode("utf-8") + b"\n"


def initialize_reply(server_name: str = "albor-test") -> bytes:
    return rpc_result(1, {"serverInfo": {"name": server_name}, "capabilities": {}})


class McpClientHandshakeChecks(unittest.TestCase):

    def setUp(self) -> None:
        # every check talks to a fake socket, so no endpoint is involved
        self._socket = FakeSocket([initialize_reply()])
        self._patch_socket(self._socket)
        self._patch_select(self._socket)
        self._client = McpClient("/tmp/mcp.sock", "the-token")

    def _patch_socket(self, sock: FakeSocket) -> None:
        # the client constructs its own socket, which the patch replaces
        patcher = mock.patch("albor_automation.mcp_client.socket.socket", lambda family, kind: sock)
        patcher.start()
        self.addCleanup(patcher.stop)

    def _patch_select(self, sock: FakeSocket) -> None:
        # select always reports the fake socket readable, which keeps recv driving
        patcher = mock.patch("albor_automation.mcp_client.select.select", lambda *args, **kwargs: ([sock], [], []))
        patcher.start()
        self.addCleanup(patcher.stop)

    def test_connect_runs_the_token_handshake(self) -> None:
        # act
        self._client.connect()
        # assert: the initialize request reaches the endpoint as one line
        self.assertEqual(len(self._socket.sent), 1)
        request = json.loads(self._socket.sent[0].decode("utf-8"))
        self.assertEqual(request["method"], "initialize")
        self.assertEqual(request["jsonrpc"], "2.0")
        self.assertEqual(request["id"], 1)
        self.assertEqual(request["params"]["protocolVersion"], "2024-11-05")
        self.assertEqual(request["params"]["_meta"]["albor/token"], "the-token")
        self.assertEqual(request["params"]["clientInfo"]["name"], "albor-test-framework")

    def test_connect_stores_the_server_info(self) -> None:
        # act
        self._client.connect()
        # assert
        self.assertEqual(self._client.server_info()["name"], "albor-test")

    def test_connect_is_idempotent(self) -> None:
        # arrange: connect once, then reconnect through the same client
        self._client.connect()
        # act
        self._client.connect()
        # assert: the handshake ran once, so no second frame was written
        self.assertEqual(len(self._socket.sent), 1)

    def test_connect_failure_raises_mcp_error(self) -> None:
        # arrange: the endpoint refuses the connection
        self._socket.connect_error = OSError("no such file or directory")
        # act / assert
        with self.assertRaises(McpError) as context:
            self._client.connect()
        self.assertIn("connection to /tmp/mcp.sock failed", str(context.exception))

    def test_handshake_failure_raises_mcp_error(self) -> None:
        # arrange: the endpoint refuses the token
        self._socket._chunks = [rpc_error(1, -32000, "authentication failed")]
        # act / assert
        with self.assertRaises(McpError) as context:
            self._client.connect()
        self.assertIn("-32000", str(context.exception))
        self.assertIn("authentication failed", str(context.exception))

    def test_call_before_connect_raises(self) -> None:
        # act / assert: no socket means no exchange is possible
        with self.assertRaises(McpError) as context:
            self._client.call("tools/list")
        self.assertIn("not connected", str(context.exception))


class McpClientExchangeChecks(unittest.TestCase):
    # the exchange drives the framing: one request, then reads until the
    # matching reply arrives within the deadline

    def _make_client(self, chunks: list[bytes]) -> tuple[McpClient, FakeSocket]:
        # build the client over a fake socket whose chunks arrive in order
        sock = FakeSocket(chunks)
        patcher = mock.patch("albor_automation.mcp_client.socket.socket", lambda family, kind: sock)
        patcher.start()
        self.addCleanup(patcher.stop)
        select_patcher = mock.patch("albor_automation.mcp_client.select.select", lambda *args, **kwargs: ([sock], [], []))
        select_patcher.start()
        self.addCleanup(select_patcher.stop)
        return McpClient("/tmp/mcp.sock", "token"), sock

    def test_reply_in_one_chunk(self) -> None:
        # arrange: the reply arrives whole
        client, sock = self._make_client([initialize_reply(), rpc_result(2, {"tools": [{"name": "semantic.tree"}]})])
        client.connect()
        # act
        result = client.call("tools/list")
        # assert
        self.assertEqual(result["tools"][0]["name"], "semantic.tree")

    def test_reply_split_across_chunks_is_reassembled(self) -> None:
        # arrange: the reply bytes arrive in three fragments, which models a
        # reply the kernel delivered piecemeal
        line = rpc_result(2, {"value": 42})
        client, sock = self._make_client([initialize_reply(), line[:10], line[10:20], line[20:]])
        client.connect()
        # act
        result = client.call("anything")
        # assert: the buffer held the partial line until the last fragment arrived
        self.assertEqual(result["value"], 42)

    def test_two_messages_in_one_chunk_are_both_consumed(self) -> None:
        # arrange: a notification and the reply share one frame
        client, sock = self._make_client([initialize_reply(), rpc_notification("ui/frame") + rpc_result(2, {"ok": True})])
        client.connect()
        # act
        result = client.call("anything")
        # assert: the notification was skipped and the reply consumed
        self.assertEqual(result["ok"], True)
        self.assertEqual(client._buffer, b"")

    def test_stale_reply_is_skipped(self) -> None:
        # arrange: a reply for another request id arrives first
        client, sock = self._make_client([initialize_reply(), rpc_result(99, {"stale": True}), rpc_result(2, {"fresh": True})])
        client.connect()
        # act
        result = client.call("anything")
        # assert: the stale reply was discarded and the matching one returned
        self.assertEqual(result["fresh"], True)

    def test_garbage_line_is_skipped(self) -> None:
        # arrange: a malformed line precedes the real reply
        client, sock = self._make_client([initialize_reply(), b"this is not json\n", rpc_result(2, {"good": True})])
        client.connect()
        # act
        result = client.call("anything")
        # assert: the parser recovered and consumed the valid reply
        self.assertEqual(result["good"], True)

    def test_rpc_error_reply_raises_mcp_error(self) -> None:
        # arrange
        client, sock = self._make_client([initialize_reply(), rpc_error(2, -32601, "no such method: nope")])
        client.connect()
        # act / assert
        with self.assertRaises(McpError) as context:
            client.call("nope")
        self.assertIn("-32601", str(context.exception))
        self.assertIn("no such method: nope", str(context.exception))

    def test_send_failure_raises_mcp_error(self) -> None:
        # arrange: the connection drops before the write
        client, sock = self._make_client([initialize_reply()])
        client.connect()
        sock.send_error = OSError("broken pipe")
        # act / assert
        with self.assertRaises(McpError) as context:
            client.call("tools/list")
        self.assertIn("connection dropped before the request", str(context.exception))

    def test_recv_failure_raises_mcp_error_and_closes(self) -> None:
        # arrange: the connection resets while reading
        client, sock = self._make_client([initialize_reply()])
        client.connect()
        sock.recv_error = OSError("connection reset")
        # act / assert
        with self.assertRaises(McpError) as context:
            client.call("tools/list")
        self.assertIn("connection dropped while reading", str(context.exception))
        self.assertTrue(sock.closed)

    def test_server_close_raises_mcp_error(self) -> None:
        # arrange: the server closes the conversation without a reply
        client, sock = self._make_client([initialize_reply(), b""])
        client.connect()
        # act / assert
        with self.assertRaises(McpError) as context:
            client.call("tools/list")
        self.assertIn("connection was closed by the server", str(context.exception))
        self.assertTrue(sock.closed)

    def test_timeout_closes_the_client(self) -> None:
        # arrange: select never reports readable, so the deadline expires
        client, sock = self._make_client([initialize_reply()])
        client.connect()
        # arrange: select reports nothing readable for this check only
        stop_readability = mock.patch("albor_automation.mcp_client.select.select", lambda *args, **kwargs: ([], [], []))
        stop_readability.start()
        self.addCleanup(stop_readability.stop)
        # act / assert: the exchange raises before any real timeout elapses
        with self.assertRaises(McpError) as context:
            client._exchange({"jsonrpc": "2.0", "id": 2, "method": "stuck"}, timeout=0.05)
        self.assertIn("timed out waiting for the reply to request 2", str(context.exception))
        self.assertTrue(sock.closed)


class McpClientToolChecks(unittest.TestCase):

    def setUp(self) -> None:
        # every check talks through the same connected fake transport
        self._socket = FakeSocket([initialize_reply()])
        patcher = mock.patch("albor_automation.mcp_client.socket.socket", lambda family, kind: self._socket)
        patcher.start()
        self.addCleanup(patcher.stop)
        select_patcher = mock.patch("albor_automation.mcp_client.select.select", lambda *args, **kwargs: ([self._socket], [], []))
        select_patcher.start()
        self.addCleanup(select_patcher.stop)
        self._client = McpClient("/tmp/mcp.sock", "token")
        self._client.connect()

    def _queue_tool_result(self, result: dict) -> None:
        # queue the reply the next tools/call request receives
        self._socket._chunks.append(rpc_result(2, {"content": [{"type": "text", "text": json.dumps(result)}], "isError": False}))

    def test_call_tool_decodes_nested_json(self) -> None:
        # arrange
        self._queue_tool_result({"nodes": [{"id": "ok"}]})
        # act
        result = self._client.call_tool("semantic.tree")
        # assert
        self.assertEqual(result["nodes"][0]["id"], "ok")

    def test_call_tool_returns_raw_text_when_not_json(self) -> None:
        # arrange: the payload is plain prose, which json decoding must not touch
        self._socket._chunks.append(rpc_result(2, {"content": [{"type": "text", "text": "plain prose"}], "isError": False}))
        # act
        result = self._client.call_tool("semantic.actions", {"id": "ok"})
        # assert
        self.assertEqual(result, "plain prose")

    def test_call_tool_sends_name_and_arguments(self) -> None:
        # arrange
        self._queue_tool_result({})
        # act
        self._client.call_tool("semantic.invoke", {"id": "ok", "action": "activate"})
        # assert: the tools/call envelope carries the tool name and arguments
        request = json.loads(self._socket.sent[-1].decode("utf-8"))
        self.assertEqual(request["method"], "tools/call")
        self.assertEqual(request["params"]["name"], "semantic.invoke")
        self.assertEqual(request["params"]["arguments"], {"id": "ok", "action": "activate"})

    def test_call_tool_without_arguments_sends_empty_object(self) -> None:
        # arrange
        self._queue_tool_result({})
        # act
        self._client.call_tool("semantic.tree")
        # assert
        request = json.loads(self._socket.sent[-1].decode("utf-8"))
        self.assertEqual(request["params"]["arguments"], {})

    def test_call_tool_raises_on_tool_error(self) -> None:
        # arrange: the tool itself refused, which arrives as isError
        self._socket._chunks.append(rpc_result(2, {"content": [{"type": "text", "text": "no node named missing"}], "isError": True}))
        # act / assert
        with self.assertRaises(McpError) as context:
            self._client.call_tool("semantic.invoke", {"id": "missing"})
        self.assertIn("no node named missing", str(context.exception))

    def test_call_tool_decodes_image_payloads(self) -> None:
        # arrange: the capture tool answers with a base64 image block
        payload = base64.b64encode(b"png-bytes").decode("utf-8")
        self._socket._chunks.append(rpc_result(2, {"content": [{"type": "image", "data": payload}]}))
        # act
        result = self._client.call_tool("capture.screenshot", {"format": "png"})
        # assert
        self.assertEqual(result, b"png-bytes")

    def test_list_tools_returns_the_definitions(self) -> None:
        # arrange
        self._socket._chunks.append(rpc_result(2, {"tools": [{"name": "semantic.tree"}, {"name": "semantic.invoke"}]}))
        # act
        tools = self._client.list_tools()
        # assert
        self.assertEqual([tool["name"] for tool in tools], ["semantic.tree", "semantic.invoke"])

    def test_close_releases_the_socket(self) -> None:
        # act
        self._client.close()
        # assert: the socket is closed and further calls refuse
        self.assertTrue(self._socket.closed)
        with self.assertRaises(McpError) as context:
            self._client.call("tools/list")
        self.assertIn("not connected", str(context.exception))

    def test_close_is_idempotent(self) -> None:
        # act: closing twice must not raise
        self._client.close()
        self._client.close()


if __name__ == "__main__":
    unittest.main()

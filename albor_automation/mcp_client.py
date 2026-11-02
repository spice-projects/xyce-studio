import base64
import json
import select
import socket
import time
from typing import Any

from .errors import McpError
from .log import logger

DEFAULT_READ_TIMEOUT = 30.0
MCP_PROTOCOL_VERSION = "2024-11-05"
_LOGGER = logger()


class McpClient:
    # one conversation over the albor unix socket transport; the server serves one
    # line per event-loop frame, so requests and replies never interleave and every
    # call waits for its own reply with a deadline

    def __init__(self, socket_path: str, token: str) -> None:
        # socket_path is the endpoint the application published in its descriptor
        self._socket_path = socket_path
        # token is the session secret read from the token file beside the socket
        self._token = token
        # the conversation is unauthenticated until the handshake succeeds
        self._connected = False
        # track the sequential json-rpc request id
        self._next_id = 1
        # the unix socket handle held for the whole conversation
        self._socket: socket.socket | None = None
        # buffered bytes received but not yet consumed by a complete line
        self._buffer = b""
        # the server info captured during the handshake
        self._server_info: dict = {}

    def connect(self) -> None:
        # open the socket and run the initialize handshake with the session token
        if self._connected:
            return
        try:
            self._socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self._socket.connect(self._socket_path)
        except OSError as error:
            raise McpError(f"connection to {self._socket_path} failed: {error}") from error
        # the handshake proves the client holds the session token; a later
        # request without a successful initialize is refused by the session
        result = self._handshake()
        # remember the server info the handshake answered with
        self._server_info = result.get("serverInfo", {})
        self._connected = True

    def _handshake(self) -> dict:
        # build the initialize handshake parameters with the token in _meta
        params = {
            "protocolVersion": MCP_PROTOCOL_VERSION,
            "capabilities": {},
            "clientInfo": {"name": "albor-test-framework", "version": "0.1.0"},
            "_meta": {"albor/token": self._token},
        }
        # send the initialize request through the normal exchange path
        return self.call("initialize", params)

    def server_info(self) -> dict:
        # return the server info captured during the handshake
        return self._server_info

    def list_tools(self) -> list[dict]:
        # return the tool definitions advertised by the server
        result = self.call("tools/list")
        return result["tools"]

    def call_tool(self, name: str, arguments: dict | None = None) -> Any:
        # invoke the tool by name through the tools/call method
        result = self.call("tools/call", {"name": name, "arguments": arguments or {}})
        # tool failures arrive as normal results flagged with isError
        if result.get("isError"):
            raise McpError(result["content"][0]["text"])
        # decode the content blocks for the caller
        return _decode_content(result["content"])

    def call(self, method: str, params: dict | None = None) -> dict:
        # build the json-rpc request envelope with the next sequential id
        request = {"jsonrpc": "2.0", "id": self._next_id, "method": method}
        # consume the request id
        self._next_id += 1
        # attach params when the request carries them
        if params is not None:
            request["params"] = params
        # expose the full protocol request at debug level
        _LOGGER.debug("mcp request: %s", json.dumps(request))
        # the conversation is one request and one reply, so a busy main thread
        # simply delays the answer and the read waits within the timeout
        reply = self._exchange(request)
        # surface json-rpc level errors as framework errors
        if "error" in reply:
            raise McpError(f"json-rpc error {reply['error'].get('code')}: {reply['error'].get('message')}")
        # return the result payload on success
        return reply["result"]

    def close(self) -> None:
        # release the socket so the server can drop the conversation
        self._connected = False
        self._buffer = b""
        if self._socket is not None:
            try:
                self._socket.close()
            except OSError:
                # a socket the server already dropped must not mask the outcome
                pass
            self._socket = None

    def _exchange(self, request: dict, timeout: float = DEFAULT_READ_TIMEOUT) -> dict:
        # write one framed request and read until the matching reply arrives
        if self._socket is None:
            raise McpError("the mcp client is not connected")
        # frame the request as one newline-delimited line
        line = json.dumps(request).encode("utf-8") + b"\n"
        try:
            self._socket.sendall(line)
        except OSError as error:
            raise McpError(f"the connection dropped before the request: {error}") from error
        # the deadline bounds a busy main thread, which serves one line per frame
        deadline = time.monotonic() + timeout
        while True:
            reply = self._pop_reply(request["id"])
            if reply is not None:
                _LOGGER.debug("mcp reply: %s", json.dumps(reply))
                return reply
            # stop at the deadline before the client hangs on a busy main thread
            if time.monotonic() >= deadline:
                self.close()
                raise McpError(f"timed out waiting for the reply to request {request['id']}")
            # poll the socket for readable data without blocking the loop
            ready = select.select([self._socket], [], [], min(0.5, timeout))
            if not ready[0]:
                continue
            try:
                chunk = self._socket.recv(65536)
            except OSError as error:
                self.close()
                raise McpError(f"the connection dropped while reading: {error}") from error
            # an empty read means the server closed the conversation
            if not chunk:
                self.close()
                raise McpError("the connection was closed by the server")
            # buffer the bytes and look for complete lines
            self._buffer += chunk

    def _pop_reply(self, request_id: int) -> dict | None:
        # scan the buffer for one complete json line matching the request id
        while b"\n" in self._buffer:
            line, self._buffer = self._buffer.split(b"\n", 1)
            # a line may still be partial json if the frame boundary arrived late
            try:
                message = json.loads(line.decode("utf-8"))
            except (json.JSONDecodeError, UnicodeDecodeError):
                continue
            # notifications carry no id or a null id and are skipped
            if message.get("id") != request_id:
                continue
            # the reply must be a json-rpc object with result or error
            if "result" in message or "error" in message:
                return message
        return None


def _decode_content(content: list[dict]) -> Any:
    # json tool results carry pretty-printed json inside the first text block
    if content and content[0].get("type") == "text":
        try:
            # decode the nested json document
            return json.loads(content[0]["text"])
        except json.JSONDecodeError:
            # fall back to the raw text when the block is not json
            return content[0]["text"]
    # image blocks carry the base64 encoded png payload
    if content and content[0].get("type") == "image":
        # decode the png bytes for the caller
        return base64.b64decode(content[0]["data"])
    # image and unknown block types are returned untouched
    return content

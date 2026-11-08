from .log import logger

_LOGGER = logger()

# the flags a tree dump line may carry after the name, each appended by the
# server as one space delimited token; a value that contains spaces is read
# until the first of these trailing flags, which is the boundary the server
# itself wrote when it appended the state after the text
_TRAILING_FLAGS = ("writable", "selection=", "read_only", "multiline", "expanded", "disabled", "selected", "focused", "indeterminate")


class AlborClient:
    # ui facade over the albor mcp transport; every method talks one tool call

    def __init__(self, mcp_client) -> None:
        # the transport that carries the json-rpc conversation
        self._mcp = mcp_client

    def server_info(self) -> dict:
        # expose the server info captured during the launch handshake
        return self._mcp.server_info()

    def list_tools(self) -> list[dict]:
        # return the tool definitions advertised by the server
        return self._mcp.list_tools()

    def tree(self) -> list[dict]:
        # dump the semantic tree as parsed nodes
        text = self._mcp.call_tool("semantic.tree")
        # the dump is one payload, which the parser walks line by line
        return parse_tree(text)

    def find(self, node_id: str) -> dict | None:
        # resolve one node by author assigned id, or None when the tree lacks it
        for node in self.tree():
            if node["id"] == node_id:
                return node
        return None

    def invoke(self, node_id: str, action: str = "activate") -> None:
        # run one registered action on the node, which presses the control
        self._mcp.call_tool("semantic.invoke", {"id": node_id, "action": action})

    def set_value(self, node_id: str, value: str) -> None:
        # write the text an editable node edits
        self._mcp.call_tool("semantic.set_value", {"id": node_id, "value": value})

    def set_focused(self, node_id: str, focused: bool) -> None:
        # record what assistive technology reported about focus
        self._mcp.call_tool("semantic.set_focused", {"id": node_id, "value": "1" if focused else "0"})

    def screenshot(self) -> bytes:
        # capture a png screenshot of the active window
        data = self._mcp.call_tool("capture.screenshot", {"format": "png"})
        # the transport decodes the image block into png bytes
        return data


def parse_tree(text: str) -> list[dict]:
    # parse the dump into node dictionaries, one per non empty line
    nodes: list[dict] = []
    for line in text.splitlines():
        # blank lines carry no node and are skipped rather than misread
        if not line.strip():
            continue
        nodes.append(parse_node(line))
    return nodes


def parse_node(line: str) -> dict:
    # parse one dump line: role, id, name, then the trailing state flags
    # the role is the first token and the id the second
    role, _, remainder = line.partition(" ")
    node_id, _, remainder = remainder.partition(" ")
    node = {"role": role, "id": node_id}
    # the name may carry spaces, so the boundary between the name and the
    # value flag is the first unescaped value= token in the remainder
    before, found, tail = _find_boundary(remainder, ("value=",))
    if found == "value=":
        # the name ends at the value flag, and the value itself ends where
        # the first trailing state flag begins
        node["name"] = _unescape(before)
        # strip the value= token from the tail so the tail starts at the value
        tail = tail[len("value="):]
        value_text, _, flag_tail = _find_boundary(tail, _TRAILING_FLAGS)
        node["value"] = _unescape(value_text)
        # the remaining flags are walked token by token
        _parse_flags(node, flag_tail)
    else:
        # a node without a value may still carry plain state flags
        before, _, flag_tail = _find_boundary(remainder, _TRAILING_FLAGS)
        node["name"] = _unescape(before)
        _parse_flags(node, flag_tail)
    return node


def _find_boundary(text: str, needles: tuple[str, ...]) -> tuple[str, str, str]:
    # find the first unescaped occurrence of one needle token in a set;
    # returns the text before, the token itself, and the tail starting at the
    # token, so the caller decides whether to strip it
    index = 0
    while index < len(text):
        # a backslash escapes the following character, so escaped positions
        # never open a boundary even when they spell a flag token
        if text[index] == "\\" and index + 1 < len(text):
            index += 2
            continue
        for candidate in needles:
            # the token must be space delimited, so a name like "readable"
            # does not open the read_only flag
            if not text.startswith(candidate, index):
                continue
            if index > 0 and text[index - 1] != " ":
                continue
            return text[:index].rstrip(" "), candidate, text[index:]
        index += 1
    # no boundary found, so the whole text is the first element
    return text, "", ""


def _parse_flags(node: dict, text: str) -> None:
    # walk the flag tail token by token into the node dictionary
    index = 0
    while index < len(text):
        # skip the separating spaces
        if text[index] == " ":
            index += 1
            continue
        if text.startswith("selection=", index):
            index += len("selection=")
            value, index = _read_token(text, index)
            # the range is half open start:end
            start, _, end = value.partition(":")
            node["selection"] = (int(start), int(end))
        elif text.startswith("value=", index):
            # a value flag inside the tail is unusual but read for completeness
            index += len("value=")
            value, index = _read_token(text, index)
            node["value"] = _unescape(value)
        elif text.startswith("writable", index):
            index += len("writable")
            node["writable"] = True
        elif text.startswith("read_only", index):
            index += len("read_only")
            node["read_only"] = True
        elif text.startswith("multiline", index):
            index += len("multiline")
            node["multiline"] = True
        elif text.startswith("expanded", index):
            index += len("expanded")
            node["expanded"] = True
        elif text.startswith("disabled", index):
            index += len("disabled")
            node["disabled"] = True
        elif text.startswith("selected", index):
            index += len("selected")
            node["selected"] = True
        elif text.startswith("focused", index):
            index += len("focused")
            node["focused"] = True
        elif text.startswith("indeterminate", index):
            index += len("indeterminate")
            node["indeterminate"] = True
        else:
            # an unknown token is skipped one token, so a newer server flag
            # never wedges the parser
            _, index = _read_token(text, index)


def _read_token(text: str, index: int) -> tuple[str, int]:
    # read one space delimited token from the index
    end = index
    while end < len(text) and text[end] != " ":
        end += 1
    return text[index:end], end


def _unescape(text: str) -> str:
    # decode the escapes the server wrote, which are the four control pairs
    result = ""
    index = 0
    while index < len(text):
        char = text[index]
        if char == "\\" and index + 1 < len(text):
            # decode the two character escape the server wrote
            next_char = text[index + 1]
            result += {"n": "\n", "r": "\r", "t": "\t"}.get(next_char, next_char)
            index += 2
        else:
            result += char
            index += 1
    return result

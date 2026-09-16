import base64

from .errors import McpError
from .mcp_client import McpClient


class SlintClient:

    def __init__(self, mcp: McpClient) -> None:
        # mcp provides the json-rpc transport to the embedded server
        self._mcp = mcp

    def server_info(self) -> dict | None:
        # return the server info captured during the launch handshake
        return self._mcp.server_info()

    def list_tools(self) -> list[dict]:
        # return the tool definitions advertised by the server
        return self._mcp.list_tools()

    def list_windows(self) -> list[dict]:
        # list all open windows through the mcp server
        # the server omits empty repeated fields so default to an empty list
        return self._mcp.call_tool("list_windows").get("windowHandles", [])

    def find_elements_by_id(self, elements_id: str) -> list[dict]:
        # find elements by qualified id inside the first open window
        # the server omits empty repeated fields so default to an empty list
        result = self._mcp.call_tool("find_elements_by_id", {"windowHandle": self._first_window(), "elementsId": elements_id})
        return result.get("elementHandles", [])

    def find_by_role(self, role: str) -> list[dict]:
        # find the first element with the accessible role inside the first open window
        root = self.get_window_properties()["rootElementHandle"]
        # build the query pipeline recursing through all descendants
        query_stack = [{"matchDescendants": True}, {"matchElementAccessibleRole": role}]
        # find_all duplicates elements per nesting path so use the first match only
        result = self._mcp.call_tool("query_element_descendants", {"elementHandle": root, "queryStack": query_stack, "findAll": False})
        # the server omits empty repeated fields so default to an empty list
        return result.get("elementHandles", [])

    def find_by_type(self, type_name: str) -> list[dict]:
        # find elements by their slint type name in the window in document order
        root = self.get_window_properties()["rootElementHandle"]
        # delegate to the subtree variant rooted at the window root
        return self.find_by_type_in(root, type_name)

    def find_by_type_in(self, element_handle: dict, type_name: str) -> list[dict]:
        # find elements by their slint type name within a subtree in document order
        # the server clamps max elements to 1000; large dialogs need the full tree
        # (the simulation parameters dialog alone spans ~430 elements)
        tree = self._mcp.call_tool("get_element_tree", {"elementHandle": element_handle, "maxElements": 1000})
        # collect the handles of the elements whose primary type matches
        return [e["handle"] for e in tree.get("elements", []) if e.get("typeNamesAndIds", [{}])[0].get("typeName") == type_name]

    def get_window_properties(self) -> dict:
        # read the properties of the first open window
        return self._mcp.call_tool("get_window_properties", {"windowHandle": self._first_window()})

    def get_element_properties(self, element_handle: dict) -> dict:
        # read the full properties of a single element handle
        return self._mcp.call_tool("get_element_properties", {"elementHandle": element_handle})

    def click_element(self, element_handle: dict, action: str = "SingleClick", button: str = "Left") -> None:
        # simulate a mouse click on the element handle
        self._mcp.call_tool("click_element", {"elementHandle": element_handle, "action": action, "button": button})

    def invoke_accessibility_action(self, element_handle: dict, action: str = "Default_") -> None:
        # invoke a semantic accessibility action on the element handle
        self._mcp.call_tool("invoke_accessibility_action", {"elementHandle": element_handle, "action": action})

    def drag_element(self, element_handle: dict, target_x: float, target_y: float) -> None:
        # drag from the element center to the logical target position
        self._mcp.call_tool("drag_element", {"elementHandle": element_handle, "target": {"x": target_x, "y": target_y}})

    def set_element_value(self, element_handle: dict, value: str) -> None:
        # set the accessible value of the element handle
        self._mcp.call_tool("set_element_value", {"elementHandle": element_handle, "value": value})

    def dispatch_key_event(self, text: str, event_type: str = "PressAndRelease") -> None:
        # dispatch a key event to the first open window
        self._mcp.call_tool("dispatch_key_event", {"windowHandle": self._first_window(), "text": text, "eventType": event_type})

    def take_screenshot(self) -> bytes:
        # capture a png screenshot of the first open window
        result = self._mcp.call_tool("take_screenshot", {"windowHandle": self._first_window()})
        # locate the image content block in the response
        for block in result:
            # image blocks carry the base64 encoded png payload
            if block.get("type") == "image":
                # decode the png bytes for the caller
                return base64.b64decode(block["data"])
        # fail when the server returned no image block
        raise McpError("screenshot response contained no image block")

    def get_element_tree(self, max_elements: int = 50) -> dict:
        # traverse the element tree rooted at the first window root element
        root = self.get_window_properties()["rootElementHandle"]
        return self._mcp.call_tool("get_element_tree", {"elementHandle": root, "maxElements": max_elements})

    def get_window(self) -> dict | None:
        # return the first open window handle
        return self._first_window()

    def get_status(self) -> str:
        # placeholder until a real status source is exposed by the server
        return "running"

    def _first_window(self) -> dict | None:
        # pick the first window handle from the server window list
        windows = self.list_windows()
        # report no window when the application has none open
        return windows[0] if windows else None

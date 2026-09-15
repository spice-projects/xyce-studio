import base64
import unittest

from slint_automation.errors import McpError
from slint_automation.slint_client import SlintClient


class FakeMcpClient:

    def __init__(self, responses: dict[str, dict]) -> None:
        # responses maps tool names to the canned tool results
        self._responses = responses
        # _calls records every invoked tool name with its arguments
        self._calls: list[tuple[str, dict | None]] = []

    def call_tool(self, name: str, arguments: dict | None = None) -> dict:
        # record the invocation for later assertions
        self._calls.append((name, arguments))
        # return the canned result for the requested tool
        return self._responses.get(name, {})

    def calls(self) -> list[tuple[str, dict | None]]:
        # return the recorded tool invocations
        return self._calls

    def server_info(self) -> dict:
        # serve the canned server info
        return {"serverInfo": {"name": "fake-mcp"}}

    def list_tools(self) -> list[dict]:
        # serve the canned tool definitions
        return [{"name": "list_windows"}]


class SlintClientFacadeChecks(unittest.TestCase):

    def test_get_status_returns_running(self) -> None:
        # arrange
        client = SlintClient(FakeMcpClient({}))
        # act
        status = client.get_status()
        # assert
        self.assertEqual(status, "running")

    def test_get_window_returns_first_handle(self) -> None:
        # arrange
        first = {"index": "1", "generation": "1"}
        second = {"index": "2", "generation": "1"}
        client = SlintClient(FakeMcpClient({"list_windows": {"windowHandles": [first, second]}}))
        # act
        window = client.get_window()
        # assert
        self.assertEqual(window, first)

    def test_get_window_returns_none_without_windows(self) -> None:
        # arrange
        client = SlintClient(FakeMcpClient({"list_windows": {}}))
        # act
        window = client.get_window()
        # assert
        self.assertIsNone(window)

    def test_list_windows_defaults_to_empty(self) -> None:
        # arrange
        client = SlintClient(FakeMcpClient({"list_windows": {}}))
        # act
        windows = client.list_windows()
        # assert
        self.assertEqual(windows, [])

    def test_find_elements_by_id_defaults_to_empty(self) -> None:
        # arrange
        client = SlintClient(FakeMcpClient({"list_windows": {"windowHandles": [{"index": "1", "generation": "1"}]}, "find_elements_by_id": {}}))
        # act
        handles = client.find_elements_by_id("App::missing")
        # assert
        self.assertEqual(handles, [])

    def test_find_elements_by_id_passes_first_window(self) -> None:
        # arrange
        handle = {"index": "1", "generation": "1"}
        mcp = FakeMcpClient({"list_windows": {"windowHandles": [handle]}, "find_elements_by_id": {"elementHandles": [{"index": "9", "generation": "1"}]}})
        client = SlintClient(mcp)
        # act
        handles = client.find_elements_by_id("App::button")
        # assert
        self.assertEqual(handles, [{"index": "9", "generation": "1"}])
        self.assertEqual(mcp.calls()[1][1]["windowHandle"], handle)
        self.assertEqual(mcp.calls()[1][1]["elementsId"], "App::button")

    def test_get_element_tree_uses_root_element_handle(self) -> None:
        # arrange
        root = {"index": "5", "generation": "1"}
        mcp = FakeMcpClient({"get_window_properties": {"rootElementHandle": root}, "get_element_tree": {"totalCount": 3}})
        client = SlintClient(mcp)
        # act
        tree = client.get_element_tree()
        # assert
        self.assertEqual(tree["totalCount"], 3)
        self.assertEqual(mcp.calls()[2][1]["elementHandle"], root)
        self.assertEqual(mcp.calls()[2][1]["maxElements"], 50)

    def test_get_element_tree_passes_custom_limit(self) -> None:
        # arrange
        mcp = FakeMcpClient({"get_window_properties": {"rootElementHandle": {"index": "5", "generation": "1"}}, "get_element_tree": {"totalCount": 0}})
        client = SlintClient(mcp)
        # act
        client.get_element_tree(max_elements=10)
        # assert
        self.assertEqual(mcp.calls()[2][1]["maxElements"], 10)

    def test_server_info_delegates_to_mcp(self) -> None:
        # arrange
        client = SlintClient(FakeMcpClient({}))
        # act
        info = client.server_info()
        # assert
        self.assertEqual(info["serverInfo"]["name"], "fake-mcp")

    def test_list_tools_delegates_to_mcp(self) -> None:
        # arrange
        client = SlintClient(FakeMcpClient({}))
        # act
        tools = client.list_tools()
        # assert
        self.assertEqual(tools[0]["name"], "list_windows")

    def test_click_element_passes_handle_and_defaults(self) -> None:
        # arrange
        mcp = FakeMcpClient({})
        client = SlintClient(mcp)
        # act
        client.click_element({"index": "3", "generation": "1"})
        # assert
        self.assertEqual(mcp.calls()[0], ("click_element", {"elementHandle": {"index": "3", "generation": "1"}, "action": "SingleClick", "button": "Left"}))

    def test_click_element_passes_action_and_button(self) -> None:
        # arrange
        mcp = FakeMcpClient({})
        client = SlintClient(mcp)
        # act
        client.click_element({"index": "3", "generation": "1"}, action="DoubleClick", button="Middle")
        # assert
        self.assertEqual(mcp.calls()[0][1]["action"], "DoubleClick")
        self.assertEqual(mcp.calls()[0][1]["button"], "Middle")

    def test_invoke_accessibility_action_passes_handle_and_action(self) -> None:
        # arrange
        mcp = FakeMcpClient({})
        client = SlintClient(mcp)
        # act
        client.invoke_accessibility_action({"index": "5", "generation": "1"})
        # assert: the default action is sent on the element handle
        self.assertEqual(mcp.calls()[0], ("invoke_accessibility_action", {"elementHandle": {"index": "5", "generation": "1"}, "action": "Default_"}))

    def test_drag_element_passes_handle_and_target(self) -> None:
        # arrange
        mcp = FakeMcpClient({})
        client = SlintClient(mcp)
        # act
        client.drag_element({"index": "5", "generation": "1"}, 120.0, 80.0)
        # assert
        self.assertEqual(mcp.calls()[0], ("drag_element", {"elementHandle": {"index": "5", "generation": "1"}, "target": {"x": 120.0, "y": 80.0}}))

    def test_get_element_properties_passes_handle(self) -> None:
        # arrange
        handle = {"index": "7", "generation": "1"}
        mcp = FakeMcpClient({"get_element_properties": {"accessibleLabel": "Run"}})
        client = SlintClient(mcp)
        # act
        properties = client.get_element_properties(handle)
        # assert
        self.assertEqual(properties["accessibleLabel"], "Run")
        self.assertEqual(mcp.calls()[0], ("get_element_properties", {"elementHandle": handle}))

    def test_get_window_properties_uses_first_window(self) -> None:
        # arrange
        handle = {"index": "1", "generation": "1"}
        mcp = FakeMcpClient({"list_windows": {"windowHandles": [handle]}, "get_window_properties": {"title": "Xyce Studio"}})
        client = SlintClient(mcp)
        # act
        properties = client.get_window_properties()
        # assert
        self.assertEqual(properties["title"], "Xyce Studio")
        self.assertEqual(mcp.calls()[1], ("get_window_properties", {"windowHandle": handle}))

    def test_take_screenshot_decodes_the_image_block(self) -> None:
        # arrange
        encoded = base64.b64encode(b"png-bytes").decode("utf-8")
        mcp = FakeMcpClient({"list_windows": {"windowHandles": [{"index": "1", "generation": "1"}]}, "take_screenshot": [{"type": "image", "data": encoded}]})
        client = SlintClient(mcp)
        # act
        png = client.take_screenshot()
        # assert
        self.assertEqual(png, b"png-bytes")

    def test_take_screenshot_raises_without_image_block(self) -> None:
        # arrange
        mcp = FakeMcpClient({"list_windows": {"windowHandles": [{"index": "1", "generation": "1"}]}, "take_screenshot": [{"type": "text", "text": "no image"}]})
        client = SlintClient(mcp)
        # act / assert
        with self.assertRaises(McpError) as context:
            client.take_screenshot()
        self.assertIn("no image block", str(context.exception))

    def test_set_element_value_passes_args(self) -> None:
        # arrange
        mcp = FakeMcpClient({})
        client = SlintClient(mcp)
        # act
        client.set_element_value({"index": "8", "generation": "1"}, "hello")
        # assert
        self.assertEqual(mcp.calls()[0], ("set_element_value", {"elementHandle": {"index": "8", "generation": "1"}, "value": "hello"}))

    def test_dispatch_key_event_uses_first_window(self) -> None:
        # arrange
        handle = {"index": "1", "generation": "1"}
        mcp = FakeMcpClient({"list_windows": {"windowHandles": [handle]}})
        client = SlintClient(mcp)
        # act
        client.dispatch_key_event("Return")
        # assert
        self.assertEqual(mcp.calls()[1], ("dispatch_key_event", {"windowHandle": handle, "text": "Return", "eventType": "PressAndRelease"}))

    def test_find_by_role_builds_query_pipeline(self) -> None:
        # arrange
        root = {"index": "1", "generation": "1"}
        mcp = FakeMcpClient({"get_window_properties": {"rootElementHandle": root}, "query_element_descendants": {"elementHandles": [{"index": "7", "generation": "1"}]}})
        client = SlintClient(mcp)
        # act
        handles = client.find_by_role("Button")
        # assert
        self.assertEqual(handles, [{"index": "7", "generation": "1"}])
        self.assertEqual(mcp.calls()[2][1]["elementHandle"], root)
        self.assertEqual(mcp.calls()[2][1]["queryStack"], [{"matchDescendants": True}, {"matchElementAccessibleRole": "Button"}])
        self.assertEqual(mcp.calls()[2][1]["findAll"], False)

    def test_find_by_role_defaults_to_empty(self) -> None:
        # arrange
        mcp = FakeMcpClient({"get_window_properties": {"rootElementHandle": {"index": "1", "generation": "1"}}, "query_element_descendants": {}})
        client = SlintClient(mcp)
        # act
        handles = client.find_by_role("Slider")
        # assert
        self.assertEqual(handles, [])

    def test_find_by_type_filters_by_primary_type_name(self) -> None:
        # arrange
        root = {"index": "1", "generation": "1"}
        tree = {"elements": [{"handle": {"index": "2", "generation": "1"}, "typeNamesAndIds": [{"typeName": "ToolbarButton"}]}, {"handle": {"index": "3", "generation": "1"}, "typeNamesAndIds": [{"typeName": "Rectangle"}]}, {"handle": {"index": "4", "generation": "1"}, "typeNamesAndIds": [{"typeName": "ToolbarButton"}]}]}
        mcp = FakeMcpClient({"list_windows": {"windowHandles": [root]}, "get_window_properties": {"rootElementHandle": root}, "get_element_tree": tree})
        client = SlintClient(mcp)
        # act
        handles = client.find_by_type("ToolbarButton")
        # assert
        self.assertEqual(handles, [{"index": "2", "generation": "1"}, {"index": "4", "generation": "1"}])

    def test_find_by_type_ignores_base_type_names(self) -> None:
        # arrange: base entries carry id root and must not match the primary type
        root = {"index": "1", "generation": "1"}
        tree = {"elements": [{"handle": {"index": "2", "generation": "1"}, "typeNamesAndIds": [{"typeName": "ToolbarButton"}, {"typeName": "Rectangle", "id": "root"}]}]}
        mcp = FakeMcpClient({"list_windows": {"windowHandles": [root]}, "get_window_properties": {"rootElementHandle": root}, "get_element_tree": tree})
        client = SlintClient(mcp)
        # act
        handles = client.find_by_type("Rectangle")
        # assert
        self.assertEqual(handles, [])

    def test_find_by_type_defaults_to_empty(self) -> None:
        # arrange
        root = {"index": "1", "generation": "1"}
        mcp = FakeMcpClient({"list_windows": {"windowHandles": [root]}, "get_window_properties": {"rootElementHandle": root}, "get_element_tree": {"elements": [], "totalCount": 0}})
        client = SlintClient(mcp)
        # act
        handles = client.find_by_type("ToolbarButton")
        # assert
        self.assertEqual(handles, [])

    def test_find_by_type_in_filters_subtree(self) -> None:
        # arrange
        root = {"index": "1", "generation": "1"}
        tree = {"elements": [{"handle": {"index": "9", "generation": "1"}, "typeNamesAndIds": [{"typeName": "Image"}]}, {"handle": {"index": "10", "generation": "1"}, "typeNamesAndIds": [{"typeName": "TouchArea"}]}]}
        mcp = FakeMcpClient({"get_element_tree": tree})
        client = SlintClient(mcp)
        # act
        handles = client.find_by_type_in(root, "Image")
        # assert
        self.assertEqual(handles, [{"index": "9", "generation": "1"}])
        self.assertEqual(mcp.calls()[0][1]["elementHandle"], root)

    def test_find_by_type_delegates_to_window_root(self) -> None:
        # arrange
        root = {"index": "1", "generation": "1"}
        tree = {"elements": [{"handle": {"index": "2", "generation": "1"}, "typeNamesAndIds": [{"typeName": "ToolbarButton"}]}]}
        mcp = FakeMcpClient({"list_windows": {"windowHandles": [root]}, "get_window_properties": {"rootElementHandle": root}, "get_element_tree": tree})
        client = SlintClient(mcp)
        # act
        handles = client.find_by_type("ToolbarButton")
        # assert
        self.assertEqual(handles, [{"index": "2", "generation": "1"}])
        self.assertEqual(mcp.calls()[2][1]["elementHandle"], root)

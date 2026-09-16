import unittest
from typing import Any

from slint_automation.errors import LocatorError, McpError
from slint_automation.locator import Locator
from slint_automation.locator import LocatorCollection


class StaleHandleClient:

    def __init__(self, handles: list[dict]) -> None:
        # handles is the canned handle list returned by every resolution
        self._handles = handles
        # _click_attempts counts the click invocations including stale ones
        self._click_attempts = 0
        # _fill_attempts counts the fill invocations including stale ones
        self._fill_attempts = 0
        # _drag_attempts counts the drag invocations including stale ones
        self._drag_attempts = 0
        # _resolve_count counts the element resolutions
        self._resolve_count = 0
        # _clicked records the handle of the successful click
        self._clicked: dict | None = None
        # _filled records the value of the successful fill
        self._filled: str | None = None
        # _dragged records the target of the successful drag
        self._dragged: tuple[float, float] | None = None

    def find_elements_by_id(self, elements_id: str) -> list[dict]:
        # count the resolution
        self._resolve_count += 1
        # return the canned handle list
        return list(self._handles)

    def click_element(self, element_handle: dict, action: str = "SingleClick", button: str = "Left") -> None:
        # count the attempt
        self._click_attempts += 1
        # fail the first attempt with a stale handle like the live server does
        if self._click_attempts == 1:
            raise McpError("Error: Invalid handle")
        # record the handle that the retried click used
        self._clicked = element_handle

    def set_element_value(self, element_handle: dict, value: str) -> None:
        # count the attempt
        self._fill_attempts += 1
        # fail the first attempt with a stale handle like the live server does
        if self._fill_attempts == 1:
            raise McpError("Error: Invalid handle")
        # record the value that the retried fill used
        self._filled = value

    def drag_element(self, element_handle: dict, target_x: float, target_y: float) -> None:
        # count the attempt
        self._drag_attempts += 1
        # fail the first attempt with a stale handle like the live server does
        if self._drag_attempts == 1:
            raise McpError("Error: Invalid handle")
        # record the target that the retried drag used
        self._dragged = (target_x, target_y)

    def resolve_count(self) -> int:
        # report how often elements were resolved
        return self._resolve_count

    def click_attempts(self) -> int:
        # report how many clicks were attempted
        return self._click_attempts

    def clicked(self) -> dict | None:
        # report the handle of the successful click
        return self._clicked

    def fill_attempts(self) -> int:
        # report how many fills were attempted
        return self._fill_attempts

    def filled(self) -> str | None:
        # report the value of the successful fill
        return self._filled

    def drag_attempts(self) -> int:
        # report how many drags were attempted
        return self._drag_attempts

    def dragged(self) -> tuple[float, float] | None:
        # report the target of the successful drag
        return self._dragged


class UnrelatedErrorClient:

    def find_elements_by_id(self, elements_id: str) -> list[dict]:
        # return the canned handle list
        return [{"index": "3", "generation": "1"}]

    def click_element(self, element_handle: dict, action: str = "SingleClick", button: str = "Left") -> None:
        # fail with an error that must not trigger the stale handle retry
        raise McpError("connection to 127.0.0.1 failed")


class FakeClient:

    def __init__(self, handles: dict[str, list[dict]], properties: dict[str, dict], subtree: dict[tuple[str, str], list[dict]] | None = None) -> None:
        # handles maps the requested id to the canned handle list
        self._handles = handles
        # subtree maps scope handle index and type to the canned child handles
        self._subtree = subtree or {}
        # properties maps a handle index to the canned property dict
        self._properties = properties
        # _resolve_count tracks how often elements were resolved
        self._resolve_count = 0
        # _clicks records every click invocation
        self._clicks: list[tuple[dict, str, str]] = []
        # _fills records every fill invocation
        self._fills: list[tuple[dict, str]] = []
        # _drags records every drag invocation
        self._drags: list[tuple[dict, float, float]] = []
        # _keys records every key dispatch invocation
        self._keys: list[tuple[str, str]] = []

    def find_elements_by_id(self, elements_id: str) -> list[dict]:
        # count the resolution calls for the lazy semantics test
        self._resolve_count += 1
        # return the canned handles for the requested id
        return self._handles.get(elements_id, [])

    def find_by_role(self, role: str) -> list[dict]:
        # count the resolution calls for the lazy semantics test
        self._resolve_count += 1
        # return the canned handles for the requested role
        return self._handles.get(role, [])

    def find_by_type(self, type_name: str) -> list[dict]:
        # count the resolution calls for the lazy semantics test
        self._resolve_count += 1
        # return the canned handles for the requested type
        return self._handles.get(type_name, [])

    def find_by_type_in(self, element_handle: dict, type_name: str) -> list[dict]:
        # count the resolution calls for the lazy semantics test
        self._resolve_count += 1
        # return the canned subtree handles for the requested scope
        return self._subtree.get((element_handle["index"], type_name), [])

    def get_element_properties(self, element_handle: dict) -> dict:
        # return the canned properties for the requested handle
        return self._properties[element_handle["index"]]

    def click_element(self, element_handle: dict, action: str = "SingleClick", button: str = "Left") -> None:
        # record the click invocation for later assertions
        self._clicks.append((element_handle, action, button))

    def set_element_value(self, element_handle: dict, value: str) -> None:
        # record the fill invocation for later assertions
        self._fills.append((element_handle, value))

    def drag_element(self, element_handle: dict, target_x: float, target_y: float) -> None:
        # record the drag invocation for later assertions
        self._drags.append((element_handle, target_x, target_y))

    def dispatch_key_event(self, text: str, event_type: str = "PressAndRelease") -> None:
        # record the key dispatch invocation for later assertions
        self._keys.append((text, event_type))

    def resolve_count(self) -> int:
        # report how many times elements were resolved
        return self._resolve_count

    def clicks(self) -> list[tuple[dict, str, str]]:
        # return the recorded click invocations
        return self._clicks

    def fills(self) -> list[tuple[dict, str]]:
        # return the recorded fill invocations
        return self._fills

    def drags(self) -> list[tuple[dict, float, float]]:
        # return the recorded drag invocations
        return self._drags

    def keys(self) -> list[tuple[str, str]]:
        # return the recorded key dispatch invocations
        return self._keys


class LocatorResolutionChecks(unittest.TestCase):

    def test_locator_creation_does_not_resolve(self) -> None:
        # arrange
        client = FakeClient({}, {})
        # act
        Locator(client, "App::missing")
        # assert
        self.assertEqual(client.resolve_count(), 0)

    def test_exists_returns_true_for_known_element(self) -> None:
        # arrange
        client = FakeClient({"App::button": [{"index": "1", "generation": "1"}]}, {})
        locator = Locator(client, "App::button")
        # act
        exists = locator.exists()
        # assert
        self.assertTrue(exists)

    def test_exists_returns_false_for_unknown_element(self) -> None:
        # arrange
        client = FakeClient({}, {})
        locator = Locator(client, "App::missing")
        # act
        exists = locator.exists()
        # assert
        self.assertFalse(exists)

    def test_text_reads_accessible_value(self) -> None:
        # arrange
        client = FakeClient({"App::status": [{"index": "7", "generation": "1"}]}, {"7": {"accessibleValue": "Ready"}})
        locator = Locator(client, "App::status")
        # act
        text = locator.text()
        # assert
        self.assertEqual(text, "Ready")

    def test_text_falls_back_to_accessible_label(self) -> None:
        # arrange
        client = FakeClient({"App::status": [{"index": "7", "generation": "1"}]}, {"7": {"accessibleValue": "", "accessibleLabel": "Simulation ready"}})
        locator = Locator(client, "App::status")
        # act
        text = locator.text()
        # assert
        self.assertEqual(text, "Simulation ready")

    def test_text_returns_empty_for_missing_properties(self) -> None:
        # arrange
        client = FakeClient({"App::status": [{"index": "7", "generation": "1"}]}, {"7": {}})
        locator = Locator(client, "App::status")
        # act
        text = locator.text()
        # assert
        self.assertEqual(text, "")

    def test_property_reads_named_value(self) -> None:
        # arrange
        client = FakeClient({"App::button": [{"index": "3", "generation": "1"}]}, {"3": {"accessibleRole": "Button", "accessibleEnabled": True}})
        locator = Locator(client, "App::button")
        # act
        role = locator.property("accessibleRole")
        # assert
        self.assertEqual(role, "Button")

    def test_property_returns_none_for_unknown_name(self) -> None:
        # arrange
        client = FakeClient({"App::button": [{"index": "3", "generation": "1"}]}, {"3": {}})
        locator = Locator(client, "App::button")
        # act
        value: Any = locator.property("bogusProperty")
        # assert
        self.assertIsNone(value)

    def test_strict_resolution_raises_for_missing_element(self) -> None:
        # arrange
        client = FakeClient({}, {})
        locator = Locator(client, "App::missing")
        # act
        with self.assertRaises(LocatorError) as context:
            locator.text()
        # assert
        self.assertIn("element not found", str(context.exception))

    def test_strict_resolution_raises_for_ambiguous_element(self) -> None:
        # arrange
        client = FakeClient({"App::items": [{"index": "1", "generation": "1"}, {"index": "2", "generation": "1"}]}, {})
        locator = Locator(client, "App::items")
        # act
        with self.assertRaises(LocatorError) as context:
            locator.text()
        # assert
        self.assertIn("ambiguous element", str(context.exception))

    def test_locator_re_resolves_on_every_operation(self) -> None:
        # arrange
        client = FakeClient({"App::status": [{"index": "7", "generation": "1"}]}, {"7": {"accessibleValue": "Ready"}})
        locator = Locator(client, "App::status")
        # act
        locator.text()
        locator.text()
        # assert
        self.assertEqual(client.resolve_count(), 2)

    def test_click_passes_resolved_handle_and_defaults(self) -> None:
        # arrange
        client = FakeClient({"App::button": [{"index": "3", "generation": "1"}]}, {})
        locator = Locator(client, "App::button")
        # act
        locator.click()
        # assert
        self.assertEqual(client.clicks(), [({"index": "3", "generation": "1"}, "SingleClick", "Left")])

    def test_click_supports_action_and_button_override(self) -> None:
        # arrange
        client = FakeClient({"App::button": [{"index": "3", "generation": "1"}]}, {})
        locator = Locator(client, "App::button")
        # act
        locator.click(action="DoubleClick", button="Right")
        # assert
        self.assertEqual(client.clicks(), [({"index": "3", "generation": "1"}, "DoubleClick", "Right")])

    def test_click_resolves_fresh_before_acting(self) -> None:
        # arrange
        client = FakeClient({"App::button": [{"index": "3", "generation": "1"}]}, {})
        locator = Locator(client, "App::button")
        # act
        locator.click()
        # assert
        self.assertEqual(client.resolve_count(), 1)

    def test_click_missing_element_raises(self) -> None:
        # arrange
        client = FakeClient({}, {})
        locator = Locator(client, "App::missing")
        # act
        with self.assertRaises(LocatorError) as context:
            locator.click()
        # assert
        self.assertIn("element not found", str(context.exception))
        self.assertEqual(client.clicks(), [])

    def test_fill_passes_value_to_element(self) -> None:
        # arrange
        client = FakeClient({"App::input": [{"index": "8", "generation": "1"}]}, {})
        locator = Locator(client, "App::input")
        # act
        locator.fill("test-project.kicad_pro")
        # assert
        self.assertEqual(client.fills(), [({"index": "8", "generation": "1"}, "test-project.kicad_pro")])

    def test_fill_missing_element_raises(self) -> None:
        # arrange
        client = FakeClient({}, {})
        locator = Locator(client, "App::missing")
        # act
        with self.assertRaises(LocatorError) as context:
            locator.fill("text")
        # assert
        self.assertIn("element not found", str(context.exception))
        self.assertEqual(client.fills(), [])

    def test_press_resolves_then_dispatches(self) -> None:
        # arrange
        client = FakeClient({"App::input": [{"index": "8", "generation": "1"}]}, {})
        locator = Locator(client, "App::input")
        # act
        locator.press("Return")
        # assert
        self.assertEqual(client.resolve_count(), 1)
        self.assertEqual(client.keys(), [("Return", "PressAndRelease")])

    def test_press_supports_event_type_override(self) -> None:
        # arrange
        client = FakeClient({"App::input": [{"index": "8", "generation": "1"}]}, {})
        locator = Locator(client, "App::input")
        # act
        locator.press("Control", event_type="Press")
        # assert
        self.assertEqual(client.keys(), [("Control", "Press")])

    def test_drag_passes_resolved_handle_and_target(self) -> None:
        # arrange
        client = FakeClient({"App::chart": [{"index": "6", "generation": "1"}]}, {})
        locator = Locator(client, "App::chart")
        # act
        locator.drag(150.0, 90.0)
        # assert
        self.assertEqual(client.drags(), [({"index": "6", "generation": "1"}, 150.0, 90.0)])

    def test_drag_missing_element_raises(self) -> None:
        # arrange
        client = FakeClient({}, {})
        locator = Locator(client, "App::missing")
        # act / assert
        with self.assertRaises(LocatorError) as context:
            locator.drag(10.0, 10.0)
        self.assertIn("element not found", str(context.exception))
        self.assertEqual(client.drags(), [])

    def test_count_returns_number_of_current_matches(self) -> None:
        # arrange
        client = FakeClient({"App::items": [{"index": "1", "generation": "1"}, {"index": "2", "generation": "1"}]}, {})
        locator = Locator(client, "App::items")
        # act
        count = locator.count()
        # assert
        self.assertEqual(count, 2)

    def test_id_locator_describes_by_id(self) -> None:
        # arrange
        client = FakeClient({}, {})
        locator = Locator(client, "App::status")
        # act
        description = locator.describe()
        # assert
        self.assertEqual(description, "App::status")

    def test_role_locator_exists_when_matches_found(self) -> None:
        # arrange
        client = FakeClient({"Button": [{"index": "3", "generation": "1"}]}, {})
        locator = Locator(client, role="Button")
        # act
        exists = locator.exists()
        # assert
        self.assertTrue(exists)

    def test_role_locator_exists_false_without_matches(self) -> None:
        # arrange
        client = FakeClient({}, {})
        locator = Locator(client, role="Button")
        # act
        exists = locator.exists()
        # assert
        self.assertFalse(exists)

    def test_role_locator_strict_resolution_raises_when_ambiguous(self) -> None:
        # arrange
        client = FakeClient({"Text": [{"index": "1", "generation": "1"}, {"index": "2", "generation": "1"}]}, {})
        locator = Locator(client, role="Text")
        # act / assert
        with self.assertRaises(LocatorError) as context:
            locator.text()
        self.assertIn("ambiguous element: role Text", str(context.exception))

    def test_role_locator_raises_when_missing(self) -> None:
        # arrange
        client = FakeClient({}, {})
        locator = Locator(client, role="Slider")
        # act / assert
        with self.assertRaises(LocatorError) as context:
            locator.text()
        self.assertIn("element not found: role Slider", str(context.exception))

    def test_role_locator_describes_by_role(self) -> None:
        # arrange
        client = FakeClient({}, {})
        locator = Locator(client, role="Button")
        # act
        description = locator.describe()
        # assert
        self.assertEqual(description, "role Button")

    def test_locator_requires_exactly_one_selector(self) -> None:
        # arrange
        client = FakeClient({}, {})
        # act / assert
        with self.assertRaises(LocatorError):
            Locator(client)
        with self.assertRaises(LocatorError):
            Locator(client, elements_id="App::button", role="Button")
        with self.assertRaises(LocatorError):
            Locator(client, elements_id="App::button", type_name="Button")
        with self.assertRaises(LocatorError):
            Locator(client, elements_id="App::button", ordinal=0)

    def test_type_locator_exists_when_matches_found(self) -> None:
        # arrange
        client = FakeClient({"ToolbarButton": [{"index": "3", "generation": "1"}]}, {})
        locator = Locator(client, type_name="ToolbarButton")
        # act
        exists = locator.exists()
        # assert
        self.assertTrue(exists)

    def test_type_locator_describes_type_and_ordinal(self) -> None:
        # arrange
        client = FakeClient({}, {})
        locator = Locator(client, type_name="ToolbarButton", ordinal=1)
        # act
        description = locator.describe()
        # assert
        self.assertEqual(description, "type ToolbarButton[1]")

    def test_type_locator_nth_resolves_requested_occurrence(self) -> None:
        # arrange
        first = {"index": "3", "generation": "1"}
        second = {"index": "7", "generation": "1"}
        client = FakeClient({"ToolbarButton": [first, second]}, {})
        collection = LocatorCollection(client, "ToolbarButton")
        # act
        collection.nth(1).click()
        # assert
        self.assertEqual(client.clicks(), [(second, "SingleClick", "Left")])

    def test_type_locator_nth_beyond_count_raises(self) -> None:
        # arrange
        client = FakeClient({"ToolbarButton": [{"index": "3", "generation": "1"}]}, {})
        collection = LocatorCollection(client, "ToolbarButton")
        # act / assert
        with self.assertRaises(LocatorError) as context:
            collection.nth(1).click()
        self.assertIn("matched only 1 elements", str(context.exception))
        self.assertEqual(client.clicks(), [])

    def test_type_locator_strict_without_ordinal_raises_when_multiple(self) -> None:
        # arrange
        client = FakeClient({"ToolbarButton": [{"index": "3", "generation": "1"}, {"index": "7", "generation": "1"}]}, {})
        locator = Locator(client, type_name="ToolbarButton")
        # act / assert
        with self.assertRaises(LocatorError) as context:
            locator.click()
        self.assertIn("ambiguous element: type ToolbarButton", str(context.exception))

    def test_locator_collection_counts_matches(self) -> None:
        # arrange
        client = FakeClient({"ToolbarButton": [{"index": "3", "generation": "1"}, {"index": "7", "generation": "1"}, {"index": "11", "generation": "1"}]}, {})
        collection = LocatorCollection(client, "ToolbarButton")
        # act
        count = collection.count()
        # assert
        self.assertEqual(count, 3)

    def test_locator_collection_all_returns_positional_locators(self) -> None:
        # arrange
        client = FakeClient({"ToolbarButton": [{"index": "3", "generation": "1"}, {"index": "7", "generation": "1"}]}, {})
        collection = LocatorCollection(client, "ToolbarButton")
        # act
        locators = collection.all()
        # assert
        self.assertEqual(len(locators), 2)
        locators[1].click()
        self.assertEqual(client.clicks(), [({"index": "7", "generation": "1"}, "SingleClick", "Left")])

    def test_child_locator_resolves_within_scope(self) -> None:
        # arrange
        button = {"index": "7", "generation": "1"}
        icon = {"index": "8", "generation": "1"}
        client = FakeClient({"ToolbarButton": [button]}, {}, subtree={("7", "Image"): [icon]})
        tool = Locator(client, type_name="ToolbarButton", ordinal=0)
        # act
        child = tool.child("Image")
        # assert
        self.assertTrue(child.exists())
        child.click()
        self.assertEqual(client.clicks(), [(icon, "SingleClick", "Left")])

    def test_child_locator_raises_when_scope_missing(self) -> None:
        # arrange
        client = FakeClient({}, {})
        tool = Locator(client, type_name="ToolbarButton", ordinal=0)
        # act / assert
        with self.assertRaises(LocatorError) as context:
            tool.child("Image").exists()
        self.assertIn("element not found: type ToolbarButton[0]", str(context.exception))

    def test_child_locator_describes_scope(self) -> None:
        # arrange
        client = FakeClient({"ToolbarButton": [{"index": "7", "generation": "1"}]}, {})
        tool = Locator(client, type_name="ToolbarButton", ordinal=0)
        # act
        description = tool.child("Image").describe()
        # assert
        self.assertEqual(description, "type Image in type ToolbarButton[0]")

    def test_scope_requires_type_name_selector(self) -> None:
        # arrange
        client = FakeClient({}, {})
        tool = Locator(client, type_name="ToolbarButton", ordinal=0)
        # act / assert
        with self.assertRaises(LocatorError):
            Locator(client, elements_id="App::button", scope=tool)


class StaleHandleChecks(unittest.TestCase):

    def test_click_retries_once_on_stale_handle(self) -> None:
        # arrange
        client = StaleHandleClient([{"index": "3", "generation": "1"}])
        locator = Locator(client, "App::button")
        # act
        locator.click()
        # assert: the click was retried once with a fresh resolution
        self.assertEqual(client.click_attempts(), 2)
        self.assertEqual(client.resolve_count(), 2)
        self.assertEqual(client.clicked(), {"index": "3", "generation": "1"})

    def test_fill_retries_once_on_stale_handle(self) -> None:
        # arrange
        client = StaleHandleClient([{"index": "3", "generation": "1"}])
        locator = Locator(client, "App::button")
        # act
        locator.fill("value")
        # assert: the fill was retried once with a fresh resolution
        self.assertEqual(client.fill_attempts(), 2)
        self.assertEqual(client.filled(), "value")

    def test_drag_retries_once_on_stale_handle(self) -> None:
        # arrange
        client = StaleHandleClient([{"index": "6", "generation": "1"}])
        locator = Locator(client, "App::chart")
        # act
        locator.drag(120.0, 60.0)
        # assert: the drag was retried once with a fresh resolution
        self.assertEqual(client.drag_attempts(), 2)
        self.assertEqual(client.dragged(), (120.0, 60.0))

    def test_click_does_not_retry_on_unrelated_errors(self) -> None:
        # arrange
        client = UnrelatedErrorClient()
        locator = Locator(client, "App::button")
        # act / assert
        with self.assertRaises(McpError):
            locator.click()

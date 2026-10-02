from typing import Any, Callable

from .errors import LocatorError, McpError
from .slint_client import SlintClient
from .waiting import DEFAULT_POLL_INTERVAL, DEFAULT_WAIT_TIMEOUT, reports_test_frames, wait_for


class Locator:

    def __init__(self, client: SlintClient, elements_id: str | None = None, role: str | None = None, type_name: str | None = None, ordinal: int | None = None, scope: "Locator | None" = None, label: str | None = None) -> None:
        # collect the provided selectors
        selectors = [s for s in (elements_id, role, type_name) if s is not None]
        # reject locators without exactly one selector
        if len(selectors) != 1:
            raise LocatorError("locator requires exactly one of elements_id, role, or type_name (label narrows one of them)")
        # reject ordinals on single element selectors
        if ordinal is not None and elements_id is not None:
            raise LocatorError("ordinal requires a multi element selector such as type_name")
        # reject scopes on id and role selectors
        if scope is not None and type_name is None:
            raise LocatorError("scope requires a type_name selector")
        # client performs the mcp backed element operations
        self._client = client
        # elements_id is the qualified id selector when provided
        self._elements_id = elements_id
        # role is the accessible role selector when provided
        self._role = role
        # type_name is the slint type selector when provided
        self._type_name = type_name
        # ordinal selects the requested occurrence of a multi element match
        self._ordinal = ordinal
        # scope restricts the resolution to another element's subtree
        self._scope = scope
        # label narrows the selector to elements with this accessible label
        self._label = label

    def exists(self) -> bool:
        # report whether at least one element matches the selector
        return len(self._resolve()) >= 1

    def text(self) -> str:
        # read the accessible text of the matched element
        properties = self.properties()
        # prefer the accessible value and fall back to the accessible label
        return properties.get("accessibleValue") or properties.get("accessibleLabel") or ""

    def property(self, name: str) -> Any:
        # read a single property of the matched element
        return self.properties().get(name)

    def properties(self) -> dict:
        # read the full properties of the matched element
        return self._client.get_element_properties(self._matched_handle())

    def describe(self) -> str:
        # describe the id selector when provided
        if self._elements_id is not None:
            description = self._elements_id
        # describe the role selector when provided
        elif self._role is not None:
            description = f"role {self._role}"
        # describe the scoped type selector when a scope is provided
        elif self._scope is not None:
            description = f"type {self._type_name} in {self._scope.describe()}"
        # describe the type selector with the ordinal otherwise
        else:
            suffix = f"[{self._ordinal}]" if self._ordinal is not None else ""
            description = f"type {self._type_name}{suffix}"
        # include the accessible label narrowing when provided
        if self._label is not None:
            return f"{description} labelled {self._label!r}"
        return description

    def click(self, action: str = "SingleClick", button: str = "Left") -> None:
        # click the freshly resolved element handle, retrying once on a stale handle
        self._perform(lambda handle: self._client.click_element(handle, action=action, button=button))

    def drag(self, target_x: float, target_y: float) -> None:
        # drag from the matched element center to the logical target position
        self._perform(lambda handle: self._client.drag_element(handle, target_x, target_y))

    def scroll(self, delta_x: float = 0.0, delta_y: float = 0.0) -> None:
        # send a mouse wheel event over the matched element center; a negative
        # delta_y reveals content further below like a wheel - down does
        self._perform(lambda handle: self._client.scroll_element(handle, delta_x, delta_y))

    def count(self) -> int:
        # report the number of currently matched elements
        return len(self._resolve())

    def nth(self, index: int) -> "Locator":
        # create a positional locator over the same selector and label
        return Locator(self._client, type_name=self._type_name, ordinal=index, scope=self._scope, label=self._label)

    def with_label(self, label: str) -> "Locator":
        # narrow this locator to elements carrying the given accessible label
        return Locator(self._client, elements_id=self._elements_id, role=self._role, type_name=self._type_name, ordinal=self._ordinal, scope=self._scope, label=label)

    def fill(self, text: str) -> None:
        # set the value on the freshly resolved element handle, retrying once on a stale handle
        self._perform(lambda handle: self._client.set_element_value(handle, text))

    def press(self, key: str, event_type: str = "PressAndRelease") -> None:
        # resolve the element first so missing elements fail fast
        self._matched_handle()
        # dispatch the key event to the application window
        self._client.dispatch_key_event(key, event_type)

    def select_option(self, value: str, *, max_steps: int = 64) -> None:
        # open the combobox popup; the click also moves focus to the control
        self.click()
        # an already highlighted option only needs the confirmation press
        if self.text() == value:
            self.press("\n")
            return
        # walk downwards first; the highlight clamps at the last entry
        found = False
        previous = self.text()
        for _ in range(max_steps):
            self.press("\uf701")
            current = self.text()
            if current == value:
                found = True
                break
            if current == previous:
                break
            previous = current
        # walk upwards from there when the entry sits above the start position
        if not found:
            for _ in range(max_steps):
                self.press("\uf700")
                current = self.text()
                if current == value:
                    found = True
                    break
                if current == previous:
                    break
                previous = current
        if not found:
            # close the popup before surfacing the failure
            self.press("\x1b")
            raise LocatorError(f"option {value!r} not offered by {self.describe()}")
        # confirm the highlighted entry and close the popup
        self.press("\n")

    def scroll_into_view(self, scroller: "Locator", *, max_steps: int = 120, step: float = -60.0) -> None:
        # scroll the container until the element sits fully inside its viewport
        for _ in range(max_steps):
            if self._is_within(scroller):
                return
            scroller.scroll(0.0, step)
        raise LocatorError(f"element {self.describe()!r} did not scroll into view of {scroller.describe()}")

    def _is_within(self, scroller: "Locator") -> bool:
        # clipped elements only resolve once they enter a viewport
        if not self.exists() or not scroller.exists():
            return False
        element = self.properties()
        viewport = scroller.properties()
        top = element["absolutePosition"]["y"]
        bottom = top + element["size"]["height"]
        viewport_top = viewport["absolutePosition"]["y"]
        viewport_bottom = viewport_top + viewport["size"]["height"]
        return top >= viewport_top and bottom <= viewport_bottom

    def child(self, type_name: str) -> "Locator":
        # create a strict locator for a child element of this element by slint type
        return Locator(self._client, type_name=type_name, scope=self)

    @reports_test_frames
    def wait_for_exists(self, timeout: float = DEFAULT_WAIT_TIMEOUT, poll_interval: float = DEFAULT_POLL_INTERVAL) -> None:
        # wait until the element appears in the ui
        wait_for(self.exists, timeout=timeout, poll_interval=poll_interval, timeout_message=f"timed out waiting for element {self.describe()!r} to exist", observe=lambda: "exists" if self.exists() else "missing")

    @reports_test_frames
    def wait_for_gone(self, timeout: float = DEFAULT_WAIT_TIMEOUT, poll_interval: float = DEFAULT_POLL_INTERVAL) -> None:
        # wait until the element disappears from the ui
        wait_for(lambda: not self.exists(), timeout=timeout, poll_interval=poll_interval, timeout_message=f"timed out waiting for element {self.describe()!r} to be removed", observe=lambda: "exists" if self.exists() else "missing")

    def _resolve(self) -> list[dict]:
        # resolve within the scoped element subtree when a scope is provided
        if self._scope is not None:
            handles = self._client.find_by_type_in(self._scope._matched_handle(), self._type_name)
        # resolve by qualified id when the id selector is provided
        elif self._elements_id is not None:
            handles = self._client.find_elements_by_id(self._elements_id)
        # resolve by accessible role when the role selector is provided
        elif self._role is not None:
            handles = self._client.find_by_role(self._role)
        # resolve by slint type name in document order otherwise
        else:
            handles = self._client.find_by_type(self._type_name)
        # narrow to the accessible label when one was requested
        if self._label is not None:
            handles = [handle for handle in handles if self._client.get_element_properties(handle).get("accessibleLabel") == self._label]
        return handles

    def _matched_handle(self) -> dict:
        # resolve the current handles for the selector
        handles = self._resolve()
        # select the requested occurrence when an ordinal is provided
        if self._ordinal is not None:
            # reject ordinals beyond the matched elements
            if len(handles) <= self._ordinal:
                raise LocatorError(f"element not found: {self.describe()} matched only {len(handles)} elements")
            # use the handle at the requested position
            return handles[self._ordinal]
        # reject missing elements on strict resolution
        if not handles:
            raise LocatorError(f"element not found: {self.describe()}")
        # reject ambiguous matches that cannot be addressed safely
        if len(handles) > 1:
            raise LocatorError(f"ambiguous element: {self.describe()} matched {len(handles)} elements")
        # use the single matched handle
        return handles[0]

    def _perform(self, action: Callable[[dict], None]) -> None:
        # attempt the action with the freshly resolved element handle
        try:
            action(self._matched_handle())
        except McpError as error:
            # re - resolve once when the handle went stale between resolution and the action
            if "invalid handle" not in str(error).lower():
                raise
            action(self._matched_handle())


class LocatorCollection:

    def __init__(self, client: SlintClient, type_name: str, label: str | None = None) -> None:
        # client performs the mcp backed element operations
        self._client = client
        # type_name is the slint type shared by the matched elements
        self._type_name = type_name
        # label narrows the collection to elements with this accessible label
        self._label = label

    def count(self) -> int:
        # report the number of currently matched elements
        return len(self._resolve())

    def nth(self, index: int) -> Locator:
        # create a positional locator over the same selector and label
        return Locator(self._client, type_name=self._type_name, ordinal=index, label=self._label)

    def all(self) -> list[Locator]:
        # create positional locators for all currently matched elements
        return [self.nth(index) for index in range(self.count())]

    def with_label(self, label: str) -> "LocatorCollection":
        # narrow this collection to elements carrying the given accessible label
        return LocatorCollection(self._client, self._type_name, label=label)

    def _resolve(self) -> list[dict]:
        # resolve by slint type name in document order
        handles = self._client.find_by_type(self._type_name)
        # narrow to the accessible label when one was requested
        if self._label is not None:
            handles = [handle for handle in handles if self._client.get_element_properties(handle).get("accessibleLabel") == self._label]
        return handles

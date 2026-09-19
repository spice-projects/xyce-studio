from typing import Any, Callable

from .errors import LocatorError, McpError
from .slint_client import SlintClient
from .waiting import DEFAULT_POLL_INTERVAL, DEFAULT_WAIT_TIMEOUT, reports_test_frames, wait_for


class Locator:

    def __init__(self, client: SlintClient, elements_id: str | None = None, role: str | None = None, type_name: str | None = None, ordinal: int | None = None, scope: "Locator | None" = None) -> None:
        # collect the provided selectors
        selectors = [s for s in (elements_id, role, type_name) if s is not None]
        # reject locators without exactly one selector
        if len(selectors) != 1:
            raise LocatorError("locator requires exactly one of elements_id, role, or type_name")
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
            return self._elements_id
        # describe the role selector when provided
        if self._role is not None:
            return f"role {self._role}"
        # describe the scoped type selector when a scope is provided
        if self._scope is not None:
            return f"type {self._type_name} in {self._scope.describe()}"
        # describe the type selector with the ordinal otherwise
        suffix = f"[{self._ordinal}]" if self._ordinal is not None else ""
        return f"type {self._type_name}{suffix}"

    def click(self, action: str = "SingleClick", button: str = "Left") -> None:
        # click the freshly resolved element handle, retrying once on a stale handle
        self._perform(lambda handle: self._client.click_element(handle, action=action, button=button))

    def drag(self, target_x: float, target_y: float) -> None:
        # drag from the matched element center to the logical target position
        self._perform(lambda handle: self._client.drag_element(handle, target_x, target_y))

    def scroll(self, delta_x: float = 0.0, delta_y: float = 0.0) -> None:
        # send a mouse wheel event over the matched element center; a negative
        # delta_y reveals content further below like a wheel-down does
        self._perform(lambda handle: self._client.scroll_element(handle, delta_x, delta_y))

    def count(self) -> int:
        # report the number of currently matched elements
        return len(self._resolve())

    def nth(self, index: int) -> "Locator":
        # create a positional locator over the same selector
        return Locator(self._client, type_name=self._type_name, ordinal=index, scope=self._scope)

    def fill(self, text: str) -> None:
        # set the value on the freshly resolved element handle, retrying once on a stale handle
        self._perform(lambda handle: self._client.set_element_value(handle, text))

    def press(self, key: str, event_type: str = "PressAndRelease") -> None:
        # resolve the element first so missing elements fail fast
        self._matched_handle()
        # dispatch the key event to the application window
        self._client.dispatch_key_event(key, event_type)

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
            return self._client.find_by_type_in(self._scope._matched_handle(), self._type_name)
        # resolve by qualified id when the id selector is provided
        if self._elements_id is not None:
            return self._client.find_elements_by_id(self._elements_id)
        # resolve by accessible role when the role selector is provided
        if self._role is not None:
            return self._client.find_by_role(self._role)
        # resolve by slint type name in document order otherwise
        return self._client.find_by_type(self._type_name)

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
            # re-resolve once when the handle went stale between resolution and the action
            if "invalid handle" not in str(error).lower():
                raise
            action(self._matched_handle())


class LocatorCollection:

    def __init__(self, client: SlintClient, type_name: str) -> None:
        # client performs the mcp backed element operations
        self._client = client
        # type_name is the slint type shared by the matched elements
        self._type_name = type_name

    def count(self) -> int:
        # report the number of currently matched elements
        return len(self._client.find_by_type(self._type_name))

    def nth(self, index: int) -> Locator:
        # create a positional locator for the requested occurrence
        return Locator(self._client, type_name=self._type_name, ordinal=index)

    def all(self) -> list[Locator]:
        # create positional locators for all currently matched elements
        return [self.nth(index) for index in range(self.count())]

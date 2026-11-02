from .errors import LocatorError
from .log import logger
from .waiting import DEFAULT_WAIT_TIMEOUT, DEFAULT_POLL_INTERVAL, wait_for

_LOGGER = logger()


class Locator:
    # selects one node by its author assigned id, which is the only selector
    # the albor tree offers; the node is resolved against the live tree on
    # every access, so a rebuilt tree keeps the locator correct

    def __init__(self, app, node_id: str) -> None:
        # app is the AlborApplication the node lives in
        self._app = app
        # node_id is the author assigned identity automation addresses
        self._node_id = node_id

    def node_id(self) -> str:
        # return the author assigned id this locator selects
        return self._node_id

    def describe(self) -> str:
        # build the human readable description failure messages use
        node = self._find()
        if node is None:
            return f"id={self._node_id} (not found)"
        # the role and name make the mismatch obvious in assertions
        return f"{node['role']} id={node['id']} name={node['name']!r}"

    def exists(self) -> bool:
        # report whether the node is present in the live tree
        return self._find() is not None

    def wait_for_exists(self, timeout: float = DEFAULT_WAIT_TIMEOUT, poll_interval: float = DEFAULT_POLL_INTERVAL) -> None:
        # poll until the node appears, which syncs the test with the frame loop
        wait_for(self.exists, timeout=timeout, poll_interval=poll_interval, timeout_message=f"node {self._node_id!r} did not appear", observe=self._app.tree_text)

    def wait_for_gone(self, timeout: float = DEFAULT_WAIT_TIMEOUT, poll_interval: float = DEFAULT_POLL_INTERVAL) -> None:
        # poll until the node disappears, which syncs the test with the frame loop
        wait_for(lambda: not self.exists(), timeout=timeout, poll_interval=poll_interval, timeout_message=f"node {self._node_id!r} did not disappear", observe=self._app.tree_text)

    def node(self) -> dict:
        # return the parsed node dictionary, raising when the node is absent
        node = self._find()
        if node is None:
            raise LocatorError(f"no node with id {self._node_id!r}")
        return node

    def text(self) -> str:
        # return the node name, which is the label a reader announces
        return self.node()["name"]

    def value(self) -> str:
        # return the editable value the node carries, empty when it edits none
        return self.node().get("value", "")

    def writable(self) -> bool:
        # report whether the node accepts a value write
        return self.node().get("writable", False)

    def click(self, action: str = "activate") -> None:
        # invoke the node action, which presses a control the way a user does
        self._app.client().invoke(self._node_id, action)

    def fill(self, text: str) -> None:
        # write the value the editable node edits, refused when it edits none
        self._app.client().set_value(self._node_id, text)

    def _find(self) -> dict | None:
        # resolve the node against the live tree without raising
        try:
            return self._app.client().find(self._node_id)
        except Exception:
            # a transport failure reads as absence, which the wait handles
            return None

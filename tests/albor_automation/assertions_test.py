import unittest

from albor_automation.assertions import expect
from albor_automation.errors import AlborAssertionError
from albor_automation.locator import Locator


class FakeClient:
    # canned tree the assertions read, which keeps the checks free of any
    # running application

    def __init__(self, nodes: list[dict]) -> None:
        self._nodes = nodes

    def find(self, node_id: str) -> dict | None:
        for node in self._nodes:
            if node["id"] == node_id:
                return dict(node)
        return None


class FakeApp:
    # the application surface the locator touches

    def __init__(self, nodes: list[dict]) -> None:
        self._client = FakeClient(nodes)

    def client(self) -> FakeClient:
        return self._client

    def tree_text(self) -> str:
        return "tree dump"


class ExpectExistenceChecks(unittest.TestCase):

    def test_expect_builds_an_expectation(self) -> None:
        # arrange
        app = FakeApp([{"role": "button", "id": "ok", "name": "OK"}])
        locator = Locator(app, "ok")
        # act / assert: to_exist polls until the node is present
        expect(locator).to_exist()

    def test_to_exist_times_out_when_missing(self) -> None:
        # arrange: the tree carries nothing
        app = FakeApp([])
        locator = Locator(app, "missing")
        # act / assert: the failure names the node and the description
        with self.assertRaises(AlborAssertionError) as context:
            expect(locator).to_exist(timeout=0.2, poll_interval=0.05)
        self.assertIn("expected node id=missing (not found) to exist", str(context.exception))

    def test_not_to_exist_passes_when_absent(self) -> None:
        # arrange
        app = FakeApp([])
        locator = Locator(app, "missing")
        # act / assert
        expect(locator).not_().to_exist()

    def test_not_to_exist_times_out_when_present(self) -> None:
        # arrange: the node is present, so the negated assertion waits forever
        app = FakeApp([{"role": "button", "id": "ok", "name": "OK"}])
        locator = Locator(app, "ok")
        # act / assert: the failure states the negation
        with self.assertRaises(AlborAssertionError) as context:
            expect(locator).not_().to_exist(timeout=0.2, poll_interval=0.05)
        self.assertIn("to NOT exist", str(context.exception))
        self.assertIn("button id=ok name='OK'", str(context.exception))


class ExpectNameChecks(unittest.TestCase):

    def test_to_have_name_passes_on_match(self) -> None:
        # arrange
        app = FakeApp([{"role": "button", "id": "ok", "name": "OK"}])
        locator = Locator(app, "ok")
        # act / assert
        expect(locator).to_have_name("OK")

    def test_to_have_name_times_out_on_mismatch(self) -> None:
        # arrange: the name differs from the expectation
        app = FakeApp([{"role": "button", "id": "ok", "name": "Cancel"}])
        locator = Locator(app, "ok")
        # act / assert: the failure shows the expected and the observed name
        with self.assertRaises(AlborAssertionError) as context:
            expect(locator).to_have_name("OK", timeout=0.2, poll_interval=0.05)
        self.assertIn("to have name 'OK'", str(context.exception))
        self.assertIn("name='Cancel'", str(context.exception))


class ExpectValueChecks(unittest.TestCase):

    def test_to_have_value_passes_on_match(self) -> None:
        # arrange
        app = FakeApp([{"role": "textfield", "id": "path", "name": "path", "value": "/bin/echo"}])
        locator = Locator(app, "path")
        # act / assert
        expect(locator).to_have_value("/bin/echo")

    def test_to_have_value_times_out_on_mismatch(self) -> None:
        # arrange: the value differs from the expectation
        app = FakeApp([{"role": "textfield", "id": "path", "name": "path", "value": "/bin/echo"}])
        locator = Locator(app, "path")
        # act / assert: the failure shows the expected value
        with self.assertRaises(AlborAssertionError) as context:
            expect(locator).to_have_value("/bin/ls", timeout=0.2, poll_interval=0.05)
        self.assertIn("to have value '/bin/ls'", str(context.exception))

    def test_to_have_value_matches_empty(self) -> None:
        # arrange: a button carries no editable value, which reads as empty
        app = FakeApp([{"role": "button", "id": "ok", "name": "OK"}])
        locator = Locator(app, "ok")
        # act / assert
        expect(locator).to_have_value("")


class ExpectStateChecks(unittest.TestCase):

    def test_to_be_writable_passes_on_flag(self) -> None:
        # arrange
        app = FakeApp([{"role": "textfield", "id": "path", "name": "path", "writable": True}])
        locator = Locator(app, "path")
        # act / assert
        expect(locator).to_be_writable()

    def test_to_be_writable_times_out_without_flag(self) -> None:
        # arrange: a button is not writable
        app = FakeApp([{"role": "button", "id": "ok", "name": "OK"}])
        locator = Locator(app, "ok")
        # act / assert
        with self.assertRaises(AlborAssertionError) as context:
            expect(locator).to_be_writable(timeout=0.2, poll_interval=0.05)
        self.assertIn("to be writable", str(context.exception))

    def test_to_have_state_passes_on_flag(self) -> None:
        # arrange: the disabled flag lands on the parsed node
        app = FakeApp([{"role": "button", "id": "ok", "name": "OK", "disabled": True}])
        locator = Locator(app, "ok")
        # act / assert
        expect(locator).to_have_state("disabled")

    def test_to_have_state_times_out_without_flag(self) -> None:
        # arrange: the selected flag is missing
        app = FakeApp([{"role": "button", "id": "ok", "name": "OK"}])
        locator = Locator(app, "ok")
        # act / assert: the failure names the requested state
        with self.assertRaises(AlborAssertionError) as context:
            expect(locator).to_have_state("selected", timeout=0.2, poll_interval=0.05)
        self.assertIn("to have state selected", str(context.exception))

    def test_not_to_have_state_passes_when_flag_absent(self) -> None:
        # arrange: the node carries no disabled flag
        app = FakeApp([{"role": "button", "id": "ok", "name": "OK"}])
        locator = Locator(app, "ok")
        # act / assert
        expect(locator).not_().to_have_state("disabled")

    def test_not_to_be_writable_passes_when_flag_absent(self) -> None:
        # arrange: a button is not writable
        app = FakeApp([{"role": "button", "id": "ok", "name": "OK"}])
        locator = Locator(app, "ok")
        # act / assert
        expect(locator).not_().to_be_writable()


class ExpectFailureMessageChecks(unittest.TestCase):

    def test_failure_message_includes_the_last_observed_state(self) -> None:
        # arrange: the node never appears, so the poll observes absence
        app = FakeApp([])
        locator = Locator(app, "missing")
        # act / assert: the wait message carries the observed description
        with self.assertRaises(AlborAssertionError) as context:
            expect(locator).to_have_name("OK", timeout=0.2, poll_interval=0.05)
        self.assertIn("last observed: id=missing (not found)", str(context.exception))


if __name__ == "__main__":
    unittest.main()

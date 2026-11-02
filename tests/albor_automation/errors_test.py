import unittest

from albor_automation.errors import (
    AlborAssertionError,
    AlborTestError,
    ApplicationStartupError,
    LocatorError,
    McpError,
)


class ErrorHierarchyChecks(unittest.TestCase):

    def test_transport_errors_share_the_framework_base(self) -> None:
        # every launcher and transport error is a framework error, which lets
        # scenarios catch the whole family through one base type
        self.assertTrue(issubclass(ApplicationStartupError, AlborTestError))
        self.assertTrue(issubclass(McpError, AlborTestError))
        self.assertTrue(issubclass(LocatorError, AlborTestError))

    def test_assertion_error_is_a_python_assertion_error(self) -> None:
        # assertion failures integrate with runner failure reporting, which is
        # what distinguishes a failed test from an errored one
        self.assertTrue(issubclass(AlborAssertionError, AssertionError))

    def test_errors_are_raiseable_and_catchable_through_the_base(self) -> None:
        # each concrete error raises and is caught through the framework base
        for error_type in (ApplicationStartupError, McpError, LocatorError):
            with self.assertRaises(AlborTestError):
                raise error_type("message")

    def test_assertion_error_catches_through_both_base_types(self) -> None:
        # the assertion error is caught through both the framework base and
        # the python assertion base, which is what the runner relies on
        with self.assertRaises(AlborTestError):
            raise AlborAssertionError("message")
        with self.assertRaises(AssertionError):
            raise AlborAssertionError("message")


if __name__ == "__main__":
    unittest.main()

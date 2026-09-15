import unittest

from slint_automation.errors import (
    ApplicationStartupError,
    LocatorError,
    McpError,
    SlintAssertionError,
    SlintTestError,
)


class ErrorHierarchyChecks(unittest.TestCase):

    def test_application_errors_share_the_framework_base(self) -> None:
        # act / assert: every launcher and transport error is a SlintTestError
        self.assertTrue(issubclass(ApplicationStartupError, SlintTestError))
        self.assertTrue(issubclass(McpError, SlintTestError))
        self.assertTrue(issubclass(LocatorError, SlintTestError))

    def test_assertion_error_is_a_python_assertion_error(self) -> None:
        # act / assert: assertion failures integrate with runner failure reporting
        self.assertTrue(issubclass(SlintAssertionError, AssertionError))

    def test_errors_are_raiseable_and_catchable_through_the_base(self) -> None:
        # act / assert: each concrete error raises and is caught by the base
        for error_type in (ApplicationStartupError, McpError, LocatorError):
            with self.assertRaises(SlintTestError):
                raise error_type("message")

    def test_assertion_error_catches_through_the_base_types(self) -> None:
        # act / assert: assertion failures are caught by both bases
        with self.assertRaises(AssertionError):
            raise SlintAssertionError("message")
        with self.assertRaises(SlintAssertionError):
            raise SlintAssertionError("message")

class AlborTestError(Exception):
    pass


class ApplicationStartupError(AlborTestError):
    pass


class McpError(AlborTestError):
    pass


class LocatorError(AlborTestError):
    pass


class AlborAssertionError(AssertionError, AlborTestError):
    pass

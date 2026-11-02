import logging
import os
import unittest

from albor_automation.log import configure_from_environment, logger


class LoggingChecks(unittest.TestCase):

    def test_logger_uses_the_framework_namespace(self) -> None:
        # act
        log = logger()
        # assert: the framework logger keeps its own namespace
        self.assertEqual(log.name, "albor_automation")

    def test_configure_from_environment_enables_debug(self) -> None:
        # arrange: the debug flag requests protocol level logging
        os.environ["ALBOR_AUTOMATION_DEBUG"] = "1"
        # arrange: capture the loggers for level restoration
        log = logging.getLogger("albor_automation")
        root = logging.getLogger()
        self.addCleanup(log.setLevel, logging.NOTSET)
        self.addCleanup(root.setLevel, logging.WARNING)
        # act
        configure_from_environment()
        # assert
        self.assertEqual(log.level, logging.DEBUG)

    def test_configure_without_environment_keeps_the_default_level(self) -> None:
        # arrange: no debug flag set
        os.environ.pop("ALBOR_AUTOMATION_DEBUG", None)
        # arrange: capture the logger for level restoration
        log = logging.getLogger("albor_automation")
        self.addCleanup(log.setLevel, logging.NOTSET)
        # act
        configure_from_environment()
        # assert: the logger level stays untouched
        self.assertEqual(log.level, logging.NOTSET)


if __name__ == "__main__":
    unittest.main()

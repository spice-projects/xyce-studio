import logging
import os

# the framework logger name, which keeps the namespace under the package
_FRAMEWORK_LOGGER_NAME = "albor_automation"


def logger() -> logging.Logger:
    # return the framework level logger under the package namespace
    return logging.getLogger(_FRAMEWORK_LOGGER_NAME)


def configure_from_environment() -> None:
    # enable the debug logging when the caller asks for it through the environment
    if os.environ.get("ALBOR_AUTOMATION_DEBUG"):
        # log the protocol level details at debug level
        logger().setLevel(logging.DEBUG)

import os
import subprocess
import sys
import unittest
from pathlib import Path
from unittest import mock

import slint_automation.slint_application
from slint_automation import launch
from slint_automation.errors import ApplicationStartupError, McpError
from slint_automation.locator import Locator, LocatorCollection
from slint_automation.mcp_client import McpClient
from slint_automation.slint_application import SlintApplication, default_executable
from slint_automation.slint_client import SlintClient


class FakeReadyMcp:
    # in-process replacement for the mcp transport so the launch handshake
    # succeeds without any socket, loopback server or thread

    def __init__(self, port: int) -> None:
        # port is the localhost port passed by the launcher
        self._port = port
        # connect_calls counts the readiness handshake attempts
        self.connect_calls = 0

    def connect(self) -> None:
        # the handshake doubles as the readiness probe and succeeds
        self.connect_calls += 1

    def server_info(self) -> dict:
        # serve the canned server info
        return {"serverInfo": {"name": "fake-mcp"}}

    def list_tools(self) -> list[dict]:
        # serve the canned tool definitions
        return [{"name": "list_windows"}]

    def call_tool(self, name: str, arguments: dict | None = None) -> dict:
        # serve the canned window list for window discovery
        return {"windowHandles": [{"index": "1", "generation": "1"}]}


class FakeNeverReadyMcp(FakeReadyMcp):
    # replacement transport whose handshake always fails like an unreachable
    # or malformed server would

    def connect(self) -> None:
        # fail every handshake attempt until the startup deadline expires
        self.connect_calls += 1
        raise McpError(f"connection to 127.0.0.1:{self._port} failed")


class FakeProcess:

    def __init__(self, exit_code: int | None = None, hang_on_wait: bool = False) -> None:
        # exit_code is the code reported by poll once the process finished
        self._exit_code = exit_code
        # returncode mirrors the final exit code for startup error messages
        self.returncode = exit_code if exit_code is not None else 0
        # hang_on_wait makes wait raise a timeout until the process is killed
        self._hang_on_wait = hang_on_wait
        # _terminated tracks whether terminate was called
        self._terminated = False
        # _killed tracks whether kill was called
        self._killed = False
        # _terminate_count counts terminate invocations
        self._terminate_count = 0

    def poll(self) -> int | None:
        # report the exit code once the process finished
        return self._exit_code

    def terminate(self) -> None:
        # record the graceful termination request and finish the process
        self._terminated = True
        self._terminate_count += 1
        self._exit_code = 0
        self.returncode = 0

    def kill(self) -> None:
        # record the forced kill and release the hanging wait
        self._killed = True
        self._hang_on_wait = False
        self._exit_code = 0
        self.returncode = 0

    def wait(self, timeout: float | None = None) -> int:
        # simulate a hanging process until it is killed
        if self._hang_on_wait:
            raise subprocess.TimeoutExpired(cmd="fake-app", timeout=timeout)
        # report a zero exit code once the wait completes
        return 0

    def was_terminated(self) -> bool:
        # report whether terminate was requested
        return self._terminated

    def was_killed(self) -> bool:
        # report whether kill was requested
        return self._killed

    def terminate_count(self) -> int:
        # report how many times terminate was requested
        return self._terminate_count


class ApplicationLaunchChecks(unittest.TestCase):

    def setUp(self) -> None:
        # arrange: patch the port allocation, the spawned process and the mcp
        # transport so no socket, loopback server or thread is involved
        self._allocated_port = 47500
        self._process = FakeProcess()
        port_patcher = mock.patch("slint_automation.slint_application._allocate_port", return_value=self._allocated_port)
        port_patcher.start()
        self.addCleanup(port_patcher.stop)
        mcp_patcher = mock.patch("slint_automation.slint_application.McpClient", FakeReadyMcp)
        mcp_patcher.start()
        self.addCleanup(mcp_patcher.stop)
        patcher = mock.patch("slint_automation.slint_application.subprocess.Popen", return_value=self._process)
        self._popen = patcher.start()
        self.addCleanup(patcher.stop)
        # arrange: launch the application through the patched launch path
        self._app = launch("/fake/xyce-studio", startup_timeout=5.0)

    def tearDown(self) -> None:
        # cleanup: terminate the application after each test
        self._app.close()

    def test_application_launch(self) -> None:
        # assert: verify the application launched successfully
        self.assertIsNotNone(self._app)

    def test_application_has_client(self) -> None:
        # act: get the client from the application
        client = self._app.client()
        # assert: verify the client is not None
        self.assertIsNotNone(client)

    def test_application_status(self) -> None:
        # act: check the application status
        client = self._app.client()
        status = client.get_status()
        # assert: verify the application is running
        self.assertEqual(status, "running")

    def test_application_window(self) -> None:
        # act: get the application window
        client = self._app.client()
        window = client.get_window()
        # assert: verify the mock window handle is retrieved
        self.assertEqual(window, {"index": "1", "generation": "1"})

    def test_application_uses_dynamic_mcp_port(self) -> None:
        # assert: verify the app talks to the allocated port
        self.assertEqual(self._app.port(), self._allocated_port)

    def test_application_passes_mcp_port_env(self) -> None:
        # act: inspect the environment passed to the spawned process
        _, kwargs = self._popen.call_args
        # assert: verify the child received the allocated port
        self.assertEqual(kwargs["env"]["SLINT_MCP_PORT"], str(self._allocated_port))

    def test_application_passes_command_line_arguments(self) -> None:
        # arrange: relaunch with command line arguments for the application
        self._popen.reset_mock()
        # act: launch with the file loading arguments
        launch("/fake/kicad-xyce-plugin", startup_timeout=5.0, args=["--netlist", "/tmp/amp.cir", "--raw=/tmp/out.raw"])
        # assert: verify the arguments follow the executable path
        args, _ = self._popen.call_args
        self.assertEqual(args[0][0], "/fake/kicad-xyce-plugin")
        self.assertEqual(args[0][1:], ["--netlist", "/tmp/amp.cir", "--raw=/tmp/out.raw"])

    def test_application_receives_clean_environment(self) -> None:
        # arrange: inject an external variable like an editor .env file would
        os.environ["KICAD_API_TOKEN"] = "external-value"
        self.addCleanup(os.environ.pop, "KICAD_API_TOKEN", None)
        # act: inspect the environment passed to the spawned process
        _, kwargs = self._popen.call_args
        # assert: verify the external variable never reaches the application
        self.assertNotIn("KICAD_API_TOKEN", kwargs["env"])
        # assert: verify the essential system variables are preserved
        self.assertIn("PATH", kwargs["env"])
        self.assertIn("HOME", kwargs["env"])

    def test_application_injects_explicit_environment(self) -> None:
        # arrange: relaunch with the plugin simulation variables injected
        self._popen.reset_mock()
        # arrange: snapshot the test process environment to detect leaks
        before = dict(os.environ)
        # act: launch with explicit application variables
        launch("/fake/kicad-xyce-plugin", startup_timeout=5.0, env={"KICAD_API_SOCKET": "ipc://test", "KICAD_API_TOKEN": "explicit"})
        # assert: verify the injected variables reach the application
        _, kwargs = self._popen.call_args
        self.assertEqual(kwargs["env"]["KICAD_API_SOCKET"], "ipc://test")
        self.assertEqual(kwargs["env"]["KICAD_API_TOKEN"], "explicit")
        # assert: verify the launch left the test process environment untouched
        self.assertEqual(os.environ, before)

    def test_get_by_id_creates_a_locator_scoped_to_the_client(self) -> None:
        # act: build a locator by qualified element id
        locator = self._app.get_by_id("App::status")
        # assert: the locator lazily describes the requested selector
        self.assertIsInstance(locator, Locator)
        self.assertEqual(locator.describe(), "App::status")

    def test_get_by_role_creates_a_locator_scoped_to_the_client(self) -> None:
        # act: build a locator by accessible role
        locator = self._app.get_by_role("Button")
        # assert: the locator lazily describes the requested role
        self.assertIsInstance(locator, Locator)
        self.assertEqual(locator.describe(), "role Button")

    def test_get_by_type_creates_a_collection_scoped_to_the_client(self) -> None:
        # act: build a collection locator by slint type name
        collection = self._app.get_by_type("ToolbarButton")
        # assert: the collection targets the requested type
        self.assertIsInstance(collection, LocatorCollection)
        self.assertEqual(collection.nth(0).describe(), "type ToolbarButton[0]")


class ApplicationLifecycleChecks(unittest.TestCase):

    def test_close_terminates_process(self) -> None:
        # arrange
        process = FakeProcess()
        app = SlintApplication(process, SlintClient(McpClient(1)), 1)
        # act
        app.close()
        # assert
        self.assertTrue(process.was_terminated())

    def test_close_is_idempotent(self) -> None:
        # arrange
        process = FakeProcess()
        app = SlintApplication(process, SlintClient(McpClient(1)), 1)
        # act
        app.close()
        app.close()
        # assert
        self.assertEqual(process.terminate_count(), 1)

    def test_close_kills_when_wait_times_out(self) -> None:
        # arrange
        process = FakeProcess(hang_on_wait=True)
        app = SlintApplication(process, SlintClient(McpClient(1)), 1)
        # act
        app.close()
        # assert
        self.assertTrue(process.was_killed())

    def test_context_manager_closes_on_exit(self) -> None:
        # arrange
        process = FakeProcess()
        app = SlintApplication(process, SlintClient(McpClient(1)), 1)
        # act
        with app as entered:
            # assert: the context manager yields the application itself
            self.assertIs(entered, app)
            self.assertTrue(app.is_running())
        # assert: the application was closed by the context exit
        self.assertTrue(process.was_terminated())
        self.assertFalse(app.is_running())

    def test_context_manager_closes_on_error_and_propagates(self) -> None:
        # arrange
        process = FakeProcess()
        app = SlintApplication(process, SlintClient(McpClient(1)), 1)
        # act / assert
        with self.assertRaises(RuntimeError):
            with app:
                raise RuntimeError("test body failed")
        # assert: the application was closed despite the raised exception
        self.assertTrue(process.was_terminated())

    def test_launch_raises_after_retries_when_process_exits(self) -> None:
        # arrange
        process = FakeProcess(exit_code=3)
        port_patcher = mock.patch("slint_automation.slint_application._allocate_port", return_value=47500)
        port_patcher.start()
        self.addCleanup(port_patcher.stop)
        patcher = mock.patch("slint_automation.slint_application.subprocess.Popen", return_value=process)
        popen = patcher.start()
        self.addCleanup(patcher.stop)
        # act
        with self.assertRaises(ApplicationStartupError) as context:
            launch("/fake/xyce-studio", startup_timeout=0.3)
        # assert
        self.assertIn("failed to launch application", str(context.exception))
        self.assertEqual(popen.call_count, 3)

    def test_launch_closes_process_when_server_never_ready(self) -> None:
        # arrange: make the handshake fail like an unreachable server would
        process = FakeProcess()
        port_patcher = mock.patch("slint_automation.slint_application._allocate_port", return_value=47500)
        port_patcher.start()
        self.addCleanup(port_patcher.stop)
        mcp_patcher = mock.patch("slint_automation.slint_application.McpClient", FakeNeverReadyMcp)
        mcp_patcher.start()
        self.addCleanup(mcp_patcher.stop)
        patcher = mock.patch("slint_automation.slint_application.subprocess.Popen", return_value=process)
        popen = patcher.start()
        self.addCleanup(patcher.stop)
        # act
        with self.assertRaises(ApplicationStartupError):
            launch("/fake/xyce-studio", startup_timeout=0.3)
        # assert
        self.assertEqual(popen.call_count, 3)
        self.assertTrue(process.was_terminated())


class ConfigurationIsolationChecks(unittest.TestCase):

    def setUp(self) -> None:
        # arrange: patch the port allocation, the spawned process and the mcp
        # transport so no socket, loopback server or thread is involved
        self._allocated_port = 47500
        self._process = FakeProcess()
        port_patcher = mock.patch("slint_automation.slint_application._allocate_port", return_value=self._allocated_port)
        port_patcher.start()
        self.addCleanup(port_patcher.stop)
        mcp_patcher = mock.patch("slint_automation.slint_application.McpClient", FakeReadyMcp)
        mcp_patcher.start()
        self.addCleanup(mcp_patcher.stop)
        patcher = mock.patch("slint_automation.slint_application.subprocess.Popen", return_value=self._process)
        self._popen = patcher.start()
        self.addCleanup(patcher.stop)

    def test_application_isolates_persistent_configuration(self) -> None:
        # arrange: poison the test process configuration root like an external tool would
        configuration_variable = "APPDATA" if sys.platform == "win32" else "XDG_CONFIG_HOME"
        os.environ[configuration_variable] = "/external/config"
        self.addCleanup(os.environ.pop, configuration_variable, None)
        # act: launch the application
        launch("/fake/xyce-studio", startup_timeout=5.0)
        # act: inspect the environment passed to the spawned process
        _, kwargs = self._popen.call_args
        # assert: the external configuration root never reaches the application
        self.assertNotEqual(kwargs["env"][configuration_variable], "/external/config")
        # assert: the child configuration root is an isolated temporary directory
        self.assertTrue(os.path.basename(kwargs["env"][configuration_variable]).startswith("xyce-studio-config-"))

    def test_application_removes_isolated_configuration_on_close(self) -> None:
        # arrange: launch the application
        app = launch("/fake/xyce-studio", startup_timeout=5.0)
        # arrange: read the isolated configuration root passed to the application
        _, kwargs = self._popen.call_args
        configuration_variable = "APPDATA" if sys.platform == "win32" else "XDG_CONFIG_HOME"
        config_dir = kwargs["env"][configuration_variable]
        # assert: the isolated directory exists while the application runs
        self.assertTrue(os.path.isdir(config_dir))
        # act: close the application
        app.close()
        # assert: the isolated directory is removed after the application closed
        self.assertFalse(os.path.exists(config_dir))


class CallerConfigurationChecks(unittest.TestCase):

    def setUp(self) -> None:
        # arrange: patch the port allocation, the spawned process and the mcp
        # transport so no socket, loopback server or thread is involved
        self._allocated_port = 47500
        self._process = FakeProcess()
        port_patcher = mock.patch("slint_automation.slint_application._allocate_port", return_value=self._allocated_port)
        port_patcher.start()
        self.addCleanup(port_patcher.stop)
        mcp_patcher = mock.patch("slint_automation.slint_application.McpClient", FakeReadyMcp)
        mcp_patcher.start()
        self.addCleanup(mcp_patcher.stop)
        patcher = mock.patch("slint_automation.slint_application.subprocess.Popen", return_value=self._process)
        self._popen = patcher.start()
        self.addCleanup(patcher.stop)

    def test_caller_provided_configuration_root_wins(self) -> None:
        # arrange: launch the application with an explicit configuration root
        configuration_variable = "APPDATA" if sys.platform == "win32" else "XDG_CONFIG_HOME"
        launch("/fake/xyce-studio", startup_timeout=5.0, env={configuration_variable: "/shared/config"})
        # act: inspect the environment passed to the spawned process
        _, kwargs = self._popen.call_args
        # assert: the caller configuration root reaches the application untouched
        self.assertEqual(kwargs["env"][configuration_variable], "/shared/config")


class DefaultExecutableChecks(unittest.TestCase):

    def tearDown(self) -> None:
        # cleanup: drop the executable override after each check
        os.environ.pop("SLINT_TEST_APPLICATION", None)

    def test_default_executable_prefers_the_environment_override(self) -> None:
        # arrange
        os.environ["SLINT_TEST_APPLICATION"] = "/custom/xyce-studio"
        # act
        executable = default_executable()
        # assert
        self.assertEqual(executable, "/custom/xyce-studio")

    def test_default_executable_falls_back_to_the_debug_build(self) -> None:
        # arrange: drop the override so the default kicks in
        os.environ.pop("SLINT_TEST_APPLICATION", None)
        # act
        executable = default_executable()
        # assert: the debug build next to the framework package is used
        expected = str(Path(slint_automation.slint_application.__file__).resolve().parents[1] / ".build-debug" / "xyce-studio")
        self.assertEqual(executable, expected)


class RecordingTriggerClient:

    def __init__(self) -> None:
        # _values records every set_element_value invocation
        self._values: list[tuple[str, str]] = []

    def find_elements_by_id(self, elements_id: str) -> list[dict]:
        # serve the canned handle of the hidden chart action trigger
        return [{"index": "3", "generation": "1"}]

    def set_element_value(self, element_handle: dict, value: str) -> None:
        # record the value sent to the trigger
        self._values.append((str(element_handle.get("index")), value))

    def values(self) -> list[tuple[str, str]]:
        # return the recorded trigger invocations
        return self._values


class ChartActionTriggerChecks(unittest.TestCase):

    def test_invoke_chart_action_fills_the_trigger_with_the_position(self) -> None:
        # arrange: an application backed by a recording client
        app = SlintApplication(FakeProcess(), RecordingTriggerClient(), 1)
        # act: invoke the add chart trigger after the first chart
        app.invoke_chart_action("add-chart", chart_position=0.25)
        # assert: the trigger was located by its qualified id and received the
        # position as its value, which fires accessible-action-set-value
        self.assertEqual(app.client().values(), [("3", "0.25")])

    def test_invoke_chart_action_defaults_to_the_first_chart(self) -> None:
        # arrange: an application backed by a recording client
        app = SlintApplication(FakeProcess(), RecordingTriggerClient(), 1)
        # act
        app.invoke_chart_action("delete-chart")
        # assert
        self.assertEqual(app.client().values(), [("3", "0.5")])

import tempfile
import unittest

from albor_automation import AlborTestCase, expect, launch


class PluginConfigDialogChecks(AlborTestCase):
    # the dialog shows the configured path on open, persists an accepted value
    # across launches, and reports validation errors without persisting them

    def test_dialog_shows_configured_path_on_open(self) -> None:
        # arrange: launch the application with an isolated configuration
        self.start_session(launch(args=["--xyce", "/bin/echo"]))
        app = self._app
        # step 1: open the plugin configuration dialog from the toolbar
        app.get_by_id("main_plugin_settings").click()
        # arrange: locate the xyce path text box inside the dialog
        field = app.get_by_id("plugin_config_path")
        field.wait_for_exists()
        # assert: the value shown matches the configured path
        expect(field).to_have_value("/bin/echo")

    def test_accepted_path_persists_across_launches(self) -> None:
        # arrange: an isolated configuration root both launches share
        with tempfile.TemporaryDirectory() as config_root:
            # arrange: launch reading the isolated configuration
            self.start_session(launch(env={"XDG_CONFIG_HOME": config_root}))
            app = self._app
            # step 1: open the dialog and accept a new path
            app.get_by_id("main_plugin_settings").click()
            field = app.get_by_id("plugin_config_path")
            field.wait_for_exists()
            field.fill("/bin/echo")
            app.get_by_id("plugin_config_ok").click()
            # assert: the dialog closes after the acceptance
            field.wait_for_gone()
            app.close()
            # step 2: relaunch with the same configuration root
            self.start_session(launch(env={"XDG_CONFIG_HOME": config_root}))
            app = self._app
            app.get_by_id("main_plugin_settings").click()
            field = app.get_by_id("plugin_config_path")
            field.wait_for_exists()
            # assert: the path persisted across the relaunch
            expect(field).to_have_value("/bin/echo")

    def test_invalid_path_reports_error_and_dialog_stays_open(self) -> None:
        # arrange: launch the application with an isolated configuration
        self.start_session(launch(args=["--xyce", "/bin/echo"]))
        app = self._app
        # step 1: open the dialog and type a path that does not exist
        app.get_by_id("main_plugin_settings").click()
        field = app.get_by_id("plugin_config_path")
        field.wait_for_exists()
        field.fill("/nonexistent/xyce")
        # step 2: accept the dialog, which the validation must refuse
        app.get_by_id("plugin_config_ok").click()
        # assert: the error label appears and states the problem
        error = app.get_by_id("plugin_config_error")
        error.wait_for_exists()
        expect(error).to_have_name("Selected path is not an executable file")
        # assert: the dialog stays open, so the value can be corrected
        self.assertTrue(app.get_by_id("plugin_config_ok").exists())

    def test_cancel_discards_the_rejected_value(self) -> None:
        # arrange: launch and open the dialog with a valid configured path
        self.start_session(launch(args=["--xyce", "/bin/echo"]))
        app = self._app
        app.get_by_id("main_plugin_settings").click()
        field = app.get_by_id("plugin_config_path")
        field.wait_for_exists()
        # step 1: type an invalid path and let the validation refuse it
        field.fill("/nonexistent/xyce")
        app.get_by_id("plugin_config_ok").click()
        app.get_by_id("plugin_config_error").wait_for_exists()
        # step 2: cancel, which dismisses the dialog without persisting
        app.get_by_id("plugin_config_cancel").click()
        field.wait_for_gone()
        # step 3: reopen and check the field reverted to the configured path
        app.get_by_id("main_plugin_settings").click()
        field = app.get_by_id("plugin_config_path")
        field.wait_for_exists()
        expect(field).to_have_value("/bin/echo")
        expect(app.get_by_id("plugin_config_error")).not_().to_exist()

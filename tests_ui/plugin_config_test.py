import shutil
import sys
import tempfile
import unittest
from pathlib import Path

from slint_automation import expect, TestSession, launch


class PluginConfigDialogChecks(unittest.TestCase):

    def test_config_dialog_shows_configured_xyce_path(self) -> None:
        # arrange: resolve the xyce executable used to configure the application
        xyce = shutil.which("Xyce")
        # arrange: skip the scenario when the xyce executable is not available
        if xyce is None:
            self.skipTest("Xyce executable not found")
        # arrange: launch the application with the xyce executable
        with TestSession(launch(args=["--xyce", xyce]), self.id()) as app:
            # arrange: locate the toolbar tools by their slint type in declaration order
            tools = app.get_by_type("ToolbarButton")
            # step 1: open the plugin configuration dialog from the toolbar
            tools.nth(8).click()
            # arrange: locate the xyce path text box inside the dialog
            fields = app.get_by_type("LineEdit")
            # assert: exactly one text box is present in the dialog
            self.assertEqual(fields.count(), 1)
            # assert: the text box shows the configured xyce path
            expect(fields.nth(0)).to_have_text(xyce)


class PluginConfigPersistenceChecks(unittest.TestCase):

    def test_configured_path_persists_across_launches(self) -> None:
        # arrange: create a shared configuration root both launches read from
        config_variable = "APPDATA" if sys.platform == "win32" else "XDG_CONFIG_HOME"
        with tempfile.TemporaryDirectory() as config_root:
            # arrange: create a placeholder executable the dialog validation accepts
            xyce = Path(config_root) / "Xyce"
            xyce.write_text("#!/bin/sh\n")
            xyce.chmod(0o755)
            # arrange: seed the plugin configuration with the placeholder path
            config_dir = Path(config_root) / "xyce-studio"
            config_dir.mkdir()
            (config_dir / "config.json").write_text('{\n    "xyce_executable_path": "' + str(xyce) + '"\n}\n')
            # step 1: launch the application reading the shared configuration
            with TestSession(launch(env={config_variable: config_root}), self.id()) as app:
                # step 2: open the plugin configuration dialog
                app.get_by_type("ToolbarButton").nth(8).click()
                field = app.get_by_type("LineEdit").nth(0)
                field.wait_for_exists()
                # assert: the dialog shows the persisted xyce path
                expect(field).to_have_text(str(xyce))
                # step 3: accept the dialog so the application rewrites the configuration
                root = app.client().get_window_properties()["rootElementHandle"]
                ok = [handle for handle in app.client().find_by_type_in(root, "Button") if app.client().get_element_properties(handle).get("accessibleLabel") == "OK"][0]
                app.client().click_element(ok)
                field.wait_for_gone()
            # assert: the configuration file still holds the xyce path
            self.assertIn(str(xyce), (config_dir / "config.json").read_text())
            # step 4: relaunch the application with the same configuration root
            with TestSession(launch(env={config_variable: config_root}), self.id()) as app:
                # step 5: open the plugin configuration dialog again
                app.get_by_type("ToolbarButton").nth(8).click()
                field = app.get_by_type("LineEdit").nth(0)
                field.wait_for_exists()
                # assert: the dialog shows the persisted path after the relaunch
                expect(field).to_have_text(str(xyce))

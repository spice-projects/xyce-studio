import unittest
from pathlib import Path

from albor_automation import TestSession, launch


class DialogButtonBarChecks(unittest.TestCase):
    # dialog footers size Cancel and OK alike and mark their action area

    def test_cancel_and_ok_share_one_size_in_every_dialog(self) -> None:
        # arrange: a netlist so the document dialogs are available
        netlist = Path(__file__).resolve().parents[1] / "netlists" / "tran-simple-01.cir"
        with TestSession(launch(args=["--netlist", str(netlist)]), self.id()) as app:
            # step 1: open the plugin configuration dialog
            app.get_by_id("main_plugin_settings").click()
            app.get_by_id("plugin_config_ok").wait_for_exists()
            # assert: both actions fill the same box
            cancel = app.get_by_id("plugin_config_cancel").bounds()
            ok = app.get_by_id("plugin_config_ok").bounds()
            self.assertEqual((cancel["width"], cancel["height"]), (80.0, 28.0))
            self.assertEqual((ok["width"], ok["height"]), (80.0, 28.0))
            # step 2: dismiss it and open the options dialog
            app.get_by_id("plugin_config_cancel").click()
            app.get_by_id("plugin_config_ok").wait_for_gone()
            app.get_by_id("main_options").click()
            app.get_by_id("options_ok").wait_for_exists()
            # assert: the options footer uses the same geometry
            cancel = app.get_by_id("options_cancel").bounds()
            ok = app.get_by_id("options_ok").bounds()
            self.assertEqual((cancel["width"], cancel["height"]), (80.0, 28.0))
            self.assertEqual((ok["width"], ok["height"]), (80.0, 28.0))


if __name__ == "__main__":
    unittest.main()

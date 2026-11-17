import os
import tempfile
import unittest
from pathlib import Path

from albor_automation import TestSession, expect, launch


class DefaultPlotChecks(unittest.TestCase):
    # the .PLOT directives declare the charts a run presents

    def test_a_plotted_quantity_missing_from_the_print_is_added_to_it(self) -> None:
        # arrange: a transient netlist whose print names a voltage while the chart plots a current
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "Xyce"
            exe.write_text("#!/bin/sh\ncat \"$1\"\n")
            os.chmod(exe, 0o755)
            netlist = Path(tmp) / "tran.cir"
            netlist.write_text("* plot\nR1 1 0 1k\nL1 1 0 1m\n.TRAN 1m 10m\n.PRINT TRAN V(1)\n.PLOT I(L1)\n.END\n")
            with TestSession(launch(args=["--xyce", str(exe), "--netlist", str(netlist)]), self.id()) as app:
                # act: run the simulation
                app.get_by_id("main_run").click()
                # assert: the run netlist prints the plotted quantity beside the requested one
                log = app.get_by_id("output_log")
                log.wait_for_exists()
                app.wait_for_condition(lambda: "I(L1)" in log.value(), timeout=10.0, message="expected the run print to carry the plotted quantity")
                self.assertIn("V(1)", log.value())

    def test_the_charts_follow_the_plot_directives_edited_before_a_rerun(self) -> None:
        # arrange: a netlist declaring one chart and a fake Xyce that produces the table
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "Xyce"
            exe.write_text(
                "#!/bin/sh\n"
                "DIR=$(dirname \"$1\")\n"
                "cat > \"$DIR/netlist.cir.csv\" <<'EOF'\n"
                "TIME,V(1),I(L1)\n"
                "0,0,0\n"
                "1,1,2\n"
                "EOF\n"
                "echo 'Simulation completed.'\n"
            )
            os.chmod(exe, 0o755)
            netlist = Path(tmp) / "tran.cir"
            netlist.write_text("* plot\nR1 1 0 1k\nL1 1 0 1m\n.TRAN 1m 10m\n.PRINT TRAN FORMAT=CSV V(*) I(*)\n.PLOT V(1)\n.END\n")
            with TestSession(launch(args=["--xyce", str(exe), "--netlist", str(netlist)]), self.id()) as app:
                # step: run the netlist as declared
                app.get_by_id("main_run").click()
                chart = app.get_by_id("charts_view")
                chart.wait_for_exists()
                expect(chart).to_have_name("1 line chart")
                # step: declare a second chart in the editor and run again
                app.get_by_id("document_tabs").click("select-tab-0")
                editor = app.get_by_id("netlist_editor")
                editor.wait_for_exists()
                editor.fill(netlist.read_text().replace(".PLOT V(1)\n", ".PLOT V(1)\n.PLOT I(L1)\n"))
                app.get_by_id("main_run").click()
                # assert: the rerun presents the charts the edited directives declare
                chart.wait_for_exists()
                app.wait_for_condition(lambda: chart.node()["name"].endswith("2 line charts"), timeout=15.0, message="expected the edited plot directives to build two charts")


if __name__ == "__main__":
    unittest.main()

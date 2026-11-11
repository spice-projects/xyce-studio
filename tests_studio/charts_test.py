import os
import tempfile
import unittest
from pathlib import Path

from albor_automation import TestSession, expect, launch


class ChartsPanelChecks(unittest.TestCase):
    # the output dataset argument opens the native chart panel, and the toolbar
    # action swaps the body between the waveform panel and the netlist editor

    def test_dataset_argument_opens_the_charts_panel(self) -> None:
        # arrange: a repository output file and a session scoped to this test
        dataset = Path(__file__).resolve().parent / "fixtures" / "tran-simple-01.csv"
        with TestSession(launch(args=["--dataset", str(dataset)]), self.id()) as app:
            # assert: the chart surface and the toolbar action are present
            chart = app.get_by_id("charts_view")
            chart.wait_for_exists()
            expect(chart).to_have_name("1 line chart")
            expect(app.get_by_id("main_charts")).to_have_name("Charts")
            # assert: the chart panel replaced the netlist editor in the body
            expect(app.get_by_id("netlist_editor")).not_().to_exist()

    def test_the_charts_action_swaps_the_body_back_to_the_netlist(self) -> None:
        # arrange: start on the chart panel through the dataset argument
        dataset = Path(__file__).resolve().parent / "fixtures" / "tran-simple-01.csv"
        with TestSession(launch(args=["--dataset", str(dataset)]), self.id()) as app:
            # step 1: press the charts action to return to the netlist editor
            app.get_by_id("main_charts").click()
            # assert: the editor mounts and the chart panel keeps its host but leaves the body
            app.get_by_id("netlist_editor").wait_for_exists()
            expect(app.get_by_id("charts_view")).not_().to_exist()
            # step 2: press the charts action again to reveal the wave forms
            app.get_by_id("main_charts").click()
            # assert: the chart panel returns and the editor unmounts
            app.get_by_id("charts_view").wait_for_exists()
            expect(app.get_by_id("netlist_editor")).not_().to_exist()

    def test_the_charts_action_is_disabled_without_a_dataset(self) -> None:
        # arrange: a plain launch with no output file
        with TestSession(launch(), self.id()) as app:
            # assert: the charts action stays disabled and the editor keeps the body
            expect(app.get_by_id("main_charts")).to_have_state("disabled")
            expect(app.get_by_id("netlist_editor")).to_exist()

    def test_two_dataset_arguments_open_two_tabs(self) -> None:
        # arrange: the same output file offered twice, which the presenter keeps as two tabs
        dataset = Path(__file__).resolve().parent / "fixtures" / "tran-simple-01.csv"
        with TestSession(launch(args=["--dataset", str(dataset), "--dataset", str(dataset)]), self.id()) as app:
            # assert: the chart surface reports the two dataset tabs
            chart = app.get_by_id("charts_view")
            chart.wait_for_exists()
            expect(chart).to_have_name("2 datasets, 1 line chart, active: tran-simple-01 (2)")

    def test_a_successful_run_auto_opens_the_charts(self) -> None:
        # arrange: a fake Xyce that writes a csv analysis output beside its netlist
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "Xyce"
            exe.write_text(
                "#!/bin/sh\n"
                "DIR=$(dirname \"$1\")\n"
                "cat > \"$DIR/netlist.cir.csv\" <<'EOF'\n"
                "TIME,V(1)\n"
                "0,0\n"
                "1,1\n"
                "2,2\n"
                "EOF\n"
                "echo 'Simulation completed.'\n"
            )
            os.chmod(exe, 0o755)
            netlist = Path(tmp) / "tran.cir"
            netlist.write_text("R1 1 0 1k\n.TRAN 1m 10m\n.PRINT TRAN FORMAT=CSV V(1)\n.END\n")
            # arrange: launch the application with the fake simulator
            with TestSession(launch(args=["--xyce", str(exe), "--netlist", str(netlist)]), self.id()) as app:
                # step 1: run the simulation
                app.get_by_id("main_run").click()
                # assert: the produced csv opens as a chart tab
                chart = app.get_by_id("charts_view")
                chart.wait_for_exists()
                expect(chart).to_have_name("1 line chart")
                expect(app.get_by_id("main_status")).to_have_name("Simulation finished")

    def test_a_run_uses_the_plot_directives_for_its_charts(self) -> None:
        # arrange: a fake Xyce whose table carries the two declared quantities
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "Xyce"
            exe.write_text(
                "#!/bin/sh\n"
                "DIR=$(dirname \"$1\")\n"
                "cat > \"$DIR/netlist.cir.csv\" <<'EOF'\n"
                "TIME,V(1),I(L1)\n"
                "0,0,0\n"
                "1,1,2\n"
                "2,2,4\n"
                "EOF\n"
                "echo 'Simulation completed.'\n"
            )
            os.chmod(exe, 0o755)
            netlist = Path(tmp) / "tran.cir"
            netlist.write_text("R1 1 0 1k\n.TRAN 1m 10m\n.PRINT TRAN FORMAT=CSV V(*) I(*)\n.PLOT V(1)\n.PLOT I(L1)\n.END\n")
            with TestSession(launch(args=["--xyce", str(exe), "--netlist", str(netlist)]), self.id()) as app:
                # act: run the simulation
                app.get_by_id("main_run").click()
                # assert: the two .PLOT groups build two charts
                chart = app.get_by_id("charts_view")
                chart.wait_for_exists()
                expect(chart).to_have_name("2 line charts")
                expect(app.get_by_id("main_status")).to_have_name("Simulation finished")

    def test_the_chart_context_menu_commands_change_the_panel(self) -> None:
        # arrange: one dataset whose chart offers the context menu commands
        dataset = Path(__file__).resolve().parent / "fixtures" / "tran-simple-01.csv"
        with TestSession(launch(args=["--dataset", str(dataset)]), self.id()) as app:
            # arrange: the chart panel is the body
            chart = app.get_by_id("charts_view")
            chart.wait_for_exists()
            expect(chart).to_have_name("1 line chart")
            # act: add a chart through the menu trigger the automation reaches
            chart.click("chart-0-add-chart")
            # assert: the empty chart joins the panel
            expect(chart).to_have_name("2 line charts")
            # act: delete the added chart through the same menu path
            chart.click("chart-1-delete-chart")
            # assert: the original chart remains
            expect(chart).to_have_name("1 line chart")

    def test_touchstone_dataset_opens_a_smith_chart(self) -> None:
        # arrange: a repository touchstone file and a session scoped to this test
        dataset = Path(__file__).resolve().parent / "fixtures" / "lin-simple-01.s2p"
        with TestSession(launch(args=["--dataset", str(dataset)]), self.id()) as app:
            # assert: the chart surface names the single smith chart
            chart = app.get_by_id("charts_view")
            chart.wait_for_exists()
            expect(chart).to_have_name("1 smith chart")

    def test_an_unsupported_dataset_argument_keeps_the_editor(self) -> None:
        # arrange: a text file the charts cannot parse and a session scoped to this test
        with TestSession(launch(args=["--dataset", __file__]), self.id()) as app:
            # assert: the shell stays on the editor and no chart panel is revealed
            expect(app.get_by_id("netlist_editor")).to_exist()
            expect(app.get_by_id("charts_view")).not_().to_exist()

    def test_switching_tabs_updates_the_active_dataset_name(self) -> None:
        # arrange: two dataset tabs where the second is active
        dataset = Path(__file__).resolve().parent / "fixtures" / "tran-simple-01.csv"
        with TestSession(launch(args=["--dataset", str(dataset), "--dataset", str(dataset)]), self.id()) as app:
            chart = app.get_by_id("charts_view")
            chart.wait_for_exists()
            expect(chart).to_have_name("2 datasets, 1 line chart, active: tran-simple-01 (2)")
            # act: select the first tab through the semantic action
            chart.click("select-tab-0")
            # assert: the active dataset follows the selection
            expect(chart).to_have_name("2 datasets, 1 line chart, active: tran-simple-01")

    def test_closing_the_last_user_dataset_returns_to_the_editor(self) -> None:
        # arrange: one closable user dataset
        dataset = Path(__file__).resolve().parent / "fixtures" / "tran-simple-01.csv"
        with TestSession(launch(args=["--dataset", str(dataset)]), self.id()) as app:
            chart = app.get_by_id("charts_view")
            chart.wait_for_exists()
            expect(chart).to_have_name("1 line chart")
            # act: close the only tab through the semantic action
            chart.click("close-tab-0")
            # assert: the charts action disables and the editor returns to the body
            expect(app.get_by_id("main_charts")).to_have_state("disabled")
            app.get_by_id("netlist_editor").wait_for_exists()
            expect(app.get_by_id("charts_view")).not_().to_exist()

    def test_a_run_with_an_open_dataset_adds_a_primary_tab(self) -> None:
        # arrange: a fake Xyce, a print netlist and a user dataset already open
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "Xyce"
            exe.write_text(
                "#!/bin/sh\n"
                "DIR=$(dirname \"$1\")\n"
                "cat > \"$DIR/netlist.cir.csv\" <<'EOF'\n"
                "TIME,V(1)\n"
                "0,0\n"
                "1,1\n"
                "2,2\n"
                "EOF\n"
                "echo 'Simulation completed.'\n"
            )
            os.chmod(exe, 0o755)
            netlist = Path(tmp) / "tran.cir"
            netlist.write_text("R1 1 0 1k\n.TRAN 1m 10m\n.PRINT TRAN FORMAT=CSV V(1)\n.END\n")
            dataset = Path(__file__).resolve().parent / "fixtures" / "tran-simple-01.csv"
            with TestSession(launch(args=["--xyce", str(exe), "--netlist", str(netlist), "--dataset", str(dataset)]), self.id()) as app:
                # act: run the simulation over the open user dataset
                app.get_by_id("main_run").click()
                # assert: the run adds its primary tab beside the user dataset
                chart = app.get_by_id("charts_view")
                chart.wait_for_exists()
                expect(chart).to_have_name("2 datasets, 1 line chart, active: tran.cir")

    def test_a_second_run_keeps_one_primary_tab(self) -> None:
        # arrange: a fake Xyce and a print netlist
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "Xyce"
            exe.write_text(
                "#!/bin/sh\n"
                "DIR=$(dirname \"$1\")\n"
                "cat > \"$DIR/netlist.cir.csv\" <<'EOF'\n"
                "TIME,V(1)\n"
                "0,0\n"
                "1,1\n"
                "2,2\n"
                "EOF\n"
                "echo 'Simulation completed.'\n"
            )
            os.chmod(exe, 0o755)
            netlist = Path(tmp) / "tran.cir"
            netlist.write_text("R1 1 0 1k\n.TRAN 1m 10m\n.PRINT TRAN FORMAT=CSV V(1)\n.END\n")
            with TestSession(launch(args=["--xyce", str(exe), "--netlist", str(netlist)]), self.id()) as app:
                # step 1: run once
                app.get_by_id("main_run").click()
                chart = app.get_by_id("charts_view")
                chart.wait_for_exists()
                expect(chart).to_have_name("1 line chart")
                # step 2: run again
                app.get_by_id("main_run").click()
                # assert: the rerun repoints the primary tab instead of adding one
                expect(chart).to_have_name("1 line chart")
                expect(app.get_by_id("main_status")).to_have_name("Simulation finished")

    def test_a_run_copies_the_requested_print_destination(self) -> None:
        # arrange: a fake Xyce, a print netlist with a FILE= destination and a workspace
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "Xyce"
            exe.write_text(
                "#!/bin/sh\n"
                "DIR=$(dirname \"$1\")\n"
                "cat > \"$DIR/netlist.cir.csv\" <<'EOF'\n"
                "TIME,V(1)\n"
                "0,0\n"
                "1,1\n"
                "2,2\n"
                "EOF\n"
                "echo 'Simulation completed.'\n"
            )
            os.chmod(exe, 0o755)
            netlist = Path(tmp) / "tran.cir"
            netlist.write_text("R1 1 0 1k\n.TRAN 1m 10m\n.PRINT TRAN FORMAT=CSV FILE=requested.csv V(1)\n.END\n")
            with TestSession(launch(args=["--xyce", str(exe), "--netlist", str(netlist)]), self.id()) as app:
                # act: run the simulation
                app.get_by_id("main_run").click()
                # assert: the run opens its chart and copies the produced file to the requested name
                chart = app.get_by_id("charts_view")
                chart.wait_for_exists()
                expect(chart).to_have_name("1 line chart")
                expect(app.get_by_id("main_status")).to_have_name("Simulation finished")
                copied = Path(tmp) / "requested.csv"
                self.assertTrue(copied.exists())

    def test_a_failed_run_does_not_open_the_charts(self) -> None:
        # arrange: a fake Xyce that writes an output but exits with an error
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "Xyce"
            exe.write_text(
                "#!/bin/sh\n"
                "DIR=$(dirname \"$1\")\n"
                "cat > \"$DIR/netlist.cir.csv\" <<'EOF'\n"
                "TIME,V(1)\n"
                "0,0\n"
                "1,1\n"
                "EOF\n"
                "exit 2\n"
            )
            os.chmod(exe, 0o755)
            netlist = Path(tmp) / "tran.cir"
            netlist.write_text("R1 1 0 1k\n.TRAN 1m 10m\n.PRINT TRAN FORMAT=CSV V(1)\n.END\n")
            with TestSession(launch(args=["--xyce", str(exe), "--netlist", str(netlist)]), self.id()) as app:
                # act: run the failing simulation
                app.get_by_id("main_run").click()
                # assert: the charts stay disabled and the editor keeps the body
                expect(app.get_by_id("main_status")).to_have_name("Simulation failed with exit code 2")
                expect(app.get_by_id("main_charts")).to_have_state("disabled")
                expect(app.get_by_id("netlist_editor")).to_exist()
                expect(app.get_by_id("charts_view")).not_().to_exist()

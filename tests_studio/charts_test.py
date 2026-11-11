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

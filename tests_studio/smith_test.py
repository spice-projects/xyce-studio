import unittest
from pathlib import Path

from albor_automation import TestSession, expect, launch


class SmithChartChecks(unittest.TestCase):
    # a stepped touchstone dataset plots on the Smith plane and keeps the step tool

    def test_the_step_tool_opens_from_the_smith_chart_context_menu(self) -> None:
        # arrange: a stepped touchstone dataset opened on the Smith plane
        dataset = Path(__file__).resolve().parents[1] / "netlists" / "lin-simple-with-step-01.s2p"
        with TestSession(launch(args=["--dataset", str(dataset)]), self.id()) as app:
            chart = app.get_by_id("charts_view")
            chart.wait_for_exists()
            expect(chart).to_have_name("1 smith chart")
            # act: open the step tool from the Smith chart menu
            chart.click("chart-0-step-tool")
            # assert: the dialog lists the steps the dataset carries
            check = app.get_by_id("step_tool_check_0")
            check.wait_for_exists()
            app.get_by_id("step_tool_cancel").click()
            check.wait_for_gone()


if __name__ == "__main__":
    unittest.main()

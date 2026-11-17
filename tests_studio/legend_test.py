import unittest
from pathlib import Path

from albor_automation import TestSession, expect, launch


class ExpressionLegendChecks(unittest.TestCase):
    # the plot selection dialog names the unit categories the dataset carries

    def test_the_add_plot_legend_lists_the_dataset_categories(self) -> None:
        # arrange: a dataset whose signals carry a time and a voltage unit
        dataset = Path(__file__).resolve().parent / "fixtures" / "tran-simple-01.csv"
        with TestSession(launch(args=["--dataset", str(dataset)]), self.id()) as app:
            chart = app.get_by_id("charts_view")
            chart.wait_for_exists()
            # act: open the plot selection dialog
            chart.click("chart-0-add-remove-plots")
            legend = app.get_by_id("add_plot_legend")
            legend.wait_for_exists()
            # assert: the legend names the categories it lists
            expect(legend).to_have_name("Legend, Voltage")
            # step: add a decibel expression, which brings its own category
            app.get_by_id("add_plot_custom").fill("db(V(1))")
            app.get_by_id("add_plot_add").click()
            app.wait_for_condition(lambda: "dB" in app.get_by_id("add_plot_legend").node()["name"], timeout=5.0, message="expected the legend to gain the decibel category")
            # step: close the dialog
            app.get_by_id("add_plot_cancel").click()
            legend.wait_for_gone()
            # act: open the FFT dialog, whose legend lists the transformable signals
            chart.click("chart-0-calculate-fft")
            fft_legend = app.get_by_id("fft_legend")
            fft_legend.wait_for_exists()
            # assert: the abscissa is not a signal, so its category is absent while the decibel magnitudes are listed
            expect(fft_legend).to_have_name("Legend, Voltage, dB")
            # step: close the dialog
            app.get_by_id("fft_cancel").click()
            fft_legend.wait_for_gone()


if __name__ == "__main__":
    unittest.main()

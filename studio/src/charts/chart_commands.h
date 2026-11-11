#pragma once

namespace studio
{
    // one action the chart context menu and its semantic triggers run
    enum class ChartCommand
    {
        // reset both axes of the targeted chart and the abscissa of the panel
        ZoomToFit,

        // reset only the ordinate of the targeted chart
        Autorange,

        // reset the abscissa extent of every chart
        ZoomAbscissaExtent,

        // edit the expressions the targeted chart plots
        AddRemovePlots,

        // remove every plotted series from the targeted chart
        DeleteAllPlots,

        // insert an empty chart after the targeted one
        AddChart,

        // remove the targeted chart, keeping at least one chart
        DeleteChart
    };
} // namespace studio

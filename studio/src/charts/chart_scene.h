#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "albor/core/geometry.h"
#include "albor/render/picture_resource.h"
#include "charts/chart_engine.h"
#include "charts/chart_palette.h"

namespace studio
{
    // one chart slot inside the retained panel scene
    struct ChartSlot
    {
        // the chart index the slot belongs to
        std::size_t chart_index = 0;

        // the chart kind the slot paints
        ChartKind kind = ChartKind::XY;

        // the chart area inside the panel
        albor::Rect frame;

        // the plot rect inside the chart area, which hit tests use
        albor::Rect plot;
    };

    // the retained pictures a charts view replays
    struct ChartScene
    {
        // the base picture, which changes only with data, zoom, steps, size or theme
        std::shared_ptr<const albor::PictureResource> base;

        // the transient overlay picture, which changes on a drag
        std::shared_ptr<const albor::PictureResource> overlay;

        // the chart slots the scene paints, in top to bottom order
        std::vector<ChartSlot> slots;

        // the logical panel size the pictures were recorded for
        float width = 0.0f;

        float height = 0.0f;

        // the palette the pictures were recorded with
        ChartPalette palette = CHART_PALETTE_LIGHT;
    };

    // one transient overlay picture and the damage it covers
    struct ChartOverlay
    {
        // the overlay commands, which the view draws above the base picture
        std::shared_ptr<const albor::PictureResource> picture;

        // the panel relative area the overlay paints
        albor::Rect bounds;
    };
} // namespace studio

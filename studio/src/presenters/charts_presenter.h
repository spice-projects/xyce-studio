#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

#include "albor/render/resource_factory.h"
#include "albor/text/text_engine.h"
#include "charts/chart_scene.h"
#include "charts/chart_scene_builder.h"
#include "charts/chart_text_measurer.h"
#include "charts/charts_controller.h"

class XyceOutputFile;

namespace studio
{
    class ChartsView;

    // loads output datasets and publishes their retained chart scenes to the native view
    class ChartsPresenter final
    {
    public:
        // build the presenter over the view it drives and the engine its labels shape with
        ChartsPresenter(ChartsView& view, albor::TextEngine& text);

        ChartsPresenter(const ChartsPresenter&) = delete;

        ChartsPresenter& operator=(const ChartsPresenter&) = delete;

        ~ChartsPresenter();

        // parse an output file and publish its first chart layout, returning false when the format is unsupported
        bool show_file(const std::filesystem::path& path);

        // adopt an already parsed dataset and build its charts
        void load_dataset(std::shared_ptr<XyceOutputFile> file, ChartKind kind = ChartKind::XY);

        // tell the layout the panel was resized to
        void set_viewport(float width, float height);

        // install the callback the shell status bar uses for the hover readout
        void set_on_hover(std::function<void(const std::string&)> handler);

        // select the light or dark chart palette and republish the scene
        void set_dark_mode(bool dark);

        // drop the view the presenter drives, which the window teardown calls before the tree dies
        void detach();

        // report whether a dataset is loaded
        [[nodiscard]] bool has_dataset() const;

        // hand out how many charts the active dataset holds
        [[nodiscard]] std::size_t chart_count() const;

        // hand out the dataset title the shell may show
        [[nodiscard]] const std::string& title() const;

        // hand out the last published scene, which the tests read
        [[nodiscard]] const ChartScene* scene() const;

        // hand out how many series one chart frame carries, which the tests read
        [[nodiscard]] std::size_t series_count(std::size_t chart_index) const;

    private:
        // record and publish the base scene at the current viewport
        void publish_scene();

        // record and publish the transient zoom selection
        void publish_overlay();

        // build the charts the dataset suggests, or one chart over its first expressions
        void build_charts(ChartKind kind);

        // the factory that stamps every retained path and line batch
        albor::ResourceFactory m_resources;

        // the label measurer the chart layout and scene builder share
        AlborChartTextMeasurer m_text;

        // the chart engines and hit test state of the active dataset
        ChartsController m_controller;

        // the frame to retained picture translator
        ChartSceneBuilder m_builder;

        // the native view the scenes are published to
        ChartsView* m_view;

        // the active dataset, which owns the expressions and step information
        std::shared_ptr<XyceOutputFile> m_file;

        // the last published scene
        std::shared_ptr<const ChartScene> m_scene;

        // the dataset title the shell may show
        std::string m_title;

        // the shell status bar sink for the hover readout
        std::function<void(const std::string&)> m_on_hover;

        // whether the dark chart palette is active
        bool m_dark_mode = false;
    };
} // namespace studio

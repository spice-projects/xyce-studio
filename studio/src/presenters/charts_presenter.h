#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "albor/render/resource_factory.h"
#include "albor/text/text_engine.h"
#include "charts/chart_commands.h"
#include "charts/chart_scene.h"
#include "charts/chart_scene_builder.h"
#include "charts/chart_text_measurer.h"
#include "charts/charts_controller.h"
#include "charts/plot_selection.h"
#include "presenters/run_output.h"

class XyceOutputFile;
class TempOutputDirectory;
class PrintParameters;

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

        // parse an output file and add it as a closable dataset tab, returning false when the format is unsupported
        bool show_file(const std::filesystem::path& path, std::shared_ptr<TempOutputDirectory> artifacts = nullptr);

        // adopt an already parsed user dataset and build its charts
        void load_dataset(std::shared_ptr<XyceOutputFile> file, ChartKind kind = ChartKind::XY, bool closable = true);

        // resolve the analysis and companion files a finished run produced and show them as tabs
        RunOutputResult show_run_output(const RunOutput& output, std::shared_ptr<TempOutputDirectory> artifacts);

        // drop the previous run's companion tabs before a new run rewrites their files
        void discard_run_companions();

        // tell the layout the panel was resized to
        void set_viewport(float width, float height);

        // install the callback the shell status bar uses for the hover readout
        void set_on_hover(std::function<void(const std::string&)> handler);

        // install the callback a dataset tab change reaches, which the shell uses to enable or hide the panel
        void set_on_tabs_changed(std::function<void()> handler);

        // select the light or dark chart palette and republish the scene
        void set_dark_mode(bool dark);

        // drop the view the presenter drives, which the window teardown calls before the tree dies
        void detach();

        // report whether a dataset is loaded
        [[nodiscard]] bool has_dataset() const;

        // hand out how many dataset tabs are open
        [[nodiscard]] std::size_t dataset_count() const;

        // hand out the active dataset index, clamped to the open tabs
        [[nodiscard]] std::size_t active_dataset() const;

        // hand out one dataset title, or an empty string past the end
        [[nodiscard]] const std::string& dataset_title(std::size_t index) const;

        // select a dataset tab
        void activate_dataset(std::size_t index);

        // close a closable dataset tab
        void close_dataset(std::size_t index);

        // run one chart context menu command on the chart at an index
        void run_chart_command(ChartCommand command, std::size_t chart_index);

        // install the callback the add/remove plots command reaches
        void set_on_add_remove_plots(std::function<void(std::size_t)> handler);

        // hand out the expressions and rules the add/remove plots dialog opens with
        [[nodiscard]] std::optional<PlotSelectionState> plot_selection(std::size_t chart_index) const;

        // resolve a custom expression against the active dataset, nothing when it does not parse
        [[nodiscard]] std::optional<PlotExpression> resolve_plot_expression(std::size_t chart_index, const std::string& expression) const;

        // apply the accepted expression names to one chart, returning false when a name does not resolve
        bool apply_plot_selection(std::size_t chart_index, const std::vector<std::string>& names);

        // hand out how many charts the active dataset holds
        [[nodiscard]] std::size_t chart_count() const;

        // hand out the dataset title the shell may show
        [[nodiscard]] const std::string& title() const;

        // hand out the last published scene of the active dataset
        [[nodiscard]] const ChartScene* scene() const;

        // hand out how many series one chart frame of the active dataset carries
        [[nodiscard]] std::size_t series_count(std::size_t chart_index) const;

    private:
        // the role a dataset plays in the tab set
        enum class DatasetRole
        {
            // a file the user opened, which stays until the user closes it
            User,
            // the analysis output of the latest run, which a rerun repoints in place
            Primary,
            // a companion output of the latest run, which the next run replaces
            Companion
        };

        // one loaded dataset and the chart state that belongs to it
        struct DatasetState
        {
            // the stable dataset identity
            int id = 0;

            // the role the dataset plays in the tab set
            DatasetRole role = DatasetRole::User;

            // the chart kind the controllers were built for
            ChartKind kind = ChartKind::XY;

            // the file that owns the expressions and step information
            std::shared_ptr<XyceOutputFile> file;

            // the chart engines and hit-test state of this dataset
            std::unique_ptr<ChartsController> controller;

            // the retained scene last built for this dataset
            std::shared_ptr<const ChartScene> scene;

            // the charts the dataset was built with
            std::vector<std::vector<std::string>> plot_groups;

            // the title the tab shows
            std::string title;

            // whether the tab carries a close affordance
            bool closable = true;
        };

        // hand out the active dataset record, or null when no dataset is open
        [[nodiscard]] DatasetState* active_state();

        [[nodiscard]] const DatasetState* active_state() const;

        // add a dataset tab at the end and make it active
        DatasetState& append_dataset(std::shared_ptr<XyceOutputFile> file, ChartKind kind, DatasetRole role, std::string title);

        // adopt the run's analysis output, repointing the previous primary in place
        void adopt_primary(std::shared_ptr<XyceOutputFile> file, ChartKind kind, const std::string& title);

        // drop every dataset of one role
        void remove_role(DatasetRole role);

        // resolve the analysis or touchstone file the run produced
        [[nodiscard]] std::optional<std::filesystem::path> resolve_primary(const RunOutput& output) const;

        // resolve a deterministic fallback when the configuration names no produced file
        [[nodiscard]] std::optional<std::filesystem::path> fallback_primary(const std::filesystem::path& directory, const std::filesystem::path* exclude = nullptr) const;

        // parse a file with the parser its extension selects
        [[nodiscard]] std::optional<std::pair<std::shared_ptr<XyceOutputFile>, ChartKind>> parse_file(const std::filesystem::path& path) const;

        // make a title unique among the open tabs
        [[nodiscard]] std::string unique_title(const std::string& base, const DatasetState* exclude = nullptr) const;

        // cancel the outgoing drag and hover before the selection changes
        void cancel_interaction();

        // record and publish the base scene at the current viewport
        void publish_scene();

        // record and publish the transient zoom selection
        void publish_overlay();

        // the strip height the open tabs need
        [[nodiscard]] float tab_bar_height() const;

        // build the charts the dataset suggests, or one chart over its first expressions
        void build_charts(ChartsController& controller, XyceOutputFile& file, ChartKind kind);

        // build the tab specs the active scene records
        [[nodiscard]] std::vector<ChartTabSpec> tab_specs() const;

        // tell the shell that the tab set or selection changed
        void notify_tabs_changed();

        // the factory that stamps every retained path and line batch
        albor::ResourceFactory m_resources;

        // the label measurer the chart layout and scene builder share
        AlborChartTextMeasurer m_text;

        // the frame to retained picture translator
        ChartSceneBuilder m_builder;

        // the native view the scenes are published to
        ChartsView* m_view;

        // the datasets shown as tabs, in first-open order
        std::vector<std::unique_ptr<DatasetState>> m_datasets;

        // the index of the active dataset
        std::size_t m_active_dataset = 0;

        // the next dataset identity
        int m_next_dataset_id = 1;

        // the logical panel size the active scene was laid out for
        float m_view_width = 800.0f;

        float m_view_height = 600.0f;

        // the shell status bar sink for the hover readout
        std::function<void(const std::string&)> m_on_hover;

        // the shell sink for a dataset tab change
        std::function<void()> m_on_tabs_changed;

        // the shell sink for the add/remove plots command
        std::function<void(std::size_t)> m_on_add_remove_plots;

        // whether the dark chart palette is active
        bool m_dark_mode = false;
    };
} // namespace studio

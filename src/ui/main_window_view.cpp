#include <algorithm>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <slint.h>
#include <spdlog/spdlog.h>

#include "../app/app.h"
#include "../charts/chart_palette.h"
#include "../netlist/netlist_lexer.h"
#include "../netlist/netlist_lexer_adapter.h"
#include "clipboard.h"
#include "file_dialog.h"
#include "main_window_view.h"

namespace
{
    // slint color from a chart color
    slint::Color to_slint_color(const ChartColor& color) { return slint::Color::from_argb_float(color.a, color.r, color.g, color.b); }

    // svg polyline commands in plot rect coordinates from a chart series run
    std::string series_commands(const ChartFrame& frame, const ChartSeriesFrame& run) {
        // commands under construction
        std::string commands;
        // first point opens a subpath, the rest are line segments
        for (size_t i = 0; i < run.points.size(); ++i) {
            // sample in plot rect coordinates
            const auto& point = run.points[i];
            commands += std::format("{} {:.2f} {:.2f} ", i == 0 ? "M" : "L", point.x - frame.plot_x, point.y - frame.plot_y);
        }
        return commands;
    }

    // convert one chart frame snapshot into the generated slint frame struct
    main_window::ChartFrameData native_frame_data(const ChartFrame& frame, bool is_dark) {
        // frame data under construction
        main_window::ChartFrameData data;
        // palette roles shared with the implot render path
        const ChartPalette& palette = chart_palette(is_dark);
        const auto slint_color = [](const ChartColor& color) { return slint::Color::from_argb_float(color.a, color.r, color.g, color.b); };
        data.text_color = slint_color(palette.text);
        data.panel_color = slint_color(palette.panel);
        data.border_color = slint_color(palette.border);
        data.grid_color = slint_color(palette.grid);
        // viewport the frame was laid out for (drives the provisional stretch)
        data.frame_w = frame.frame_w;
        data.frame_h = frame.frame_h;
        // frame and plot geometry in logical px
        data.plot_x = frame.plot_x;
        data.plot_y = frame.plot_y;
        data.plot_w = frame.plot_w;
        data.plot_h = frame.plot_h;
        data.x_datum = frame.x_datum;
        data.y2_datum = frame.y_axes[1].datum;
        data.y3_datum = frame.y_axes[2].datum;
        data.legend_x = frame.legend_x;
        data.legend_y = frame.legend_y;
        data.legend_w = frame.legend_w;
        data.legend_h = frame.legend_h;
        // x tick labels
        auto x_ticks = std::make_shared<slint::VectorModel<main_window::ChartTickData>>();
        for (const auto& tick : frame.x_ticks) {
            // slint tick entry
            main_window::ChartTickData entry;
            entry.label = tick.label;
            entry.pixel_pos = tick.pixel_pos;
            entry.show = tick.show_label;
            x_ticks->push_back(entry);
        }
        data.x_ticks = x_ticks;
        // y axis tick labels and grid lines per enabled axis
        std::array<std::shared_ptr<slint::VectorModel<main_window::ChartTickData>>, 3> tick_models = {};
        std::array<std::shared_ptr<slint::VectorModel<main_window::ChartGridData>>, 3> grid_models = {};
        for (size_t i = 0; i < 3; ++i) {
            // skip disabled axes
            if (!frame.y_axes[i].enabled)
                continue;
            // tick model of this axis
            auto tick_model = std::make_shared<slint::VectorModel<main_window::ChartTickData>>();
            for (const auto& tick : frame.y_axes[i].ticks) {
                // slint tick entry
                main_window::ChartTickData entry;
                entry.label = tick.label;
                entry.pixel_pos = tick.pixel_pos;
                entry.show = tick.show_label;
                tick_model->push_back(entry);
            }
            tick_models[i] = tick_model;
            // grid model of this axis
            auto grid_model = std::make_shared<slint::VectorModel<main_window::ChartGridData>>();
            for (const auto& line : frame.y_axes[i].grid_lines) {
                // slint grid entry
                main_window::ChartGridData entry;
                entry.pixel_pos = line.pixel_pos;
                entry.major = line.major;
                entry.alpha = line.alpha;
                grid_model->push_back(entry);
            }
            grid_models[i] = grid_model;
        }
        data.y_ticks = tick_models[0];
        data.y2_ticks = tick_models[1];
        data.y3_ticks = tick_models[2];
        data.y_grid = grid_models[0];
        data.y2_grid = grid_models[1];
        data.y3_grid = grid_models[2];
        // x grid lines
        auto x_grid = std::make_shared<slint::VectorModel<main_window::ChartGridData>>();
        for (const auto& line : frame.x_grid) {
            // slint grid entry
            main_window::ChartGridData entry;
            entry.pixel_pos = line.pixel_pos;
            entry.major = line.major;
            entry.alpha = line.alpha;
            x_grid->push_back(entry);
        }
        data.x_grid = x_grid;
        // legend entries
        auto legend = std::make_shared<slint::VectorModel<main_window::ChartLegendItemData>>();
        for (const auto& item : frame.legend) {
            // slint legend entry
            main_window::ChartLegendItemData entry;
            entry.name = item.name;
            entry.color = slint::Color::from_argb_float(item.color.a, item.color.r, item.color.g, item.color.b);
            legend->push_back(entry);
        }
        data.legend = legend;
        // series polylines as svg commands relative to the plot origin
        auto series = std::make_shared<slint::VectorModel<main_window::ChartSeriesData>>();
        for (const auto& run : frame.series) {
            // slint series entry
            main_window::ChartSeriesData entry;
            entry.name = run.name;
            entry.color = slint::Color::from_argb_float(run.color.a, run.color.r, run.color.g, run.color.b);
            entry.commands = series_commands(frame, run);
            series->push_back(entry);
        }
        data.series = series;
        return data;
    }
} // namespace

SlintMainWindowView::SlintMainWindowView(std::unique_ptr<NetlistSource> /*netlist_source*/, PluginConfig /*plugin_config*/) :
    m_window(main_window::MainWindow::create()), m_simulation_log(std::make_shared<slint::VectorModel<slint::SharedString>>()) {
    // expose the log model to the output panel
    m_window->set_simulation_output_log(m_simulation_log);
    // seed the dark-mode flag from the initial Slint theme state so the first
    // highlight model is built with the correct colours even before charts are shown
    m_dark_mode = m_window->get_is_dark();
    // push the chart palette canvas color so the charts panel background
    // matches the offscreen implot canvas in both render paths
    m_window->set_charts_background(to_slint_color(chart_palette(m_dark_mode).background));
    // wire the theme change handler early so theme switches are always tracked
    // (the charts renderer is updated only once it exists) and the visible
    // netlist highlight model is rebuilt with the new palette
    m_window->on_theme_changed([this](bool is_dark) {
        // keep the adapter colour palette in sync for the next highlight rebuild
        m_dark_mode = is_dark;
        // keep the charts panel background in sync with the chart palette
        m_window->set_charts_background(to_slint_color(chart_palette(is_dark).background));
        // rebuild the highlight model so the loaded netlist picks up the new colours
        rebuild_netlist_highlight_model();
        // update the renderer's dark mode state when the slint theme changes
        if (m_charts_renderer)
            m_charts_renderer->set_dark_mode(is_dark);
    });
}

void SlintMainWindowView::set_event_handler(MainWindowViewDefEvents& handler) {
    // store the event handler reference for later use
    m_event_handler = &handler;
    // bind the toolbar actions to the event handler methods
    const auto& actions = m_window->global<main_window::MainWindowActions>();
    // file group
    actions.on_open_xyce_file([this] { handle_open(); });
    actions.on_save_netlist([this] { guard_modal([this] { m_event_handler->on_save_netlist(); }); });
    // content view group
    actions.on_show_netlist([this] { guard_modal([this] { m_event_handler->on_show_netlist(); }); });
    actions.on_show_charts([this] { guard_modal([this] { m_event_handler->on_show_charts(); }); });
    actions.on_show_simulation_output([this] { guard_modal([this] { m_event_handler->on_show_simulation_output(); }); });
    actions.on_close_simulation_output([this] { guard_modal([this] { m_event_handler->on_close_simulation_output(); }); });
    // the copy action is a pure view operation on the buffered log
    actions.on_copy_simulation_output([this](int start, int end) { copy_simulation_selection(start, end); });
    // simulation group
    actions.on_run_simulation([this] { guard_modal([this] { m_event_handler->on_run_simulation(); }); });
    actions.on_stop_simulation([this] { guard_modal([this] { m_event_handler->on_cancel_simulation(); }); });
    actions.on_configure_simulation([this] { guard_modal([this] { m_event_handler->on_configure_simulation(); }); });
    // plugin configuration
    actions.on_configure_plugin([this] { guard_modal([this] { m_event_handler->on_configure_plugin(); }); });
    // exit
    actions.on_exit([this] {
        // do not close the window while a modal dialog is open
        if (m_modal_dialog_open)
            return;
        // close the window, which will terminate the application
        m_window->hide();
    });

    // netlist editor edits re-tokenise the text synchronously so the colours
    // track typing with no perceptible latency, then the presenter tracks the
    // dirty state and content changes
    actions.on_netlist_edited([this] {
        // rebuild the highlight model from the current editor text
        rebuild_netlist_highlight_model();
        // forward the edit to the event handler
        m_event_handler->on_netlist_editor_modified();
    });

    // charts context menu actions; chart_position is a float [0..1] from the
    // slint panel, the renderer translates it to an index using its own count
    const auto& chart_actions = m_window->global<main_window::ChartsPanelActions>();
    chart_actions.on_zoom_to_fit([this](float chart_position) { m_charts_renderer->zoom_to_fit(chart_position); });
    chart_actions.on_autorange([this](float chart_position) { m_charts_renderer->autorange(chart_position); });
    chart_actions.on_zoom_abscissa_extent([this](float chart_position) { m_charts_renderer->zoom_abscissa_extent(chart_position); });
    chart_actions.on_delete_all_plots([this](float chart_position) { m_charts_renderer->delete_all_plots(chart_position); });
    chart_actions.on_add_chart([this](float) {
        // add chart
        m_charts_renderer->add_chart();
        // refresh charts to show the new chart
        m_charts_renderer->refresh_charts();
    });
    chart_actions.on_delete_chart([this](float chart_position) { m_charts_renderer->delete_chart(chart_position); });
    // events that need presenter involvement: convert float to int via renderer
    chart_actions.on_add_remove_plots([this](float chart_position) { show_add_remove_plots_dialog(chart_position); });
    chart_actions.on_calculate_fft([this](float chart_position) { m_event_handler->on_chart_calculate_fft(m_charts_renderer->chart_count() > 0 ? static_cast<size_t>(chart_position * static_cast<float>(m_charts_renderer->chart_count())) : 0); });
    chart_actions.on_step_tool([this](float chart_position) { m_event_handler->on_chart_step_tool(m_charts_renderer->chart_count() > 0 ? static_cast<size_t>(chart_position * static_cast<float>(m_charts_renderer->chart_count())) : 0); });
    chart_actions.on_new_window([this](float chart_position) { m_event_handler->on_chart_new_window(m_charts_renderer->chart_count() > 0 ? static_cast<size_t>(chart_position * static_cast<float>(m_charts_renderer->chart_count())) : 0); });

    // plot tab navigation
    m_window->on_plot_tab_selected([this](int index) { guard_modal([this, index] { m_event_handler->on_select_plot_tab(index); }); });
    m_window->on_plot_tab_closed([this](int index) { guard_modal([this, index] { m_event_handler->on_close_plot_tab(index); }); });
    // chart drag-zoom interactions
    chart_actions.on_zoom_drag_started([this](float x, float y) {
        if (m_charts_renderer)
            m_charts_renderer->zoom_drag_started(x, y);
    });
    chart_actions.on_zoom_drag_moved([this](float x, float y) {
        if (m_charts_renderer)
            m_charts_renderer->zoom_drag_moved(x, y);
    });
    chart_actions.on_zoom_drag_ended([this] {
        if (m_charts_renderer)
            m_charts_renderer->zoom_drag_ended();
    });
    chart_actions.on_zoom_drag_canceled([this] {
        if (m_charts_renderer)
            m_charts_renderer->zoom_drag_canceled();
    });
    // chart hover readout interactions
    chart_actions.on_hover_moved([this](float x, float y) {
        if (m_charts_renderer)
            m_charts_renderer->hover_moved(x, y);
    });
    chart_actions.on_hover_ended([this] {
        if (m_charts_renderer)
            m_charts_renderer->hover_ended();
    });
}

void SlintMainWindowView::show() {
    // show the slint main window
    m_window->show();
}

slint::Window& SlintMainWindowView::window() {
    // expose the underlying slint window for event handling
    return m_window->window();
}

void SlintMainWindowView::set_title(const std::string& title) {
    // update the window title property, which is bound to the native title
    m_window->set_window_title(slint::SharedString(title));
}

void SlintMainWindowView::set_status_text(const std::string& text) {
    // retain the latest permanent status text
    m_last_status_text = text;
    // update the status bar text in the slint window
    m_window->set_status_text(slint::SharedString(text));
}

void SlintMainWindowView::set_simulation_running(bool running) {
    // toggle the Run/Stop toolbar action
    m_window->set_simulation_running(running);
}

void SlintMainWindowView::apply_action_enablement(const ActionStateEnablement& enablement) {
    // do not re-enable toolbar actions while a modal dialog blocks input
    if (m_modal_dialog_open)
        return;
    // update the slint window with the action enablement state
    m_window->set_enable_open(enablement.open);
    m_window->set_enable_save(enablement.save);
    m_window->set_enable_run_simulation(enablement.run_simulation);
    m_window->set_enable_configure_simulation(enablement.configure_simulation);
    m_window->set_enable_show_netlist(enablement.show_netlist);
    m_window->set_enable_show_charts(enablement.show_charts);
    m_window->set_enable_show_sim_output(enablement.show_simulation_output);
    // chart context tools
    m_window->set_enable_fft(enablement.fft);
    m_window->set_enable_step_tool(enablement.step_tool);
}

void SlintMainWindowView::show_netlist_view() {
    // reveal the netlist panel and hide the charts panel
    m_window->set_charts_visible(false);
    // stop publishing stale frames when the panel is hidden
    if (m_charts_renderer)
        m_charts_renderer->reset_viewport();
}

void SlintMainWindowView::show_charts_view() {
    // create the charts renderer the first time the charts panel is shown;
    // this must happen before set_charts_visible so the viewport-changed
    // callback is wired when the panel's init handler fires
    ensure_charts_renderer();
    // reveal the charts panel in the content area; the init handler inside
    // charts_panel.slint fires viewport-changed with the initial geometry
    m_window->set_charts_visible(true);
}

void SlintMainWindowView::set_netlist_editor_content(const std::string& content) {
    // push the raw text into the slint editor text property
    m_window->set_netlist_text(slint::SharedString(content));
    // tokenise the new content and rebuild the highlight model
    rebuild_netlist_highlight_model();
}

void SlintMainWindowView::rebuild_netlist_highlight_model() {
    // read the live editor text so rebuilds after user edits or theme flips stay in sync
    const std::string content(m_window->get_netlist_text());
    // tokenise the content into typed lines
    const auto token_lines = tokenize_netlist(content);
    // resolve the theme foreground so node and plain tokens follow the palette
    const auto foreground = m_window->get_editor_foreground();
    // first build: create the model and hand it to the editor
    if (!m_netlist_highlight_model) {
        m_netlist_highlight_model = build_netlist_highlight_model(token_lines, m_dark_mode, foreground);
        m_netlist_token_cache = token_lines;
        m_netlist_model_dark_mode = m_dark_mode;
        m_netlist_model_foreground = foreground;
        m_window->set_netlist_highlighted_lines(m_netlist_highlight_model);
        return;
    }
    // theme change: every row's colours are stale, so rebuild all of them
    const bool theme_changed = m_dark_mode != m_netlist_model_dark_mode || foreground != m_netlist_model_foreground;
    auto& model = *m_netlist_highlight_model;
    const std::size_t old_count = model.row_count();
    const std::size_t new_count = token_lines.size();
    // grow the model at the tail with fresh rows
    for (std::size_t i = old_count; i < new_count; ++i)
        model.push_back(build_netlist_line_model(token_lines[i], static_cast<int>(i) + 1, m_dark_mode, foreground));
    // shrink the model from the tail
    for (std::size_t i = old_count; i > new_count; --i)
        model.erase(i - 1);
    // update only rows whose tokens or theme colours changed; rows that merely
    // shifted position keep their token model and only get a new line number,
    // so a keystroke never reallocates the unchanged majority of the document
    const std::size_t common_count = std::min(old_count, new_count);
    for (std::size_t i = 0; i < common_count; ++i) {
        // compare against the cached tokenisation to detect content changes
        const bool tokens_changed = theme_changed || !netlist_line_tokens_equal(token_lines[i], m_netlist_token_cache[i]);
        // fetch the live row to inspect its gutter number
        auto row = model.row_data(i).value();
        // skip rows that are fully up to date
        if (!tokens_changed && row.line_number == static_cast<int>(i) + 1)
            continue;
        if (tokens_changed) {
            // rebuild the row with fresh token models and colours
            row = build_netlist_line_model(token_lines[i], static_cast<int>(i) + 1, m_dark_mode, foreground);
        }
        else {
            // position shift only: renumber the gutter, keep the token model
            row.line_number = static_cast<int>(i) + 1;
        }
        model.set_row_data(i, row);
    }
    // remember the tokenisation and theme the visible rows were built with
    m_netlist_token_cache = token_lines;
    m_netlist_model_dark_mode = m_dark_mode;
    m_netlist_model_foreground = foreground;
}

std::string SlintMainWindowView::netlist_editor_content() const {
    // read the live editor text from the slint property so user edits reach
    // the save path; the cached copy only holds the latest programmatic push
    // and would silently discard typed edits on save
    return std::string(m_window->get_netlist_text());
}

void SlintMainWindowView::set_netlist_editor_read_only(bool read_only) {
    // store the read-only state for the editor
    m_netlist_read_only = read_only;
    // push the read-only state into the slint editor widget
    m_window->set_netlist_read_only(read_only);
}

bool SlintMainWindowView::charts_shown() const {
    // report whether the charts panel is currently visible
    return m_window->get_charts_visible();
}

void SlintMainWindowView::show_simulation_output_panel() {
    // reveal the simulation output panel in the body
    m_window->set_simulation_output_visible(true);
}

void SlintMainWindowView::hide_simulation_output_panel() {
    // hide the simulation output panel from the body
    m_window->set_simulation_output_visible(false);
}

// replace tab characters with spaces at fixed 8-column stops so the log columns
// line up; the slint text renderer draws an unsupported tab as a box character
namespace
{
    std::string expand_tabs(std::string_view line) {
        std::string expanded;
        expanded.reserve(line.size());
        std::size_t column = 0;
        for (const char c : line) {
            if (c == '\t') {
                const std::size_t to_next_stop = 8 - (column % 8);
                expanded.append(to_next_stop, ' ');
                column += to_next_stop;
            }
            else {
                expanded.push_back(c);
                ++column;
            }
        }
        return expanded;
    }
} // namespace

void SlintMainWindowView::clear_simulation_output() {
    // drop all buffered log lines from the panel model
    m_simulation_log->clear();
}

void SlintMainWindowView::append_simulation_output_line(const std::string& line) {
    // append the line to the panel log model; tabs are expanded to spaces
    // because the slint text renderer has no tab support
    m_simulation_log->push_back(slint::SharedString(expand_tabs(line)));
}

void SlintMainWindowView::copy_simulation_selection(int start, int end) {
    // the panel reports no selection with start < 0 or start > end
    if (start < 0 || end < start)
        return;
    // clamp the selection to the buffered log
    const std::size_t row_count = m_simulation_log->row_count();
    if (row_count == 0)
        return;
    const std::size_t first = std::min(static_cast<std::size_t>(start), row_count - 1);
    const std::size_t last = std::min(static_cast<std::size_t>(end), row_count - 1);
    // join the selected lines
    std::string text;
    for (std::size_t i = first; i <= last; ++i) {
        if (!text.empty())
            text.push_back('\n');
        text.append(m_simulation_log->row_data(i).value_or(slint::SharedString()));
    }
    copy_to_clipboard(text);
}

bool SlintMainWindowView::simulation_output_panel_hidden() const {
    // report whether the simulation output panel is currently hidden
    return !m_window->get_simulation_output_visible();
}

bool SlintMainWindowView::simulation_output_has_content() const {
    // report whether the buffered log holds any lines
    return m_simulation_log->row_count() > 0;
}

void SlintMainWindowView::update_charts(const int dataset_id, ExpressionManager& expression_manager, const StepInformation& step_information, const AbscissaScale abscissa_scale, const std::vector<std::vector<std::string>>& suggested_plots) {
    // the presenter updates the charts before showing the charts view, so create
    // the renderer here if it does not exist yet
    ensure_charts_renderer();
    // forward the data to the renderer; datasets already known to the renderer
    // keep their charts and zoom state
    m_charts_renderer->update(dataset_id, expression_manager, step_information, abscissa_scale, suggested_plots);
}

void SlintMainWindowView::release_charts(const int dataset_id) {
    // the renderer may not exist yet when nothing was rendered
    if (m_charts_renderer) {
        // drop the chart state of the given dataset
        m_charts_renderer->release_dataset(dataset_id);
    }
}

void SlintMainWindowView::release_all_charts() {
    // the renderer may not exist yet when nothing was rendered
    if (m_charts_renderer) {
        // drop every dataset chart state
        m_charts_renderer->release_all_datasets();
    }
}

void SlintMainWindowView::set_plot_tabs(const std::vector<PlotTabItem>& tabs, int active_index) {
    // create the vector model for the plot tabs
    auto model = std::make_shared<slint::VectorModel<main_window::PlotTabItem>>();
    // populate the model with each tab item
    for (const auto& tab : tabs) {
        // create the slint tab item instance
        main_window::PlotTabItem item;
        // set the tab identifier
        item.id = tab.id;
        // set the tab title
        item.title = slint::SharedString(tab.title);
        // set the tab closable flag
        item.closable = tab.closable;
        // append the item to the model
        model->push_back(item);
    }
    // set the plot tabs model in the slint window
    m_window->set_plot_tabs(model);
    // set the active tab index in the slint window
    m_window->set_active_plot_tab(active_index);
}

void SlintMainWindowView::set_active_plot_tab(int active_index) {
    // update the active tab index in the slint window
    m_window->set_active_plot_tab(active_index);
}

std::optional<SimulationConfig> SlintMainWindowView::show_simulation_parameters_dialog(const SimulationConfig& current) {
    // the presenter must be wired before the dialog can deliver its result
    if (m_event_handler == nullptr)
        return std::nullopt;
    // check a modal dialog is already open, do not stack another one
    if (!begin_modal_dialog())
        return std::nullopt;
    // create the dialog view wrapper on first use
    if (!m_simulation_parameters_dialog)
        m_simulation_parameters_dialog = std::make_unique<simulation_parameters_dialog_view::SimulationParametersDialogView>(m_window);
    // show the inline panel seeded with the current configuration; the panel is
    // rendered inside the main window (no separate OS window), so modality is
    // enforced by the guard_modal gate and the panel's dimmed backdrop — no
    // modal_manager input blocking is needed. the accepted configuration is
    // delivered asynchronously through MainWindowViewDefEvents, and the modal
    // state (kept in this view) is released through the on_closed callback
    // on both accept and cancel
    m_simulation_parameters_dialog->show(current, *m_event_handler, [this] { end_modal_dialog(); });
    // nothing to return, the accepted configuration is delivered asynchronously
    // through MainWindowViewDefEvents::on_simulation_parameters_dialog_result
    return std::nullopt;
}

std::optional<PluginConfig> SlintMainWindowView::show_plugin_config_dialog(const PluginConfig& current) {
    // the presenter must be wired before the dialog can deliver its result
    if (m_event_handler == nullptr)
        return std::nullopt;
    // check a modal dialog is already open, do not stack another one
    if (!begin_modal_dialog())
        return std::nullopt;
    // create the dialog view wrapper on first use
    if (!m_plugin_config_dialog)
        m_plugin_config_dialog = std::make_unique<plugin_config_dialog_view::PluginConfigDialogView>(m_window);
    // show the inline panel seeded with the current configuration; the panel is
    // rendered inside the main window (no separate OS window), so modality is
    // enforced by the guard_modal gate and the panel's dimmed backdrop — no
    // modal_manager input blocking is needed. the accepted configuration is
    // delivered asynchronously through MainWindowViewDefEvents, and the modal
    // state (kept in this view) is released through the on_closed callback
    m_plugin_config_dialog->show(current, *m_event_handler, [this] { end_modal_dialog(); });
    // nothing to return, the accepted configuration is delivered asynchronously
    // through MainWindowViewDefEvents::on_plugin_config_dialog_result
    return std::nullopt;
}

void SlintMainWindowView::start_simulation_process(const std::string& program, const std::filesystem::path& netlist_path, const std::filesystem::path& working_directory) {
    // create a fresh runner for this run
    m_simulation_runner = std::make_unique<SimulationRunner>();
    // forward the process output and termination to the event handler; the
    // callbacks are already marshalled to the slint event loop thread by the runner
    m_simulation_runner->set_stdout_callback([this](const std::string& line) { m_event_handler->on_simulation_stdout(line); });
    m_simulation_runner->set_stderr_callback([this](const std::string& line) { m_event_handler->on_simulation_stderr(line); });
    m_simulation_runner->set_finished_callback([this](int exit_code, bool was_canceled) { m_event_handler->on_simulation_finished(exit_code, was_canceled); });
    // launch the child process
    m_simulation_runner->start(program, netlist_path, working_directory);
}

void SlintMainWindowView::cancel_simulation_process() {
    // request a graceful shutdown when a runner is active
    if (m_simulation_runner)
        m_simulation_runner->cancel();
}

void SlintMainWindowView::spawn_raw_file_window(std::shared_ptr<XyceOutputFile> raw_file) {
    // delegate the new-window creation to the app, which owns the window wiring
    // (create the view/presenter pair, bind, show) in a single place
    App::instance().new_window(std::move(raw_file));
}

void SlintMainWindowView::show_add_remove_plots_dialog(float chart_position) {
    // a modal dialog is already open, do not stack another one
    if (!begin_modal_dialog())
        return;
    // the dialog needs the charts renderer, which is created on first charts show
    ensure_charts_renderer();
    // create the dialog view wrapper on first use
    if (!m_add_plot_dialog)
        m_add_plot_dialog = std::make_unique<add_plot_dialog_view::AddPlotDialogView>(m_window, *m_charts_renderer);
    // show the panel for the chart at the given position; the accepted selection
    // is applied back to the chart through the renderer, and the modal state is
    // released through the on_closed callback on both accept and cancel
    m_add_plot_dialog->show_for_chart(chart_position, [this] { end_modal_dialog(); });
}

void SlintMainWindowView::show_fft_dialog(size_t chart_index) {
    // a modal dialog is already open, do not stack another one
    if (!begin_modal_dialog())
        return;
    // the dialog needs the charts renderer, which is created on first charts show
    ensure_charts_renderer();
    // create the dialog view wrapper on first use
    if (!m_fft_dialog)
        m_fft_dialog = std::make_unique<fft_dialog_view::FftDialogView>(m_window, *m_charts_renderer);
    // show the inline panel for the chart at the given index; the accepted
    // expressions and FFT parameters are delivered asynchronously through
    // on_fft_dialog_result, and the modal state is released through the on_closed
    // callback on both accept and cancel; the panel is rendered inside the main
    // window (no separate OS window), so no modal_manager input blocking is needed
    m_fft_dialog->show_for_chart(chart_index, *m_event_handler, [this] { end_modal_dialog(); });
}

void SlintMainWindowView::show_step_tool_dialog(size_t chart_index) {
    // a modal dialog is already open, do not stack another one
    if (!begin_modal_dialog())
        return;
    // the dialog needs the charts renderer, which is created on first charts show
    ensure_charts_renderer();
    // create the dialog view wrapper on first use
    if (!m_step_tool_dialog)
        m_step_tool_dialog = std::make_unique<step_tool_dialog_view::StepToolDialogView>(m_window, *m_charts_renderer);
    // show the inline panel for the chart at the given index; the accepted
    // step selection is applied back to the chart through the renderer, and the
    // modal state is released through the on_closed callback on both accept and
    // cancel; the panel is rendered inside the main window (no separate OS
    // window), so no modal_manager input blocking is needed
    m_step_tool_dialog->show_for_chart(chart_index, [this] { end_modal_dialog(); });
}

void SlintMainWindowView::handle_open() {
    // run the native file dialog
    const auto filepath = FileDialog::open_xyce_file();
    // user canceled the dialog
    if (!filepath.has_value())
        return;
    // forward the selected file to the event handler (presenter)
    m_event_handler->on_open_xyce_file(filepath.value());
}

void SlintMainWindowView::ensure_charts_renderer() {
    // the renderer already exists
    if (m_charts_renderer)
        return;
    // create the renderer on the first charts panel show
    m_charts_renderer = std::make_unique<ChartsRenderer>([this](slint::Image image) {
        // publish the rendered frame to the slint image property
        m_window->set_charts_image(image);
    });
    // initialize theme state; the theme-changed callback itself is wired in the
    // constructor so theme switches are tracked before the renderer exists
    m_charts_renderer->set_dark_mode(m_window->get_is_dark());
    // wire hover readout to update the status bar
    m_charts_renderer->set_hover_callback([this](const std::string& text) {
        if (text.empty())
            m_window->set_status_text(slint::SharedString(m_last_status_text));
        else
            m_window->set_status_text(slint::SharedString(text));
    });
    // bind the viewport-changed callback from slint to the renderer's set_viewport
    m_window->on_charts_viewport_changed([this](float width, float height) {
        if (m_charts_renderer) {
            const auto scale = m_window->window().scale_factor();
            m_charts_renderer->set_viewport(width, height, scale);
        }
    });
    // bind the parity engine toggle from slint to the renderer's native view switch
    const auto& chart_actions = m_window->global<main_window::ChartsPanelActions>();
    chart_actions.on_engine_toggled([this](bool native) {
        if (m_charts_renderer)
            m_charts_renderer->set_native_view(native);
    });
    // publish slint native chart frames into the slint property
    m_charts_renderer->set_publish_frames([this](const std::vector<ChartFrame>& frames) {
        // frame model under construction
        auto model = std::make_shared<slint::VectorModel<main_window::ChartFrameData>>();
        // convert each frame snapshot
        for (const auto& frame : frames)
            model->push_back(native_frame_data(frame, m_dark_mode));
        // expose the frames to the charts panel
        m_window->set_native_charts(model);
    });
}

bool SlintMainWindowView::begin_modal_dialog() {
    // a modal dialog is already open, refuse to open another one
    if (m_modal_dialog_open)
        return false;
    // dim the main window while a dialog is open
    m_window->set_modal_active(true);
    // remember the modal state
    m_modal_dialog_open = true;
    return true;
}

void SlintMainWindowView::end_modal_dialog() {
    // no modal state to release
    if (!m_modal_dialog_open)
        return;
    // restore the main window appearance
    m_window->set_modal_active(false);
    // clear the modal state
    m_modal_dialog_open = false;
}

void SlintMainWindowView::release_gpu_resources() {
    // destroy the gpu context before the window is leaked at exit
    m_charts_renderer.reset();
}

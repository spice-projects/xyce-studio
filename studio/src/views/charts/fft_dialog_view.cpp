#include "views/charts/fft_dialog_view.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "albor/application/application.h"
#include "albor/application/window_stage.h"
#include "albor/core/font_weight.h"
#include "albor/layout/column_layout.h"
#include "albor/layout/grid_layout.h"
#include "albor/layout/row_layout.h"
#include "albor/layout/sized_box_layout.h"
#include "albor/style/theme.h"
#include "albor/ui/ui_node.h"
#include "albor/widgets/checkbox.h"
#include "albor/widgets/combo_box.h"
#include "albor/widgets/label.h"
#include "albor/widgets/text_field.h"
#include "albor/widgets/theme_controller.h"
#include "albor/window/window.h"
#include "app/theme.h"
#include "charts/expression_colors.h"
#include "fft_dialog.h"
#include "views/charts/add_plot_widgets.h"

namespace
{
    // the size the dialog window opens with
    constexpr float DIALOG_WIDTH = 600.0f;

    constexpr float DIALOG_HEIGHT = 620.0f;

    // the width of one caption cell
    constexpr float CAPTION_WIDTH = 80.0f;

    // the height of one parameter row
    constexpr float ROW_HEIGHT = 24.0f;

    // the gap between two cells and between two rows
    constexpr float CELL_GAP = 8.0f;

    // find a node by the author id a compiled tree carries
    albor::UiNode* find_node(albor::UiNode& node, const std::string_view explicit_id) {
        // the node itself can be the match
        if (node.identity().explicit_id == explicit_id)
            return &node;
        // the match can sit anywhere below this node
        for (const auto& child : node.children()) {
            // the first match wins, because author ids are unique
            if (albor::UiNode* found = find_node(*child, explicit_id))
                return found;
        }
        // no node below this one carries the id
        return nullptr;
    }

    // format one field value without trailing noise
    std::string format_value(const double value) {
        // the default precision keeps the value readable
        std::ostringstream text;
        // write the value
        text << value;
        // hand the text back
        return text.str();
    }
} // namespace

studio::AlborFftDialogView::AlborFftDialogView(albor::Application& application, albor::WindowHandle parent) :
    m_app(&application), m_parent(parent) {}

studio::AlborFftDialogView::~AlborFftDialogView() {
    // a posted rebuild must not reach a destroyed view
    *m_alive = false;
    // a dialog still up at teardown asks the application to release it
    if (m_handle.valid())
        (void)m_app->request_close(m_handle);
}

void studio::AlborFftDialogView::show(FftSelectionState state, AcceptedHandler accepted) {
    // a second show while the dialog is up is ignored
    if (is_open())
        return;
    // remember the expressions and the sink the dialog works from
    m_state = std::move(state);
    m_expressions = m_state.expressions;
    m_accepted = std::move(accepted);
    // the new show owns a fresh generation
    ++m_generation;
    // describe a fixed size modal dialog owned by the shell
    albor::WindowOptions options;
    options.title = "Calculate FFT";
    options.size = albor::Size{.width = DIALOG_WIDTH, .height = DIALOG_HEIGHT};
    options.role = albor::WindowRole::Dialog;
    options.modality = albor::Modality::WindowModal;
    options.parent = m_parent;
    options.resizable = false;
    options.visible = false;
    // build the compiled dialog tree in a fresh window
    m_handle = m_app->create_window(options, m_root);
    // a refused window leaves the caller without a dialog
    if (!m_handle.valid() || m_root == nullptr) {
        m_handle = {};
        m_root = nullptr;
        m_accepted = nullptr;
        return;
    }
    // the host-built chips borrow the stage engines
    albor::WindowStage* stage = m_app->stage(m_handle);
    // a stage that is not wired cannot host the chips
    if (stage == nullptr) {
        (void)m_app->request_close(m_handle);
        return;
    }
    // remember the engines the chips read
    m_engine = &stage->binding_engine();
    m_text = &stage->text_engine();
    // mount the grid and the chrome bars before the theme walks the tree
    m_grid = dynamic_cast<albor::GridLayout*>(find_node(*m_root, "fft_grid"));
    // the breadcrumb mounts into the host the card declares
    albor::UiNode* breadcrumb_host = find_node(*m_root, "fft_breadcrumb_host");
    // the legend mounts the same way
    albor::UiNode* legend_host = find_node(*m_root, "fft_legend_host");
    // the breadcrumb navigates the expression scopes
    auto breadcrumb = std::make_unique<PlotBreadcrumb>(m_engine, m_text);
    // the bar is seeded only once its host owns it, so a missing host leaves no dangling pointer
    m_breadcrumb = breadcrumb_host != nullptr ? breadcrumb.get() : nullptr;
    // the breadcrumb only exists once its host does
    if (breadcrumb_host != nullptr)
        breadcrumb_host->add_child(std::move(breadcrumb));
    // the legend names the units the grid shows
    auto legend = std::make_unique<PlotLegend>(m_engine, m_text);
    // the bar is seeded only once its host owns it, so a missing host leaves no dangling pointer
    m_legend = legend_host != nullptr ? legend.get() : nullptr;
    // the legend only exists once its host does
    if (legend_host != nullptr)
        legend_host->add_child(std::move(legend));
    // a segment press restores its scope, which needs the bar its host carries
    if (m_breadcrumb != nullptr) {
        // a segment press restores its scope
        m_breadcrumb->set_on_segment_clicked([this](const std::size_t level) {
            // restore the scope the segment names
            m_tree.open_breadcrumb(level);
            // the chips and the chrome follow the scope
            refresh_chrome();
            // rebuild the chips the new scope publishes
            rebuild_cards();
        });
        // the selected-only link flattens the grid to the selected expressions
        m_breadcrumb->set_on_selected_clicked([this] {
            // flip the selected-only view
            m_tree.set_show_selected(!m_tree.show_selected());
            // the chips and the chrome follow the view
            refresh_chrome();
            // rebuild the chips the view publishes
            rebuild_cards();
        });
    }
    // build the tree, the parameter rows and the first chip set
    populate();
    // theme the dialog from the same system appearance as the shell
    m_theme = install_theme(*m_app, m_handle);
    // the filter flattens the grid to the matching expressions
    m_root->set_on_filter_changed([this](std::string text) {
        // the tree applies the filter to its own names
        m_tree.set_filter(std::move(text));
        // the chips and the chrome follow the filter
        refresh_chrome();
        // rebuild the chips the filter leaves
        rebuild_cards();
    });
    // the footer commits the transform
    m_root->set_on_accepted([this] { accept(); });
    // the footer dismisses the dialog
    m_root->set_on_dismissed([this] { dismiss(); });
    // clear any stale validation feedback
    set_error({});
    // show the fully seeded window, which avoids a blank first frame
    if (albor::Window* window = m_app->window(m_handle))
        window->show();
    // the filter takes the caret once the first frame has arranged the page
    m_app->post([this, alive = m_alive] {
        // a dialog that closed or died before the task ran has nothing to focus
        if (!*alive || !m_handle.valid() || m_root == nullptr)
            return;
        // the filter is the first field the user types into
        if (albor::WindowStage* posted_stage = m_app->stage(m_handle)) {
            if (albor::UiNode* filter = find_node(*m_root, "fft_filter"))
                posted_stage->focus().request_focus(filter);
        }
    });
}

void studio::AlborFftDialogView::populate() {
    // a dialog without its tree has nothing to fill
    if (m_root == nullptr)
        return;
    // the readout the validation writes into
    m_error = dynamic_cast<albor::Label*>(find_node(*m_root, "fft_error"));
    // the items the expression tree groups
    std::vector<std::pair<std::string, std::string>> items;
    // every offered expression becomes a tree leaf
    for (const PlotExpression& expression : m_expressions)
        items.emplace_back(expression.name, expression.unit);
    // rebuild the tree from the offered expressions
    m_tree.rebuild(items);
    // the chart marks the expressions it plots
    for (const PlotExpression& expression : m_expressions) {
        // only the plotted expressions start selected
        if (expression.selected)
            m_tree.set_selected(expression.name, true);
    }
    // the dialog opens at the root with no filter and the full list
    m_tree.set_filter("");
    // the selected-only view starts off
    m_tree.set_show_selected(false);
    // the root scope is the initial path
    m_tree.open_breadcrumb(0);
    // the chrome and the chips follow the tree
    refresh_chrome();
    // build the chips the tree publishes
    rebuild_cards();
    // the parameter column the rows fill
    albor::UiNode* params_host = find_node(*m_root, "fft_params");
    // the typed pin keeps the host as the column it is
    albor::ColumnLayout* params = dynamic_cast<albor::ColumnLayout*>(params_host);
    // a parameter host that failed to mount keeps the chips only
    if (params == nullptr || m_engine == nullptr || m_text == nullptr)
        return;
    // drop the rows a previous show built
    while (params->child_count() > 0) {
        // the column owns the row until the removal hands it back
        std::unique_ptr<albor::UiNode> removed = params->remove_child(params->child_at(params->child_count() - 1));
        // the removed row releases here
        (void)removed;
    }
    // the range choice decides the interval fields
    auto range = std::make_unique<albor::ComboBox>(m_engine, m_text, "fft_range");
    // the choices the range offers
    range->set_items({"All", "Current Zoom", "Custom"});
    // the dialog starts on the whole dataset
    range->set_selected(0);
    // keep the combo so the accept reads it
    m_range = range.get();
    // the range row
    add_control_row("Range", std::move(range));
    // the interval start
    auto from = std::make_unique<albor::TextField>(m_engine, m_text, format_value(m_state.all_from), "fft_from");
    // the interval start takes the remaining width
    from->set_horizontal_align(albor::Align::Stretch);
    // keep the field so the accept reads it
    m_from = from.get();
    // the interval end
    auto to = std::make_unique<albor::TextField>(m_engine, m_text, format_value(m_state.all_to), "fft_to");
    // the interval end takes the remaining width
    to->set_horizontal_align(albor::Align::Stretch);
    // keep the field so the accept reads it
    m_to = to.get();
    // the interval row holds both fields
    add_pair_row("From", std::move(from), "To", std::move(to));
    // the points preset and its custom length
    auto points = std::make_unique<albor::ComboBox>(m_engine, m_text, "fft_points");
    // the preset lengths the transform offers
    points->set_items({"256", "512", "1024", "2048", "4096", "Custom"});
    // the dialog starts at the default length
    points->set_selected(2);
    // keep the combo so the accept reads it
    m_points = points.get();
    // the custom length
    auto custom_points = std::make_unique<albor::TextField>(m_engine, m_text, std::to_string(m_state.points), "fft_custom_points");
    // the custom length takes the remaining width
    custom_points->set_horizontal_align(albor::Align::Stretch);
    // keep the field so the accept reads it
    m_custom_points = custom_points.get();
    // the points row holds the preset and the custom length
    add_pair_row("Points", std::move(points), "Custom", std::move(custom_points));
    // the window function
    auto window = std::make_unique<albor::ComboBox>(m_engine, m_text, "fft_window");
    // the windows the transform offers
    window->set_items({"Rectangular", "Hamming", "Hanning", "Blackman"});
    // the default window
    window->set_selected(2);
    // keep the combo so the accept reads it
    m_window = window.get();
    // the output presentation
    auto output = std::make_unique<albor::ComboBox>(m_engine, m_text, "fft_output");
    // the outputs the transform offers
    output->set_items({"Magnitude", "Magnitude (dB)", "Phase"});
    // the default output
    output->set_selected(0);
    // keep the combo so the accept reads it
    m_output = output.get();
    // the window and output row
    add_pair_row("Window", std::move(window), "Output", std::move(output));
    // the magnitude normalization
    auto format = std::make_unique<albor::ComboBox>(m_engine, m_text, "fft_format");
    // the formats the transform offers
    format->set_items({"Normalized", "Unnormalized"});
    // the default format
    format->set_selected(0);
    // keep the combo so the accept reads it
    m_format = format.get();
    // the DC choice
    auto keep_dc = std::make_unique<albor::CheckBox>(m_engine, "fft_keep_dc");
    // the mean stays in the spectrum by default
    keep_dc->set_checked(true);
    // keep the box so the accept reads it
    m_keep_dc = keep_dc.get();
    // the format and DC row
    add_pair_row("Format", std::move(format), "Keep DC", std::move(keep_dc));
    // the range choice owns the interval fields
    m_range->set_on_selection_changed([this](int) { apply_range_choice(); });
    // the points choice owns the custom field
    m_points->set_on_selection_changed([this](int) { apply_points_choice(); });
    // the disabled fields match the initial choices
    apply_range_choice();
    // the disabled fields match the initial choices
    apply_points_choice();
}

void studio::AlborFftDialogView::rebuild_cards() {
    // a grid that failed to mount cannot show a card
    if (m_grid == nullptr)
        return;
    // drop the chips the previous cards built
    while (m_grid->child_count() > 0) {
        // the grid owns the chip until the removal hands it back
        std::unique_ptr<albor::UiNode> removed = m_grid->remove_child(m_grid->child_at(m_grid->child_count() - 1));
        // the removed chip releases here
        (void)removed;
    }
    // the chips the new cards build
    m_chips.clear();
    // the cards the tree publishes
    const std::vector<ExpressionCard>& cards = m_tree.cards();
    // build one chip per card
    for (std::size_t index = 0; index < cards.size(); ++index) {
        // the card this chip presents
        const ExpressionCard& card = cards[index];
        // a scope card drills in, an expression card toggles
        if (card.is_scope) {
            // the scope chip carries the scope ink and count
            auto chip = std::make_unique<ScopeChip>(m_engine, m_text, card.label, card.kind, card.count, "fft_card_" + std::to_string(index));
            // the show the press belongs to
            const std::uint64_t generation = m_generation;
            // a press drills into the scope after the chip leaves the dispatch
            chip->set_on_clicked([this, index, generation, alive = m_alive] {
                // the posted rebuild drops when the view or dialog is gone
                albor::Application::instance().post([this, alive, index, generation] {
                    // a destroyed view, a closed dialog or a newer show drops the rebuild
                    if (!*alive || generation != m_generation || !m_handle.valid())
                        return;
                    // drill into the scope the card names
                    m_tree.activate(index);
                    // the chrome and the chips follow the scope
                    refresh_chrome();
                    // rebuild the chips the new scope publishes
                    rebuild_cards();
                });
            });
            // the chip joins the grid
            m_grid->add_child(std::move(chip));
            // a scope card has no expression chip to update
            m_chips.push_back(nullptr);
            // next card
            continue;
        }
        // the expression chip carries the unit ink and the selection
        auto chip = std::make_unique<ExpressionChip>(m_engine, m_text, card.label, card.full_name, expression_colors::expression_unit_color(card.type), card.selected, "fft_card_" + std::to_string(index));
        // a press toggles the expression in place, so the chip survives its own dispatch
        chip->set_on_clicked([this, index, generation = m_generation, alive = m_alive] {
            // toggle the leaf the card names
            m_tree.activate(index);
            // the selected-only view drops the deselected card, so the grid is rebuilt after this chip leaves the dispatch
            if (m_tree.show_selected()) {
                // the posted rebuild drops when the view or dialog is gone
                albor::Application::instance().post([this, alive, generation] {
                    // a destroyed view, a closed dialog or a newer show drops the rebuild
                    if (!*alive || generation != m_generation || !m_handle.valid())
                        return;
                    // the chrome and the chips follow the filtered view
                    refresh_chrome();
                    // rebuild the chips the view publishes
                    rebuild_cards();
                });
                // the chip that dispatched is replaced by the rebuild
                return;
            }
            // the card still sits at the pressed index in the full list
            if (index < m_chips.size() && m_chips[index] != nullptr && index < m_tree.cards().size())
                m_chips[index]->set_selected(m_tree.cards()[index].selected);
        });
        // the raw pointer lets the view update the chip in place
        ExpressionChip* raw = chip.get();
        // the chip joins the grid
        m_grid->add_child(std::move(chip));
        // the chip is addressable until the next rebuild
        m_chips.push_back(raw);
    }
    // the grid lays the chips out again
    m_grid->invalidate_layout();
}

void studio::AlborFftDialogView::refresh_chrome() {
    // the breadcrumb shows the browsed path and the selected-only state
    if (m_breadcrumb != nullptr)
        m_breadcrumb->set_entries(m_tree.breadcrumb(), m_tree.show_selected());
    // the units the legend names
    std::vector<std::string> units;
    // the scope kinds the dataset carries
    bool has_subcircuit = false;
    bool has_sheet = false;
    // scan every offered expression
    for (const PlotExpression& expression : m_expressions) {
        // every unit feeds the legend, which deduplicates
        units.push_back(expression.unit);
        // a subcircuit probe names a drill-in scope
        if (ExpressionTree::kind_of(expression.name) == GroupKind::Subcircuit)
            has_subcircuit = true;
        // a sheet probe names a drill-in scope
        if (ExpressionTree::kind_of(expression.name) == GroupKind::Sheet)
            has_sheet = true;
    }
    // the legend rows the units and scopes name
    if (m_legend != nullptr)
        m_legend->set_entries(expression_colors::expression_legend_entries(units, has_subcircuit, has_sheet));
}

void studio::AlborFftDialogView::add_label_cell(albor::RowLayout& row, const float width, std::string text, const float font_size, const albor::FontWeight weight) {
    // a host that failed to mount cannot hold a cell
    if (m_engine == nullptr || m_text == nullptr)
        return;
    // the cell is a plain label
    auto label = std::make_unique<albor::Label>(m_engine, m_text, std::move(text));
    // the caption and the values differ only in the stroke and size
    label->set_font_size(font_size);
    // the caption is emphasized
    label->set_font_weight(weight);
    // a zero width means the cell stretches to its share of the row
    if (width <= 0.0f) {
        // the value column takes its share of the leftover
        label->set_horizontal_align(albor::Align::Stretch);
        // the label joins the row
        row.add_child(std::move(label));
        // the stretching cell is complete
        return;
    }
    // a fixed width cell keeps the column aligned across rows
    auto cell = std::make_unique<albor::SizedBoxLayout>();
    // the cell keeps the column width
    cell->set_width(width);
    // the cell keeps the row height
    cell->set_height(ROW_HEIGHT);
    // the cell owns the label
    cell->add_child(std::move(label));
    // the cell joins the row
    row.add_child(std::move(cell));
}

void studio::AlborFftDialogView::add_control_row(std::string caption, std::unique_ptr<albor::UiNode> control) {
    // a dialog without its parameter host has no row to build
    albor::UiNode* params_host = m_root != nullptr ? find_node(*m_root, "fft_params") : nullptr;
    // the typed pin keeps the host as the column it is
    albor::ColumnLayout* params = dynamic_cast<albor::ColumnLayout*>(params_host);
    // a parameter host that failed to mount keeps the chips only
    if (params == nullptr || m_engine == nullptr || m_text == nullptr)
        return;
    // the row holds the caption and the control
    auto row = std::make_unique<albor::RowLayout>();
    // the cells keep a small gap
    row->set_gap(CELL_GAP);
    // the row spans the dialog width
    row->set_horizontal_align(albor::Align::Stretch);
    // the caption names the control
    add_label_cell(*row, CAPTION_WIDTH, std::move(caption), 13.0f, albor::FontWeight::Regular);
    // the control takes the remaining width and centres on the row
    control->set_horizontal_align(albor::Align::Stretch);
    // the control centres on the row height
    control->set_vertical_align(albor::Align::Center);
    // the row owns the control
    row->add_child(std::move(control));
    // the column owns the row
    params->add_child(std::move(row));
}

void studio::AlborFftDialogView::add_pair_row(std::string left_caption, std::unique_ptr<albor::UiNode> left_control, std::string right_caption, std::unique_ptr<albor::UiNode> right_control) {
    // a dialog without its parameter host has no row to build
    albor::UiNode* params_host = m_root != nullptr ? find_node(*m_root, "fft_params") : nullptr;
    // the typed pin keeps the host as the column it is
    albor::ColumnLayout* params = dynamic_cast<albor::ColumnLayout*>(params_host);
    // a parameter host that failed to mount keeps the chips only
    if (params == nullptr || m_engine == nullptr || m_text == nullptr)
        return;
    // the row holds both pairs
    auto row = std::make_unique<albor::RowLayout>();
    // the cells keep a small gap
    row->set_gap(CELL_GAP);
    // the row spans the dialog width
    row->set_horizontal_align(albor::Align::Stretch);
    // the left caption names its control
    add_label_cell(*row, CAPTION_WIDTH, std::move(left_caption), 13.0f, albor::FontWeight::Regular);
    // the left control takes its share
    left_control->set_horizontal_align(albor::Align::Stretch);
    // the left control centres on the row height
    left_control->set_vertical_align(albor::Align::Center);
    // the row owns the left control
    row->add_child(std::move(left_control));
    // a checkbox reads as a box with its caption, so the caption follows it directly
    if (dynamic_cast<albor::CheckBox*>(right_control.get()) != nullptr) {
        // the box centres on the row height
        right_control->set_vertical_align(albor::Align::Center);
        // the row owns the box
        row->add_child(std::move(right_control));
        // the caption follows the box
        add_label_cell(*row, 0.0f, std::move(right_caption), 13.0f, albor::FontWeight::Regular);
        // the column owns the row
        params->add_child(std::move(row));
        // the checkbox row is complete
        return;
    }
    // the right caption names its control
    add_label_cell(*row, CAPTION_WIDTH, std::move(right_caption), 13.0f, albor::FontWeight::Regular);
    // the right control takes its share
    right_control->set_horizontal_align(albor::Align::Stretch);
    // the right control centres on the row height
    right_control->set_vertical_align(albor::Align::Center);
    // the row owns the right control
    row->add_child(std::move(right_control));
    // the column owns the row
    params->add_child(std::move(row));
}

void studio::AlborFftDialogView::apply_range_choice() {
    // the choice the combo carries
    const int choice = m_range != nullptr ? m_range->selected() : 0;
    // a preset writes the interval its range names
    if (choice != 2) {
        // the zoom preset falls back to the whole dataset when the chart is not zoomed
        const bool zoom = choice == 1 && m_state.has_zoom;
        // the interval start the preset names
        const double from = zoom ? m_state.zoom_from : m_state.all_from;
        // the interval end the preset names
        const double to = zoom ? m_state.zoom_to : m_state.all_to;
        // the field shows the preset start
        if (m_from != nullptr)
            m_from->set_value(format_value(from));
        // the field shows the preset end
        if (m_to != nullptr)
            m_to->set_value(format_value(to));
    }
    // only a custom interval is editable
    if (m_from != nullptr)
        m_from->set_disabled(choice != 2);
    // only a custom interval is editable
    if (m_to != nullptr)
        m_to->set_disabled(choice != 2);
}

void studio::AlborFftDialogView::apply_points_choice() {
    // the choice the combo carries
    const int choice = m_points != nullptr ? m_points->selected() : 2;
    // the preset lengths, in the order the combo lists them
    const char* presets[] = {"256", "512", "1024", "2048", "4096", "Custom"};
    // a preset writes its length into the field
    if (choice >= 0 && choice < 5 && m_custom_points != nullptr)
        m_custom_points->set_value(presets[choice]);
    // only the custom choice is editable
    if (m_custom_points != nullptr)
        m_custom_points->set_disabled(choice != 5);
}

void studio::AlborFftDialogView::set_error(std::string message) {
    // a dialog without the readout shows no message
    if (m_error == nullptr)
        return;
    // the readout carries the message
    m_error->set_text(std::move(message));
}

void studio::AlborFftDialogView::accept() {
    // the expressions the tree selected
    const std::vector<std::string> names = m_tree.selected_names();
    // a transform needs at least one signal
    if (names.empty()) {
        set_error("Select at least one expression");
        return;
    }
    // read one numeric field into a double
    const auto read_double = [](const albor::TextField* field, double& value) {
        // a missing field reads nothing
        if (field == nullptr)
            return false;
        // parse the whole text
        try {
            std::size_t used = 0;
            value = std::stod(field->value(), &used);
            return used == field->value().size();
        }
        catch (...) {
            return false;
        }
    };
    // the interval the transform reads
    double from = 0.0;
    double to = 0.0;
    if (!read_double(m_from, from) || !read_double(m_to, to)) {
        set_error("The range must be numeric");
        return;
    }
    // an empty or reversed interval has no samples
    if (!(from < to)) {
        set_error("The range end must be greater than its start");
        return;
    }
    // the transform length the preset or the custom field names
    int points = 0;
    try {
        std::size_t used = 0;
        points = std::stoi(m_custom_points != nullptr ? m_custom_points->value() : std::string{}, &used);
        if (m_custom_points == nullptr || used != m_custom_points->value().size())
            points = 0;
    }
    catch (...) {
        points = 0;
    }
    // a transform needs a power of two of at least four samples
    const bool power_of_two = points >= 4 && (points & (points - 1)) == 0;
    if (!power_of_two) {
        set_error("Points must be a power of two of at least 4");
        return;
    }
    // a transform larger than the cap would exhaust memory before it finishes
    if (static_cast<std::size_t>(points) > MAX_FFT_POINTS) {
        set_error("Points must be at most 1048576");
        return;
    }
    // the request the presenter computes
    FftRequest request;
    request.expressions = names;
    request.from = from;
    request.to = to;
    request.points = static_cast<std::size_t>(points);
    // the window choice
    switch (m_window != nullptr ? m_window->selected() : 2) {
    case 0:
        request.window = fft::WindowFunction::RECTANGULAR;
        break;
    case 1:
        request.window = fft::WindowFunction::HAMMING;
        break;
    case 3:
        request.window = fft::WindowFunction::BLACKMAN;
        break;
    default:
        request.window = fft::WindowFunction::HANNING;
        break;
    }
    // the output presentation
    switch (m_output != nullptr ? m_output->selected() : 0) {
    case 1:
        request.output = fft::FftOutput::MAGNITUDE_DB;
        break;
    case 2:
        request.output = fft::FftOutput::PHASE;
        break;
    default:
        request.output = fft::FftOutput::MAGNITUDE;
        break;
    }
    // the magnitude normalization
    request.format = (m_format != nullptr && m_format->selected() == 1) ? fft::FftFormat::UNORM : fft::FftFormat::NORM;
    // the DC choice
    request.keep_dc = m_keep_dc == nullptr || m_keep_dc->checked();
    // the sink owns the request once it is handed back
    if (m_accepted)
        m_accepted(std::move(request));
    // the dialog is done
    close();
}

void studio::AlborFftDialogView::dismiss() { close(); }

void studio::AlborFftDialogView::close() {
    // a dialog that is already closed has nothing to release
    if (!m_handle.valid())
        return;
    // ask the application to close the window
    (void)m_app->request_close(m_handle);
}

bool studio::AlborFftDialogView::is_open() const { return m_handle.valid(); }

void studio::AlborFftDialogView::on_window_closed(const albor::WindowHandle handle) {
    // a window the dialog did not open is not its concern
    if (handle != m_handle)
        return;
    // a posted rebuild belongs to the window that just closed
    ++m_generation;
    // the facade and the rows die with the window
    m_handle = {};
    m_root = nullptr;
    m_theme.reset();
    m_engine = nullptr;
    m_text = nullptr;
    m_grid = nullptr;
    m_chips.clear();
    m_breadcrumb = nullptr;
    m_legend = nullptr;
    m_range = nullptr;
    m_from = nullptr;
    m_to = nullptr;
    m_points = nullptr;
    m_custom_points = nullptr;
    m_window = nullptr;
    m_output = nullptr;
    m_format = nullptr;
    m_keep_dc = nullptr;
    m_error = nullptr;
    m_state = FftSelectionState{};
    m_expressions.clear();
    m_accepted = nullptr;
}

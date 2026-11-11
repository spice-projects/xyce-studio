#include "views/charts/add_plot_dialog_view.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "add_plot_dialog.h"
#include "albor/application/application.h"
#include "albor/application/window_stage.h"
#include "albor/layout/row_layout.h"
#include "albor/ui/ui_node.h"
#include "albor/widgets/checkbox.h"
#include "albor/widgets/label.h"
#include "albor/widgets/theme_controller.h"
#include "albor/window/window.h"
#include "app/theme.h"
#include "core/util.h"

namespace
{
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
        // no descendant carries the id
        return nullptr;
    }
} // namespace

studio::AlborAddPlotDialogView::AlborAddPlotDialogView(albor::Application& application, albor::WindowHandle parent) :
    m_app(&application), m_parent(parent) {}

studio::AlborAddPlotDialogView::~AlborAddPlotDialogView() {
    // a dialog still up at teardown asks the application to release it
    if (m_handle.valid())
        (void)m_app->request_close(m_handle);
}

void studio::AlborAddPlotDialogView::show(PlotSelectionState state, AcceptedHandler accepted, CustomResolver resolve) {
    // a second show while the dialog is up is ignored
    if (is_open())
        return;
    // remember the state and the sinks the dialog works from
    m_state = std::move(state);
    m_accepted = std::move(accepted);
    m_resolve = std::move(resolve);
    m_rows.clear();
    // describe a fixed size modal dialog owned by the shell
    albor::WindowOptions options;
    options.title = "Select Plot Expressions";
    options.size = albor::Size{.width = 600.0f, .height = 560.0f};
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
        m_resolve = nullptr;
        return;
    }
    // the host-built rows borrow the stage engines
    albor::WindowStage* stage = m_app->stage(m_handle);
    // a stage that is not wired cannot host the rows
    if (stage == nullptr) {
        (void)m_app->request_close(m_handle);
        return;
    }
    // remember the engines the rows read
    m_engine = &stage->binding_engine();
    m_text = &stage->text_engine();
    // build the rows before the theme walks the tree, so every row takes the palette
    populate();
    // theme the dialog from the same system appearance as the shell
    m_theme = install_theme(*m_app, m_handle);
    // forward the filter, the custom row and the footer actions to this view
    m_root->set_on_filter_changed([this](std::string text) { apply_filter(std::move(text)); });
    m_root->set_on_custom_added([this] { add_custom(); });
    m_root->set_on_accepted([this] { accept(); });
    m_root->set_on_dismissed([this] { dismiss(); });
    // seed the chrome the state names
    m_root->set_allow_custom(m_state.allow_custom);
    m_root->set_error_message("");
    refresh_summary();
    // show the fully seeded window, which avoids a blank first frame
    if (albor::Window* window = m_app->window(m_handle))
        window->show();
    // the filter takes the caret once the first frame has arranged the page
    m_app->post([this] {
        // a dialog that closed before the task ran has nothing to focus
        if (!m_handle.valid() || m_root == nullptr)
            return;
        // the filter is the first field the user types into
        if (albor::WindowStage* posted_stage = m_app->stage(m_handle)) {
            if (albor::UiNode* filter = find_node(*m_root, "add_plot_filter"))
                posted_stage->focus().request_focus(filter);
        }
    });
}

void studio::AlborAddPlotDialogView::close() {
    // a close with no open window is a no-op
    if (!m_handle.valid())
        return;
    // ask the application to close the window, which defers an input driven teardown
    (void)m_app->request_close(m_handle);
}

bool studio::AlborAddPlotDialogView::is_open() const { return m_handle.valid(); }

void studio::AlborAddPlotDialogView::on_window_closed(const albor::WindowHandle handle) {
    // ignore windows this view does not own
    if (handle != m_handle)
        return;
    // the facade and the theme bindings die with the window
    m_handle = {};
    m_root = nullptr;
    m_theme.reset();
    m_engine = nullptr;
    m_text = nullptr;
    m_rows.clear();
    m_state = PlotSelectionState{};
    m_accepted = nullptr;
    m_resolve = nullptr;
}

void studio::AlborAddPlotDialogView::append_row(albor::UiNode& host, const PlotExpression& expression) {
    // the row index keeps the row ids and the toggle callback apart
    const std::size_t index = m_rows.size();
    // the row stacks the box and the expression text side by side
    auto row = std::make_unique<albor::RowLayout>();
    row->set_explicit_id("add_plot_row_" + std::to_string(index));
    row->set_gap(8.0f);
    // the box mirrors the selection and reports every toggle
    auto check = std::make_unique<albor::CheckBox>(m_engine, "add_plot_check_" + std::to_string(index));
    check->set_checked(expression.selected);
    check->set_on_toggle([this, index](const bool checked) {
        // the dialog state follows the box the user toggled
        if (index < m_rows.size())
            m_rows[index].expression.selected = checked;
        // the footer names the new selection count
        refresh_summary();
    });
    // the raw pointers stay valid because the row owns the children
    albor::CheckBox* check_ptr = check.get();
    // the name is the row identity the user reads
    auto name = std::make_unique<albor::Label>(m_engine, m_text, expression.name, "add_plot_name_" + std::to_string(index));
    // the box and the name join the row
    row->add_child(std::move(check));
    row->add_child(std::move(name));
    // a unit reads beside the name when the expression carries one
    if (!expression.unit.empty()) {
        auto unit = std::make_unique<albor::Label>(m_engine, m_text, expression.unit, "add_plot_unit_" + std::to_string(index));
        unit->set_foreground(albor::Color::rgba(0.45f, 0.47f, 0.50f, 1.0f));
        row->add_child(std::move(unit));
    }
    // the host keeps the row pointer so the filter can hide it
    albor::RowLayout* row_ptr = row.get();
    host.add_child(std::move(row));
    m_rows.push_back(RowEntry{.expression = expression, .row = row_ptr, .check = check_ptr});
}

void studio::AlborAddPlotDialogView::populate() {
    // the host column the rows stack inside
    albor::UiNode* host = m_root != nullptr ? find_node(*m_root, "add_plot_rows_host") : nullptr;
    // a dialog without its host cannot show a row
    if (host == nullptr || m_engine == nullptr || m_text == nullptr)
        return;
    // one row per expression the state lists
    for (const PlotExpression& expression : m_state.expressions)
        append_row(*host, expression);
    // the dialog opens without a filter, so every row shows
    apply_filter(std::string{});
}

void studio::AlborAddPlotDialogView::apply_filter(std::string text) {
    // the filter the text field carries, matched case insensitively
    const std::string needle = to_lower(text);
    // every row shows or hides by the expression name it carries
    for (RowEntry& entry : m_rows) {
        // a row without the needle in its name is hidden
        const bool match = needle.empty() || to_lower(entry.expression.name).find(needle) != std::string::npos;
        entry.row->set_visible(match);
    }
    // the host remeasures the visible rows
    if (albor::UiNode* host = m_root != nullptr ? find_node(*m_root, "add_plot_rows_host") : nullptr)
        host->invalidate_layout();
}

void studio::AlborAddPlotDialogView::refresh_summary() {
    // a dialog without its facade has no footer to update
    if (m_root == nullptr)
        return;
    // count the rows the selection carries
    std::size_t selected = 0;
    for (const RowEntry& entry : m_rows) {
        // a selected row joins the count
        if (entry.expression.selected)
            selected++;
    }
    // name the selection beside the row count
    m_root->set_selected_summary(std::to_string(selected) + " of " + std::to_string(m_rows.size()) + " checked");
}

void studio::AlborAddPlotDialogView::add_custom() {
    // a dialog without its facade, a refused custom row or a missing resolver does nothing
    if (m_root == nullptr || !m_state.allow_custom || !m_resolve)
        return;
    // trim the text the user typed
    const std::string text = trim(m_root->custom_text());
    // an empty entry adds nothing
    if (text.empty())
        return;
    // the resolver parses the expression against the active dataset
    const std::optional<PlotExpression> resolved = m_resolve(text);
    // a refused expression keeps the dialog open with its message
    if (!resolved) {
        m_root->set_error_message("The expression could not be evaluated");
        return;
    }
    // a parsed expression clears the previous message
    m_root->set_error_message("");
    // an expression the list already carries selects in place
    for (RowEntry& entry : m_rows) {
        if (entry.expression.name != resolved->name)
            continue;
        // the existing row takes the selection without a duplicate
        entry.expression.selected = true;
        entry.check->set_checked(true);
        refresh_summary();
        m_root->set_custom_text("");
        return;
    }
    // the new expression joins the state and the list selected
    m_state.expressions.push_back(*resolved);
    if (albor::UiNode* host = find_node(*m_root, "add_plot_rows_host"))
        append_row(*host, *resolved);
    refresh_summary();
    // the field empties for the next entry
    m_root->set_custom_text("");
}

void studio::AlborAddPlotDialogView::accept() {
    // the names the user left selected
    std::vector<std::string> names;
    for (const RowEntry& entry : m_rows) {
        // a selected row is part of the applied set
        if (entry.expression.selected)
            names.push_back(entry.expression.name);
    }
    // take the sink out before the teardown clears the view state
    AcceptedHandler accepted = std::move(m_accepted);
    m_accepted = nullptr;
    // hide the dialog before the charts it edits change
    close();
    // hand the selection to the charts presenter
    if (accepted)
        accepted(std::move(names));
}

void studio::AlborAddPlotDialogView::dismiss() {
    // a dismissed dialog applies nothing
    m_accepted = nullptr;
    close();
}

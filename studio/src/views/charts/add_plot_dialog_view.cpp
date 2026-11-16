#include "views/charts/add_plot_dialog_view.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "add_plot_dialog.h"
#include "albor/application/application.h"
#include "albor/application/window_stage.h"
#include "albor/layout/grid_layout.h"
#include "albor/style/theme.h"
#include "albor/ui/ui_node.h"
#include "albor/widgets/theme_controller.h"
#include "albor/window/window.h"
#include "app/theme.h"
#include "charts/expression_colors.h"
#include "charts/expression_tree.h"
#include "core/util.h"
#include "views/charts/add_plot_widgets.h"

namespace
{
    // the size the dialog window opens with, which is the reference card size
    constexpr float DIALOG_WIDTH = 600.0f;

    constexpr float DIALOG_HEIGHT = 560.0f;

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
    // a posted rebuild must not reach a destroyed view
    *m_alive = false;
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
    m_expressions = m_state.expressions;
    m_accepted = std::move(accepted);
    m_resolve = std::move(resolve);
    // the new show owns a fresh generation
    ++m_generation;
    // describe a fixed size modal dialog owned by the shell
    albor::WindowOptions options;
    options.title = "Select Plot Expressions";
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
        m_resolve = nullptr;
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
    albor::UiNode* grid_host = find_node(*m_root, "add_plot_grid");
    // the typed pin keeps the grid as the grid layout it is
    m_grid = dynamic_cast<albor::GridLayout*>(grid_host);
    // the breadcrumb mounts into the host the card declares
    albor::UiNode* breadcrumb_host = find_node(*m_root, "add_plot_breadcrumb_host");
    // the legend mounts the same way
    albor::UiNode* legend_host = find_node(*m_root, "add_plot_legend_host");
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
            // the cards and the chrome follow the scope
            refresh_chrome();
            // rebuild the cards the new scope publishes
            rebuild_cards();
        });
        // the selected-only link flattens the grid to the selected expressions
        m_breadcrumb->set_on_selected_clicked([this] {
            // flip the selected-only view
            m_tree.set_show_selected(!m_tree.show_selected());
            // the cards and the chrome follow the view
            refresh_chrome();
            // rebuild the cards the view publishes
            rebuild_cards();
        });
    }
    // build the tree and the first card set from the offered expressions
    populate();
    // theme the dialog from the same system appearance as the shell
    m_theme = install_theme(*m_app, m_handle);
    // the filter flattens the grid to the matching expressions
    m_root->set_on_filter_changed([this](std::string text) {
        // the tree applies the filter to its own names
        m_tree.set_filter(std::move(text));
        // the cards and the chrome follow the filter
        refresh_chrome();
        // rebuild the cards the filter leaves
        rebuild_cards();
    });
    // the custom row resolves the typed expression
    m_root->set_on_custom_added([this] { add_custom(); });
    // the footer commits the selection
    m_root->set_on_accepted([this] { accept(); });
    // the footer dismisses the dialog
    m_root->set_on_dismissed([this] { dismiss(); });
    // seed the chrome the state names
    m_root->set_allow_custom(m_state.allow_custom);
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
    // a posted rebuild belongs to the window that just closed
    ++m_generation;
    // the facade and the theme bindings die with the window
    m_handle = {};
    m_root = nullptr;
    m_theme.reset();
    m_engine = nullptr;
    m_text = nullptr;
    m_grid = nullptr;
    m_chips.clear();
    m_breadcrumb = nullptr;
    m_legend = nullptr;
    m_state = PlotSelectionState{};
    m_expressions.clear();
    m_accepted = nullptr;
    m_resolve = nullptr;
}

void studio::AlborAddPlotDialogView::populate() {
    // a dialog without its window has no tree to fill
    if (m_root == nullptr)
        return;
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
    // the chrome and the cards follow the tree
    refresh_chrome();
    // build the cards the tree publishes
    rebuild_cards();
}

void studio::AlborAddPlotDialogView::rebuild_cards() {
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
            auto chip = std::make_unique<ScopeChip>(m_engine, m_text, card.label, card.kind, card.count, "add_plot_card_" + std::to_string(index));
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
                    // the chrome and the cards follow the scope
                    refresh_chrome();
                    // rebuild the cards the new scope publishes
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
        auto chip = std::make_unique<ExpressionChip>(m_engine, m_text, card.label, card.full_name, expression_colors::expression_unit_color(card.type), card.selected, "add_plot_card_" + std::to_string(index));
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
                    // the chrome and the cards follow the filtered view
                    refresh_chrome();
                    // rebuild the cards the view publishes
                    rebuild_cards();
                });
                // the chip that dispatched is replaced by the rebuild
                return;
            }
            // the card still sits at the pressed index in the full list
            if (index < m_chips.size() && m_chips[index] != nullptr && index < m_tree.cards().size())
                m_chips[index]->set_selected(m_tree.cards()[index].selected);
        });
        // a secondary press appends the full name to the custom builder
        chip->set_on_context_menu([this, name = card.full_name](albor::Point) { append_expression(name); });
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

void studio::AlborAddPlotDialogView::refresh_chrome() {
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

void studio::AlborAddPlotDialogView::set_error(std::string message) {
    // the dialog only exists while its facade does
    if (m_root == nullptr)
        return;
    // the card shows the message only while it is non empty
    m_root->set_error_message(std::move(message));
    // the message row follows the text
    m_root->set_show_error(!m_root->error_message().empty());
}

void studio::AlborAddPlotDialogView::append_expression(std::string name) {
    // a dialog without its facade has no builder to fill
    if (m_root == nullptr)
        return;
    // the full name appends to what the builder already carries
    m_root->set_custom_text(m_root->custom_text() + std::move(name));
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
        set_error("The expression could not be evaluated");
        return;
    }
    // a parsed expression clears the previous message
    set_error({});
    // an expression the tree already carries selects in place
    if (m_tree.contains(resolved->name)) {
        // the existing leaf takes the selection without a duplicate
        m_tree.set_selected(resolved->name, true);
    }
    else {
        // the new leaf joins the tree path its name encodes
        m_tree.add_leaf(resolved->name, resolved->unit);
        // the leaf starts selected
        m_tree.set_selected(resolved->name, true);
        // the legend learns the unit the new leaf carries
        m_expressions.push_back(*resolved);
    }
    // the field empties for the next entry
    m_root->set_custom_text("");
    // the chrome and the cards follow the new leaf
    refresh_chrome();
    // rebuild the cards the tree publishes
    rebuild_cards();
}

void studio::AlborAddPlotDialogView::accept() {
    // the names the user left selected
    const std::vector<std::string> names = m_tree.selected_names();
    // take the sink out before the teardown clears the view state
    AcceptedHandler accepted = std::move(m_accepted);
    m_accepted = nullptr;
    // hide the dialog before the charts it edits change
    close();
    // hand the selection to the charts presenter
    if (accepted)
        accepted(names);
}

void studio::AlborAddPlotDialogView::dismiss() {
    // a dismissed dialog applies nothing
    m_accepted = nullptr;
    (void)m_resolve;
    close();
}

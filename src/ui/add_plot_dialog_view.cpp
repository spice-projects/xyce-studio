#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <slint.h>

#include <main_window.h>

#include "../core/util.h"
#include "add_plot_dialog_view.h"
#include "expression_tree.h"
#include "smith_plot_filter.h"

namespace add_plot_dialog_view
{
    namespace
    {
        slint::SharedString to_shared_string(std::string value) { return slint::SharedString(value); }

        // convert a visible card into the slint model item
        main_window::ExpressionItem to_item(const ExpressionCard& card) {
            return main_window::ExpressionItem{
                to_shared_string(card.label), to_shared_string(card.kind), to_shared_string(card.type), card.is_scope, card.selected, card.count, to_shared_string(card.full_name),
            };
        }

        // convert a breadcrumb entry into the slint model item
        main_window::BreadcrumbItem to_breadcrumb_item(const BreadcrumbEntry& entry) { return main_window::BreadcrumbItem{to_shared_string(entry.label), static_cast<int>(entry.level)}; }
    } // namespace

    struct AddPlotDialogView::Impl
    {
        // the main window handle; the panel is an inline child of the window,
        // so all interaction goes through the window's properties and callbacks
        slint::ComponentHandle<main_window::MainWindow> window;

        // renderer used to look up expressions and apply the selection
        ChartsRenderer& renderer;

        // notified on both accept and cancel, after the panel is hidden;
        // the caller releases the modal state from here
        std::function<void()> on_closed;

        // scope navigation over the expressions; subcircuit and sheet scopes
        // collapse into drill-in cards, the filter flattens the results
        ExpressionTree tree;

        // card model shown in the panel; the panel uses the indices of this
        // model in the expression-clicked callback
        std::shared_ptr<slint::VectorModel<main_window::ExpressionItem>> expressions;

        // breadcrumb model of the browsed scope
        std::shared_ptr<slint::VectorModel<main_window::BreadcrumbItem>> breadcrumb;

        // chart being edited
        size_t chart_index = 0;

        // active filter text, reapplied when the item list changes
        std::string m_filter_query;

        Impl(slint::ComponentHandle<main_window::MainWindow> w, ChartsRenderer& r) :
            window(w), renderer(r) {
            expressions = std::make_shared<slint::VectorModel<main_window::ExpressionItem>>();
            window->set_add_plot_expressions(expressions);
            breadcrumb = std::make_shared<slint::VectorModel<main_window::BreadcrumbItem>>();
            window->set_add_plot_breadcrumb(breadcrumb);
            connect_callbacks();
        }

        void connect_callbacks() {
            window->on_add_plot_filter_changed([this](slint::SharedString query) { apply_filter(query); });
            window->on_add_plot_expression_clicked([this](int index) { activate_card(index); });
            window->on_add_plot_expression_right_clicked([this](int index) { append_expression_name(index); });
            window->on_add_plot_breadcrumb_clicked([this](int level) { open_breadcrumb(level); });
            window->on_add_plot_selected_link_clicked([this] { toggle_show_selected(); });
            window->on_add_plot_custom_add([this] { add_custom_expression(); });
            window->on_add_plot_accepted([this] { accept(); });
            window->on_add_plot_dismissed([this] { dismiss(); });
        }

        void populate() {
            // build the (name, type) pairs from the expression manager; smith
            // charts only offer the diagonal parameter entries (sii)
            const bool smith = renderer.active_dataset_is_smith();
            std::vector<std::pair<std::string, std::string>> items;
            for (AnyExpression* expression : renderer.all_expressions()) {
                // smith charts filter out every non-diagonal expression
                if (smith && !is_smith_plot_expression(*expression))
                    continue;
                items.emplace_back(expression_name(*expression), expression_type(*expression));
            }
            // smith charts reject arbitrary custom expressions, only the
            // diagonal parameter entries can map to the gamma plane
            window->set_add_plot_allow_custom_expressions(!smith);
            // rebuild the scope tree and return to the root scope
            tree.rebuild(items);
            // mark the currently plotted expressions as selected
            const auto selected = renderer.chart_selected_expressions(chart_index);
            for (AnyExpression* expression : selected) {
                tree.set_selected(expression_name(*expression), true);
            }
            // reset the filter and error state
            m_filter_query.clear();
            window->set_add_plot_filter_text(slint::SharedString(""));
            window->set_add_plot_show_error(false);
            // rebuild the cards with the empty filter
            refresh_cards();
        }

        static std::string expression_name(const AnyExpression& expression) {
            return std::visit([](const auto& e) { return e.name(); }, expression);
        }

        static std::string expression_type(const AnyExpression& expression) {
            // the unit is the classification; expressions without a unit are misc
            std::string type = std::visit([](const auto& e) { return e.unit(); }, expression);
            return type.empty() ? "Misc" : type;
        }

        void refresh_cards() {
            // rebuild the model rows from the tree's visible cards
            expressions->clear();
            for (const ExpressionCard& card : tree.cards())
                expressions->push_back(to_item(card));
            // rebuild the breadcrumb path of the browsed scope
            breadcrumb->clear();
            for (const BreadcrumbEntry& entry : tree.breadcrumb())
                breadcrumb->push_back(to_breadcrumb_item(entry));
            // reflect the selected-only view state on the toggle link
            window->set_add_plot_show_selected(tree.show_selected());
        }

        void apply_filter(const slint::SharedString& query) {
            // remember the active filter, it is reapplied when items change
            m_filter_query = std::string(query);
            // a non-empty filter flattens every matching expression into cards
            tree.set_filter(m_filter_query);
            refresh_cards();
        }

        void activate_card(int index) {
            // scope cards drill into the scope, expression cards toggle the
            // selection
            if (index < 0 || static_cast<size_t>(index) >= tree.cards().size())
                return;
            tree.activate(static_cast<size_t>(index));
            refresh_cards();
        }

        void open_breadcrumb(int level) {
            // restore the scope stack to the clicked breadcrumb level
            if (level < 0)
                return;
            tree.open_breadcrumb(static_cast<size_t>(level));
            refresh_cards();
        }

        void toggle_show_selected() {
            // toggle the selected-only flat view
            tree.set_show_selected(!tree.show_selected());
            refresh_cards();
        }

        void append_expression_name(int index) {
            // copy the right-clicked expression name into the Expression
            // builder, only when the custom entry is shown and the card is
            // an expression
            if (!window->get_add_plot_allow_custom_expressions())
                return;
            if (index < 0 || static_cast<size_t>(index) >= tree.cards().size())
                return;
            const ExpressionCard& card = tree.cards()[static_cast<size_t>(index)];
            // scope cards have no expression name to append
            if (card.is_scope)
                return;
            // the builder needs the full expression name, not the
            // scope-relative display label
            slint::SharedString value = window->get_add_plot_custom_text();
            value = slint::SharedString(std::string(value) + card.full_name);
            window->set_add_plot_custom_text(value);
        }

        void add_custom_expression() {
            // read the custom expression text
            const std::string text = std::string(window->get_add_plot_custom_text());
            const auto trimmed_start = text.find_first_not_of(" \t\r\n");
            const auto trimmed_end = text.find_last_not_of(" \t\r\n");
            // ignore empty input
            if (trimmed_start == std::string::npos)
                return;
            const std::string trimmed = text.substr(trimmed_start, trimmed_end - trimmed_start + 1);
            // evaluate the custom expression
            AnyExpression* expression = renderer.evaluate_expression(trimmed);
            if (expression == nullptr) {
                window->set_add_plot_show_error(true);
                return;
            }
            window->set_add_plot_show_error(false);
            // derive name and type
            const std::string name = expression_name(*expression);
            const std::string type = expression_type(*expression);
            // existing expressions are selected in place, new ones are appended
            if (tree.contains(name)) {
                tree.set_selected(name, true);
            }
            else {
                tree.add_leaf(name, type);
                tree.set_selected(name, true);
            }
            // clear the custom input for the next entry
            window->set_add_plot_custom_text(slint::SharedString(""));
            // rebuild the cards, respecting the active filter
            refresh_cards();
        }

        void accept() {
            // gather the selected expressions by name
            const auto names = tree.selected_names();
            const std::set<std::string> name_set(names.begin(), names.end());
            std::set<AnyExpression*> selected;
            for (AnyExpression* expression : renderer.all_expressions()) {
                if (name_set.contains(expression_name(*expression)))
                    selected.insert(expression);
            }
            // hide the panel before applying the selection
            window->set_add_plot_visible(false);
            // release the modal state held by the caller
            if (on_closed)
                on_closed();
            // apply the selection to the chart
            renderer.plot_chart_expressions(chart_index, selected);
        }

        void dismiss() {
            // hide the panel and drop the pending state
            window->set_add_plot_visible(false);
            // release the modal state held by the caller
            if (on_closed)
                on_closed();
        }
    };

    AddPlotDialogView::AddPlotDialogView(slint::ComponentHandle<main_window::MainWindow> main_window, ChartsRenderer& renderer) :
        m_impl(std::make_unique<Impl>(main_window, renderer)) {}

    AddPlotDialogView::~AddPlotDialogView() = default;

    void AddPlotDialogView::show_for_chart(float chart_position, const std::function<void()>& on_closed) {
        // remember the close notification for this show
        m_impl->on_closed = on_closed;
        // resolve the chart index from the panel position
        m_impl->chart_index = m_impl->renderer.position_to_index(chart_position);
        // rebuild the expression list for the current state
        m_impl->populate();
        // show the inline panel
        m_impl->window->set_add_plot_visible(true);
    }
} // namespace add_plot_dialog_view

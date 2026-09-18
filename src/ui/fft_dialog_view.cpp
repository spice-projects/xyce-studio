#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <slint.h>

#include <main_window.h>

#include "../core/util.h"
#include "../dsp/fft.h"
#include "../expression/expression.h"
#include "expression_tree.h"
#include "fft_dialog_view.h"

namespace fft_dialog_view
{
    namespace
    {
        // preset np options exposed by the panel, defaulting to the xyce default of 1024
        const std::vector<size_t> NP_OPTIONS = {256, 512, 1024, 2048, 4096, 8192, 16384};

        // index of the "Custom..." entry in the np combo box
        const size_t CUSTOM_NP_INDEX = NP_OPTIONS.size();

        // canonicalize a requested np value to the nearest power of two, rounding up on the midpoint
        size_t resolve_np(size_t requested_np) {
            // find the exponent bounds around the requested value
            const double log2_value = std::log2(static_cast<double>(requested_np));
            // lower power of two exponent
            const size_t lower_exponent = static_cast<size_t>(std::floor(log2_value));
            // upper power of two exponent
            const size_t upper_exponent = static_cast<size_t>(std::ceil(log2_value));
            // lower power of two value
            const size_t lower = static_cast<size_t>(1) << lower_exponent;
            // upper power of two value
            const size_t upper = static_cast<size_t>(1) << upper_exponent;
            // round down unless closer to the upper bound
            return (requested_np - lower < upper - requested_np) ? lower : upper;
        }

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

    struct FftDialogView::Impl
    {
        // the main window handle; the panel is an inline child of the window,
        // so all interaction goes through the window's properties and callbacks
        slint::ComponentHandle<main_window::MainWindow> window;

        // renderer used to look up expressions and the abscissa range
        ChartsRenderer& renderer;

        // presenter that receives the accepted expressions and parameters
        MainWindowViewDefEvents* handler = nullptr;

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

        // full abscissa value range of the loaded file
        double m_min_abscissa_value = 0.0;
        double m_max_abscissa_value = 1.0;

        // active filter text, reapplied when the item list changes
        std::string m_filter_query;

        Impl(slint::ComponentHandle<main_window::MainWindow> w, ChartsRenderer& r) :
            window(w), renderer(r) {
            expressions = std::make_shared<slint::VectorModel<main_window::ExpressionItem>>();
            window->set_fft_expressions(expressions);
            breadcrumb = std::make_shared<slint::VectorModel<main_window::BreadcrumbItem>>();
            window->set_fft_breadcrumb(breadcrumb);
            connect_callbacks();
        }

        void connect_callbacks() {
            window->on_fft_filter_changed([this](slint::SharedString query) { apply_filter(query); });
            window->on_fft_expression_clicked([this](int index) { activate_card(index); });
            window->on_fft_breadcrumb_clicked([this](int level) { open_breadcrumb(level); });
            window->on_fft_selected_link_clicked([this] { toggle_show_selected(); });
            window->on_fft_accepted([this] { accept(); });
            window->on_fft_dismissed([this] { dismiss(); });
        }

        void populate() {
            // full abscissa value range used for the "All" and "Current Zoom" modes
            const auto [min_abscissa, max_abscissa] = renderer.abscissa_range();
            m_min_abscissa_value = min_abscissa;
            m_max_abscissa_value = max_abscissa;
            // FFT is only for real, non-time-domain expressions, so build the
            // (name, type) pairs from the eligible subset
            std::vector<std::pair<std::string, std::string>> items;
            for (AnyExpression* expression : renderer.all_expressions()) {
                // skip non-real expressions
                if (!std::holds_alternative<Expression<double>>(*expression))
                    continue;
                // skip time-domain expressions (unit "s")
                if (std::get<Expression<double>>(*expression).unit() == "s")
                    continue;
                items.emplace_back(expression_name(*expression), expression_type(*expression));
            }
            // rebuild the scope tree and return to the root scope
            tree.rebuild(items);
            // mark the currently plotted expressions as selected
            const auto selected = renderer.chart_selected_expressions(chart_index);
            for (AnyExpression* expression : selected) {
                tree.set_selected(expression_name(*expression), true);
            }
            // reset the filter and error state
            m_filter_query.clear();
            window->set_fft_filter_text(slint::SharedString(""));
            window->set_fft_show_error(false);
            window->set_fft_error_message(slint::SharedString(""));
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
            window->set_fft_show_selected(tree.show_selected());
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

        // show a validation error inline and keep the panel open
        void set_error(const char* message) {
            window->set_fft_show_error(true);
            window->set_fft_error_message(slint::SharedString(message));
        }

        // selected expressions mapped back to the expression manager pointers
        std::vector<AnyExpression*> selected_expressions() const {
            // collect the selected leaf names in tree order
            const auto names = tree.selected_names();
            const std::set<std::string> name_set(names.begin(), names.end());
            std::vector<AnyExpression*> selected;
            for (AnyExpression* expression : renderer.all_expressions()) {
                if (name_set.contains(expression_name(*expression)))
                    selected.push_back(expression);
            }
            return selected;
        }

        void accept() {
            // build the FFT parameters from the panel state; validation
            // failures keep the panel open with an inline error
            fft::FftParameters params;
            // resolve the data range
            switch (window->get_fft_range_mode()) {
            case 0: // all
            case 1: // current zoom (no zoom support in the slint renderer yet, use the full range)
                params.start = m_min_abscissa_value;
                params.stop = m_max_abscissa_value;
                break;
            case 2: { // custom
                const std::string from_str = std::string(window->get_fft_custom_from());
                const std::string to_str = std::string(window->get_fft_custom_to());
                try {
                    params.start = std::stod(from_str);
                    params.stop = std::stod(to_str);
                }
                catch (...) {
                    set_error("Invalid custom range values.");
                    return;
                }
                // validate the range order
                if (params.start >= params.stop) {
                    set_error("Custom range 'from' must be less than 'to'.");
                    return;
                }
                break;
            }
            default:
                params.start = m_min_abscissa_value;
                params.stop = m_max_abscissa_value;
                break;
            }
            // resolve the window function selection
            switch (window->get_fft_window_index()) {
            case 0:
                params.window = fft::WindowFunction::RECTANGULAR;
                break;
            case 1:
                params.window = fft::WindowFunction::HAMMING;
                break;
            case 2:
                params.window = fft::WindowFunction::HANNING;
                break;
            case 3:
                params.window = fft::WindowFunction::BLACKMAN;
                break;
            default:
                params.window = fft::WindowFunction::HANNING;
                break;
            }
            // resolve the output type selection
            switch (window->get_fft_output_index()) {
            case 0:
                params.output = fft::FftOutput::MAGNITUDE;
                break;
            case 1:
                params.output = fft::FftOutput::MAGNITUDE_DB;
                break;
            case 2:
                params.output = fft::FftOutput::PHASE;
                break;
            default:
                params.output = fft::FftOutput::MAGNITUDE;
                break;
            }
            // resolve the format selection
            switch (window->get_fft_format_index()) {
            case 0:
                params.format = fft::FftFormat::NORM;
                break;
            case 1:
                params.format = fft::FftFormat::UNORM;
                break;
            default:
                params.format = fft::FftFormat::NORM;
                break;
            }
            // read the keep-dc value
            params.keep_dc = window->get_fft_keep_dc();
            // handle the custom np input
            if (window->get_fft_np_index() == static_cast<int>(CUSTOM_NP_INDEX)) {
                // validate the custom np input
                const std::string np_str = std::string(window->get_fft_custom_np());
                if (np_str.empty()) {
                    set_error("Please enter a custom number of FFT points.");
                    return;
                }
                try {
                    params.np = static_cast<size_t>(std::stoll(np_str));
                }
                catch (...) {
                    set_error("Invalid number of FFT points.");
                    return;
                }
                // validate np is at least 4
                if (params.np < 4) {
                    set_error("Number of FFT points must be at least 4.");
                    return;
                }
                // canonicalize to the nearest power of two and show the resolved value
                params.np = resolve_np(params.np);
                window->set_fft_custom_np(slint::SharedString(std::to_string(params.np)));
            }
            else {
                // use the preset np value
                params.np = NP_OPTIONS[static_cast<size_t>(window->get_fft_np_index())];
            }
            // gather the selected expressions before hiding the panel
            std::vector<AnyExpression*> selected = selected_expressions();
            // hide the panel before delivering the result
            window->set_fft_panel_visible(false);
            // release the modal state held by the caller
            if (on_closed)
                on_closed();
            // deliver the accepted result to the presenter, which runs the transform
            if (handler)
                handler->on_fft_dialog_result(std::move(selected), params);
        }

        void dismiss() {
            // hide the panel and drop the pending state
            window->set_fft_panel_visible(false);
            // release the modal state held by the caller
            if (on_closed)
                on_closed();
        }
    };

    FftDialogView::FftDialogView(slint::ComponentHandle<main_window::MainWindow> main_window, ChartsRenderer& renderer) :
        m_impl(std::make_unique<Impl>(main_window, renderer)) {}

    FftDialogView::~FftDialogView() = default;

    void FftDialogView::show_for_chart(size_t chart_index, MainWindowViewDefEvents& handler, const std::function<void()>& on_closed) {
        // remember the result delivery and close notification for this show
        m_impl->handler = &handler;
        m_impl->on_closed = on_closed;
        // remember the chart whose plotted expressions are pre-selected
        m_impl->chart_index = chart_index;
        // rebuild the expression list for the current state
        m_impl->populate();
        // show the inline panel
        m_impl->window->set_fft_panel_visible(true);
    }
} // namespace fft_dialog_view

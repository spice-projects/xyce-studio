#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "albor/window/window_handle.h"
#include "charts/expression_tree.h"
#include "charts/plot_selection.h"

namespace albor
{
    class AddPlotDialog;
    class Application;
    class BindingEngine;
    class GridLayout;
    class TextEngine;
    class ThemeController;
    class UiNode;
} // namespace albor

namespace studio
{
    class ExpressionChip;
    class PlotBreadcrumb;
    class PlotLegend;

    // the Albor dialog window that edits the expressions one chart plots
    class AlborAddPlotDialogView final
    {
    public:
        // the accepted expression names the dialog hands back
        using AcceptedHandler = std::function<void(std::vector<std::string>)>;

        // the resolver a custom expression goes through
        using CustomResolver = std::function<std::optional<PlotExpression>(const std::string&)>;

        // build the dialog surface parented to the shell window
        AlborAddPlotDialogView(albor::Application& application, albor::WindowHandle parent);

        AlborAddPlotDialogView(const AlborAddPlotDialogView&) = delete;

        AlborAddPlotDialogView& operator=(const AlborAddPlotDialogView&) = delete;

        ~AlborAddPlotDialogView();

        // open the dialog over the expressions and rules the charts presenter supplies
        void show(PlotSelectionState state, AcceptedHandler accepted, CustomResolver resolve);

        // ask the application to close the dialog
        void close();

        // report whether the dialog window is currently open
        [[nodiscard]] bool is_open() const;

        // clear the borrowed facade when the application reports the window is gone
        void on_window_closed(albor::WindowHandle handle);

    private:
        // build the tree and the first card set for the state the dialog opened with
        void populate();

        // drop the previous chips and build the cards the tree publishes
        void rebuild_cards();

        // update the breadcrumb path and the legend
        void refresh_chrome();

        // show or clear the custom expression error
        void set_error(std::string message);

        // append a full expression name to the custom builder
        void append_expression(std::string name);

        // resolve the typed custom expression and add it selected
        void add_custom();

        // hand the selected names to the caller and close
        void accept();

        // close without applying the selection
        void dismiss();

        // the application that owns the window and its engines
        albor::Application* m_app = nullptr;

        // the shell window the dialog is parented to
        albor::WindowHandle m_parent;

        // the dialog window, invalid while it is closed
        albor::WindowHandle m_handle;

        // the borrowed generated facade
        albor::AddPlotDialog* m_root = nullptr;

        // the theme bindings installed over the dialog tree
        std::unique_ptr<albor::ThemeController> m_theme;

        // the stage engines the host-built chips borrow
        albor::BindingEngine* m_engine = nullptr;

        albor::TextEngine* m_text = nullptr;

        // the grid the expression chips fill
        albor::GridLayout* m_grid = nullptr;

        // the expression chips of the current cards, null for a scope card
        std::vector<ExpressionChip*> m_chips;

        // the breadcrumb and legend bars the chrome hosts
        PlotBreadcrumb* m_breadcrumb = nullptr;

        PlotLegend* m_legend = nullptr;

        // the expression selection tree the cards and the breadcrumb read
        ExpressionTree m_tree;

        // the expressions the dataset offered when the dialog opened
        std::vector<PlotExpression> m_expressions;

        // the accepted sink the dialog reports to
        AcceptedHandler m_accepted;

        // the resolver a custom expression goes through
        CustomResolver m_resolve;

        // the state the dialog opened with
        PlotSelectionState m_state;

        // the show generation, which a posted rebuild checks before it runs
        std::uint64_t m_generation = 0;

        // whether the view still exists, which a posted rebuild checks
        std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    };
} // namespace studio

#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "albor/window/window_handle.h"
#include "charts/plot_selection.h"

namespace albor
{
    class AddPlotDialog;
    class Application;
    class BindingEngine;
    class CheckBox;
    class RowLayout;
    class TextEngine;
    class ThemeController;
    class UiNode;
} // namespace albor

namespace studio
{
    // the Albor window that renders the add/remove plots dialog
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
        // one expression row the dialog shows
        struct RowEntry
        {
            // the expression the row edits
            PlotExpression expression;

            // the row container the filter hides
            albor::RowLayout* row = nullptr;

            // the checkbox whose state mirrors the expression
            albor::CheckBox* check = nullptr;
        };

        // build one row for an expression and add it to the host column
        void append_row(albor::UiNode& host, const PlotExpression& expression);

        // build the rows the state lists
        void populate();

        // hide the rows the filter does not match
        void apply_filter(std::string text);

        // name the number of selected expressions in the footer
        void refresh_summary();

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

        // the stage engines the host-built rows borrow
        albor::BindingEngine* m_engine = nullptr;

        albor::TextEngine* m_text = nullptr;

        // the expression rows the dialog currently shows
        std::vector<RowEntry> m_rows;

        // the state the dialog opened with
        PlotSelectionState m_state;

        // the sink the accepted selection reaches
        AcceptedHandler m_accepted;

        // the resolver a custom expression goes through
        CustomResolver m_resolve;
    };
} // namespace studio

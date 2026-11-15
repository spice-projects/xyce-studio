#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "albor/core/font_weight.h"
#include "albor/window/window_handle.h"
#include "charts/step_tool.h"

namespace albor
{
    class Application;
    class BindingEngine;
    class CheckBox;
    class ColumnLayout;
    class Label;
    class RowLayout;
    class StepToolDialog;
    class TextEngine;
    class ThemeController;
    class UiNode;
} // namespace albor

namespace studio
{
    // the Albor dialog window that selects the parameter combinations one chart keeps
    class AlborStepToolDialogView final
    {
    public:
        // the step indexes the dialog hands back
        using AcceptedHandler = std::function<void(std::vector<std::size_t>)>;

        // build the dialog surface parented to the shell window
        AlborStepToolDialogView(albor::Application& application, albor::WindowHandle parent);

        AlborStepToolDialogView(const AlborStepToolDialogView&) = delete;

        AlborStepToolDialogView& operator=(const AlborStepToolDialogView&) = delete;

        ~AlborStepToolDialogView();

        // open the dialog over the steps the charts presenter supplies
        void show(StepToolState state, AcceptedHandler accepted);

        // ask the application to close the dialog
        void close();

        // report whether the dialog window is currently open
        [[nodiscard]] bool is_open() const;

        // clear the borrowed facade when the application reports the window is gone
        void on_window_closed(albor::WindowHandle handle);

    private:
        // build the header and one row per step
        void populate();

        // select or clear every step
        void select_all(bool selected);

        // flip every step
        void invert_selection();

        // refresh the selected count the footer shows
        void update_count();

        // hand the selected indexes to the caller and close
        void accept();

        // close without applying the selection
        void dismiss();

        // append one label cell to a row, fixed or stretching
        void add_label_cell(albor::RowLayout& row, float width, std::string text, float font_size, albor::FontWeight weight);

        // the application that owns the window and its engines
        albor::Application* m_app = nullptr;

        // the shell window the dialog is parented to
        albor::WindowHandle m_parent;

        // the dialog window, invalid while it is closed
        albor::WindowHandle m_handle;

        // the borrowed generated facade
        albor::StepToolDialog* m_root = nullptr;

        // the theme bindings installed over the dialog tree
        std::unique_ptr<albor::ThemeController> m_theme;

        // the stage engines the host-built rows borrow
        albor::BindingEngine* m_engine = nullptr;

        albor::TextEngine* m_text = nullptr;

        // the column the header and rows fill
        albor::ColumnLayout* m_rows = nullptr;

        // the selected count the footer shows
        albor::Label* m_count = nullptr;

        // the checkboxes of the current rows
        std::vector<albor::CheckBox*> m_boxes;

        // the state the dialog edits
        StepToolState m_state;

        // the sink the accepted step indexes reach
        AcceptedHandler m_accepted;
    };
} // namespace studio

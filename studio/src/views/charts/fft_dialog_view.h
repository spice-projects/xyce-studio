#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "albor/core/font_weight.h"
#include "albor/window/window_handle.h"
#include "charts/expression_tree.h"
#include "charts/fft_selection.h"

namespace albor
{
    class Application;
    class BindingEngine;
    class CheckBox;
    class ColumnLayout;
    class ComboBox;
    class FftDialog;
    class GridLayout;
    class Label;
    class RowLayout;
    class TextEngine;
    class TextField;
    class ThemeController;
    class UiNode;
} // namespace albor

namespace studio
{
    class ExpressionChip;
    class PlotBreadcrumb;
    class PlotLegend;

    // the Albor dialog window that selects the expressions and parameters of one FFT
    class AlborFftDialogView final
    {
    public:
        // the transform the dialog hands back
        using AcceptedHandler = std::function<void(FftRequest)>;

        // build the dialog surface parented to the shell window
        AlborFftDialogView(albor::Application& application, albor::WindowHandle parent);

        AlborFftDialogView(const AlborFftDialogView&) = delete;

        AlborFftDialogView& operator=(const AlborFftDialogView&) = delete;

        ~AlborFftDialogView();

        // open the dialog over the expressions and ranges the charts presenter supplies
        void show(FftSelectionState state, AcceptedHandler accepted);

        // ask the application to close the dialog
        void close();

        // report whether the dialog window is currently open
        [[nodiscard]] bool is_open() const;

        // clear the borrowed facade when the application reports the window is gone
        void on_window_closed(albor::WindowHandle handle);

    private:
        // rebuild the expression tree from the offered expressions
        void populate();

        // drop the chips and build the cards the tree publishes
        void rebuild_cards();

        // update the breadcrumb and the legend from the tree
        void refresh_chrome();

        // append one label cell to a row, fixed when a width is given and stretching otherwise
        void add_label_cell(albor::RowLayout& row, float width, std::string text, float font_size, albor::FontWeight weight);

        // append one caption and one stretching control row
        void add_control_row(std::string caption, std::unique_ptr<albor::UiNode> control);

        // append two caption and control pairs on one row
        void add_pair_row(std::string left_caption, std::unique_ptr<albor::UiNode> left_control, std::string right_caption, std::unique_ptr<albor::UiNode> right_control);

        // write the fields the range choice names and enable the custom ones
        void apply_range_choice();

        // write the points the preset names and enable the custom field
        void apply_points_choice();

        // refresh the visible validation message
        void set_error(std::string message);

        // read the fields, validate them and hand the request back
        void accept();

        // close without applying the transform
        void dismiss();

        // the application that owns the window and its engines
        albor::Application* m_app = nullptr;

        // the shell window the dialog is parented to
        albor::WindowHandle m_parent;

        // the dialog window, invalid while it is closed
        albor::WindowHandle m_handle;

        // the borrowed generated facade
        albor::FftDialog* m_root = nullptr;

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

        // the expressions the tree was built from
        std::vector<PlotExpression> m_expressions;

        // the range choice and its custom interval
        albor::ComboBox* m_range = nullptr;

        albor::TextField* m_from = nullptr;

        albor::TextField* m_to = nullptr;

        // the points preset and its custom length
        albor::ComboBox* m_points = nullptr;

        albor::TextField* m_custom_points = nullptr;

        // the transform choices
        albor::ComboBox* m_window = nullptr;

        albor::ComboBox* m_output = nullptr;

        albor::ComboBox* m_format = nullptr;

        // whether the mean stays in the spectrum
        albor::CheckBox* m_keep_dc = nullptr;

        // the validation message
        albor::Label* m_error = nullptr;

        // the dialog generation, which a posted rebuild observes
        std::uint64_t m_generation = 0;

        // the shared flag a posted rebuild observes
        std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);

        // the state the dialog edits
        FftSelectionState m_state;

        // the sink the accepted transform reaches
        AcceptedHandler m_accepted;
    };
} // namespace studio

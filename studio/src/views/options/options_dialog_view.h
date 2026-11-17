#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>

#include "albor/window/window_handle.h"
#include "simulation/option_catalog.h"
#include "simulation/option_parameters.h"

namespace albor
{
    class Application;
    class BindingEngine;
    class ColumnLayout;
    class OptionsDialog;
    class TextEngine;
    class ThemeController;
    class UiNode;
} // namespace albor

namespace studio
{
    class OptionsPresenter;

    // the Albor dialog window that edits the simulation-agnostic .OPTIONS packages
    class AlborOptionsDialogView final
    {
    public:
        // the options the dialog hands back
        using AcceptedHandler = std::function<void(OptionParameters)>;

        // build the dialog surface parented to the shell window
        AlborOptionsDialogView(albor::Application& application, albor::WindowHandle parent);

        AlborOptionsDialogView(const AlborOptionsDialogView&) = delete;

        AlborOptionsDialogView& operator=(const AlborOptionsDialogView&) = delete;

        ~AlborOptionsDialogView();

        // open the dialog over the rows the presenter holds and hand the accepted options back
        void show(OptionsPresenter& presenter, AcceptedHandler accepted);

        // ask the application to close the dialog
        void close();

        // report whether the dialog window is currently open
        [[nodiscard]] bool is_open() const;

        // clear the borrowed facade when the application reports the window is gone
        void on_window_closed(albor::WindowHandle handle);

    private:
        // build one card per package the presenter shows
        void populate();

        // build the card of one package from the rows the presenter holds
        std::unique_ptr<albor::UiNode> build_card(OptionPackage package);

        // report an edited row to the presenter, which recomputes its selection
        void commit(OptionPackage package, std::size_t row, std::string value, bool flag);

        // hand the edited options to the caller and close
        void accept();

        // close without applying the edits
        void dismiss();

        // the application that owns the window and its engines
        albor::Application* m_app = nullptr;

        // the shell window the dialog is parented to
        albor::WindowHandle m_parent;

        // the dialog window, invalid while it is closed
        albor::WindowHandle m_handle;

        // the borrowed generated facade
        albor::OptionsDialog* m_root = nullptr;

        // the theme bindings installed over the dialog tree
        std::unique_ptr<albor::ThemeController> m_theme;

        // the binding engine the host-built cards borrow
        albor::BindingEngine* m_engine = nullptr;

        // the text engine the host-built cards measure with
        albor::TextEngine* m_text = nullptr;

        // the column the package cards fill
        albor::ColumnLayout* m_cards = nullptr;

        // the rows the cards were built from, which the edits commit against
        OptionsPresenter* m_presenter = nullptr;

        // the first editor the dialog focuses once the page has arranged
        albor::UiNode* m_first_editor = nullptr;

        // the flag a posted focus task observes, so it never outlives the view
        std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);

        // the sink the accepted options reach
        AcceptedHandler m_accepted;
    };
} // namespace studio

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <slint.h>

#include <main_window.h>

#include "../simulation/option_parameters.h"
#include "main_window_view_def.h"

namespace options_dialog_view
{
    // lives in its own translation unit because the generated main_window.h defines a SharedGlobals type that conflicts with other generated widget headers
    class OptionsDialogView
    {
    public:
        OptionsDialogView(slint::ComponentHandle<main_window::MainWindow> main_window);

        OptionsDialogView(const OptionsDialogView&) = delete;
        OptionsDialogView& operator=(const OptionsDialogView&) = delete;

        ~OptionsDialogView();

        // shows the panel seeded with the given options; the edited options are delivered through on_options_dialog_result, and on_closed runs on both accept and cancel after the panel hides so the caller can release the modal state
        void show(const OptionParameters& current, MainWindowViewDefEvents& handler, const std::function<void()>& on_closed);

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };

    // RG-documented option keys for the package at the panel index, in documentation order (Xyce Reference Guide 2.1.25)
    [[nodiscard]] const std::vector<std::string>& option_package_catalog(size_t package_index);

    // reference to the OptionParameters member that backs the package at the panel index
    [[nodiscard]] std::map<std::string, std::string>& package_options(OptionParameters& options, size_t package_index);
    [[nodiscard]] const std::map<std::string, std::string>& package_options(const OptionParameters& options, size_t package_index);

    // editor rows for one package: catalog keys in order first, then netlist keys the catalog does not document
    [[nodiscard]] std::vector<main_window::OptionRow> build_option_rows(const std::vector<std::string>& catalog, const std::map<std::string, std::string>& loaded);

    // one package map rebuilt from its editor rows: non-empty values write KEY=VALUE, empty rows keep only bare flags
    [[nodiscard]] std::map<std::string, std::string> apply_option_rows(const std::vector<main_window::OptionRow>& rows);
} // namespace options_dialog_view

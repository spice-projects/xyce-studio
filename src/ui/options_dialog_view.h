#pragma once

#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <slint.h>

#include <main_window.h>

#include "../simulation/option_catalog.h"
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

        // shows the panel seeded with the given options; only the simulation-agnostic packages are editable, the edited packages are delivered through on_options_dialog_result, and on_closed runs on both accept and cancel after the panel hides so the caller can release the modal state
        void show(const OptionParameters& current, MainWindowViewDefEvents& handler, const std::function<void()>& on_closed);

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };

    // the simulation-agnostic packages shown by the dialog, in the panel's card order
    [[nodiscard]] const std::vector<OptionPackage>& option_dialog_packages();

    // index of the value within the row's choices: 0 selects <default>, i+1 selects choices[i], -1 reports an unknown value or a free-text key
    [[nodiscard]] int choice_index_for(const OptionKeyInfo& info, const std::string& value);

    // editor rows for one package: catalog keys in order first, then netlist keys the catalog does not document, each filled with the guide's choices and default
    [[nodiscard]] std::vector<main_window::OptionRow> build_option_rows(OptionPackage package, const std::map<std::string, std::string>& loaded);

    // one package map rebuilt from its editor rows: non-empty values write KEY=VALUE, empty rows keep only bare flags
    [[nodiscard]] std::map<std::string, std::string> apply_option_rows(const std::vector<main_window::OptionRow>& rows);

    // rebuild the given packages from their editor rows onto the options shown on open; every other package passes through untouched
    [[nodiscard]] OptionParameters assemble_option_result(const OptionParameters& current, std::span<const OptionPackage> packages, const std::vector<std::vector<main_window::OptionRow>>& rows);

    // copy the current rows out of a host-owned model
    [[nodiscard]] std::vector<main_window::OptionRow> read_option_rows(const std::shared_ptr<slint::VectorModel<main_window::OptionRow>>& model);
} // namespace options_dialog_view

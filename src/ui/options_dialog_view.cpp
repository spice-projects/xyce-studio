#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <slint.h>

#include <main_window.h>

#include "../core/util.h"
#include "../simulation/option_catalog.h"
#include "../simulation/option_parameters.h"
#include "main_window_view_def.h"
#include "options_dialog_view.h"

namespace options_dialog_view
{
    namespace
    {
        // number of simulation-agnostic packages shown by the panel
        constexpr size_t DIALOG_PACKAGE_COUNT = 7;

        // one window row-model setter, bound to the window at construction
        using RowSetter = std::function<void(const std::shared_ptr<slint::Model<main_window::OptionRow>>&)>;

        // assemble one editor row from its key, current value and editing metadata
        [[nodiscard]] main_window::OptionRow make_option_row(const std::string& key, const std::string& value, bool flag, const OptionKeyInfo& info) {
            // the combobox model carries the <default> entry first
            auto choices = std::make_shared<slint::VectorModel<slint::SharedString>>();
            if (!info.choices.empty()) {
                choices->push_back(slint::SharedString("<default>"));
                for (const auto& choice : info.choices)
                    choices->push_back(slint::SharedString(choice));
            }
            return main_window::OptionRow{slint::SharedString(key), slint::SharedString(value), flag, choices, slint::SharedString(info.default_value), choice_index_for(info, value)};
        }
    } // namespace

    const std::vector<OptionPackage>& option_dialog_packages() {
        // the simulation-agnostic packages in the panel's card order
        static const std::vector<OptionPackage> PACKAGES = {OptionPackage::DEVICE, OptionPackage::LINSOL, OptionPackage::PARSER, OptionPackage::DIAGNOSTIC, OptionPackage::DIST, OptionPackage::MEASURE, OptionPackage::RESTART};

        return PACKAGES;
    }

    std::vector<main_window::OptionRow> build_option_rows(OptionPackage package, const std::map<std::string, std::string>& loaded) {
        std::vector<main_window::OptionRow> rows;
        // resolve the documented keys for the package
        const auto& catalog = option_package_catalog(package);
        rows.reserve(catalog.size() + loaded.size());
        // place every documented key first, pre-filled from the netlist when present
        for (const auto& key : catalog) {
            const auto found = loaded.find(key);
            const bool present = found != loaded.end();
            // absent keys render as an empty row while present bare flags keep the flag marker
            const std::string value = present ? found->second : "";
            rows.push_back(make_option_row(key, value, present && value.empty(), option_key_info(package, key)));
        }
        // remember which keys already have a row so netlist-only keys are not duplicated
        const std::set<std::string> placed(catalog.begin(), catalog.end());
        // append netlist keys the catalog does not document so no loaded option is hidden
        for (const auto& [key, value] : loaded)
            if (placed.count(key) == 0)
                rows.push_back(make_option_row(key, value, value.empty(), OptionKeyInfo{}));
        return rows;
    }

    int choice_index_for(const OptionKeyInfo& info, const std::string& value) {
        // keys without choices never render a combobox
        if (info.choices.empty())
            return -1;
        // an unset value selects the <default> entry
        if (value.empty())
            return 0;
        // locate the current value among the choices
        for (size_t i = 0; i < info.choices.size(); ++i)
            if (info.choices[i] == value)
                return static_cast<int>(i + 1);
        // an unknown value falls back to the text editor
        return -1;
    }

    std::map<std::string, std::string> apply_option_rows(const std::vector<main_window::OptionRow>& rows) {
        std::map<std::string, std::string> options;
        for (const auto& row : rows) {
            // keys reach the dialog uppercased from the catalog and the netlist parser
            const std::string key = to_upper(trim(std::string(row.key)));
            // values are written verbatim apart from surrounding whitespace
            const std::string value = trim(std::string(row.value));
            // a row without a key cannot form an option and is skipped
            if (key.empty())
                continue;
            // a non-empty value writes KEY=VALUE
            if (!value.empty())
                options[key] = value;
            // an empty row keeps only keys the netlist carried as bare flags
            else if (row.flag)
                options[key] = "";
        }
        return options;
    }

    std::vector<main_window::OptionRow> read_option_rows(const std::shared_ptr<slint::VectorModel<main_window::OptionRow>>& model) {
        std::vector<main_window::OptionRow> rows;
        rows.reserve(model->row_count());
        for (size_t i = 0; i < model->row_count(); ++i)
            rows.push_back(*model->row_data(i));
        return rows;
    }

    OptionParameters assemble_option_result(const OptionParameters& current, std::span<const OptionPackage> packages, const std::vector<std::vector<main_window::OptionRow>>& rows) {
        // start from the options shown on open so only the dialog's own packages are rewritten
        OptionParameters options = current;
        // fold each package's editor rows back into its map
        for (size_t i = 0; i < packages.size(); ++i)
            package_options(options, packages[i]) = apply_option_rows(rows.at(i));
        return options;
    }

    struct OptionsDialogView::Impl
    {
        // the main window handle; the panel is an inline child of the window, so all interaction goes through the window's properties and callbacks
        slint::ComponentHandle<main_window::MainWindow> window;

        // presenter notified with the edited options
        MainWindowViewDefEvents* handler = nullptr;

        // notified on both accept and cancel, after the panel is hidden; the caller releases the modal state from here
        std::function<void()> on_closed;

        // one host-owned row model per simulation-agnostic package, in the panel's card order
        std::array<std::shared_ptr<slint::VectorModel<main_window::OptionRow>>, DIALOG_PACKAGE_COUNT> models;

        // the options shown on open; accept rewrites only the dialog's own packages onto this copy
        OptionParameters m_current;

        Impl(slint::ComponentHandle<main_window::MainWindow> w) :
            window(w), m_current({}, {}, {}, {}, {}) {
            // the window row-model setters in the panel's card order
            const std::array<RowSetter, DIALOG_PACKAGE_COUNT> setters = {
                [this](const std::shared_ptr<slint::Model<main_window::OptionRow>>& rows) { window->set_options_device_rows(rows); }, [this](const std::shared_ptr<slint::Model<main_window::OptionRow>>& rows) { window->set_options_linsol_rows(rows); }, [this](const std::shared_ptr<slint::Model<main_window::OptionRow>>& rows) { window->set_options_parser_rows(rows); }, [this](const std::shared_ptr<slint::Model<main_window::OptionRow>>& rows) { window->set_options_diagnostic_rows(rows); }, [this](const std::shared_ptr<slint::Model<main_window::OptionRow>>& rows) { window->set_options_dist_rows(rows); }, [this](const std::shared_ptr<slint::Model<main_window::OptionRow>>& rows) { window->set_options_measure_rows(rows); }, [this](const std::shared_ptr<slint::Model<main_window::OptionRow>>& rows) { window->set_options_restart_rows(rows); },
            };
            // create one empty model per package and publish it to the matching window property
            for (size_t i = 0; i < DIALOG_PACKAGE_COUNT; ++i) {
                models[i] = std::make_shared<slint::VectorModel<main_window::OptionRow>>();
                setters[i](models[i]);
            }
            // wire the forwarded callbacks from the inline panel to this view
            window->on_options_accepted([this] { accept(); });
            window->on_options_dismissed([this] { dismiss(); });
            window->on_options_row_edited([this](int package_index, int row_index, main_window::OptionRow row) { edit(package_index, row_index, row); });
        }

        void seed(size_t position, const OptionParameters& current) {
            // resolve the package for the panel position
            const auto package = option_dialog_packages().at(position);
            // build the rows from the package catalog and the current option values
            auto rows = build_option_rows(package, package_options(current, package));
            // replace the model contents in place so the window property keeps the same model
            models[position]->set_vector(std::move(rows));
        }

        void edit(int package_index, int row_index, const main_window::OptionRow& row) {
            // map the OptionPackage ordinal onto the panel's model position
            const auto& packages = option_dialog_packages();
            const auto found = std::find(packages.begin(), packages.end(), static_cast<OptionPackage>(package_index));
            // ignore edits for packages the dialog does not show
            if (found == packages.end())
                return;
            const auto position = static_cast<size_t>(std::distance(packages.begin(), found));
            // ignore rows outside the current model
            if (row_index < 0 || static_cast<size_t>(row_index) >= models[position]->row_count())
                return;
            // refresh the combobox selection for the edited value
            auto updated = row;
            updated.choice_index = choice_index_for(option_key_info(*found, std::string(row.key)), std::string(row.value));
            // commit the edited row into the host-owned model
            models[position]->set_row_data(static_cast<size_t>(row_index), updated);
        }

        void accept() {
            // read the edited rows out of the host-owned models
            std::vector<std::vector<main_window::OptionRow>> rows;
            rows.reserve(DIALOG_PACKAGE_COUNT);
            for (const auto& model : models)
                rows.push_back(read_option_rows(model));
            // rebuild the dialog's packages onto the options shown on open
            const auto options = assemble_option_result(m_current, option_dialog_packages(), rows);
            // hide the panel before delivering the result
            window->set_options_visible(false);
            // release the modal state held by the caller
            if (on_closed)
                on_closed();
            // deliver the edited options to the presenter
            if (handler != nullptr)
                handler->on_options_dialog_result(options);
        }

        void dismiss() {
            // hide the panel
            window->set_options_visible(false);
            // release the modal state held by the caller
            if (on_closed)
                on_closed();
        }
    };

    OptionsDialogView::OptionsDialogView(slint::ComponentHandle<main_window::MainWindow> main_window) :
        m_impl(std::make_unique<Impl>(main_window)) {}

    OptionsDialogView::~OptionsDialogView() = default;

    void OptionsDialogView::show(const OptionParameters& current, MainWindowViewDefEvents& handler, const std::function<void()>& on_closed) {
        // remember the result destination for the panel lifetime
        m_impl->handler = &handler;
        // remember the close notification for this show
        m_impl->on_closed = on_closed;
        // remember the options shown on open so accept can pass the other packages through untouched
        m_impl->m_current = current;
        // rebuild every package model from its catalog and the current options
        for (size_t i = 0; i < DIALOG_PACKAGE_COUNT; ++i)
            m_impl->seed(i, current);
        // show the inline panel
        m_impl->window->set_options_visible(true);
    }
} // namespace options_dialog_view

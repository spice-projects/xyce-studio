#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "config/plugin_config.h"
#include "dsp/fft.h"
#include "expression/expression.h"
#include "simulation/simulation_config.h"
#include "ui/options_dialog_view.h"

namespace
{
    // recording event double that captures the delivered options result
    class RecordingEvents : public MainWindowViewDefEvents
    {
    public:
        void on_open_xyce_file(const std::filesystem::path&) override {}

        void on_save_netlist() override {}

        void on_show_netlist() override {}

        void on_show_charts() override {}

        void on_show_simulation_output() override {}

        void on_close_simulation_output() override {}

        void on_run_simulation() override {}

        void on_cancel_simulation() override {}

        void on_configure_simulation() override {}

        void on_edit_options() override {}

        void on_configure_plugin() override {}

        void on_plugin_config_dialog_result(const PluginConfig&) override {}

        void on_simulation_parameters_dialog_result(const SimulationConfig&) override {}

        void on_options_dialog_result(const OptionParameters& options) override { last_options = options; }

        void on_fft_dialog_result(std::vector<AnyExpression*>, const fft::FftParameters&) override {}

        void on_select_plot_tab(int) override {}

        void on_close_plot_tab(int) override {}

        void on_chart_calculate_fft(size_t) override {}

        void on_chart_step_tool(size_t) override {}

        void on_chart_new_window(size_t) override {}

        void on_chart_moved(size_t, size_t) override {}

        void on_simulation_finished(int, bool) override {}

        void on_simulation_stdout(const std::string&) override {}

        void on_simulation_stderr(const std::string&) override {}

        void on_netlist_editor_modified() override {}

        void on_extract_schematic_netlist() override {}

        std::optional<OptionParameters> last_options;
    };
} // namespace

TEST(OptionsDialogViewChecks, dialog_shows_only_the_simulation_agnostic_packages) {
    // arrange / act
    const auto& packages = options_dialog_view::option_dialog_packages();
    const std::vector<OptionPackage> expected = {OptionPackage::DEVICE, OptionPackage::LINSOL, OptionPackage::PARSER, OptionPackage::DIAGNOSTIC, OptionPackage::DIST, OptionPackage::MEASURE, OptionPackage::RESTART};
    // assert — exactly the seven simulation-agnostic packages in the panel's card order
    EXPECT_EQ(packages, expected);
}

TEST(OptionsDialogViewChecks, build_option_rows_places_catalog_keys_then_netlist_only_keys) {
    // arrange — a netlist carrying one catalog key plus one unknown key for the TIMEINT package
    const std::map<std::string, std::string> loaded = {{"RELTOL", "1e-4"}, {"EXTRA", "5"}};
    // act — build the editor rows
    const auto rows = options_dialog_view::build_option_rows(OptionPackage::TIMEINT, loaded);
    // assert — catalog order first with the loaded value, then the netlist-only key
    ASSERT_EQ(rows.size(), option_package_catalog(OptionPackage::TIMEINT).size() + 1);
    EXPECT_EQ(std::string(rows[0].key), "METHOD");
    EXPECT_EQ(std::string(rows[1].key), "RELTOL");
    EXPECT_EQ(std::string(rows[1].value), "1e-4");
    EXPECT_FALSE(rows[1].flag);
    EXPECT_EQ(std::string(rows[2].key), "ABSTOL");
    EXPECT_EQ(std::string(rows[2].value), "");
    EXPECT_FALSE(rows[2].flag);
    EXPECT_EQ(std::string(rows.back().key), "EXTRA");
    EXPECT_EQ(std::string(rows.back().value), "5");
    EXPECT_FALSE(rows.back().flag);
}

TEST(OptionsDialogViewChecks, build_option_rows_marks_bare_flags) {
    // arrange — a catalog key the netlist carries as a bare flag without a value
    const std::map<std::string, std::string> loaded = {{"FFTOUT", ""}};
    // act — build the editor rows
    const auto rows = options_dialog_view::build_option_rows(OptionPackage::FFT, loaded);
    // assert — the FFTOUT row keeps the empty value and carries the flag marker
    ASSERT_EQ(rows.size(), option_package_catalog(OptionPackage::FFT).size());
    EXPECT_EQ(std::string(rows[1].key), "FFTOUT");
    EXPECT_EQ(std::string(rows[1].value), "");
    EXPECT_TRUE(rows[1].flag);
}

TEST(OptionsDialogViewChecks, build_option_rows_fills_choices_and_defaults) {
    // arrange — a netlist carrying a closed-choice key set to one of its values
    const std::map<std::string, std::string> loaded = {{"METHOD", "trap"}};
    // act — build the editor rows
    const auto rows = options_dialog_view::build_option_rows(OptionPackage::TIMEINT, loaded);
    // assert — the choice row carries the <default> model entry and the selected index
    ASSERT_EQ(rows.size(), option_package_catalog(OptionPackage::TIMEINT).size());
    ASSERT_EQ(rows[0].choices->row_count(), 5u);
    EXPECT_EQ(std::string(*rows[0].choices->row_data(0)), "<default>");
    EXPECT_EQ(std::string(*rows[0].choices->row_data(1)), "trap");
    EXPECT_EQ(std::string(*rows[0].choices->row_data(4)), "8");
    EXPECT_EQ(rows[0].choice_index, 1);
    // assert — the free-text row carries the reference default and no choices
    EXPECT_EQ(rows[1].choices->row_count(), 0u);
    EXPECT_EQ(std::string(rows[1].default_value), "1.0E-03");
    EXPECT_EQ(rows[1].choice_index, -1);
}

TEST(OptionsDialogViewChecks, choice_index_for_selects_default_choices_and_fallbacks) {
    // arrange — a closed-choice key and a free-text key
    const OptionKeyInfo choice = {{"trap", "7", "gear", "8"}, "trap"};
    const OptionKeyInfo text = {{}, "1.0E-03"};
    // act / assert — the index selects <default>, a choice, or the text fallback
    EXPECT_EQ(options_dialog_view::choice_index_for(choice, ""), 0);
    EXPECT_EQ(options_dialog_view::choice_index_for(choice, "trap"), 1);
    EXPECT_EQ(options_dialog_view::choice_index_for(choice, "8"), 4);
    EXPECT_EQ(options_dialog_view::choice_index_for(choice, "bogus"), -1);
    EXPECT_EQ(options_dialog_view::choice_index_for(text, "1e-3"), -1);
}

TEST(OptionsDialogViewChecks, apply_option_rows_writes_values_and_keeps_flags) {
    // arrange — one valued row, one empty row without a flag, one empty flag row
    const std::vector<main_window::OptionRow> rows = {
        main_window::OptionRow{slint::SharedString("RELTOL"), slint::SharedString("1e-3"), false, {}, "", -1},
        main_window::OptionRow{slint::SharedString("ABSTOL"), slint::SharedString(""), false, {}, "", -1},
        main_window::OptionRow{slint::SharedString("FFTOUT"), slint::SharedString(""), true, {}, "", -1},
    };
    // act — fold the rows back into one package map
    const auto options = options_dialog_view::apply_option_rows(rows);
    // assert — the valued row is written, the empty non-flag row is omitted, the flag row stays
    ASSERT_EQ(options.size(), 2u);
    EXPECT_EQ(options.at("RELTOL"), "1e-3");
    EXPECT_EQ(options.count("ABSTOL"), 0u);
    EXPECT_TRUE(options.contains("FFTOUT"));
    EXPECT_EQ(options.at("FFTOUT"), "");
}

TEST(OptionsDialogViewChecks, apply_option_rows_trims_values_and_uppercases_keys) {
    // arrange — a row with surrounding whitespace in the key and the value
    const std::vector<main_window::OptionRow> rows = {
        main_window::OptionRow{slint::SharedString(" reltol "), slint::SharedString(" 1e-3 "), false, {}, "", -1},
    };
    // act — fold the rows back into one package map
    const auto options = options_dialog_view::apply_option_rows(rows);
    // assert — the key is normalized and the value is written without padding
    ASSERT_EQ(options.size(), 1u);
    EXPECT_EQ(options.at("RELTOL"), "1e-3");
}

TEST(OptionsDialogViewChecks, read_option_rows_copies_every_row_out_of_the_model) {
    // arrange — a host-owned model with two rows
    auto model = std::make_shared<slint::VectorModel<main_window::OptionRow>>();
    model->push_back(main_window::OptionRow{slint::SharedString("GMIN"), slint::SharedString("1e-12"), false, {}, "", -1});
    model->push_back(main_window::OptionRow{slint::SharedString("FFTOUT"), slint::SharedString(""), true, {}, "", -1});
    // act
    const auto rows = options_dialog_view::read_option_rows(model);
    // assert — both rows are copied with their flag markers
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(std::string(rows[0].key), "GMIN");
    EXPECT_EQ(std::string(rows[0].value), "1e-12");
    EXPECT_FALSE(rows[0].flag);
    EXPECT_EQ(std::string(rows[1].key), "FFTOUT");
    EXPECT_TRUE(rows[1].flag);
}

TEST(OptionsDialogViewChecks, assemble_option_result_rewrites_only_the_dialog_packages) {
    // arrange — options carrying a shown package plus two packages the dialog does not edit
    OptionParameters current({}, {}, {}, {}, {});
    current.device["GMIN"] = "1e-11";
    current.timeint["RELTOL"] = "1e-4";
    current.samples["NUMSAMPLES"] = "50";
    // act — the device rows edit GMIN, clear ABSTOL and add an unknown key
    const std::vector<std::vector<main_window::OptionRow>> rows = {
        {main_window::OptionRow{slint::SharedString("GMIN"), slint::SharedString("2e-11"), false, {}, "", -1}, main_window::OptionRow{slint::SharedString("ABSTOL"), slint::SharedString(""), false, {}, "", -1}, main_window::OptionRow{slint::SharedString("EXTRA"), slint::SharedString("7"), false, {}, "", -1}}, {main_window::OptionRow{slint::SharedString("TYPE"), slint::SharedString(""), false, {}, "", -1}}, {main_window::OptionRow{slint::SharedString("SCALE"), slint::SharedString(""), false, {}, "", -1}}, {main_window::OptionRow{slint::SharedString("GMIN"), slint::SharedString(""), false, {}, "", -1}}, {main_window::OptionRow{slint::SharedString("STRATEGY"), slint::SharedString(""), false, {}, "", -1}}, {main_window::OptionRow{slint::SharedString("MEASDGT"), slint::SharedString(""), false, {}, "", -1}}, {main_window::OptionRow{slint::SharedString("PACK"), slint::SharedString(""), false, {}, "", -1}},
    };
    const auto result = options_dialog_view::assemble_option_result(current, options_dialog_view::option_dialog_packages(), rows);
    // assert — the edited device package holds exactly the valued row
    ASSERT_EQ(result.device.size(), 2u);
    EXPECT_EQ(result.device.at("GMIN"), "2e-11");
    EXPECT_EQ(result.device.at("EXTRA"), "7");
    // assert — the packages outside the dialog pass through untouched
    EXPECT_EQ(result.timeint.at("RELTOL"), "1e-4");
    EXPECT_EQ(result.samples.at("NUMSAMPLES"), "50");
    // assert — empty rows dropped their keys from the dialog packages
    EXPECT_EQ(result.linsol.count("TYPE"), 0u);
    EXPECT_EQ(result.parser.count("SCALE"), 0u);
    EXPECT_EQ(result.restart.count("PACK"), 0u);
}

TEST(OptionsDialogViewChecks, assemble_option_result_rejects_mismatched_row_lists) {
    // arrange — one package but no row lists
    const OptionParameters current({}, {}, {}, {}, {});
    // act / assert — the missing row list is rejected
    ASSERT_THROW(static_cast<void>(options_dialog_view::assemble_option_result(current, options_dialog_view::option_dialog_packages(), {})), std::out_of_range);
}

TEST(OptionsDialogViewChecks, constructor_publishes_an_empty_model_per_package) {
    // arrange / act
    auto window = main_window::MainWindow::create();
    options_dialog_view::OptionsDialogView dialog(window);
    // assert — one empty model per simulation-agnostic package
    EXPECT_EQ(window->get_options_device_rows()->row_count(), 0u);
    EXPECT_EQ(window->get_options_linsol_rows()->row_count(), 0u);
    EXPECT_EQ(window->get_options_parser_rows()->row_count(), 0u);
    EXPECT_EQ(window->get_options_diagnostic_rows()->row_count(), 0u);
    EXPECT_EQ(window->get_options_dist_rows()->row_count(), 0u);
    EXPECT_EQ(window->get_options_measure_rows()->row_count(), 0u);
    EXPECT_EQ(window->get_options_restart_rows()->row_count(), 0u);
}

TEST(OptionsDialogViewChecks, show_seeds_models_and_marks_the_panel_visible) {
    // arrange — options carrying a shown key plus packages the dialog does not edit
    OptionParameters options({}, {}, {}, {}, {});
    options.device["GMIN"] = "1e-11";
    options.timeint["RELTOL"] = "1e-4";
    options.samples["NUMSAMPLES"] = "50";
    RecordingEvents events;
    int closed = 0;
    auto window = main_window::MainWindow::create();
    options_dialog_view::OptionsDialogView dialog(window);
    // act
    dialog.show(options, events, [&closed] { closed++; });
    // assert — the panel is visible and the close callback has not run
    EXPECT_TRUE(window->get_options_visible());
    EXPECT_EQ(closed, 0);
    // assert — the device model carries one row per catalog key with GMIN pre-filled
    const auto rows = window->get_options_device_rows();
    ASSERT_EQ(rows->row_count(), option_package_catalog(OptionPackage::DEVICE).size());
    bool found = false;
    for (size_t i = 0; i < rows->row_count(); ++i) {
        const auto row = rows->row_data(i);
        if (row && std::string(row->key) == "GMIN") {
            found = true;
            EXPECT_EQ(std::string(row->value), "1e-11");
            EXPECT_FALSE(row->flag);
        }
    }
    EXPECT_TRUE(found);
}

TEST(OptionsDialogViewChecks, row_edit_commits_into_the_model_and_ignores_unknown_targets) {
    // arrange — a shown package seeded with one value
    OptionParameters options({}, {}, {}, {}, {});
    options.device["GMIN"] = "1e-11";
    RecordingEvents events;
    auto window = main_window::MainWindow::create();
    options_dialog_view::OptionsDialogView dialog(window);
    dialog.show(options, events, [] {});
    // act — edit the GMIN row of the device package
    size_t gmin_index = 0;
    const auto rows = window->get_options_device_rows();
    for (size_t i = 0; i < rows->row_count(); ++i)
        if (const auto row = rows->row_data(i); row && std::string(row->key) == "GMIN")
            gmin_index = i;
    window->invoke_options_row_edited(static_cast<int>(OptionPackage::DEVICE), static_cast<int>(gmin_index), main_window::OptionRow{slint::SharedString("GMIN"), slint::SharedString("2e-11"), false, {}, "", -1});
    // act — an unknown package and an out-of-range row are ignored
    window->invoke_options_row_edited(99, 0, main_window::OptionRow{slint::SharedString("KEY"), slint::SharedString("1"), false, {}, "", -1});
    window->invoke_options_row_edited(static_cast<int>(OptionPackage::DEVICE), -1, main_window::OptionRow{slint::SharedString("KEY"), slint::SharedString("1"), false, {}, "", -1});
    window->invoke_options_row_edited(static_cast<int>(OptionPackage::DEVICE), 999, main_window::OptionRow{slint::SharedString("KEY"), slint::SharedString("1"), false, {}, "", -1});
    // assert — only the GMIN edit landed
    const auto updated = rows->row_data(gmin_index);
    ASSERT_TRUE(updated.has_value());
    EXPECT_EQ(std::string(updated->value), "2e-11");
    EXPECT_EQ(rows->row_data(999), std::nullopt);
}

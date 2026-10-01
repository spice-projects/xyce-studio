#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "simulation/option_catalog.h"
#include "ui/simulation_parameters_dialog_view.h"

TEST(SimulationParametersDialogViewChecks, copy_constructor_is_deleted) {
    // arrange / act
    using ViewT = simulation_parameters_dialog_view::SimulationParametersDialogView;
    constexpr bool is_copy_constructible = std::is_copy_constructible_v<ViewT>;
    constexpr bool is_copy_assignable = std::is_copy_assignable_v<ViewT>;
    // assert
    EXPECT_FALSE(is_copy_constructible);
    EXPECT_FALSE(is_copy_assignable);
}

TEST(SimulationParametersDialogViewChecks, constructor_takes_main_window_handle) {
    // arrange / act
    using ViewT = simulation_parameters_dialog_view::SimulationParametersDialogView;
    // assert
    EXPECT_TRUE((std::is_constructible_v<ViewT, slint::ComponentHandle<main_window::MainWindow>>));
}

TEST(SimulationParametersDialogViewChecks, destructor_is_user_declared) {
    // arrange / act
    using ViewT = simulation_parameters_dialog_view::SimulationParametersDialogView;
    // assert
    EXPECT_FALSE(std::is_trivially_destructible_v<ViewT>);
}

TEST(SimulationParametersDialogViewChecks, show_method_exists) {
    // arrange / act
    using ViewT = simulation_parameters_dialog_view::SimulationParametersDialogView;
    // assert
    EXPECT_TRUE((std::is_member_function_pointer_v<decltype(&ViewT::show)>));
}

TEST(SimulationParametersDialogViewChecks, show_method_signature_matches_interface) {
    // arrange / act
    using ViewT = simulation_parameters_dialog_view::SimulationParametersDialogView;
    // assert
    EXPECT_TRUE((std::is_invocable_v<decltype(&ViewT::show), ViewT*, const SimulationConfig&, MainWindowViewDefEvents&, const std::function<void()>&>));
}

namespace
{
    // recording event double that captures the delivered configuration
    class RecordingConfigEvents : public MainWindowViewDefEvents
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

        void on_simulation_parameters_dialog_result(const SimulationConfig& config) override { last_config = config; }

        void on_options_dialog_result(const OptionParameters&) override {}

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

        std::optional<SimulationConfig> last_config;
    };
} // namespace

TEST(SimulationParametersDialogViewChecks, show_seeds_the_analysis_specific_option_models) {
    // arrange — a configuration carrying values for a shown and an unshown package
    SimulationConfig config = SimulationConfig::from_xyce_directives({});
    config.analysis_type = "OP";
    config.options.timeint["RELTOL"] = "1e-4";
    config.options.timeint["METHOD"] = "trap";
    config.options.fft["FFTOUT"] = "";
    config.options.device["GMIN"] = "1e-11";
    RecordingConfigEvents events;
    auto window = main_window::MainWindow::create();
    simulation_parameters_dialog_view::SimulationParametersDialogView dialog(window);
    // act
    dialog.show(config, events, [] {});
    // assert — the timeint model carries the value and the fft model carries the flag row
    const auto timeint_rows = window->get_timeint_rows();
    ASSERT_EQ(timeint_rows->row_count(), option_package_catalog(OptionPackage::TIMEINT).size());
    bool found_relto = false;
    bool found_method = false;
    for (size_t i = 0; i < timeint_rows->row_count(); ++i)
        if (const auto row = timeint_rows->row_data(i); row && std::string(row->key) == "RELTOL") {
            found_relto = true;
            EXPECT_EQ(std::string(row->value), "1e-4");
        }
        else if (row && std::string(row->key) == "METHOD") {
            found_method = true;
            // assert — the closed-choice row carries the combobox model and the selection index
            EXPECT_EQ(row->choices->row_count(), 5u);
            EXPECT_EQ(std::string(*row->choices->row_data(0)), "<default>");
            EXPECT_EQ(row->choice_index, 1);
        }
    EXPECT_TRUE(found_relto);
    EXPECT_TRUE(found_method);
    const auto fft_rows = window->get_fft_rows();
    ASSERT_EQ(fft_rows->row_count(), option_package_catalog(OptionPackage::FFT).size());
    bool found_fftout = false;
    for (size_t i = 0; i < fft_rows->row_count(); ++i)
        if (const auto row = fft_rows->row_data(i); row && std::string(row->key) == "FFTOUT") {
            found_fftout = true;
            EXPECT_TRUE(row->flag);
        }
    EXPECT_TRUE(found_fftout);
    // assert — the package the dialog does not edit never reaches the window models
    EXPECT_EQ(window->get_options_device_rows()->row_count(), 0u);
}

TEST(SimulationParametersDialogViewChecks, row_edit_commits_into_the_model_and_ignores_unknown_targets) {
    // arrange — a configuration carrying one seeded value
    SimulationConfig config = SimulationConfig::from_xyce_directives({});
    config.analysis_type = "OP";
    config.options.timeint["RELTOL"] = "1e-4";
    RecordingConfigEvents events;
    auto window = main_window::MainWindow::create();
    simulation_parameters_dialog_view::SimulationParametersDialogView dialog(window);
    dialog.show(config, events, [] {});
    // act — edit the RELTOL row of the timeint package
    size_t reltol_index = 0;
    const auto rows = window->get_timeint_rows();
    for (size_t i = 0; i < rows->row_count(); ++i)
        if (const auto row = rows->row_data(i); row && std::string(row->key) == "RELTOL")
            reltol_index = i;
    window->invoke_sim_options_row_edited(static_cast<int>(OptionPackage::TIMEINT), static_cast<int>(reltol_index), main_window::OptionRow{slint::SharedString("RELTOL"), slint::SharedString("2e-4"), false, {}, "", -1});
    // act — edit the METHOD choice row to another documented value
    size_t method_index = 0;
    for (size_t i = 0; i < rows->row_count(); ++i)
        if (const auto row = rows->row_data(i); row && std::string(row->key) == "METHOD")
            method_index = i;
    window->invoke_sim_options_row_edited(static_cast<int>(OptionPackage::TIMEINT), static_cast<int>(method_index), main_window::OptionRow{slint::SharedString("METHOD"), slint::SharedString("gear"), false, {}, "", -1});
    // act — an unknown package and an out-of-range row are ignored
    window->invoke_sim_options_row_edited(99, 0, main_window::OptionRow{slint::SharedString("KEY"), slint::SharedString("1"), false, {}, "", -1});
    window->invoke_sim_options_row_edited(static_cast<int>(OptionPackage::TIMEINT), -1, main_window::OptionRow{slint::SharedString("KEY"), slint::SharedString("1"), false, {}, "", -1});
    window->invoke_sim_options_row_edited(static_cast<int>(OptionPackage::TIMEINT), 999, main_window::OptionRow{slint::SharedString("KEY"), slint::SharedString("1"), false, {}, "", -1});
    // assert — only the RELTOL edit landed
    const auto updated = rows->row_data(reltol_index);
    ASSERT_TRUE(updated.has_value());
    EXPECT_EQ(std::string(updated->value), "2e-4");
    EXPECT_EQ(rows->row_data(999), std::nullopt);
    // assert — the METHOD edit landed and the combobox selection followed the value
    const auto method_row = rows->row_data(method_index);
    ASSERT_TRUE(method_row.has_value());
    EXPECT_EQ(std::string(method_row->value), "gear");
    EXPECT_EQ(method_row->choice_index, 3);
}

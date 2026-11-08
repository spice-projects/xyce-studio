#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "presenters/simulation_parameters_presenter.h"
#include "simulation/ac_simulation_parameters.h"
#include "simulation/dc_simulation_parameters.h"
#include "simulation/transient_simulation_parameters.h"
#include "views/simulation_parameters/simulation_parameters_dialog_view_def.h"

namespace
{
    // the view double records what the presenter asks the surface to do
    class FakeView final : public studio::SimulationParametersDialogView
    {
    public:
        void show(const SimulationConfig& current, studio::SimulationParametersDialogEvents& new_events) override {
            // the seed the presenter handed the dialog
            shown = current;
            // the presenter that receives the simulated user actions
            events = &new_events;
            show_count++;
        }

        void close() override { close_count++; }

        void show_error(std::string_view message) override { errors.emplace_back(message); }

        // the configuration the presenter seeded into the view
        SimulationConfig shown = SimulationConfig::from_xyce_directives({});

        // the presenter that receives the simulated user actions
        studio::SimulationParametersDialogEvents* events = nullptr;

        // how many times each surface action ran
        int show_count = 0;
        int close_count = 0;

        // every validation message the presenter produced
        std::vector<std::string> errors;
    };

    // build a configuration carrying one analysis over the defaults of an empty netlist
    [[nodiscard]] SimulationConfig configuration_with(SimulationAnalysis analysis, std::string analysis_type) {
        // the empty configuration carries the options and steps every run starts from
        SimulationConfig config = SimulationConfig::from_xyce_directives({});
        // the variant names the analysis the dialog edits
        config.analysis = std::move(analysis);
        // the stored spelling is what the run reports
        config.analysis_type = std::move(analysis_type);
        return config;
    }
} // namespace

TEST(StudioSimulationParametersPresenterChecks, show_seeds_the_view_with_the_current_configuration) {
    // arrange: a transient configuration and an open request
    FakeView view;
    studio::SimulationParametersPresenter presenter;
    const SimulationConfig current = configuration_with(TransientSimulationParameters("1m", "10m", "", "", "", {}, std::nullopt, {}, {}, {}, std::nullopt, std::nullopt), "TRAN");
    // act
    presenter.show(view, current, {});
    // assert: the view received the configuration and no action ran yet
    ASSERT_EQ(view.show_count, 1);
    ASSERT_NE(view.events, nullptr);
    EXPECT_EQ(view.shown.analysis_type, "TRAN");
    EXPECT_EQ(view.close_count, 0);
}

TEST(StudioSimulationParametersPresenterChecks, a_valid_transient_configuration_closes_and_is_delivered) {
    // arrange: an open dialog and a configuration carrying both required times
    FakeView view;
    studio::SimulationParametersPresenter presenter;
    bool accepted = false;
    SimulationConfig delivered = SimulationConfig::from_xyce_directives({});
    presenter.show(view, SimulationConfig::from_xyce_directives({}), [&accepted, &delivered](const SimulationConfig& config) {
        // the acceptance is observable through the callback
        accepted = true;
        delivered = config;
    });
    ASSERT_NE(view.events, nullptr);
    // act
    view.events->on_accept(configuration_with(TransientSimulationParameters("1m", "10m", "", "", "", {}, std::nullopt, {}, {}, {}, std::nullopt, std::nullopt), "TRAN"));
    // assert: the dialog closed and the callback received the analysis
    EXPECT_EQ(view.close_count, 1);
    EXPECT_TRUE(accepted);
    EXPECT_EQ(delivered.analysis_type, "TRAN");
    ASSERT_TRUE(std::holds_alternative<TransientSimulationParameters>(delivered.analysis));
    EXPECT_EQ(std::get<TransientSimulationParameters>(delivered.analysis).initial_step_value, "1m");
    EXPECT_TRUE(view.errors.empty());
}

TEST(StudioSimulationParametersPresenterChecks, a_transient_without_a_final_time_keeps_the_dialog_open) {
    // arrange: an open dialog and a configuration missing the final time
    FakeView view;
    studio::SimulationParametersPresenter presenter;
    int accepted = 0;
    presenter.show(view, SimulationConfig::from_xyce_directives({}), [&accepted](const SimulationConfig&) { accepted++; });
    ASSERT_NE(view.events, nullptr);
    // act
    view.events->on_accept(configuration_with(TransientSimulationParameters("1m", "   ", "", "", "", {}, std::nullopt, {}, {}, {}, std::nullopt, std::nullopt), "TRAN"));
    // assert: the presenter reports the required value and keeps the dialog open
    ASSERT_EQ(view.errors.size(), 1);
    EXPECT_EQ(view.errors[0], "Final time is required");
    EXPECT_EQ(view.close_count, 0);
    EXPECT_EQ(accepted, 0);
}

TEST(StudioSimulationParametersPresenterChecks, a_transient_without_an_initial_step_keeps_the_dialog_open) {
    // arrange: an open dialog and a configuration missing the initial step
    FakeView view;
    studio::SimulationParametersPresenter presenter;
    presenter.show(view, SimulationConfig::from_xyce_directives({}), {});
    ASSERT_NE(view.events, nullptr);
    // act
    view.events->on_accept(configuration_with(TransientSimulationParameters("", "10m", "", "", "", {}, std::nullopt, {}, {}, {}, std::nullopt, std::nullopt), "TRAN"));
    // assert: the presenter reports the required value and keeps the dialog open
    ASSERT_EQ(view.errors.size(), 1);
    EXPECT_EQ(view.errors[0], "Initial step is required");
    EXPECT_EQ(view.close_count, 0);
}

TEST(StudioSimulationParametersPresenterChecks, a_dc_sweep_without_a_variable_reports_the_core_error) {
    // arrange: an open dialog and a linear DC sweep with no swept variable
    FakeView view;
    studio::SimulationParametersPresenter presenter;
    presenter.show(view, SimulationConfig::from_xyce_directives({}), {});
    ASSERT_NE(view.events, nullptr);
    // act
    view.events->on_accept(configuration_with(DCSimulationParameters("LIN", {DcSweep{.variable = "", .start = "0", .stop = "5", .step = "1"}}, "", std::nullopt, {}, std::nullopt, std::nullopt), "DC"));
    // assert: the core validation message is the one the dialog shows
    ASSERT_EQ(view.errors.size(), 1);
    EXPECT_EQ(view.errors[0], "DC LIN sweep: sweep variable is required");
    EXPECT_EQ(view.close_count, 0);
}

TEST(StudioSimulationParametersPresenterChecks, a_valid_dc_sweep_closes_and_is_delivered) {
    // arrange: an open dialog and a complete linear DC sweep
    FakeView view;
    studio::SimulationParametersPresenter presenter;
    int accepted = 0;
    presenter.show(view, SimulationConfig::from_xyce_directives({}), [&accepted](const SimulationConfig&) { accepted++; });
    ASSERT_NE(view.events, nullptr);
    // act
    view.events->on_accept(configuration_with(DCSimulationParameters("LIN", {DcSweep{.variable = "V1", .start = "0", .stop = "5", .step = "1"}}, "", std::nullopt, {}, std::nullopt, std::nullopt), "DC"));
    // assert
    EXPECT_EQ(view.close_count, 1);
    EXPECT_EQ(accepted, 1);
    EXPECT_TRUE(view.errors.empty());
}

TEST(StudioSimulationParametersPresenterChecks, a_dc_data_sweep_without_a_table_reports_the_error) {
    // arrange: an open dialog and a DATA sweep with no table name
    FakeView view;
    studio::SimulationParametersPresenter presenter;
    presenter.show(view, SimulationConfig::from_xyce_directives({}), {});
    ASSERT_NE(view.events, nullptr);
    // act
    view.events->on_accept(configuration_with(DCSimulationParameters("DATA", {}, "", std::nullopt, {}, std::nullopt, std::nullopt), "DC"));
    // assert
    ASSERT_EQ(view.errors.size(), 1);
    EXPECT_EQ(view.errors[0], "Data table is required for a DATA sweep");
    EXPECT_EQ(view.close_count, 0);
}

TEST(StudioSimulationParametersPresenterChecks, an_ac_sweep_without_points_reports_the_error) {
    // arrange: an open dialog and a sweep missing its point count
    FakeView view;
    studio::SimulationParametersPresenter presenter;
    presenter.show(view, SimulationConfig::from_xyce_directives({}), {});
    ASSERT_NE(view.events, nullptr);
    // act
    view.events->on_accept(configuration_with(AcSimulationParameters("DEC", "", "1", "1e6", "", std::nullopt, {}, std::nullopt), "AC"));
    // assert
    ASSERT_EQ(view.errors.size(), 1);
    EXPECT_EQ(view.errors[0], "Points is required");
    EXPECT_EQ(view.close_count, 0);
}

TEST(StudioSimulationParametersPresenterChecks, a_valid_ac_data_sweep_closes_and_is_delivered) {
    // arrange: an open dialog and a DATA sweep naming its table
    FakeView view;
    studio::SimulationParametersPresenter presenter;
    int accepted = 0;
    presenter.show(view, SimulationConfig::from_xyce_directives({}), [&accepted](const SimulationConfig&) { accepted++; });
    ASSERT_NE(view.events, nullptr);
    // act
    view.events->on_accept(configuration_with(AcSimulationParameters("DATA", "", "", "", "sweep_table", std::nullopt, {}, std::nullopt), "AC"));
    // assert
    EXPECT_EQ(view.close_count, 1);
    EXPECT_EQ(accepted, 1);
    EXPECT_TRUE(view.errors.empty());
}

TEST(StudioSimulationParametersPresenterChecks, a_configuration_without_an_analysis_is_refused) {
    // arrange: an open dialog and a configuration that names no analysis
    FakeView view;
    studio::SimulationParametersPresenter presenter;
    presenter.show(view, SimulationConfig::from_xyce_directives({}), {});
    ASSERT_NE(view.events, nullptr);
    // act
    view.events->on_accept(SimulationConfig::from_xyce_directives({}));
    // assert
    ASSERT_EQ(view.errors.size(), 1);
    EXPECT_EQ(view.errors[0], "Select an analysis");
    EXPECT_EQ(view.close_count, 0);
}

TEST(StudioSimulationParametersPresenterChecks, cancel_closes_without_delivering) {
    // arrange: an open dialog with a callback
    FakeView view;
    studio::SimulationParametersPresenter presenter;
    int accepted = 0;
    presenter.show(view, SimulationConfig::from_xyce_directives({}), [&accepted](const SimulationConfig&) { accepted++; });
    ASSERT_NE(view.events, nullptr);
    // act
    view.events->on_cancel();
    // assert
    EXPECT_EQ(view.close_count, 1);
    EXPECT_EQ(accepted, 0);
}

TEST(StudioSimulationParametersPresenterChecks, show_is_ignored_while_the_dialog_is_open) {
    // arrange: two open requests
    FakeView view;
    studio::SimulationParametersPresenter presenter;
    int first_calls = 0;
    int second_calls = 0;
    // act: the second request arrives while the first dialog is still open
    presenter.show(view, SimulationConfig::from_xyce_directives({}), [&first_calls](const SimulationConfig&) { first_calls++; });
    presenter.show(view, SimulationConfig::from_xyce_directives({}), [&second_calls](const SimulationConfig&) { second_calls++; });
    // assert: only the first request reached the view and kept its callback
    EXPECT_EQ(view.show_count, 1);
    ASSERT_NE(view.events, nullptr);
    // act: cancel the open dialog
    view.events->on_cancel();
    // assert: the dialog closed and the ignored callback never ran
    EXPECT_EQ(view.close_count, 1);
    EXPECT_EQ(first_calls, 0);
    EXPECT_EQ(second_calls, 0);
}

TEST(StudioSimulationParametersPresenterChecks, stale_accept_cancel_and_closed_are_ignored) {
    // arrange: a presenter with no open dialog
    FakeView view;
    studio::SimulationParametersPresenter presenter;
    // act: deliver every stale notification a late window may raise
    presenter.on_dialog_closed();
    presenter.on_accept(configuration_with(TransientSimulationParameters("1m", "10m", "", "", "", {}, std::nullopt, {}, {}, {}, std::nullopt, std::nullopt), "TRAN"));
    presenter.on_cancel();
    // assert: no surface action ran and no validation message was produced
    EXPECT_EQ(view.show_count, 0);
    EXPECT_EQ(view.close_count, 0);
    EXPECT_TRUE(view.errors.empty());
}

TEST(StudioSimulationParametersPresenterChecks, dialog_closed_allows_a_new_show) {
    // arrange: an open dialog with a callback
    FakeView view;
    studio::SimulationParametersPresenter presenter;
    int first_calls = 0;
    int second_calls = 0;
    presenter.show(view, SimulationConfig::from_xyce_directives({}), [&first_calls](const SimulationConfig&) { first_calls++; });
    ASSERT_NE(view.events, nullptr);
    // act: the window disappears without accept or cancel
    view.events->on_dialog_closed();
    presenter.show(view, SimulationConfig::from_xyce_directives({}), [&second_calls](const SimulationConfig&) { second_calls++; });
    // assert: the second show reached the view and the first callback is gone
    EXPECT_EQ(view.show_count, 2);
    ASSERT_NE(view.events, nullptr);
    view.events->on_accept(configuration_with(AcSimulationParameters("LIN", "10", "1", "1e6", "", std::nullopt, {}, std::nullopt), "AC"));
    EXPECT_EQ(first_calls, 0);
    EXPECT_EQ(second_calls, 1);
}

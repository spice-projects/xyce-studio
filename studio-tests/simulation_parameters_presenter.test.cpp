#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "presenters/simulation_parameters_presenter.h"
#include "simulation/simulation_config.h"
#include "views/simulation_parameters/simulation_parameters_dialog_view_def.h"

namespace
{
    // the dialog double records the calls the presenter makes
    class FakeSimulationParametersView final : public studio::SimulationParametersDialogView
    {
    public:
        void show(const SimulationConfig& current, studio::SimulationParametersDialogEvents& events) override {
            // the presenter drives the dialog through the events it hands over
            opened++;
            m_events = &events;
            shown = current;
        }

        void close() override {
            // a closed dialog leaves the events behind
            closed++;
            m_events = nullptr;
        }

        void show_error(std::string_view message) override {
            // the validation message the dialog displays
            errors.emplace_back(message);
        }

        // the events the open dialog reports to
        studio::SimulationParametersDialogEvents* m_events = nullptr;

        // the configuration the dialog was seeded with
        SimulationConfig shown = SimulationConfig::from_xyce_directives({});

        // how many times each surface call ran
        int opened = 0;
        int closed = 0;

        // the validation messages the dialog displayed
        std::vector<std::string> errors;
    };
} // namespace

TEST(StudioSimulationParametersPresenterChecks, a_cancelled_dialog_reports_the_cancellation_once) {
    // arrange: an open dialog
    studio::SimulationParametersPresenter presenter;
    FakeSimulationParametersView view;
    int accepted = 0;
    int cancelled = 0;
    presenter.show(view, SimulationConfig::from_xyce_directives({}), [&](const SimulationConfig&) { accepted++; }, [&] { cancelled++; });
    ASSERT_EQ(view.opened, 1);
    // act
    view.m_events->on_cancel();
    // assert: the shell observes one cancellation and no accept
    EXPECT_EQ(cancelled, 1);
    EXPECT_EQ(accepted, 0);
    EXPECT_EQ(view.closed, 1);
}

TEST(StudioSimulationParametersPresenterChecks, a_dialog_that_closes_without_an_answer_reports_the_cancellation) {
    // arrange: an open dialog
    studio::SimulationParametersPresenter presenter;
    FakeSimulationParametersView view;
    int cancelled = 0;
    presenter.show(view, SimulationConfig::from_xyce_directives({}), [](const SimulationConfig&) {}, [&] { cancelled++; });
    // act: the window disappears without accept or cancel
    view.m_events->on_dialog_closed();
    // assert
    EXPECT_EQ(cancelled, 1);
}

TEST(StudioSimulationParametersPresenterChecks, an_accepted_dialog_does_not_report_a_cancellation) {
    // arrange: an open dialog
    studio::SimulationParametersPresenter presenter;
    FakeSimulationParametersView view;
    int accepted = 0;
    int cancelled = 0;
    presenter.show(view, SimulationConfig::from_xyce_directives({}), [&](const SimulationConfig&) { accepted++; }, [&] { cancelled++; });
    // act: the dialog accepts a transient analysis
    view.m_events->on_accept(SimulationConfig::from_xyce_directives({".TRAN 1m 10m"}));
    // assert: the shell observes the accept only
    EXPECT_EQ(accepted, 1);
    EXPECT_EQ(cancelled, 0);
    EXPECT_EQ(view.closed, 1);
}

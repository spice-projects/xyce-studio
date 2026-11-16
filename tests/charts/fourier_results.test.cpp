#include <complex>
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "charts/fourier_results.h"
#include "core/step_information.h"
#include "expression/expression.h"
#include "expression/expression_manager.h"
#include "io/xyce_output_file.h"

TEST(FourierResultsTest, the_tables_carry_one_block_per_signal_and_step) {
    // arrange: a two-step file holding one complex spectrum and one real expression
    std::vector<std::pair<std::size_t, std::size_t>> slices{{0, 3}, {3, 6}};
    // the harmonic frequencies of both steps
    std::vector<double> frequencies{0.0, 1000.0, 2000.0, 0.0, 1000.0, 2000.0};
    // the complex spectrum of both steps, whose second harmonic sits at twice the fundamental
    std::vector<std::complex<double>> spectrum{{3.0, 0.0}, {0.0, -1.0}, {1.5, 0.0}, {3.0, 0.0}, {0.0, -1.0}, {1.5, 0.0}};
    // the real expression the table skips
    std::vector<double> real_values{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    // the settings every block records
    const std::unordered_map<std::string, std::string> settings{{"No. Harmonics", "3"}, {"Gridsize", "200"}, {"Interpolation Type", "Cubic Spline"}};
    // the metadata of the first step, which records a THD
    std::unordered_map<std::string, std::string> first_metadata = settings;
    // the first step records a finite THD
    first_metadata["THD"] = "1.234500e+00";
    // the metadata of the second step, which records no finite THD
    std::unordered_map<std::string, std::string> second_metadata = settings;
    // the second step records the nan token
    second_metadata["THD"] = "nan";
    // the metadata of both steps
    std::vector<std::unordered_map<std::string, std::string>> metadata{std::move(first_metadata), std::move(second_metadata)};
    // the expressions the manager publishes
    std::vector<AnyExpression> expressions;
    // the abscissa carries the harmonic frequency
    expressions.emplace_back(Expression<double>("frequency", std::move(frequencies), slices, "Hz", "FOUR"));
    // the real expression is not a spectrum
    expressions.emplace_back(Expression<double>("V(REAL)", std::move(real_values), slices, "V", "FOUR"));
    // the complex spectrum is the block the table lists
    expressions.emplace_back(Expression<std::complex<double>>("V(1)", std::move(spectrum), slices, "V", "FOUR", std::move(metadata)));
    // the manager owns the expressions and their step slices
    ExpressionManager manager(expressions, slices);
    // the file carries two steps
    StepInformation steps({}, {}, {{0.0, 2000.0}, {0.0, 2000.0}});
    // the dataset reads as a Fourier analysis output
    XyceOutputFile file({}, "FOUR: 1000 Hz", true, std::move(steps), PlotType::FOURIER, AbscissaScale::LINEAR, std::move(manager), nullptr);
    // act: build the table blocks
    const std::vector<fourier::SignalTable> tables = fourier::build_tables(file);
    // assert: one block per step, labelled and named after the signal
    ASSERT_EQ(tables.size(), 2U);
    EXPECT_EQ(tables[0].signal, "V(1)");
    EXPECT_EQ(tables[0].step, "Step 1");
    EXPECT_EQ(tables[1].step, "Step 2");
    // assert: the recorded THD reads as a percentage and a missing one reads as none
    EXPECT_EQ(tables[0].thd, "1.234 %");
    EXPECT_EQ(tables[1].thd, "n/a");
    // assert: the block carries the settings the file recorded
    EXPECT_EQ(tables[0].harmonics, "3");
    EXPECT_EQ(tables[0].gridsize, "200");
    EXPECT_EQ(tables[0].interpolation, "Cubic Spline");
    // assert: the rows mirror the file columns in scientific notation
    ASSERT_EQ(tables[0].rows.size(), 3U);
    EXPECT_EQ(tables[0].rows[0].harmonic, 0);
    EXPECT_EQ(tables[0].rows[0].frequency, "0.000000e+00");
    EXPECT_EQ(tables[0].rows[0].magnitude, "3.000000e+00");
    EXPECT_EQ(tables[0].rows[0].phase, "0.000000e+00");
    EXPECT_EQ(tables[0].rows[1].frequency, "1.000000e+03");
    EXPECT_EQ(tables[0].rows[1].magnitude, "1.000000e+00");
    EXPECT_EQ(tables[0].rows[1].phase, "-9.000000e+01");
    // assert: the normalized cells are relative to the fundamental
    EXPECT_EQ(tables[0].rows[0].norm_magnitude, "0.000000e+00");
    EXPECT_EQ(tables[0].rows[0].norm_phase, "0.000000e+00");
    EXPECT_EQ(tables[0].rows[1].norm_magnitude, "1.000000e+00");
    EXPECT_EQ(tables[0].rows[1].norm_phase, "0.000000e+00");
    EXPECT_EQ(tables[0].rows[2].norm_magnitude, "1.500000e+00");
    EXPECT_EQ(tables[0].rows[2].norm_phase, "9.000000e+01");
}

TEST(FourierResultsTest, a_single_step_file_labels_no_step) {
    // arrange: a one-step file holding one complex spectrum
    std::vector<std::pair<std::size_t, std::size_t>> slices{{0, 1}};
    // the harmonic frequency of the only step
    std::vector<double> frequencies{1000.0};
    // the complex spectrum of the only step
    std::vector<std::complex<double>> spectrum{{1.0, 0.0}};
    // the metadata of the only step, which records no settings
    std::vector<std::unordered_map<std::string, std::string>> metadata{{{"THD", "0.000000e+00"}}};
    // the expressions the manager publishes
    std::vector<AnyExpression> expressions;
    // the abscissa carries the harmonic frequency
    expressions.emplace_back(Expression<double>("frequency", std::move(frequencies), slices, "Hz", "FOUR"));
    // the complex spectrum is the block the table lists
    expressions.emplace_back(Expression<std::complex<double>>("I(L1)", std::move(spectrum), slices, "A", "FOUR", std::move(metadata)));
    // the manager owns the expressions and their step slices
    ExpressionManager manager(expressions, slices);
    // the file carries one step
    StepInformation steps({}, {}, {{1000.0, 1000.0}});
    // the dataset reads as a Fourier analysis output
    XyceOutputFile file({}, "FOUR: 1000 Hz", true, std::move(steps), PlotType::FOURIER, AbscissaScale::LINEAR, std::move(manager), nullptr);
    // act: build the table blocks
    const std::vector<fourier::SignalTable> tables = fourier::build_tables(file);
    // assert: the block carries no step label and one harmonic row
    ASSERT_EQ(tables.size(), 1U);
    EXPECT_EQ(tables[0].signal, "I(L1)");
    EXPECT_EQ(tables[0].step, "");
    EXPECT_EQ(tables[0].thd, "0 %");
    ASSERT_EQ(tables[0].rows.size(), 1U);
    EXPECT_EQ(tables[0].rows[0].frequency, "1.000000e+03");
    // assert: a file without settings records none
    EXPECT_EQ(tables[0].harmonics, "");
    EXPECT_EQ(tables[0].gridsize, "");
    EXPECT_EQ(tables[0].interpolation, "");
}

TEST(FourierResultsTest, a_negative_dc_term_mirrors_the_file_columns) {
    // arrange: a one-step file whose dc component is a negative mean
    std::vector<std::pair<std::size_t, std::size_t>> slices{{0, 2}};
    // the harmonic frequencies of the only step
    std::vector<double> frequencies{0.0, 1000.0};
    // the dc term keeps its sign and the fundamental carries no phase
    std::vector<std::complex<double>> spectrum{{-0.3125, 0.0}, {1.0, 0.0}};
    // the metadata of the only step
    std::vector<std::unordered_map<std::string, std::string>> metadata{{{"THD", "0.000000e+00"}}};
    // the expressions the manager publishes
    std::vector<AnyExpression> expressions;
    // the abscissa carries the harmonic frequency
    expressions.emplace_back(Expression<double>("frequency", std::move(frequencies), slices, "Hz", "FOUR"));
    // the complex spectrum is the block the table lists
    expressions.emplace_back(Expression<std::complex<double>>("V(1)", std::move(spectrum), slices, "V", "FOUR", std::move(metadata)));
    // the manager owns the expressions and their step slices
    ExpressionManager manager(expressions, slices);
    // the file carries one step
    StepInformation steps({}, {}, {{0.0, 1000.0}});
    // the dataset reads as a Fourier analysis output
    XyceOutputFile file({}, "FOUR: 1000 Hz", true, std::move(steps), PlotType::FOURIER, AbscissaScale::LINEAR, std::move(manager), nullptr);
    // act: build the table blocks
    const std::vector<fourier::SignalTable> tables = fourier::build_tables(file);
    // assert: the dc row prints the mean the file writes, not its modulus
    ASSERT_EQ(tables.size(), 1U);
    ASSERT_EQ(tables[0].rows.size(), 2U);
    EXPECT_EQ(tables[0].rows[0].magnitude, "-3.125000e-01");
    EXPECT_EQ(tables[0].rows[0].phase, "0.000000e+00");
    // assert: the normalized dc cells stay at zero like the file leaves them
    EXPECT_EQ(tables[0].rows[0].norm_magnitude, "0.000000e+00");
    EXPECT_EQ(tables[0].rows[0].norm_phase, "0.000000e+00");
}

TEST(FourierResultsTest, a_vanished_fundamental_reads_as_no_value) {
    // arrange: a one-step file whose fundamental carries no magnitude
    std::vector<std::pair<std::size_t, std::size_t>> slices{{0, 3}};
    // the harmonic frequencies of the only step
    std::vector<double> frequencies{0.0, 1000.0, 2000.0};
    // the fundamental vanishes, so the ratio has no value
    std::vector<std::complex<double>> spectrum{{0.5, 0.0}, {0.0, 0.0}, {1.0, 0.0}};
    // the metadata of the only step
    std::vector<std::unordered_map<std::string, std::string>> metadata{{{"THD", "0.000000e+00"}}};
    // the expressions the manager publishes
    std::vector<AnyExpression> expressions;
    // the abscissa carries the harmonic frequency
    expressions.emplace_back(Expression<double>("frequency", std::move(frequencies), slices, "Hz", "FOUR"));
    // the complex spectrum is the block the table lists
    expressions.emplace_back(Expression<std::complex<double>>("V(1)", std::move(spectrum), slices, "V", "FOUR", std::move(metadata)));
    // the manager owns the expressions and their step slices
    ExpressionManager manager(expressions, slices);
    // the file carries one step
    StepInformation steps({}, {}, {{0.0, 2000.0}});
    // the dataset reads as a Fourier analysis output
    XyceOutputFile file({}, "FOUR: 1000 Hz", true, std::move(steps), PlotType::FOURIER, AbscissaScale::LINEAR, std::move(manager), nullptr);
    // act: build the table blocks
    const std::vector<fourier::SignalTable> tables = fourier::build_tables(file);
    // assert: a ratio the division cannot express reads as none rather than inf
    ASSERT_EQ(tables.size(), 1U);
    ASSERT_EQ(tables[0].rows.size(), 3U);
    EXPECT_EQ(tables[0].rows[1].norm_magnitude, "n/a");
    EXPECT_EQ(tables[0].rows[1].norm_phase, "0.000000e+00");
    EXPECT_EQ(tables[0].rows[2].norm_magnitude, "n/a");
    EXPECT_EQ(tables[0].rows[2].norm_phase, "0.000000e+00");
}

TEST(FourierResultsTest, a_thd_token_without_a_number_reads_as_none) {
    // arrange: one step whose THD carries a unit and one whose THD is empty
    std::vector<std::pair<std::size_t, std::size_t>> slices{{0, 1}, {1, 2}};
    // the harmonic frequency of both steps
    std::vector<double> frequencies{1000.0, 1000.0};
    // the complex spectrum of both steps
    std::vector<std::complex<double>> spectrum{{1.0, 0.0}, {1.0, 0.0}};
    // the metadata of both steps, neither carrying a plain percentage
    std::vector<std::unordered_map<std::string, std::string>> metadata{{{"THD", "28.64%"}}, {{"THD", ""}}};
    // the expressions the manager publishes
    std::vector<AnyExpression> expressions;
    // the abscissa carries the harmonic frequency
    expressions.emplace_back(Expression<double>("frequency", std::move(frequencies), slices, "Hz", "FOUR"));
    // the complex spectrum is the block the table lists
    expressions.emplace_back(Expression<std::complex<double>>("V(1)", std::move(spectrum), slices, "V", "FOUR", std::move(metadata)));
    // the manager owns the expressions and their step slices
    ExpressionManager manager(expressions, slices);
    // the file carries two steps
    StepInformation steps({}, {}, {{1000.0, 1000.0}, {1000.0, 1000.0}});
    // the dataset reads as a Fourier analysis output
    XyceOutputFile file({}, "FOUR: 1000 Hz", true, std::move(steps), PlotType::FOURIER, AbscissaScale::LINEAR, std::move(manager), nullptr);
    // act: build the table blocks
    const std::vector<fourier::SignalTable> tables = fourier::build_tables(file);
    // assert: a token the percentage cannot read from reports no THD
    ASSERT_EQ(tables.size(), 2U);
    EXPECT_EQ(tables[0].thd, "n/a");
    EXPECT_EQ(tables[1].thd, "n/a");
}

TEST(FourierResultsTest, a_step_without_metadata_reports_no_settings) {
    // arrange: a two-step file whose metadata covers only the first step
    std::vector<std::pair<std::size_t, std::size_t>> slices{{0, 1}, {1, 2}};
    // the harmonic frequency of both steps
    std::vector<double> frequencies{1000.0, 1000.0};
    // the complex spectrum of both steps
    std::vector<std::complex<double>> spectrum{{3.0, 0.0}, {3.0, 0.0}};
    // the metadata of the first step only
    std::vector<std::unordered_map<std::string, std::string>> metadata{{{"THD", "1.000000e+00"}, {"No. Harmonics", "1"}, {"Gridsize", "200"}, {"Interpolation Type", "Cubic Spline"}}};
    // the expressions the manager publishes
    std::vector<AnyExpression> expressions;
    // the abscissa carries the harmonic frequency
    expressions.emplace_back(Expression<double>("frequency", std::move(frequencies), slices, "Hz", "FOUR"));
    // the complex spectrum is the block the table lists
    expressions.emplace_back(Expression<std::complex<double>>("V(1)", std::move(spectrum), slices, "V", "FOUR", std::move(metadata)));
    // the manager owns the expressions and their step slices
    ExpressionManager manager(expressions, slices);
    // the file carries two steps
    StepInformation steps({}, {}, {{1000.0, 1000.0}, {1000.0, 1000.0}});
    // the dataset reads as a Fourier analysis output
    XyceOutputFile file({}, "FOUR: 1000 Hz", true, std::move(steps), PlotType::FOURIER, AbscissaScale::LINEAR, std::move(manager), nullptr);
    // act: build the table blocks
    const std::vector<fourier::SignalTable> tables = fourier::build_tables(file);
    // assert: the step without metadata still lists its rows but records nothing else
    ASSERT_EQ(tables.size(), 2U);
    EXPECT_EQ(tables[1].thd, "n/a");
    EXPECT_EQ(tables[1].harmonics, "");
    EXPECT_EQ(tables[1].gridsize, "");
    EXPECT_EQ(tables[1].interpolation, "");
    ASSERT_EQ(tables[1].rows.size(), 1U);
    EXPECT_EQ(tables[1].rows[0].frequency, "1.000000e+03");
}

TEST(FourierResultsTest, a_file_without_a_complex_expression_builds_no_block) {
    // arrange: a one-step file that holds a real expression only
    std::vector<std::pair<std::size_t, std::size_t>> slices{{0, 2}};
    // the harmonic frequency of the only step
    std::vector<double> frequencies{1000.0, 2000.0};
    // the real expression is not a spectrum
    std::vector<double> real_values{1.0, 2.0};
    // the expressions the manager publishes
    std::vector<AnyExpression> expressions;
    // the abscissa carries the harmonic frequency
    expressions.emplace_back(Expression<double>("frequency", std::move(frequencies), slices, "Hz", "FOUR"));
    // the real expression joins the manager without a spectrum
    expressions.emplace_back(Expression<double>("V(REAL)", std::move(real_values), slices, "V", "FOUR"));
    // the manager owns the expressions and their step slices
    ExpressionManager manager(expressions, slices);
    // the file carries one step
    StepInformation steps({}, {}, {{1000.0, 2000.0}});
    // the dataset reads as a Fourier analysis output
    XyceOutputFile file({}, "FOUR: 1000 Hz", false, std::move(steps), PlotType::FOURIER, AbscissaScale::LINEAR, std::move(manager), nullptr);
    // act: build the table blocks
    const std::vector<fourier::SignalTable> tables = fourier::build_tables(file);
    // assert: a file without a spectrum has no block to show
    EXPECT_TRUE(tables.empty());
}

TEST(FourierResultsTest, two_signals_keep_the_order_the_file_lists) {
    // arrange: a one-step file carrying two complex spectra
    std::vector<std::pair<std::size_t, std::size_t>> slices{{0, 2}};
    // the harmonic frequencies of the only step
    std::vector<double> frequencies{1000.0, 2000.0};
    // the first spectrum of the file
    std::vector<std::complex<double>> first{{1.0, 0.0}, {0.5, 0.0}};
    // the second spectrum of the file
    std::vector<std::complex<double>> second{{2.0, 0.0}, {1.0, 0.0}};
    // the metadata of the only step
    std::vector<std::unordered_map<std::string, std::string>> metadata{{{"THD", "0.000000e+00"}}};
    // the expressions the manager publishes
    std::vector<AnyExpression> expressions;
    // the abscissa carries the harmonic frequency
    expressions.emplace_back(Expression<double>("frequency", std::move(frequencies), slices, "Hz", "FOUR"));
    // the first spectrum joins the manager
    expressions.emplace_back(Expression<std::complex<double>>("V(1)", std::move(first), slices, "V", "FOUR", metadata));
    // the second spectrum follows it
    expressions.emplace_back(Expression<std::complex<double>>("V(2)", std::move(second), slices, "V", "FOUR", std::move(metadata)));
    // the manager owns the expressions and their step slices
    ExpressionManager manager(expressions, slices);
    // the file carries one step
    StepInformation steps({}, {}, {{1000.0, 2000.0}});
    // the dataset reads as a Fourier analysis output
    XyceOutputFile file({}, "FOUR: 1000 Hz", true, std::move(steps), PlotType::FOURIER, AbscissaScale::LINEAR, std::move(manager), nullptr);
    // act: build the table blocks
    const std::vector<fourier::SignalTable> tables = fourier::build_tables(file);
    // assert: the blocks follow the order the file lists the signals in
    ASSERT_EQ(tables.size(), 2U);
    EXPECT_EQ(tables[0].signal, "V(1)");
    EXPECT_EQ(tables[1].signal, "V(2)");
}

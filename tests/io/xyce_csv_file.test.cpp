#include <chrono>
#include <cmath>
#include <complex>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <vector>

#include "expression/expression.h"
#include "expression/expression_manager.h"
#include "io/xyce_csv_file.h"

namespace
{
    // manages a temporary csv file with lifecycle RAII
    class TempFileRAII
    {
    public:
        // construct file with given content and name
        explicit TempFileRAII(const std::string& content, const std::string& name = "") {
            static int counter = 0;
            // build unique path
            const std::string stem = name.empty() ? ("test_csv_gtest_" + std::to_string(counter++) + "_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count())) : name;
            m_path = std::filesystem::temp_directory_path() / (stem + ".csv");
            // open file stream
            std::ofstream out(m_path, std::ios::binary);
            // write content
            out.write(content.data(), static_cast<std::streamsize>(content.size()));
            // close file
            out.close();
        }

        // destructor
        ~TempFileRAII() {
            if (std::filesystem::exists(m_path)) {
                // delete temporary file
                std::filesystem::remove(m_path);
            }
        }

        // path getter
        [[nodiscard]] const std::filesystem::path& path() const {
            // return path
            return m_path;
        }

    private:
        // path field
        std::filesystem::path m_path;
    };

} // namespace

TEST(XyceCsvFileParserTest, returns_nullopt_when_file_not_found) {
    // arrange
    const std::filesystem::path path = "/tmp/nonexistent_csv_file_abc123.csv";
    // act
    const auto result = xyce_csv_file_parser(path);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceCsvFileParserTest, returns_nullopt_when_path_is_a_directory) {
    // arrange — the path exists but cannot be opened as a file
    const auto path = std::filesystem::temp_directory_path() / "xyce_csv_parser_directory_test";
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    // act
    const auto result = xyce_csv_file_parser(path);
    // assert
    ASSERT_FALSE(result.has_value());
    // cleanup
    std::filesystem::remove(path);
}

TEST(XyceCsvFileParserTest, returns_nullopt_when_file_is_not_readable) {
    // arrange — the file exists but read permission is withdrawn
    const auto path = std::filesystem::temp_directory_path() / "xyce_csv_unreadable_test.csv";
    {
        std::ofstream out(path, std::ios::binary);
        out << "TIME,V(1)\n0.0,1.0\n";
    }
    std::error_code ec;
    std::filesystem::permissions(path, std::filesystem::perms::none, ec);
    // act
    const auto result = xyce_csv_file_parser(path);
    // assert
    EXPECT_FALSE(result.has_value());
    // cleanup — restore permissions so the file can be removed
    std::filesystem::permissions(path, std::filesystem::perms::all, ec);
    std::filesystem::remove(path, ec);
}

TEST(XyceCsvFileParserTest, returns_nullopt_when_file_is_empty) {
    // arrange
    const TempFileRAII temp_file("");
    // act
    const auto result = xyce_csv_file_parser(temp_file.path());
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceCsvFileParserTest, returns_nullopt_when_file_has_only_blank_lines) {
    // arrange — lines exist but none carries a header
    const TempFileRAII temp_file("\n\n\n");
    // act
    const auto result = xyce_csv_file_parser(temp_file.path());
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceCsvFileParserTest, returns_nullopt_when_header_only_file) {
    // arrange — a header with no data rows at all
    const TempFileRAII temp_file("TIME,V(1)\n");
    // act
    const auto result = xyce_csv_file_parser(temp_file.path());
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceCsvFileParserTest, returns_nullopt_when_no_numeric_rows) {
    // arrange — the only row repeats the header: it is not numeric
    const TempFileRAII temp_file("TIME,V(1)\nTIME,V(1)\n");
    // act
    const auto result = xyce_csv_file_parser(temp_file.path());
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceCsvFileParserTest, parses_transient_csv_values_and_units) {
    // arrange
    const TempFileRAII temp_file("TIME,V(1),I(V1)\n0.0,1.0,0.1\n1e-9,1.1,0.2\n2e-9,1.2,0.3\n");
    // act
    const auto result = xyce_csv_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto& file = *result.value();
    auto& abscissa = file.abscissa();
    auto abscissa_data = abscissa.data();
    ASSERT_EQ(abscissa_data.size(), 3);
    EXPECT_DOUBLE_EQ(abscissa_data[0], 0.0);
    EXPECT_DOUBLE_EQ(abscissa_data[2], 2e-9);
    EXPECT_EQ(file.abscissa().unit(), "s");
    AnyExpression* v1 = file.expression_manager().evaluate("V(1)");
    ASSERT_NE(v1, nullptr);
    auto* v1_real = std::get_if<Expression<double>>(v1);
    ASSERT_NE(v1_real, nullptr);
    auto v1_data = v1_real->data();
    ASSERT_EQ(v1_data.size(), 3);
    EXPECT_DOUBLE_EQ(v1_data[1], 1.1);
    EXPECT_EQ(v1_real->unit(), "V");
    AnyExpression* i1 = file.expression_manager().evaluate("I(V1)");
    ASSERT_NE(i1, nullptr);
    auto* i1_real = std::get_if<Expression<double>>(i1);
    ASSERT_NE(i1_real, nullptr);
    EXPECT_EQ(i1_real->unit(), "A");
}

TEST(XyceCsvFileParserTest, metadata_title_and_single_step) {
    // arrange
    const TempFileRAII temp_file("TIME,V(1)\n0.0,1.0\n1e-9,1.1\n");
    // act
    const auto result = xyce_csv_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto& file = *result.value();
    EXPECT_EQ(file.metadata().at("format"), "CSV");
    EXPECT_EQ(file.metadata().at("delimiter"), ",");
    EXPECT_EQ(file.title(), temp_file.path().stem().string());
    EXPECT_FALSE(file.is_complex());
    ASSERT_EQ(file.step_information().length(), 1);
    EXPECT_EQ(file.step_information().keys().at(0), "step");
    EXPECT_TRUE(file.suggested_plots().empty());
    EXPECT_EQ(file.abscissa_scale(), AbscissaScale::LINEAR);
}

TEST(XyceCsvFileParserTest, parses_ac_csv_complex_pair) {
    // arrange — a frequency-domain file: the real and imaginary parts of V(1)
    // arrive as Re(V(1)),Im(V(1)) column pairs
    const TempFileRAII temp_file("FREQ,Re(V(1)),Im(V(1))\n10,1.0,-2.0\n100,3.0,-4.0\n", "test_csv_ac_pair");
    // act
    const auto result = xyce_csv_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto& file = *result.value();
    EXPECT_TRUE(file.is_complex());
    EXPECT_EQ(file.plot_type(), PlotType::AC);
    EXPECT_EQ(file.abscissa().unit(), "Hz");
    AnyExpression* v1 = file.expression_manager().evaluate("V(1)");
    ASSERT_NE(v1, nullptr);
    auto* v1_complex = std::get_if<Expression<std::complex<double>>>(v1);
    ASSERT_NE(v1_complex, nullptr);
    auto data = v1_complex->data();
    ASSERT_EQ(data.size(), 2);
    EXPECT_DOUBLE_EQ(data[0].real(), 1.0);
    EXPECT_DOUBLE_EQ(data[0].imag(), -2.0);
    EXPECT_DOUBLE_EQ(data[1].real(), 3.0);
    EXPECT_DOUBLE_EQ(data[1].imag(), -4.0);
}

TEST(XyceCsvFileParserTest, parses_ac_csv_reversed_complex_pair) {
    // arrange — the imaginary column may come before the real column
    const TempFileRAII temp_file("FREQ,Im(V(1)),Re(V(1))\n10,-2.0,1.0\n100,-4.0,3.0\n", "test_csv_ac_reversed");
    // act
    const auto result = xyce_csv_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto& file = *result.value();
    EXPECT_TRUE(file.is_complex());
    AnyExpression* v1 = file.expression_manager().evaluate("V(1)");
    ASSERT_NE(v1, nullptr);
    auto* v1_complex = std::get_if<Expression<std::complex<double>>>(v1);
    ASSERT_NE(v1_complex, nullptr);
    auto data = v1_complex->data();
    ASSERT_EQ(data.size(), 2);
    EXPECT_DOUBLE_EQ(data[0].real(), 1.0);
    EXPECT_DOUBLE_EQ(data[0].imag(), -2.0);
}

TEST(XyceCsvFileParserTest, promotes_plain_columns_of_complex_file) {
    // arrange — a parameter column in a complex file must carry its values in
    // the real component of a complex expression
    const TempFileRAII temp_file("FREQ,Re(V(1)),Im(V(1)),ISWEEP\n10,1.0,-2.0,0.5\n100,3.0,-4.0,0.75\n", "test_csv_ac_promote");
    // act
    const auto result = xyce_csv_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto& file = *result.value();
    EXPECT_TRUE(file.is_complex());
    auto expressions = file.expression_manager().expressions();
    ASSERT_EQ(expressions.size(), 3);
    auto* sweep = std::get_if<Expression<std::complex<double>>>(expressions[2]);
    ASSERT_NE(sweep, nullptr);
    auto data = sweep->data();
    ASSERT_EQ(data.size(), 2);
    EXPECT_DOUBLE_EQ(data[1].real(), 0.75);
    EXPECT_DOUBLE_EQ(data[1].imag(), 0.0);
}

TEST(XyceCsvFileParserTest, keeps_comma_inside_column_names) {
    // arrange — the voltage-difference name V(n1,n2) carries a comma that is
    // part of the name, not a field separator
    const TempFileRAII temp_file("TIME,V(n1,n2),I(V1)\n0.0,1.5,0.2\n1e-9,2.5,0.3\n");
    // act
    const auto result = xyce_csv_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto& file = *result.value();
    auto expressions = file.expression_manager().expressions();
    // the abscissa plus exactly two data columns
    ASSERT_EQ(expressions.size(), 3);
    auto* diff = std::get_if<Expression<double>>(expressions[1]);
    ASSERT_NE(diff, nullptr);
    EXPECT_EQ(diff->name(), "V(n1,n2)");
    auto data = diff->data();
    ASSERT_EQ(data.size(), 2);
    EXPECT_DOUBLE_EQ(data[0], 1.5);
    EXPECT_DOUBLE_EQ(data[1], 2.5);
}

TEST(XyceCsvFileParserTest, keeps_comma_inside_braced_expressions) {
    // arrange — an expression column with an embedded comma function call
    const TempFileRAII temp_file("TIME,{MIN(V(1),V(2))}\n0.0,0.5\n1e-9,0.6\n");
    // act
    const auto result = xyce_csv_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto& file = *result.value();
    auto expressions = file.expression_manager().expressions();
    ASSERT_EQ(expressions.size(), 2);
    auto* expr = std::get_if<Expression<double>>(expressions[1]);
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->name(), "{MIN(V(1),V(2))}");
    auto data = expr->data();
    ASSERT_EQ(data.size(), 2);
    EXPECT_DOUBLE_EQ(data[1], 0.6);
}

TEST(XyceCsvFileParserTest, detects_semicolon_delimiter) {
    // arrange — DELIMITER=SEMICOLON keeps the .csv extension
    const TempFileRAII temp_file("TIME;V(1);I(V1)\n0.0;1.0;0.1\n1e-9;1.1;0.2\n");
    // act
    const auto result = xyce_csv_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value()->metadata().at("delimiter"), ";");
    auto& file = *result.value();
    auto expressions = file.expression_manager().expressions();
    ASSERT_EQ(expressions.size(), 3);
}

TEST(XyceCsvFileParserTest, detects_colon_delimiter) {
    // arrange — DELIMITER=COLON
    const TempFileRAII temp_file("TIME:V(1)\n0.0:1.0\n1e-9:1.1\n");
    // act
    const auto result = xyce_csv_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value()->metadata().at("delimiter"), ":");
}

TEST(XyceCsvFileParserTest, detects_tab_delimiter) {
    // arrange — DELIMITER=TAB
    const TempFileRAII temp_file("TIME\tV(1)\n0.0\t1.0\n1e-9\t1.1\n");
    // act
    const auto result = xyce_csv_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value()->metadata().at("delimiter"), "\t");
    auto& file = *result.value();
    auto expressions = file.expression_manager().expressions();
    ASSERT_EQ(expressions.size(), 2);
}

TEST(XyceCsvFileParserTest, single_column_file_falls_back_to_comma) {
    // arrange — no candidate delimiter reaches a multi-field agreement, so the
    // default comma is kept for the single-column file
    const TempFileRAII temp_file("TIME\n0.0\n1e-9\n2e-9\n");
    // act
    const auto result = xyce_csv_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value()->metadata().at("delimiter"), ",");
    auto data = result.value()->abscissa().data();
    ASSERT_EQ(data.size(), 3);
    EXPECT_DOUBLE_EQ(data[2], 2e-9);
}

TEST(XyceCsvFileParserTest, splits_steps_on_abscissa_restart) {
    // arrange — .STEP runs append to one continuous table: the restart of the
    // abscissa at 0.0 opens the second step
    const TempFileRAII temp_file("TIME,V(1)\n0.0,1.0\n1e-9,1.1\n0.0,2.0\n1e-9,2.1\n");
    // act
    const auto result = xyce_csv_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto& file = *result.value();
    ASSERT_EQ(file.step_information().length(), 2);
    EXPECT_EQ(file.step_information().keys().at(1), "step 2");
    AnyExpression* v1 = file.expression_manager().evaluate("V(1)");
    ASSERT_NE(v1, nullptr);
    auto* v1_real = std::get_if<Expression<double>>(v1);
    ASSERT_NE(v1_real, nullptr);
    auto slices = v1_real->step_indices();
    ASSERT_EQ(slices.size(), 2);
    EXPECT_EQ(slices[0].first, 0);
    EXPECT_EQ(slices[0].second, 2);
    EXPECT_EQ(slices[1].first, 2);
    EXPECT_EQ(slices[1].second, 4);
}

TEST(XyceCsvFileParserTest, ignores_blank_lines_without_splitting_steps) {
    // arrange — blank lines are not step markers in CSV format
    const TempFileRAII temp_file("TIME,V(1)\n0.0,1.0\n\n1e-9,1.1\n\n");
    // act
    const auto result = xyce_csv_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value()->step_information().length(), 1);
}

TEST(XyceCsvFileParserTest, skips_rows_with_wrong_field_count) {
    // arrange — the first row establishes the comma delimiter; a later
    // overlong row and a truncated row must not corrupt the table
    const TempFileRAII temp_file("TIME,V(1)\n0.0,1.0\n1e-9,1.1,2.2\n2e-9,2.2\n0.5\n");
    // act
    const auto result = xyce_csv_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto data = result.value()->abscissa().data();
    ASSERT_EQ(data.size(), 2);
    EXPECT_DOUBLE_EQ(data[0], 0.0);
    EXPECT_DOUBLE_EQ(data[1], 2e-9);
}

TEST(XyceCsvFileParserTest, skips_rows_with_non_numeric_values) {
    // arrange — a row carrying text and a row carrying an out-of-range number
    const TempFileRAII temp_file("TIME,V(1)\n0.0,abc\n1e-9,1.1\n2e-9,1e999\n");
    // act
    const auto result = xyce_csv_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto data = result.value()->abscissa().data();
    ASSERT_EQ(data.size(), 1);
    EXPECT_DOUBLE_EQ(data[0], 1e-9);
}

TEST(XyceCsvFileParserTest, handles_windows_line_endings) {
    // arrange
    const TempFileRAII temp_file("TIME,V(1)\r\n0.0,1.0\r\n1e-9,1.1\r\n");
    // act
    const auto result = xyce_csv_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto data = result.value()->abscissa().data();
    ASSERT_EQ(data.size(), 2);
    EXPECT_DOUBLE_EQ(data[1], 1e-9);
}

TEST(XyceCsvFileParserTest, skips_leading_blank_lines_before_header) {
    // arrange
    const TempFileRAII temp_file("\nTIME,V(1)\n0.0,1.0\n");
    // act
    const auto result = xyce_csv_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto data = result.value()->abscissa().data();
    ASSERT_EQ(data.size(), 1);
}

TEST(XyceCsvFileParserTest, detects_decade_scale_from_frequency_axis) {
    // arrange — a decade step per point
    const TempFileRAII temp_file("FREQ,V(1)\n10,1.0\n100,1.1\n1000,1.2\n", "test_csv_decade");
    // act
    const auto result = xyce_csv_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value()->abscissa_scale(), AbscissaScale::DECADE);
}

TEST(XyceCsvFileParserTest, detects_octave_scale_from_frequency_axis) {
    // arrange — two points per octave
    const TempFileRAII temp_file("FREQ,V(1)\n100,1.0\n141.4213562,1.1\n200,1.2\n282.8427125,1.3\n400,1.4\n", "test_csv_octave");
    // act
    const auto result = xyce_csv_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value()->abscissa_scale(), AbscissaScale::OCTAVE);
}

TEST(XyceCsvFileParserTest, step_ranges_follow_restarts) {
    // arrange — the per-step abscissa ranges are captured independently
    const TempFileRAII temp_file("TIME,V(1)\n0.0,1.0\n1e-9,1.1\n0.0,2.0\n2e-9,2.1\n");
    // act
    const auto result = xyce_csv_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto& steps = result.value()->step_information();
    ASSERT_EQ(steps.length(), 2);
    EXPECT_DOUBLE_EQ(steps.step_abscissa_left_value(0), 0.0);
    EXPECT_DOUBLE_EQ(steps.step_abscissa_right_value(0), 1e-9);
    EXPECT_DOUBLE_EQ(steps.step_abscissa_left_value(1), 0.0);
    EXPECT_DOUBLE_EQ(steps.step_abscissa_right_value(1), 2e-9);
}

TEST(XyceCsvFileParserTest, plot_type_from_fd_and_hb_suffixes) {
    // arrange — AC and HB frequency-domain outputs
    const TempFileRAII fd("FREQ,V(1)\n10,1.0\n100,1.1\n", "test_plot_ac.FD");
    const TempFileRAII hb_fd("FREQ,V(1)\n10,1.0\n100,1.1\n", "test_plot_hb.HB.FD");
    const TempFileRAII sens_fd("FREQ,d_v(1)/d_p\n10,1.0\n100,1.1\n", "test_plot_sens.FD.SENS");
    // act
    const auto ac = xyce_csv_file_parser(fd.path());
    const auto hb = xyce_csv_file_parser(hb_fd.path());
    const auto sens = xyce_csv_file_parser(sens_fd.path());
    // assert
    ASSERT_TRUE(ac.has_value());
    EXPECT_EQ(ac.value()->plot_type(), PlotType::AC);
    ASSERT_TRUE(hb.has_value());
    EXPECT_EQ(hb.value()->plot_type(), PlotType::AC);
    ASSERT_TRUE(sens.has_value());
    EXPECT_EQ(sens.value()->plot_type(), PlotType::AC);
}

TEST(XyceCsvFileParserTest, plot_type_from_time_domain_suffixes) {
    // arrange — AC_IC, HB time data, HB initial conditions, HB startup, homotopy, transient adjoint and sensitivity outputs
    const TempFileRAII td("TIME,V(1)\n0.0,1.0\n", "test_plot_acic.TD");
    const TempFileRAII hb_td("TIME,V(1)\n0.0,1.0\n", "test_plot_hbtd.HB.TD");
    const TempFileRAII hb_ic("TIME,V(1)\n0.0,1.0\n", "test_plot_hbic.hb_ic");
    const TempFileRAII startup("TIME,V(1)\n0.0,1.0\n", "test_plot_startup.startup");
    const TempFileRAII homotopy("TIME,V(1)\n0.0,1.0\n", "test_plot_homo.HOMOTOPY");
    const TempFileRAII tradj("TIME,V(1)\n0.0,1.0\n", "test_plot_tradj.TRADJ");
    const TempFileRAII sens("TIME,d_v(1)/d_p\n0.0,1.0\n", "test_plot_sens.SENS");
    // act
    const auto td_result = xyce_csv_file_parser(td.path());
    const auto hb_result = xyce_csv_file_parser(hb_td.path());
    const auto ic_result = xyce_csv_file_parser(hb_ic.path());
    const auto startup_result = xyce_csv_file_parser(startup.path());
    const auto homotopy_result = xyce_csv_file_parser(homotopy.path());
    const auto tradj_result = xyce_csv_file_parser(tradj.path());
    const auto sens_result = xyce_csv_file_parser(sens.path());
    // assert
    ASSERT_TRUE(td_result.has_value());
    EXPECT_EQ(td_result.value()->plot_type(), PlotType::TRANSIENT);
    ASSERT_TRUE(hb_result.has_value());
    EXPECT_EQ(hb_result.value()->plot_type(), PlotType::TRANSIENT);
    ASSERT_TRUE(ic_result.has_value());
    EXPECT_EQ(ic_result.value()->plot_type(), PlotType::TRANSIENT);
    ASSERT_TRUE(startup_result.has_value());
    EXPECT_EQ(startup_result.value()->plot_type(), PlotType::TRANSIENT);
    ASSERT_TRUE(homotopy_result.has_value());
    EXPECT_EQ(homotopy_result.value()->plot_type(), PlotType::TRANSIENT);
    ASSERT_TRUE(tradj_result.has_value());
    EXPECT_EQ(tradj_result.value()->plot_type(), PlotType::TRANSIENT);
    ASSERT_TRUE(sens_result.has_value());
    EXPECT_EQ(sens_result.value()->plot_type(), PlotType::TRANSIENT);
}

TEST(XyceCsvFileParserTest, plot_type_from_noise_suffix) {
    // arrange
    const TempFileRAII noise("FREQ,INOISE\n10,1e-9\n100,2e-9\n", "test_plot_noise.NOISE");
    // act
    const auto result = xyce_csv_file_parser(noise.path());
    // assert
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value()->plot_type(), PlotType::NOISE);
}

TEST(XyceCsvFileParserTest, plot_type_from_netlist_name_markers) {
    // arrange — plain .csv files carry the analysis marker in the netlist name
    const TempFileRAII tran("TIME,V(1)\n0.0,1.0\n", "testplot_tran_file");
    const TempFileRAII ac("TIME,V(1)\n0.0,1.0\n", "testplot_ac_file");
    const TempFileRAII dc("SOURCE,V(1)\n0.0,1.0\n", "testplot_dc_file");
    const TempFileRAII noise("SOURCE,V(1)\n0.0,1.0\n", "noisetest_file");
    // act
    const auto tran_result = xyce_csv_file_parser(tran.path());
    const auto ac_result = xyce_csv_file_parser(ac.path());
    const auto dc_result = xyce_csv_file_parser(dc.path());
    const auto noise_result = xyce_csv_file_parser(noise.path());
    // assert
    ASSERT_TRUE(tran_result.has_value());
    EXPECT_EQ(tran_result.value()->plot_type(), PlotType::TRANSIENT);
    ASSERT_TRUE(ac_result.has_value());
    EXPECT_EQ(ac_result.value()->plot_type(), PlotType::AC);
    ASSERT_TRUE(dc_result.has_value());
    EXPECT_EQ(dc_result.value()->plot_type(), PlotType::DC);
    ASSERT_TRUE(noise_result.has_value());
    EXPECT_EQ(noise_result.value()->plot_type(), PlotType::NOISE);
}

TEST(XyceCsvFileParserTest, plot_type_unknown_for_unrecognized_name) {
    // arrange
    const TempFileRAII temp_file("SOURCE,V(1)\n0.0,1.0\n", "testplot_unrecognized");
    // act
    const auto result = xyce_csv_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value()->plot_type(), PlotType::UNKNOWN);
}

TEST(XyceCsvFileParserTest, mismatched_delimiters_fall_back_to_comma) {
    // arrange — the header uses a comma while the data row uses a semicolon: no candidate agrees on a multi-field split, the comma default is kept, the row is dropped for its field count and the file yields no data
    const TempFileRAII temp_file("TIME,V(1)\n0.0;1.0\n");
    // act
    const auto result = xyce_csv_file_parser(temp_file.path());
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceCsvFileParserTest, parses_noise_csv_columns) {
    // arrange — the NOISE table prints the frequency abscissa and real spectral columns
    const TempFileRAII noise("FREQ,INOISE,ONOISE,VNB(R1)\n10,1.5e-9,3e-9,4e-9\n100,1.6e-9,3.1e-9,4.1e-9\n1000,1.7e-9,3.2e-9,4.2e-9\n", "test_csv_noise.NOISE");
    // act
    const auto result = xyce_csv_file_parser(noise.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto& file = *result.value();
    EXPECT_FALSE(file.is_complex());
    ASSERT_EQ(file.plot_type(), PlotType::NOISE);
    auto expressions = file.expression_manager().expressions();
    ASSERT_EQ(expressions.size(), 4);
    auto* ono = std::get_if<Expression<double>>(expressions[2]);
    ASSERT_NE(ono, nullptr);
    auto data = ono->data();
    ASSERT_EQ(data.size(), 3);
    EXPECT_DOUBLE_EQ(data[2], 3.2e-9);
}

TEST(XyceCsvFileParserTest, parses_sensitivity_columns) {
    // arrange — sensitivity columns carry d_<output>/d_<parameter> names
    const TempFileRAII sens("TIME,d_v(1)/d_isw\n0.0,1.25\n1e-9,1.5\n", "test_csv_sens.SENS");
    // act
    const auto result = xyce_csv_file_parser(sens.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto& file = *result.value();
    auto expressions = file.expression_manager().expressions();
    ASSERT_EQ(expressions.size(), 2);
    auto* grad = std::get_if<Expression<double>>(expressions[1]);
    ASSERT_NE(grad, nullptr);
    EXPECT_EQ(grad->name(), "d_v(1)/d_isw");
    auto data = grad->data();
    ASSERT_EQ(data.size(), 2);
    EXPECT_DOUBLE_EQ(data[0], 1.25);
}

TEST(XyceCsvFileParserTest, parses_pce_csv_with_repeated_headers) {
    // arrange — the PCE outputter may emit a header before each variable
    // section: the repeated headers are not numeric and must be skipped
    const TempFileRAII pce("TIME,V(1)\n0.0,1.0\n1e-9,1.1\nTIME,V(1)\n2e-9,1.2\n", "test_csv_pce.PCE");
    // act
    const auto result = xyce_csv_file_parser(pce.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto data = result.value()->abscissa().data();
    ASSERT_EQ(data.size(), 3);
    EXPECT_DOUBLE_EQ(data[2], 2e-9);
}

TEST(XyceCsvFileParserTest, detects_power_and_phase_columns) {
    // arrange — power and phase variables get their units from the column name
    const TempFileRAII temp_file("TIME,P(R1),PH(V(1))\n0.0,1e-3,45\n1e-9,2e-3,90\n", "test_csv_units");
    // act
    const auto result = xyce_csv_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto& file = *result.value();
    auto expressions = file.expression_manager().expressions();
    ASSERT_EQ(expressions.size(), 3);
    auto* power = std::get_if<Expression<double>>(expressions[1]);
    ASSERT_NE(power, nullptr);
    EXPECT_EQ(power->unit(), "W");
    auto* phase = std::get_if<Expression<double>>(expressions[2]);
    ASSERT_NE(phase, nullptr);
    EXPECT_EQ(phase->unit(), "\u00b0");
}

TEST(XyceCsvFileParserTest, detects_sweep_parameter_column) {
    // arrange — a DC sweep abscissa named "sweep" resolves to the parameter type
    const TempFileRAII temp_file("sweep,V(1)\n0.0,1.0\n0.5,1.1\n1.0,1.2\n", "test_csv_sweep");
    // act
    const auto result = xyce_csv_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value()->abscissa().unit(), "");
    auto data = result.value()->abscissa().data();
    ASSERT_EQ(data.size(), 3);
}

TEST(XyceCsvFileParserTest, skips_rows_with_empty_fields) {
    // arrange — the middle row has an empty field, which is not numeric, so it is dropped
    const TempFileRAII temp_file("TIME,V(1),I(V1)\n0.0,1.0,0.1\n1e-9,,0.2\n2e-9,1.2,0.3\n");
    // act
    const auto result = xyce_csv_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto data = result.value()->abscissa().data();
    ASSERT_EQ(data.size(), 2);
    EXPECT_DOUBLE_EQ(data[1], 2e-9);
}

TEST(XyceCsvFileParserTest, skips_blank_lines_between_header_and_data) {
    // arrange — blank lines before the first data row are ignored during delimiter detection
    const TempFileRAII temp_file("TIME,V(1)\n\n\n0.0,1.0\n1e-9,1.1\n");
    // act
    const auto result = xyce_csv_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto data = result.value()->abscissa().data();
    ASSERT_EQ(data.size(), 2);
}

TEST(XyceCsvFileParserTest, keeps_crlf_delimiter_detection_working) {
    // arrange — Windows line endings: the trailing carriage return must not defeat detection
    const TempFileRAII temp_file("TIME;V(1)\r\n0.0;1.0\r\n1e-9;1.1\r\n");
    // act
    const auto result = xyce_csv_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value()->metadata().at("delimiter"), ";");
    auto data = result.value()->abscissa().data();
    ASSERT_EQ(data.size(), 2);
}

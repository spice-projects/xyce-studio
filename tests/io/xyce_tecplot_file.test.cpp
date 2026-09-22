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
#include "io/xyce_tecplot_file.h"

namespace
{
    // manages a temporary dat file with lifecycle RAII
    class TempFileRAII
    {
    public:
        // construct file with given content and name
        explicit TempFileRAII(const std::string& content, const std::string& name = "") {
            static int counter = 0;
            // build unique path
            const std::string stem = name.empty() ? ("test_tecplot_gtest_" + std::to_string(counter++) + "_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count())) : name;
            m_path = std::filesystem::temp_directory_path() / (stem + ".dat");
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

TEST(XyceTecplotFileParserTest, returns_nullopt_when_file_not_found) {
    // arrange
    const std::filesystem::path path = "/tmp/nonexistent_tecplot_file_abc123.dat";
    // act
    const auto result = xyce_tecplot_file_parser(path);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceTecplotFileParserTest, returns_nullopt_when_path_is_a_directory) {
    // arrange — the path exists but cannot be opened as a file
    const auto path = std::filesystem::temp_directory_path() / "xyce_tecplot_parser_directory_test";
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    // act
    const auto result = xyce_tecplot_file_parser(path);
    // assert
    ASSERT_FALSE(result.has_value());
    // cleanup
    std::filesystem::remove(path);
}

TEST(XyceTecplotFileParserTest, returns_nullopt_when_file_is_not_readable) {
    // arrange — the file exists but read permission is withdrawn; platforms that do not deny reads on permission-none files (Windows maps perms::none to the read-only attribute) skip the assertion
    const auto path = std::filesystem::temp_directory_path() / "xyce_tecplot_unreadable_test.dat";
    {
        std::ofstream out(path, std::ios::binary);
        out << "TITLE = \"t\", \n\tVARIABLES = \" TIME\" \n\" V(1)\" \nZONE F=POINT \n0.0 1.0\n";
    }
    std::error_code ec;
    std::filesystem::permissions(path, std::filesystem::perms::none, ec);
    // probe whether the platform really denies the read
    {
        std::ifstream probe(path);
        if (probe.is_open()) {
            // restore and remove the file before skipping
            std::filesystem::permissions(path, std::filesystem::perms::all, ec);
            std::filesystem::remove(path, ec);
            GTEST_SKIP() << "platform does not deny reads on permission-none files";
        }
    }
    // act
    const auto result = xyce_tecplot_file_parser(path);
    // assert
    EXPECT_FALSE(result.has_value());
    // cleanup — restore permissions so the file can be removed
    std::filesystem::permissions(path, std::filesystem::perms::all, ec);
    std::filesystem::remove(path, ec);
}

TEST(XyceTecplotFileParserTest, returns_nullopt_when_file_is_empty) {
    // arrange
    const TempFileRAII temp_file("");
    // act
    const auto result = xyce_tecplot_file_parser(temp_file.path());
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceTecplotFileParserTest, returns_nullopt_when_file_has_only_blank_lines) {
    // arrange — lines exist but none carries the title
    const TempFileRAII temp_file("\n\n\n");
    // act
    const auto result = xyce_tecplot_file_parser(temp_file.path());
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceTecplotFileParserTest, returns_nullopt_when_file_has_no_title) {
    // arrange — a file carrying only data lines and no TITLE marker
    const TempFileRAII temp_file("0.0 1.0\n1.0 2.0\n");
    // act
    const auto result = xyce_tecplot_file_parser(temp_file.path());
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceTecplotFileParserTest, returns_nullopt_when_title_only_file) {
    // arrange — a title with no variables block at all
    const TempFileRAII temp_file("TITLE = \"circuit.cir - Xyce\", \n");
    // act
    const auto result = xyce_tecplot_file_parser(temp_file.path());
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceTecplotFileParserTest, returns_nullopt_when_variables_only_file) {
    // arrange — a title and a variables block but no data rows
    const TempFileRAII temp_file("TITLE = \"circuit.cir - Xyce\", \n\tVARIABLES = \" TIME\" \n\" V(1)\" \n");
    // act
    const auto result = xyce_tecplot_file_parser(temp_file.path());
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceTecplotFileParserTest, returns_nullopt_when_no_numeric_rows) {
    // arrange — the only lines after the variables block are auxiliary markers
    const TempFileRAII temp_file("TITLE = \"circuit.cir - Xyce\", \n\tVARIABLES = \" TIME\" \n\" V(1)\" \nDATASETAUXDATA TEMP = \"27.0 \" \nZONE F=POINT T=\"circuit.cir \" \nEnd of Xyce(TM) Simulation\n");
    // act
    const auto result = xyce_tecplot_file_parser(temp_file.path());
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceTecplotFileParserTest, parses_transient_tecplot_values_and_units) {
    // arrange — a transient file in the standard Xyce layout: the first variable
    // rides on the VARIABLES line and each further variable sits on its own
    // quoted line
    const TempFileRAII temp_file("TITLE = \"circuit.cir - Xyce Electrical Simulator\", \n\tVARIABLES = \" TIME\" \n\" V(1)\" \n\" I(V1)\" \nZONE F=POINT T=\"circuit.cir \" \n0.000000000000e+00 0.000000000000e+00 0.000000000000e+00 \n1.000000000000e-09 5.000000000000e-01 -5.000000000000e-01 \n2.000000000000e-09 8.066076679809e-01 -8.066076679809e-01 \n", "test_tecplot_tran");
    // act
    const auto result = xyce_tecplot_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto& file = *result.value();
    EXPECT_EQ(file.plot_type(), PlotType::TRANSIENT);
    EXPECT_FALSE(file.is_complex());
    EXPECT_EQ(file.title(), "circuit.cir - Xyce Electrical Simulator");
    EXPECT_EQ(file.metadata().at("format"), "TECPLOT");
    EXPECT_EQ(file.abscissa().name(), "TIME");
    EXPECT_EQ(file.abscissa().unit(), "s");
    auto abscissa_data = file.abscissa().data();
    ASSERT_EQ(abscissa_data.size(), 3);
    EXPECT_DOUBLE_EQ(abscissa_data[0], 0.0);
    EXPECT_DOUBLE_EQ(abscissa_data[2], 2e-9);
    AnyExpression* v1 = file.expression_manager().evaluate("V(1)");
    ASSERT_NE(v1, nullptr);
    auto* v1_real = std::get_if<Expression<double>>(v1);
    ASSERT_NE(v1_real, nullptr);
    EXPECT_EQ(v1_real->unit(), "V");
    auto v1_data = v1_real->data();
    ASSERT_EQ(v1_data.size(), 3);
    EXPECT_DOUBLE_EQ(v1_data[1], 0.5);
    AnyExpression* i_v1 = file.expression_manager().evaluate("I(V1)");
    ASSERT_NE(i_v1, nullptr);
    auto* i_v1_real = std::get_if<Expression<double>>(i_v1);
    ASSERT_NE(i_v1_real, nullptr);
    EXPECT_EQ(i_v1_real->unit(), "A");
}

TEST(XyceTecplotFileParserTest, captures_title_from_pce_companion_header) {
    // arrange — the intrusive PCE companion files put the TITLE and VARIABLES
    // markers on one line and indent every quoted variable line with a tab;
    // blank lines inside the variables block are skipped
    const TempFileRAII temp_file("TITLE = \"intrusive PCE output\"\tVARIABLES= \n\t\" TIME \"\n\n\t\" V(1)_mean\"\n\t\" V(1)_stddev\"\nZONE F=POINT  T=\"Xyce data\"\n0.0 1.0 2.0\n1e-9 1.1 2.1\n");
    // act
    const auto result = xyce_tecplot_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto& file = *result.value();
    EXPECT_EQ(file.title(), "intrusive PCE output");
    EXPECT_EQ(file.abscissa().name(), "TIME");
    AnyExpression* mean = file.expression_manager().evaluate("V(1)_mean");
    ASSERT_NE(mean, nullptr);
}

TEST(XyceTecplotFileParserTest, parses_pce_companion_index_abscissa) {
    // arrange — the DC operating point PCE companion writes an INDEX column
    // instead of TIME
    const TempFileRAII temp_file("TITLE = \"intrusive PCE output\"\tVARIABLES= \n\t\" INDEX \"\n\t\" var_0_quad_pce_mean\"\nZONE F=POINT  T=\"Xyce data\"\n0 1.0\n");
    // act
    const auto result = xyce_tecplot_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto& file = *result.value();
    EXPECT_EQ(file.abscissa().name(), "INDEX");
    EXPECT_EQ(file.abscissa().unit(), "");
}

TEST(XyceTecplotFileParserTest, bare_variables_marker_declares_no_column) {
    // arrange — a standalone VARIABLES marker carrying no quoted span declares
    // no column, so the quoted lines that follow still build the columns
    const TempFileRAII temp_file("TITLE = \"t\", \n\tVARIABLES= \n\" TIME\" \n\" V(1)\" \nZONE F=POINT T=\"t \" \n0.0 1.0\n");
    // act
    const auto result = xyce_tecplot_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto& file = *result.value();
    EXPECT_EQ(file.abscissa().name(), "TIME");
    AnyExpression* v1 = file.expression_manager().evaluate("V(1)");
    ASSERT_NE(v1, nullptr);
}

TEST(XyceTecplotFileParserTest, parses_ac_tecplot_complex_pair) {
    // arrange — a frequency-domain file: the real and imaginary parts of V(1)
    // arrive as Re(V(1)),Im(V(1)) quoted variable pairs
    const TempFileRAII temp_file(" TITLE = \" Xyce Frequency Domain data, circuit.cir\", \n\tVARIABLES = \" FREQ\" \n\" Re(V(1))\" \n\" Im(V(1))\" \nZONE F=POINT  T=\"Xyce data\" \n1.000000000000e+02 1.000000000000e+00 -2.000000000000e+00 \n1.000000000000e+03 3.000000000000e+00 -4.000000000000e+00 \n", "test_tecplot_ac_pair");
    // act
    const auto result = xyce_tecplot_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto& file = *result.value();
    EXPECT_TRUE(file.is_complex());
    EXPECT_EQ(file.plot_type(), PlotType::AC);
    EXPECT_EQ(file.title(), "Xyce Frequency Domain data, circuit.cir");
    EXPECT_EQ(file.abscissa().name(), "FREQ");
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

TEST(XyceTecplotFileParserTest, parses_ac_tecplot_reversed_complex_pair) {
    // arrange — the imaginary column may come before the real column
    const TempFileRAII temp_file(" TITLE = \" Xyce Frequency Domain data, circuit.cir\", \n\tVARIABLES = \" FREQ\" \n\" Im(V(1))\" \n\" Re(V(1))\" \nZONE F=POINT  T=\"Xyce data\" \n1.000000000000e+02 -2.000000000000e+00 1.000000000000e+00 \n1.000000000000e+03 -4.000000000000e+00 3.000000000000e+00 \n", "test_tecplot_ac_reversed");
    // act
    const auto result = xyce_tecplot_file_parser(temp_file.path());
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

TEST(XyceTecplotFileParserTest, promotes_plain_columns_of_complex_file) {
    // arrange — one Re/Im pair is enough to make the file complex; the plain
    // columns carry purely real values and are promoted to complex expressions
    const TempFileRAII temp_file(" TITLE = \" Xyce Frequency Domain data, circuit.cir\", \n\tVARIABLES = \" FREQ\" \n\" Re(V(1))\" \n\" Im(V(1))\" \n\" V(2)\" \nZONE F=POINT  T=\"Xyce data\" \n1.000000000000e+02 1.000000000000e+00 -2.000000000000e+00 3.000000000000e+00 \n", "test_tecplot_complex_promote");
    // act
    const auto result = xyce_tecplot_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto& file = *result.value();
    EXPECT_TRUE(file.is_complex());
    AnyExpression* v2 = file.expression_manager().evaluate("V(2)");
    ASSERT_NE(v2, nullptr);
    auto* v2_complex = std::get_if<Expression<std::complex<double>>>(v2);
    ASSERT_NE(v2_complex, nullptr);
    auto data = v2_complex->data();
    ASSERT_EQ(data.size(), 1);
    EXPECT_DOUBLE_EQ(data[0].real(), 3.0);
    EXPECT_DOUBLE_EQ(data[0].imag(), 0.0);
}

TEST(XyceTecplotFileParserTest, keeps_braced_expressions_as_single_columns) {
    // arrange — brace-enclosed expressions stay one variable
    const TempFileRAII temp_file("TITLE = \"circuit.cir - Xyce\", \n\tVARIABLES = \" TIME\" \n\" {MIN(V(1),V(2))}\" \nZONE F=POINT T=\"circuit.cir \" \n0.0 1.0\n1e-9 2.0\n", "test_tecplot_tran_brace");
    // act
    const auto result = xyce_tecplot_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto& file = *result.value();
    AnyExpression* expr = file.expression_manager().evaluate("{MIN(V(1),V(2))}");
    ASSERT_NE(expr, nullptr);
}

TEST(XyceTecplotFileParserTest, splits_steps_on_zone_lines) {
    // arrange — each zone of a .STEP run starts with a ZONE marker line
    const TempFileRAII temp_file("TITLE = \"circuit.cir - Xyce\", \n\tVARIABLES = \" TIME\" \n\" V(1)\" \nZONE F=POINT T=\"circuit.cir \" \n0.0 1.0\n1e-9 2.0\nZONE F=POINT T=\"circuit.cir \" \n0.0 3.0\n1e-9 4.0\nEnd of Xyce(TM) Parameter Sweep\n", "test_tecplot_tran_steps");
    // act
    const auto result = xyce_tecplot_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto& file = *result.value();
    EXPECT_EQ(file.step_information().length(), 2);
    EXPECT_EQ(file.step_information().keys().size(), 2);
    auto abscissa_data = file.abscissa().data();
    ASSERT_EQ(abscissa_data.size(), 4);
    EXPECT_DOUBLE_EQ(abscissa_data[2], 0.0);
}

TEST(XyceTecplotFileParserTest, step_ranges_follow_zones) {
    // arrange — the value range of each step covers only its own rows
    const TempFileRAII temp_file("TITLE = \"circuit.cir - Xyce\", \n\tVARIABLES = \" TIME\" \n\" V(1)\" \nZONE F=POINT T=\"circuit.cir \" \n0.0 1.0\n1e-9 2.0\nZONE F=POINT T=\"circuit.cir \" \n0.0 3.0\n5e-9 4.0\nEnd of Xyce(TM) Parameter Sweep\n", "test_tecplot_tran_ranges");
    // act
    const auto result = xyce_tecplot_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto& steps = result.value()->step_information();
    EXPECT_EQ(steps.length(), 2);
    EXPECT_DOUBLE_EQ(steps.step_abscissa_left_value(0), 0.0);
    EXPECT_DOUBLE_EQ(steps.step_abscissa_right_value(0), 1e-9);
    EXPECT_DOUBLE_EQ(steps.step_abscissa_left_value(1), 0.0);
    EXPECT_DOUBLE_EQ(steps.step_abscissa_right_value(1), 5e-9);
}

TEST(XyceTecplotFileParserTest, ignores_blank_lines_without_splitting_steps) {
    // arrange — blank lines between data rows are not step markers
    const TempFileRAII temp_file("TITLE = \"circuit.cir - Xyce\", \n\tVARIABLES = \" TIME\" \n\" V(1)\" \nZONE F=POINT T=\"circuit.cir \" \n0.0 1.0\n\n1e-9 2.0\n", "test_tecplot_tran_blank");
    // act
    const auto result = xyce_tecplot_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto& file = *result.value();
    EXPECT_EQ(file.step_information().length(), 1);
}

TEST(XyceTecplotFileParserTest, skips_auxdata_lines_inside_steps) {
    // arrange — the sweep AUXDATA line follows each ZONE marker
    const TempFileRAII temp_file("TITLE = \"circuit.cir - Xyce\", \n\tVARIABLES = \" TIME\" \n\" V(1)\" \nZONE F=POINT T= \" TEMP = 25 \" \nAUXDATA TEMP = \" 25 \" \n0.0 1.0\n1e-9 2.0\nZONE F=POINT T= \" TEMP = 50 \" \nAUXDATA TEMP = \" 50 \" \n0.0 3.0\n1e-9 4.0\nEnd of Xyce(TM) Parameter Sweep\n", "test_tecplot_tran_aux");
    // act
    const auto result = xyce_tecplot_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto& file = *result.value();
    EXPECT_EQ(file.step_information().length(), 2);
    auto abscissa_data = file.abscissa().data();
    ASSERT_EQ(abscissa_data.size(), 4);
    EXPECT_DOUBLE_EQ(abscissa_data[3], 1e-9);
}

TEST(XyceTecplotFileParserTest, stops_reading_at_footer) {
    // arrange — everything after the footer is ignored
    const TempFileRAII temp_file("TITLE = \"circuit.cir - Xyce\", \n\tVARIABLES = \" TIME\" \n\" V(1)\" \nZONE F=POINT T=\"circuit.cir \" \n0.0 1.0\n1e-9 2.0\nEnd of Xyce(TM) Simulation\n1e-9 99.0\n", "test_tecplot_tran_footer");
    // act
    const auto result = xyce_tecplot_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto& file = *result.value();
    auto abscissa_data = file.abscissa().data();
    ASSERT_EQ(abscissa_data.size(), 2);
}

TEST(XyceTecplotFileParserTest, skips_rows_with_wrong_field_count) {
    // arrange — the second row carries one field too few
    const TempFileRAII temp_file("TITLE = \"circuit.cir - Xyce\", \n\tVARIABLES = \" TIME\" \n\" V(1)\" \n\" V(2)\" \nZONE F=POINT T=\"circuit.cir \" \n0.0 1.0 2.0\n1e-9 3.0\n", "test_tecplot_tran_fields");
    // act
    const auto result = xyce_tecplot_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto& file = *result.value();
    auto abscissa_data = file.abscissa().data();
    ASSERT_EQ(abscissa_data.size(), 1);
    EXPECT_DOUBLE_EQ(abscissa_data[0], 0.0);
}

TEST(XyceTecplotFileParserTest, skips_rows_with_non_numeric_values) {
    // arrange — a row carrying a non-numeric token is skipped
    const TempFileRAII temp_file("TITLE = \"circuit.cir - Xyce\", \n\tVARIABLES = \" TIME\" \n\" V(1)\" \nZONE F=POINT T=\"circuit.cir \" \nnot-a-number 1.0\n0.0 1.0\n", "test_tecplot_tran_nonnum");
    // act
    const auto result = xyce_tecplot_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto& file = *result.value();
    auto abscissa_data = file.abscissa().data();
    ASSERT_EQ(abscissa_data.size(), 1);
    EXPECT_DOUBLE_EQ(abscissa_data[0], 0.0);
}

TEST(XyceTecplotFileParserTest, handles_windows_line_endings) {
    // arrange — the file uses CRLF line endings
    const TempFileRAII temp_file("TITLE = \"circuit.cir - Xyce\", \r\n\tVARIABLES = \" TIME\" \r\n\" V(1)\" \r\nZONE F=POINT T=\"circuit.cir \" \r\n0.0 1.0\r\n1e-9 2.0\r\n", "test_tecplot_tran_crlf");
    // act
    const auto result = xyce_tecplot_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto& file = *result.value();
    auto abscissa_data = file.abscissa().data();
    ASSERT_EQ(abscissa_data.size(), 2);
    EXPECT_DOUBLE_EQ(abscissa_data[1], 1e-9);
}

TEST(XyceTecplotFileParserTest, skips_leading_blank_lines_before_title) {
    // arrange — blank lines precede the title line
    const TempFileRAII temp_file("\n\nTITLE = \"circuit.cir - Xyce\", \n\tVARIABLES = \" TIME\" \n\" V(1)\" \nZONE F=POINT T=\"circuit.cir \" \n0.0 1.0\n", "test_tecplot_tran_lead");
    // act
    const auto result = xyce_tecplot_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto& file = *result.value();
    EXPECT_EQ(file.title(), "circuit.cir - Xyce");
}

TEST(XyceTecplotFileParserTest, detects_decade_scale_from_frequency_axis) {
    // arrange — a logarithmic decade sweep of the abscissa
    const TempFileRAII temp_file(" TITLE = \" Xyce Frequency Domain data, circuit.cir\", \n\tVARIABLES = \" FREQ\" \n\" V(1)\" \nZONE F=POINT  T=\"Xyce data\" \n1.000000000000e+00 1.0 \n1.000000000000e+01 2.0 \n1.000000000000e+02 3.0 \n1.000000000000e+03 4.0 \n", "test_tecplot_ac_decade");
    // act
    const auto result = xyce_tecplot_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto& file = *result.value();
    EXPECT_EQ(file.abscissa_scale(), AbscissaScale::DECADE);
}

TEST(XyceTecplotFileParserTest, detects_octave_scale_from_frequency_axis) {
    // arrange — a logarithmic octave sweep with two points per octave
    const TempFileRAII temp_file(" TITLE = \" Xyce Frequency Domain data, circuit.cir\", \n\tVARIABLES = \" FREQ\" \n\" V(1)\" \nZONE F=POINT  T=\"Xyce data\" \n1.000000000000e+02 1.0 \n1.414213562000e+02 2.0 \n2.000000000000e+02 3.0 \n2.828427125000e+02 4.0 \n4.000000000000e+02 5.0 \n", "test_tecplot_ac_octave");
    // act
    const auto result = xyce_tecplot_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto& file = *result.value();
    EXPECT_EQ(file.abscissa_scale(), AbscissaScale::OCTAVE);
}

TEST(XyceTecplotFileParserTest, plot_type_from_fd_and_hb_suffixes) {
    // arrange — AC, HB frequency-domain and AC sensitivity outputs
    const TempFileRAII fd(" TITLE = \" Xyce Frequency Domain data, circuit.cir\", \n\tVARIABLES = \" FREQ\" \n\" V(1)\" \nZONE F=POINT  T=\"Xyce data\" \n1.0 2.0\n", "test_plot_ac.FD");
    const TempFileRAII hb_fd(" TITLE = \" Xyce Frequency Domain data, circuit.cir\", \n\tVARIABLES = \" FREQ\" \n\" V(1)\" \nZONE F=POINT  T=\"Xyce data\" \n1.0 2.0\n", "test_plot_hb.HB.FD");
    const TempFileRAII sens_fd(" TITLE = \" Xyce Frequency Domain data, circuit.cir\", \n\tVARIABLES = \" FREQ\" \n\" d_v(1)/d_p\" \nZONE F=POINT  T=\"Xyce data\" \n1.0 2.0\n", "test_plot_sens.FD.SENS");
    // act
    const auto ac = xyce_tecplot_file_parser(fd.path());
    const auto hb = xyce_tecplot_file_parser(hb_fd.path());
    const auto sens = xyce_tecplot_file_parser(sens_fd.path());
    // assert
    ASSERT_TRUE(ac.has_value());
    EXPECT_EQ(ac.value()->plot_type(), PlotType::AC);
    ASSERT_TRUE(hb.has_value());
    EXPECT_EQ(hb.value()->plot_type(), PlotType::AC);
    ASSERT_TRUE(sens.has_value());
    EXPECT_EQ(sens.value()->plot_type(), PlotType::AC);
}

TEST(XyceTecplotFileParserTest, plot_type_from_time_domain_suffixes) {
    // arrange — AC_IC, HB time data, HB initial conditions, HB startup, homotopy, transient adjoint and sensitivity outputs
    const TempFileRAII td("TITLE = \"t\", \n\tVARIABLES = \" TIME\" \n\" V(1)\" \nZONE F=POINT T=\"t \" \n0.0 1.0\n", "test_plot_acic.TD");
    const TempFileRAII hb_td("TITLE = \"t\", \n\tVARIABLES = \" TIME\" \n\" V(1)\" \nZONE F=POINT T=\"t \" \n0.0 1.0\n", "test_plot_hbtd.HB.TD");
    const TempFileRAII hb_ic("TITLE = \"t\", \n\tVARIABLES = \" TIME\" \n\" V(1)\" \nZONE F=POINT T=\"t \" \n0.0 1.0\n", "test_plot_hbic.hb_ic");
    const TempFileRAII startup("TITLE = \"t\", \n\tVARIABLES = \" TIME\" \n\" V(1)\" \nZONE F=POINT T=\"t \" \n0.0 1.0\n", "test_plot_startup.startup");
    const TempFileRAII homotopy("TITLE = \"t\", \n\tVARIABLES = \" TIME\" \n\" V(1)\" \nZONE F=POINT T=\"t \" \n0.0 1.0\n", "test_plot_homo.HOMOTOPY");
    const TempFileRAII tradj("TITLE = \"t\", \n\tVARIABLES = \" TIME\" \n\" V(1)\" \nZONE F=POINT T=\"t \" \n0.0 1.0\n", "test_plot_tradj.TRADJ");
    const TempFileRAII sens("TITLE = \"t\", \n\tVARIABLES = \" TIME\" \n\" V(1)\" \nZONE F=POINT T=\"t \" \n0.0 1.0\n", "test_plot_sens.SENS");
    // act
    const auto td_result = xyce_tecplot_file_parser(td.path());
    const auto hb_result = xyce_tecplot_file_parser(hb_td.path());
    const auto ic_result = xyce_tecplot_file_parser(hb_ic.path());
    const auto startup_result = xyce_tecplot_file_parser(startup.path());
    const auto homotopy_result = xyce_tecplot_file_parser(homotopy.path());
    const auto tradj_result = xyce_tecplot_file_parser(tradj.path());
    const auto sens_result = xyce_tecplot_file_parser(sens.path());
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

TEST(XyceTecplotFileParserTest, plot_type_from_noise_suffix) {
    // arrange
    const TempFileRAII noise(" TITLE = \" Xyce Frequency Domain data, circuit.cir\", \n\tVARIABLES = \" FREQ\" \n\" INOISE\" \nZONE F=POINT  T=\"Xyce data\" \n1.0 2.0\n", "test_plot_noise.NOISE");
    // act
    const auto result = xyce_tecplot_file_parser(noise.path());
    // assert
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value()->plot_type(), PlotType::NOISE);
}

TEST(XyceTecplotFileParserTest, plot_type_from_netlist_name_markers) {
    // arrange — plain .dat files carry the analysis marker in the netlist name
    const TempFileRAII tran("TITLE = \"t\", \n\tVARIABLES = \" TIME\" \n\" V(1)\" \nZONE F=POINT T=\"t \" \n0.0 1.0\n", "testplot_tran_file");
    const TempFileRAII ac("TITLE = \"t\", \n\tVARIABLES = \" TIME\" \n\" V(1)\" \nZONE F=POINT T=\"t \" \n0.0 1.0\n", "testplot_ac_file");
    const TempFileRAII dc("TITLE = \"t\", \n\tVARIABLES = \" VSWEEP\" \n\" V(1)\" \nZONE F=POINT T=\"t \" \n1.0 2.0\n", "testplot_dc_file");
    const TempFileRAII noise("TITLE = \"t\", \n\tVARIABLES = \" FREQ\" \n\" V(1)\" \nZONE F=POINT T=\"t \" \n1.0 2.0\n", "noisetest_file");
    // act
    const auto tran_result = xyce_tecplot_file_parser(tran.path());
    const auto ac_result = xyce_tecplot_file_parser(ac.path());
    const auto dc_result = xyce_tecplot_file_parser(dc.path());
    const auto noise_result = xyce_tecplot_file_parser(noise.path());
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

TEST(XyceTecplotFileParserTest, plot_type_unknown_for_unrecognized_name) {
    // arrange
    const TempFileRAII temp_file("TITLE = \"t\", \n\tVARIABLES = \" TIME\" \n\" V(1)\" \nZONE F=POINT T=\"t \" \n0.0 1.0\n", "testplot_unrecognized");
    // act
    const auto result = xyce_tecplot_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value()->plot_type(), PlotType::UNKNOWN);
}

TEST(XyceTecplotFileParserTest, detects_power_and_phase_columns) {
    // arrange — power and phase columns carry their measurement units
    const TempFileRAII temp_file("TITLE = \"circuit.cir - Xyce\", \n\tVARIABLES = \" TIME\" \n\" P(V1)\" \n\" PH(V1)\" \nZONE F=POINT T=\"circuit.cir \" \n0.0 1.0 2.0\n", "test_tecplot_tran_power");
    // act
    const auto result = xyce_tecplot_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto& file = *result.value();
    AnyExpression* power = file.expression_manager().evaluate("P(V1)");
    ASSERT_NE(power, nullptr);
    auto* power_real = std::get_if<Expression<double>>(power);
    ASSERT_NE(power_real, nullptr);
    EXPECT_EQ(power_real->unit(), "W");
    AnyExpression* phase = file.expression_manager().evaluate("PH(V1)");
    ASSERT_NE(phase, nullptr);
    auto* phase_real = std::get_if<Expression<double>>(phase);
    ASSERT_NE(phase_real, nullptr);
    EXPECT_EQ(phase_real->unit(), "\u00b0");
}

TEST(XyceTecplotFileParserTest, detects_sweep_parameter_column) {
    // arrange — the PARAMETER column type is recognized
    const TempFileRAII temp_file("TITLE = \"circuit.cir - Xyce\", \n\tVARIABLES = \" PARAMETER\" \n\" V(1)\" \nZONE F=POINT T=\"circuit.cir \" \n0.0 1.0\n", "test_tecplot_param");
    // act
    const auto result = xyce_tecplot_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto& file = *result.value();
    AnyExpression* sweep = file.expression_manager().evaluate("PARAMETER");
    ASSERT_NE(sweep, nullptr);
    auto* sweep_real = std::get_if<Expression<double>>(sweep);
    ASSERT_NE(sweep_real, nullptr);
    EXPECT_EQ(sweep_real->unit(), "");
}

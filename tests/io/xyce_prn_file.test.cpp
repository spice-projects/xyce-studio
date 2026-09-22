#include <chrono>
#include <cmath>
#include <complex>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iomanip>
#include <optional>
#include <string>
#include <vector>

#include "expression/expression.h"
#include "expression/expression_manager.h"
#include "io/xyce_prn_file.h"

namespace
{
    // manages a temporary file with lifecycle RAII
    class TempFileRAII
    {
    public:
        // construct file with given content and optional explicit stem
        explicit TempFileRAII(const std::string& content, const std::string& name = "") {
            static int counter = 0;
            // build unique path
            const std::string stem = name.empty() ? ("test_prn_gtest_" + std::to_string(counter++) + "_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count())) : name;
            m_path = std::filesystem::temp_directory_path() / (stem + ".prn");
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

TEST(XycePrnFileParserTest, returns_nullopt_when_file_not_found) {
    // arrange
    const std::filesystem::path path = "/tmp/nonexistent_prn_file_abc123.prn";
    // act
    const auto result = xyce_prn_file_parser(path);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XycePrnFileParserTest, returns_nullopt_when_file_is_empty) {
    // arrange
    const TempFileRAII temp_file("");
    // act
    const auto result = xyce_prn_file_parser(temp_file.path());
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XycePrnFileParserTest, parses_std_format_with_index_column) {
    // arrange
    const std::string content = "INDEX TIME V(1) I(R1)\n"
                                "0 0.0 1.0 0.1\n"
                                "1 1e-9 1.1 0.2\n"
                                "2 2e-9 1.2 0.3\n"
                                ".\n";
    const TempFileRAII temp_file(content);
    // act
    const auto result = xyce_prn_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_FALSE(result.value()->filename().string().empty());
}

TEST(XycePrnFileParserTest, title_is_filename_stem) {
    // arrange
    const std::string content = "INDEX TIME V(1)\n"
                                "0 0.0 1.0\n"
                                "1 1e-9 1.1\n"
                                "2 2e-9 1.2\n"
                                ".\n";
    const TempFileRAII temp_file(content);
    // act
    const auto result = xyce_prn_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value()->title(), temp_file.path().stem().string());
}

TEST(XycePrnFileParserTest, parses_std_format_abscissa_values) {
    // arrange
    const std::string content = "INDEX TIME V(1)\n"
                                "0 0.0 1.0\n"
                                "1 1e-9 1.1\n"
                                "2 2e-9 1.2\n"
                                ".\n";
    const TempFileRAII temp_file(content);
    // act
    const auto result = xyce_prn_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto& file = *result.value();
    auto expr_list = file.expression_manager().expressions();
    ASSERT_GE(expr_list.size(), 2);
}

TEST(XycePrnFileParserTest, parses_gnuplot_format_with_steps) {
    // arrange
    const std::string content = "INDEX TIME V(1)\n"
                                "0 0.0 1.0\n"
                                "1 1e-9 1.1\n"
                                "\n"
                                "0 0.0 2.0\n"
                                "1 1e-9 2.1\n"
                                ".\n";
    const TempFileRAII temp_file(content);
    // act
    const auto result = xyce_prn_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value()->step_information().length(), 2);
}

TEST(XycePrnFileParserTest, parses_splot_format_with_steps) {
    // arrange
    const std::string content = "INDEX TIME V(1)\n"
                                "0 0.0 1.0\n"
                                "1 1e-9 1.1\n"
                                "\n"
                                "0 0.0 2.0\n"
                                "1 1e-9 2.1\n";
    const TempFileRAII temp_file(content);
    // act
    const auto result = xyce_prn_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value()->step_information().length(), 2);
}

TEST(XycePrnFileParserTest, handles_format_token_in_header) {
    // arrange
    const std::string content = "INDEX TIME V(1) FORMAT=NOINDEX\n"
                                "0 0.0 1.0\n"
                                "1 1e-9 1.1\n";
    const TempFileRAII temp_file(content);
    // act
    const auto result = xyce_prn_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
}

TEST(XycePrnFileParserTest, handles_single_step_std_format) {
    // arrange
    const std::string content = "INDEX FREQ V(1)\n"
                                "0 1000 1.0\n"
                                "1 2000 1.1\n"
                                "2 3000 1.2\n"
                                ".\n";
    const TempFileRAII temp_file(content);
    // act
    const auto result = xyce_prn_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value()->step_information().length(), 1);
}

TEST(XycePrnFileParserTest, parses_file_with_blank_lines_between_blocks) {
    // arrange
    const std::string content = "INDEX TIME V(1)\n"
                                "0 0.0 1.0\n"
                                "1 1e-9 1.1\n"
                                "\n"
                                "0 1e-9 2.0\n"
                                "1 2e-9 2.1\n";
    const TempFileRAII temp_file(content);
    // act
    const auto result = xyce_prn_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
}

TEST(XycePrnFileParserTest, handles_column_data_correctly) {
    // arrange
    const std::string content = "INDEX TIME V(1)\n"
                                "0 0.0 1.0\n"
                                "1 1e-9 1.1\n"
                                ".\n";
    const TempFileRAII temp_file(content);
    // act
    const auto result = xyce_prn_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());

    // explicitly check the expression manager can evaluate
    auto& file = *result.value();
    auto& expr_manager = file.expression_manager();
    // check that V(1) expression exists
    AnyExpression* v1 = expr_manager.evaluate("V(1)");
    ASSERT_NE(v1, nullptr);
    auto* v1_real = std::get_if<Expression<double>>(v1);
    ASSERT_NE(v1_real, nullptr);
}

TEST(XycePrnFileParserTest, metadata_contains_format) {
    // arrange
    const std::string content = "INDEX TIME V(1)\n"
                                "0 0.0 1.0\n"
                                "1 1e-9 1.1\n"
                                ".\n";
    const TempFileRAII temp_file(content);
    // act
    const auto result = xyce_prn_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    const auto& metadata = result.value()->metadata();
    ASSERT_EQ(metadata.at("format"), "STD");
}

TEST(XycePrnFileParserTest, metadata_contains_has_index) {
    // arrange
    const std::string content = "INDEX TIME V(1)\n"
                                "0 0.0 1.0\n"
                                "1 1e-9 1.1\n"
                                ".\n";
    const TempFileRAII temp_file(content);
    // act
    const auto result = xyce_prn_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    const auto& metadata = result.value()->metadata();
    ASSERT_EQ(metadata.at("has_index"), "true");
}

TEST(XycePrnFileParserTest, evaluates_expression_data_values) {
    // arrange
    const std::string content = "INDEX FREQ V(1)\n"
                                "0 100 1.0\n"
                                "1 200 1.1\n"
                                "2 300 1.2\n"
                                ".\n";
    const TempFileRAII temp_file(content);
    // act
    const auto result = xyce_prn_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto& file = *result.value();
    auto& expr_manager = file.expression_manager();
    auto* v1_expr = expr_manager.evaluate("V(1)");
    ASSERT_NE(v1_expr, nullptr);
    auto* v1_real = std::get_if<Expression<double>>(v1_expr);
    ASSERT_NE(v1_real, nullptr);
    auto data = v1_real->step_data(0);
    ASSERT_EQ(data.size(), 3);
    ASSERT_DOUBLE_EQ(data[0], 1.0);
    ASSERT_DOUBLE_EQ(data[1], 1.1);
    ASSERT_DOUBLE_EQ(data[2], 1.2);
}

TEST(XycePrnFileParserTest, evaluates_abscissa_data_values) {
    // arrange
    const std::string content = "INDEX FREQ V(1)\n"
                                "0 100 1.0\n"
                                "1 200 1.1\n"
                                "2 300 1.2\n"
                                ".\n";
    const TempFileRAII temp_file(content);
    // act
    const auto result = xyce_prn_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto& file = *result.value();
    auto& abscissa = file.abscissa();
    auto data = abscissa.step_data(0);
    ASSERT_EQ(data.size(), 3);
}

TEST(XycePrnFileParserTest, power_variable_detected_correctly) {
    // arrange
    const std::string content = "INDEX TIME P(R1) V(1)\n"
                                "0 0.0 0.5 1.0\n"
                                "1 1e-9 0.6 1.1\n"
                                ".\n";
    const TempFileRAII temp_file(content);
    // act
    const auto result = xyce_prn_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto& file = *result.value();
    auto& mgr = file.expression_manager();
    AnyExpression* expr = mgr.evaluate("P(R1)");
    ASSERT_NE(expr, nullptr);
    auto* real = std::get_if<Expression<double>>(expr);
    ASSERT_NE(real, nullptr);
}

TEST(XycePrnFileParserTest, expression_variable_detected_correctly) {
    // arrange
    const std::string content = "INDEX TIME {V(1)-V(2)} V(2)\n"
                                "0 0.0 0.0 0.0\n"
                                "1 1e-9 0.1 0.2\n"
                                ".\n";
    const TempFileRAII temp_file(content);
    // act
    const auto result = xyce_prn_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
}

TEST(XycePrnFileParserTest, decade_scale_detected_correctly) {
    // arrange — decade spacing with 10 points per decade: 1, 1.26, 1.58, ..., 10
    std::ostringstream content;
    content << std::setprecision(15) << "INDEX FREQ V(1)\n";
    for (int k = 0; k <= 10; ++k) {
        double f = std::pow(10.0, static_cast<double>(k) / 10.0);
        content << "0 " << f << " 0.5\n";
    }
    content << ".\n";
    const TempFileRAII temp_file(content.str());
    // act
    const auto result = xyce_prn_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value()->abscissa_scale(), AbscissaScale::DECADE);
}

TEST(XycePrnFileParserTest, octave_scale_detected_correctly) {
    // arrange — octave spacing with 10 points per octave: powers of 2^(1/10)
    std::ostringstream content;
    content << std::setprecision(15) << "INDEX FREQ V(1)\n";
    // 1 point per step, octave steps: 2^0, 2^0.1, 2^0.2, ..., 2^1.0
    for (int k = 0; k <= 10; ++k) {
        double f = std::pow(2.0, static_cast<double>(k) / 10.0);
        content << "0 " << f << " 0.5\n";
    }
    content << ".\n";
    const TempFileRAII temp_file(content.str());
    // act
    const auto result = xyce_prn_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value()->abscissa_scale(), AbscissaScale::OCTAVE);
}

TEST(XycePrnFileParserTest, header_only_file_returns_no_data) {
    // arrange — a file with only a header and no data rows
    const std::string content = "INDEX TIME V(1)\n";
    const TempFileRAII temp_file(content);
    // act
    const auto result = xyce_prn_file_parser(temp_file.path());
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XycePrnFileParserTest, non_paired_imag_column_is_not_complex) {
    // arrange — a column named with "imag" in the header: Xyce only writes
    // Re(X)/Im(X) pairs for complex data, so a lone IMAG column is a plain
    // real column and does not mark the file as complex
    const std::string content = "INDEX FREQ V(1) IMAG(V(1))\n"
                                "0 100 0.5 0.1\n"
                                "1 200 0.7 0.2\n"
                                ".\n";
    const TempFileRAII temp_file(content);
    // act
    const auto result = xyce_prn_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_FALSE(result.value()->is_complex());
    // both columns stay real expressions
    auto& expr_manager = result.value()->expression_manager();
    auto* v1_expr = expr_manager.evaluate("V(1)");
    ASSERT_NE(v1_expr, nullptr);
    EXPECT_NE(std::get_if<Expression<double>>(v1_expr), nullptr);
    auto* imag_expr = expr_manager.evaluate("IMAG(V(1))");
    ASSERT_NE(imag_expr, nullptr);
    EXPECT_NE(std::get_if<Expression<double>>(imag_expr), nullptr);
}

TEST(XycePrnFileParserTest, complex_file_promotes_plain_columns_and_keeps_real_abscissa) {
    // arrange — a complex file with a Re(X)/Im(X) pair and a plain column:
    // the abscissa stays real and every data expression becomes complex, the
    // plain column values ride in the real component
    const std::string content = "Index FREQ Re(V(N2)) Im(V(N2)) V(IN)\n"
                                "0 100 1.0 -0.01 1.5\n"
                                "1 200 2.0 -0.02 1.6\n"
                                "End of Xyce(TM) Simulation\n";
    const TempFileRAII temp_file(content);
    // act
    const auto result = xyce_prn_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result.value()->is_complex());
    auto& expr_manager = result.value()->expression_manager();
    // the abscissa expression is a real number
    auto* freq_expr = expr_manager.evaluate("FREQ");
    ASSERT_NE(freq_expr, nullptr);
    EXPECT_NE(std::get_if<Expression<double>>(freq_expr), nullptr);
    // the paired column is a complex expression
    auto* vn2_expr = expr_manager.evaluate("V(N2)");
    ASSERT_NE(vn2_expr, nullptr);
    auto* vn2_complex = std::get_if<Expression<std::complex<double>>>(vn2_expr);
    ASSERT_NE(vn2_complex, nullptr);
    auto vn2_data = vn2_complex->step_data(0);
    ASSERT_EQ(vn2_data.size(), 2);
    ASSERT_DOUBLE_EQ(vn2_data[0].real(), 1.0);
    ASSERT_DOUBLE_EQ(vn2_data[0].imag(), -0.01);
    ASSERT_DOUBLE_EQ(vn2_data[1].real(), 2.0);
    ASSERT_DOUBLE_EQ(vn2_data[1].imag(), -0.02);
    // the plain column is promoted to a complex expression with the values in the real component
    auto* vin_expr = expr_manager.evaluate("V(IN)");
    ASSERT_NE(vin_expr, nullptr);
    auto* vin_complex = std::get_if<Expression<std::complex<double>>>(vin_expr);
    ASSERT_NE(vin_complex, nullptr);
    auto vin_data = vin_complex->step_data(0);
    ASSERT_EQ(vin_data.size(), 2);
    ASSERT_DOUBLE_EQ(vin_data[0].real(), 1.5);
    ASSERT_DOUBLE_EQ(vin_data[0].imag(), 0.0);
    ASSERT_DOUBLE_EQ(vin_data[1].real(), 1.6);
    ASSERT_DOUBLE_EQ(vin_data[1].imag(), 0.0);
}

TEST(XycePrnFileParserTest, parses_fd_complex_columns_into_complex_variables) {
    // arrange — the real Xyce FD prn header shape: mixed case Index column and
    // Re(X)/Im(X) column pairs for complex data
    const std::string content = "Index       FREQ            Re(V(N2))         Im(V(N2))         Re(I(V1))         Im(I(V1))    \n"
                                "0        1.00000000e+00    1.00000000e+00   -6.28318779e-04   -3.94784332e-09   -6.28318531e-06\n"
                                "1        5.26410526e+03   -9.05764875e-02   -3.01399710e-02   -9.96890055e-04    2.99584892e-03\n"
                                "End of Xyce(TM) Simulation\n";
    const TempFileRAII temp_file(content);
    // act
    const auto result = xyce_prn_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result.value()->is_complex());
    ASSERT_EQ(result.value()->metadata().at("has_index"), "true");
    ASSERT_EQ(result.value()->metadata().at("format"), "STD");
    // the abscissa is the frequency column, not the index column
    auto& file = *result.value();
    auto& abscissa = file.abscissa();
    ASSERT_EQ(abscissa.name(), "FREQ");
    ASSERT_EQ(abscissa.step_data(0).size(), 2);
    ASSERT_DOUBLE_EQ(abscissa.step_data(0)[0], 1.00000000e+00);
    ASSERT_DOUBLE_EQ(abscissa.step_data(0)[1], 5.26410526e+03);
    // each Re(X)/Im(X) pair is combined into a single complex variable X
    auto& expr_manager = file.expression_manager();
    auto* vn2_expr = expr_manager.evaluate("V(N2)");
    ASSERT_NE(vn2_expr, nullptr);
    auto* vn2_complex = std::get_if<Expression<std::complex<double>>>(vn2_expr);
    ASSERT_NE(vn2_complex, nullptr);
    auto vn2_data = vn2_complex->step_data(0);
    ASSERT_EQ(vn2_data.size(), 2);
    ASSERT_DOUBLE_EQ(vn2_data[0].real(), 1.00000000e+00);
    ASSERT_DOUBLE_EQ(vn2_data[0].imag(), -6.28318779e-04);
    ASSERT_DOUBLE_EQ(vn2_data[1].real(), -9.05764875e-02);
    ASSERT_DOUBLE_EQ(vn2_data[1].imag(), -3.01399710e-02);
    auto* iv1_expr = expr_manager.evaluate("I(V1)");
    ASSERT_NE(iv1_expr, nullptr);
    auto* iv1_complex = std::get_if<Expression<std::complex<double>>>(iv1_expr);
    ASSERT_NE(iv1_complex, nullptr);
    auto iv1_data = iv1_complex->step_data(0);
    ASSERT_EQ(iv1_data.size(), 2);
    ASSERT_DOUBLE_EQ(iv1_data[1].real(), -9.96890055e-04);
    ASSERT_DOUBLE_EQ(iv1_data[1].imag(), 2.99584892e-03);
}

TEST(XycePrnFileParserTest, parses_mixed_case_index_column) {
    // arrange — the Xyce output writes the index column with mixed case
    const std::string content = "Index TIME V(1)\n"
                                "0 0.0 1.0\n"
                                "1 1e-9 1.1\n"
                                "2 2e-9 1.2\n"
                                ".\n";
    const TempFileRAII temp_file(content);
    // act
    const auto result = xyce_prn_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value()->metadata().at("has_index"), "true");
    // the abscissa is the time column with its unit
    auto& abscissa = result.value()->abscissa();
    ASSERT_EQ(abscissa.name(), "TIME");
    ASSERT_DOUBLE_EQ(abscissa.step_data(0)[2], 2e-9);
}

TEST(XycePrnFileParserTest, parses_complex_columns_in_reverse_order) {
    // arrange — the imaginary part column appears before the real part column
    const std::string content = "Index FREQ Im(V(N2)) Re(V(N2)) Im(I(V1)) Re(I(V1))\n"
                                "0 100 0.25 0.5 -0.01 0.02\n"
                                "1 200 0.35 0.7 -0.02 0.04\n"
                                "End of Xyce(TM) Simulation\n";
    const TempFileRAII temp_file(content);
    // act
    const auto result = xyce_prn_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result.value()->is_complex());
    // each Im(X)/Re(X) pair is combined into a single complex variable X with
    // the real and imaginary components taken from their own columns
    auto& expr_manager = result.value()->expression_manager();
    auto* vn2_expr = expr_manager.evaluate("V(N2)");
    ASSERT_NE(vn2_expr, nullptr);
    auto* vn2_complex = std::get_if<Expression<std::complex<double>>>(vn2_expr);
    ASSERT_NE(vn2_complex, nullptr);
    auto vn2_data = vn2_complex->step_data(0);
    ASSERT_EQ(vn2_data.size(), 2);
    ASSERT_DOUBLE_EQ(vn2_data[0].real(), 0.5);
    ASSERT_DOUBLE_EQ(vn2_data[0].imag(), 0.25);
    ASSERT_DOUBLE_EQ(vn2_data[1].real(), 0.7);
    ASSERT_DOUBLE_EQ(vn2_data[1].imag(), 0.35);
    auto* iv1_expr = expr_manager.evaluate("I(V1)");
    ASSERT_NE(iv1_expr, nullptr);
    auto* iv1_complex = std::get_if<Expression<std::complex<double>>>(iv1_expr);
    ASSERT_NE(iv1_complex, nullptr);
    auto iv1_data = iv1_complex->step_data(0);
    ASSERT_EQ(iv1_data.size(), 2);
    ASSERT_DOUBLE_EQ(iv1_data[0].real(), 0.02);
    ASSERT_DOUBLE_EQ(iv1_data[0].imag(), -0.01);
    ASSERT_DOUBLE_EQ(iv1_data[1].real(), 0.04);
    ASSERT_DOUBLE_EQ(iv1_data[1].imag(), -0.02);
}

TEST(XycePrnFileParserTest, parses_interleaved_complex_columns) {
    // arrange — the parts of two complex variables are interleaved and appear
    // in mixed order
    const std::string content = "Index FREQ Im(V(1)) Im(V(2)) Re(V(1)) Re(V(2))\n"
                                "0 100 0.1 0.2 1.0 2.0\n"
                                "1 200 0.3 0.4 3.0 4.0\n"
                                "End of Xyce(TM) Simulation\n";
    const TempFileRAII temp_file(content);
    // act
    const auto result = xyce_prn_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result.value()->is_complex());
    // each pair is combined into a single complex variable regardless of the
    // interleaving
    auto& expr_manager = result.value()->expression_manager();
    auto* v1_expr = expr_manager.evaluate("V(1)");
    ASSERT_NE(v1_expr, nullptr);
    auto* v1_complex = std::get_if<Expression<std::complex<double>>>(v1_expr);
    ASSERT_NE(v1_complex, nullptr);
    auto v1_data = v1_complex->step_data(0);
    ASSERT_EQ(v1_data.size(), 2);
    ASSERT_DOUBLE_EQ(v1_data[0].real(), 1.0);
    ASSERT_DOUBLE_EQ(v1_data[0].imag(), 0.1);
    ASSERT_DOUBLE_EQ(v1_data[1].real(), 3.0);
    ASSERT_DOUBLE_EQ(v1_data[1].imag(), 0.3);
    auto* v2_expr = expr_manager.evaluate("V(2)");
    ASSERT_NE(v2_expr, nullptr);
    auto* v2_complex = std::get_if<Expression<std::complex<double>>>(v2_expr);
    ASSERT_NE(v2_complex, nullptr);
    auto v2_data = v2_complex->step_data(0);
    ASSERT_EQ(v2_data.size(), 2);
    ASSERT_DOUBLE_EQ(v2_data[0].real(), 2.0);
    ASSERT_DOUBLE_EQ(v2_data[0].imag(), 0.2);
    ASSERT_DOUBLE_EQ(v2_data[1].real(), 4.0);
    ASSERT_DOUBLE_EQ(v2_data[1].imag(), 0.4);
}

TEST(XycePrnFileParserTest, returns_nullopt_when_file_is_not_readable) {
    // arrange — the file exists but read permission is withdrawn
    const auto path = std::filesystem::temp_directory_path() / "xyce_prn_unreadable_test.prn";
    {
        std::ofstream out(path, std::ios::binary);
        out << "TIME V(1)\n0.0 1.0\n";
    }
    std::error_code ec;
    std::filesystem::permissions(path, std::filesystem::perms::none, ec);
    // act
    const auto result = xyce_prn_file_parser(path);
    // assert
    EXPECT_FALSE(result.has_value());
    // cleanup — restore permissions so the file can be removed
    std::filesystem::permissions(path, std::filesystem::perms::all, ec);
    std::filesystem::remove(path, ec);
}

TEST(XycePrnFileParserTest, returns_nullopt_when_file_has_only_crlf_blank_lines) {
    // arrange — carriage-only lines: the header search strips them and finds no header
    const TempFileRAII temp_file("\r\n\r\n");
    // act
    const auto result = xyce_prn_file_parser(temp_file.path());
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XycePrnFileParserTest, returns_nullopt_when_header_line_is_whitespace_only) {
    // arrange — the first non-blank line holds no tokens
    const TempFileRAII temp_file("   \nTIME V(1)\n0.0 1.0\n");
    // act
    const auto result = xyce_prn_file_parser(temp_file.path());
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XycePrnFileParserTest, returns_nullopt_when_header_has_only_format_token) {
    // arrange — the FORMAT= token is not a column, so no columns remain
    const TempFileRAII temp_file("FORMAT=UNKNOWN\n0.0\n");
    // act
    const auto result = xyce_prn_file_parser(temp_file.path());
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XycePrnFileParserTest, returns_nullopt_when_index_column_has_no_absconissa) {
    // arrange — an index-only header leaves no abscissa column
    const TempFileRAII temp_file("INDEX\n0\n");
    // act
    const auto result = xyce_prn_file_parser(temp_file.path());
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XycePrnFileParserTest, explicit_gnuplot_format_token_wins_over_inference) {
    // arrange
    const TempFileRAII temp_file("TIME V(1) FORMAT=GNUPLOT\n0.0 1.0\n1e-9 1.1\n");
    // act
    const auto result = xyce_prn_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value()->metadata().at("format"), "GNUPLOT");
}

TEST(XycePrnFileParserTest, explicit_splot_format_token_wins_over_inference) {
    // arrange
    const TempFileRAII temp_file("TIME V(1) FORMAT=SPLOT\n0.0 1.0\n1e-9 1.1\n");
    // act
    const auto result = xyce_prn_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value()->metadata().at("format"), "SPLOT");
}

TEST(XycePrnFileParserTest, explicit_std_format_token_wins_over_inference) {
    // arrange
    const TempFileRAII temp_file("TIME V(1) FORMAT=STD\n0.0 1.0\n1e-9 1.1\n");
    // act
    const auto result = xyce_prn_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value()->metadata().at("format"), "STD");
}

TEST(XycePrnFileParserTest, handles_windows_line_endings_in_header_and_data) {
    // arrange
    const TempFileRAII temp_file("TIME V(1)\r\n0.0 1.0\r\n1e-9 1.1\r\n.\r\n");
    // act
    const auto result = xyce_prn_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto data = result.value()->abscissa().data();
    ASSERT_EQ(data.size(), 2);
    EXPECT_DOUBLE_EQ(data[1], 1e-9);
}

TEST(XycePrnFileParserTest, skips_rows_with_non_numeric_tokens) {
    // arrange
    const TempFileRAII temp_file("TIME V(1)\n0.0 1.0\nabc 2.0\n1e-9 1.1\n");
    // act
    const auto result = xyce_prn_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto data = result.value()->abscissa().data();
    ASSERT_EQ(data.size(), 2);
    EXPECT_DOUBLE_EQ(data[1], 1e-9);
}

TEST(XycePrnFileParserTest, plot_type_from_noise_suffix) {
    // arrange
    const TempFileRAII temp_file("FREQ INOISE\n100 1.0\n", "probe_noise.NOISE");
    // act
    const auto result = xyce_prn_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value()->plot_type(), PlotType::NOISE);
}

TEST(XycePrnFileParserTest, plot_type_from_netlist_name_markers) {
    // arrange — plain .prn files carry the analysis marker in the netlist name
    const TempFileRAII tran("TIME V(1)\n0.0 1.0\n", "probe_tran_file");
    const TempFileRAII ac("FREQ V(1)\n100 1.0\n", "probe_ac_file");
    const TempFileRAII dc("SOURCE V(1)\n0.0 1.0\n", "probe_dc_file");
    const TempFileRAII noise("SOURCE V(1)\n0.0 1.0\n", "probename_noise_x");
    // act
    const auto tran_result = xyce_prn_file_parser(tran.path());
    const auto ac_result = xyce_prn_file_parser(ac.path());
    const auto dc_result = xyce_prn_file_parser(dc.path());
    const auto noise_result = xyce_prn_file_parser(noise.path());
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

#include <chrono>
#include <cmath>
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
        // construct file with given content
        explicit TempFileRAII(const std::string& content) {
            static int counter = 0;
            // build unique path
            m_path = std::filesystem::temp_directory_path() / ("test_prn_gtest_" + std::to_string(counter++) + "_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".prn");
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

TEST(XycePrnFileParserTest, complex_data_detected_correctly) {
    // arrange — a column named with "imag" in the header
    const std::string content = "INDEX FREQ V(1) IMAG(V(1))\n"
                                "0 100 0.5 0.1\n"
                                "1 200 0.7 0.2\n"
                                ".\n";
    const TempFileRAII temp_file(content);
    // act
    const auto result = xyce_prn_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result.value()->is_complex());
}

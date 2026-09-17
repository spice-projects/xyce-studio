#include <chrono>
#include <cmath>
#include <complex>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iomanip>
#include <numbers>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "expression/expression.h"
#include "expression/expression_manager.h"
#include "io/touchstone_file.h"

namespace
{
    class TempFileRAII
    {
    public:
        explicit TempFileRAII(const std::string& content) {
            static int counter = 0;
            m_path = std::filesystem::temp_directory_path() / ("test_touchstone_gtest_" + std::to_string(counter++) + "_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".s2p");
            std::ofstream out(m_path, std::ios::binary);
            out.write(content.data(), static_cast<std::streamsize>(content.size()));
            out.close();
        }

        explicit TempFileRAII(const std::string& content, const std::string& extension) {
            static int counter = 0;
            m_path = std::filesystem::temp_directory_path() / ("test_touchstone_gtest_" + std::to_string(counter++) + "_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + extension);
            std::ofstream out(m_path, std::ios::binary);
            out.write(content.data(), static_cast<std::streamsize>(content.size()));
            out.close();
        }

        ~TempFileRAII() {
            if (std::filesystem::exists(m_path))
                std::filesystem::remove(m_path);
        }

        [[nodiscard]] const std::filesystem::path& path() const { return m_path; }

    private:
        std::filesystem::path m_path;
    };
} // namespace

TEST(TouchstoneFileParserChecks, returns_nullopt_for_nonexistent_file) {
    // arrange
    const std::filesystem::path nonexistent = "/tmp/nonexistent_touchstone_file.s2p";
    // act
    const auto result = touchstone_file_parser(nonexistent);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(TouchstoneFileParserChecks, parses_v2_ri_2port_12_21_ordering) {
    // arrange
    const std::string content = "[Version] 2.0\n"
                                "# Hz S RI R 50\n"
                                "[Number of Ports] 2\n"
                                "[Two-Port Data Order] 12_21\n"
                                "[Number of Frequencies] 2\n"
                                "[Reference] 50 50\n"
                                "[Network Data]\n"
                                "! comment line\n"
                                "1.0  0.5  0.1  0.8  0.2  0.3  0.4  0.7  0.05\n"
                                "2.0  0.4  0.2  0.7  0.3  0.2  0.5  0.6  0.1\n"
                                "[End]\n";
    TempFileRAII temp_file(content);
    // act
    const auto result = touchstone_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    const auto& file = result.value();
    // check title
    ASSERT_EQ(file->title(), "LIN Analysis");
    // check plot type
    ASSERT_EQ(file->plot_type(), PlotType::AC);
    // check is_complex
    ASSERT_TRUE(file->is_complex());
    // check abscissa scale (linear spacing)
    ASSERT_EQ(file->abscissa_scale(), AbscissaScale::LINEAR);
    // check metadata
    const auto& metadata = file->metadata();
    ASSERT_EQ(metadata.at("parameter_type"), "S");
    ASSERT_EQ(metadata.at("data_format"), "RI");
    ASSERT_EQ(metadata.at("reference_impedance"), "50.000000");
    // check expression manager has 1 + 4 = 5 expressions (frequency + S11, S12, S21, S22)
    auto expressions = file->expression_manager().expressions();
    ASSERT_EQ(expressions.size(), 5);
    // check expression names
    auto names = file->expression_manager().expression_names();
    ASSERT_EQ(names[0], "frequency");
    ASSERT_EQ(names[1], "S11");
    ASSERT_EQ(names[2], "S12");
    ASSERT_EQ(names[3], "S21");
    ASSERT_EQ(names[4], "S22");
    // check frequency abscissa
    auto& freq_expr = file->expression_manager().abscissa();
    auto freq_data = freq_expr.step_data(0);
    ASSERT_EQ(freq_data.size(), 2);
    EXPECT_DOUBLE_EQ(freq_data[0], 1.0);
    EXPECT_DOUBLE_EQ(freq_data[1], 2.0);
    // check S11 (12_21: S11, S21, S12, S22) — data order 0,1,2,3 → S11, S21, S12, S22
    // S11 = (0.5, 0.1), S21 = (0.8, 0.2), S12 = (0.3, 0.4), S22 = (0.7, 0.05)
    {
        auto* expr = expressions[1];
        ASSERT_TRUE(std::holds_alternative<Expression<std::complex<double>>>(*expr));
        auto& s11 = std::get<Expression<std::complex<double>>>(*expr);
        auto data = s11.step_data(0);
        ASSERT_EQ(data.size(), 2);
        EXPECT_DOUBLE_EQ(data[0].real(), 0.5);
        EXPECT_DOUBLE_EQ(data[0].imag(), 0.1);
        EXPECT_DOUBLE_EQ(data[1].real(), 0.4);
        EXPECT_DOUBLE_EQ(data[1].imag(), 0.2);
    }
    // check S21 = (0.8, 0.2)
    {
        auto* expr = expressions[3];
        ASSERT_TRUE(std::holds_alternative<Expression<std::complex<double>>>(*expr));
        auto& s21 = std::get<Expression<std::complex<double>>>(*expr);
        auto data = s21.step_data(0);
        EXPECT_DOUBLE_EQ(data[0].real(), 0.8);
        EXPECT_DOUBLE_EQ(data[0].imag(), 0.2);
    }
    // check S12 = (0.3, 0.4)
    {
        auto* expr = expressions[2];
        ASSERT_TRUE(std::holds_alternative<Expression<std::complex<double>>>(*expr));
        auto& s12 = std::get<Expression<std::complex<double>>>(*expr);
        auto data = s12.step_data(0);
        EXPECT_DOUBLE_EQ(data[0].real(), 0.3);
        EXPECT_DOUBLE_EQ(data[0].imag(), 0.4);
    }
    // check S22 = (0.7, 0.05)
    {
        auto* expr = expressions[4];
        ASSERT_TRUE(std::holds_alternative<Expression<std::complex<double>>>(*expr));
        auto& s22 = std::get<Expression<std::complex<double>>>(*expr);
        auto data = s22.step_data(0);
        EXPECT_DOUBLE_EQ(data[0].real(), 0.7);
        EXPECT_DOUBLE_EQ(data[0].imag(), 0.05);
    }
}

TEST(TouchstoneFileParserChecks, parses_v2_ri_2port_21_12_ordering) {
    // arrange
    const std::string content = "[Version] 2.0\n"
                                "# Hz S RI R 50\n"
                                "[Number of Ports] 2\n"
                                "[Two-Port Data Order] 21_12\n"
                                "[Number of Frequencies] 1\n"
                                "[Reference] 50 50\n"
                                "[Network Data]\n"
                                "1.0  0.5  0.1  0.3  0.4  0.8  0.2  0.7  0.05\n"
                                "[End]\n";
    TempFileRAII temp_file(content);
    // act
    const auto result = touchstone_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto expressions = result.value()->expression_manager().expressions();
    ASSERT_EQ(expressions.size(), 5);
    // 21_12: S11, S12, S21, S22
    // S12 = (0.3, 0.4), S21 = (0.8, 0.2)
    {
        auto* expr = expressions[2];
        auto& s12 = std::get<Expression<std::complex<double>>>(*expr);
        auto data = s12.step_data(0);
        EXPECT_DOUBLE_EQ(data[0].real(), 0.3);
        EXPECT_DOUBLE_EQ(data[0].imag(), 0.4);
    }
    {
        auto* expr = expressions[3];
        auto& s21 = std::get<Expression<std::complex<double>>>(*expr);
        auto data = s21.step_data(0);
        EXPECT_DOUBLE_EQ(data[0].real(), 0.8);
        EXPECT_DOUBLE_EQ(data[0].imag(), 0.2);
    }
}

TEST(TouchstoneFileParserChecks, parses_v1_ma_2port) {
    // arrange
    const std::string content = "! V1 MA test\n"
                                "# kHz S MA R 75\n"
                                "1.0  0.5  -10  0.8  20  0.3  30  0.7  -45\n"
                                "2.0  0.4  -20  0.7  10  0.2  35  0.6  -40\n";
    TempFileRAII temp_file(content, ".s2p");
    // act
    const auto result = touchstone_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    // check frequency is normalized from kHz to Hz
    auto& freq_expr = result.value()->expression_manager().abscissa();
    auto freq_data = freq_expr.step_data(0);
    ASSERT_EQ(freq_data.size(), 2);
    EXPECT_DOUBLE_EQ(freq_data[0], 1000.0);
    EXPECT_DOUBLE_EQ(freq_data[1], 2000.0);
    // check S11 magnitude is converted correctly
    // 0.5 magnitude at -10 degrees
    auto expressions = result.value()->expression_manager().expressions();
    auto* expr = expressions[1];
    auto& s11 = std::get<Expression<std::complex<double>>>(*expr);
    auto data = s11.step_data(0);
    // 0.5 * cos(-10 deg) = 0.5 * 0.9848 = 0.4924
    EXPECT_NEAR(data[0].real(), 0.5 * cos(-10.0 * std::numbers::pi / 180.0), 1e-9);
    // 0.5 * sin(-10 deg) = 0.5 * (-0.1736) = -0.0868
    EXPECT_NEAR(data[0].imag(), 0.5 * sin(-10.0 * std::numbers::pi / 180.0), 1e-9);
    // check S21: 0.8 magnitude at 20 degrees
    auto* s21_expr = expressions[3];
    auto& s21 = std::get<Expression<std::complex<double>>>(*s21_expr);
    auto s21_data = s21.step_data(0);
    // 0.8 * cos(20 deg) = 0.8 * 0.9397 = 0.7518
    EXPECT_NEAR(s21_data[0].real(), 0.8 * cos(20.0 * std::numbers::pi / 180.0), 1e-9);
    EXPECT_NEAR(s21_data[0].imag(), 0.8 * sin(20.0 * std::numbers::pi / 180.0), 1e-9);
    // check metadata for default R=75
    const auto& metadata = result.value()->metadata();
    ASSERT_EQ(metadata.at("reference_impedance"), "75.000000");
}

TEST(TouchstoneFileParserChecks, parses_v1_db_2port) {
    // arrange
    const std::string content = "# MHz S DB R 50\n"
                                "1.0  -6.0  -10  -3.0  20  -10.0  30  -1.0  -45\n"
                                "2.0  -8.0  -20  -4.0  10  -12.0  35  -2.0  -40\n";
    TempFileRAII temp_file(content, ".s2p");
    // act
    const auto result = touchstone_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    // check frequency is normalized from MHz to Hz
    auto& freq_expr = result.value()->expression_manager().abscissa();
    auto freq_data = freq_expr.step_data(0);
    ASSERT_EQ(freq_data.size(), 2);
    EXPECT_DOUBLE_EQ(freq_data[0], 1e6);
    EXPECT_DOUBLE_EQ(freq_data[1], 2e6);
    // check S11: -6 dB at -10 degrees
    auto expressions = result.value()->expression_manager().expressions();
    auto* expr = expressions[1];
    auto& s11 = std::get<Expression<std::complex<double>>>(*expr);
    auto data = s11.step_data(0);
    // 10^(-6/20) = 0.501187...
    double expected_mag = std::pow(10.0, -6.0 / 20.0);
    // 12_21 order: S11, S21, S12, S22
    EXPECT_NEAR(data[0].real(), expected_mag * cos(-10.0 * std::numbers::pi / 180.0), 1e-9);
    EXPECT_NEAR(data[0].imag(), expected_mag * sin(-10.0 * std::numbers::pi / 180.0), 1e-9);
    // check S21: -3 dB at 20 degrees
    auto* s21_expr = expressions[3];
    auto& s21 = std::get<Expression<std::complex<double>>>(*s21_expr);
    auto s21_data = s21.step_data(0);
    double expected_mag21 = std::pow(10.0, -3.0 / 20.0);
    EXPECT_NEAR(s21_data[0].real(), expected_mag21 * cos(20.0 * std::numbers::pi / 180.0), 1e-9);
    EXPECT_NEAR(s21_data[0].imag(), expected_mag21 * sin(20.0 * std::numbers::pi / 180.0), 1e-9);
}

TEST(TouchstoneFileParserChecks, parses_v1_ri_2port) {
    // arrange
    const std::string content = "# Hz S RI R 50\n"
                                "1.0  0.5  0.1  0.8  0.2  0.3  0.4  0.7  0.05\n"
                                "2.0  0.4  0.2  0.7  0.3  0.2  0.5  0.6  0.1\n";
    TempFileRAII temp_file(content, ".s2p");
    // act
    const auto result = touchstone_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    // check expressions exist
    auto expressions = result.value()->expression_manager().expressions();
    ASSERT_EQ(expressions.size(), 5);
    // check S11 directly (RI format, no conversion needed)
    auto& s11 = std::get<Expression<std::complex<double>>>(*expressions[1]);
    auto data = s11.step_data(0);
    EXPECT_DOUBLE_EQ(data[0].real(), 0.5);
    EXPECT_DOUBLE_EQ(data[0].imag(), 0.1);
    // check S22 (last in data for 12_21 ordering)
    auto& s22 = std::get<Expression<std::complex<double>>>(*expressions[4]);
    auto s22_data = s22.step_data(0);
    EXPECT_DOUBLE_EQ(data[0].real(), 0.5);
    EXPECT_DOUBLE_EQ(s22_data[0].real(), 0.7);
    EXPECT_DOUBLE_EQ(s22_data[0].imag(), 0.05);
}

TEST(TouchstoneFileParserChecks, handles_continuation_lines) {
    // arrange
    const std::string content = "[Version] 2.0\n"
                                "# Hz S RI R 50\n"
                                "[Number of Ports] 2\n"
                                "[Number of Frequencies] 1\n"
                                "[Network Data]\n"
                                "1.0 0.5 0.1 0.8 0.2\n"
                                "0.3 0.4 0.7 0.05\n"
                                "[End]\n";
    TempFileRAII temp_file(content);
    // act
    const auto result = touchstone_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto expressions = result.value()->expression_manager().expressions();
    ASSERT_EQ(expressions.size(), 5);
    // S11 = (0.5, 0.1), S21 = (0.8, 0.2), S12 = (0.3, 0.4), S22 = (0.7, 0.05)
    auto& s11 = std::get<Expression<std::complex<double>>>(*expressions[1]);
    auto s11_data = s11.step_data(0);
    EXPECT_DOUBLE_EQ(s11_data[0].real(), 0.5);
    EXPECT_DOUBLE_EQ(s11_data[0].imag(), 0.1);
    auto& s12 = std::get<Expression<std::complex<double>>>(*expressions[2]);
    auto s12_data = s12.step_data(0);
    EXPECT_DOUBLE_EQ(s12_data[0].real(), 0.3);
    EXPECT_DOUBLE_EQ(s12_data[0].imag(), 0.4);
}

TEST(TouchstoneFileParserChecks, parses_v2_ri_3port) {
    // arrange
    const std::string content = "[Version] 2.0\n"
                                "# Hz S RI R 50\n"
                                "[Number of Ports] 3\n"
                                "[Number of Frequencies] 1\n"
                                "[Reference] 50 50 50\n"
                                "[Network Data]\n"
                                "1.0 0.5 0.1 0.2 0.0 0.3 0.0 0.8 0.2 0.7 0.05 0.2 0.0 0.1 0.0 0.4 0.0 0.6 0.05\n"
                                "[End]\n";
    TempFileRAII temp_file(content, ".s3p");
    // act
    const auto result = touchstone_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto expressions = result.value()->expression_manager().expressions();
    // 1 frequency + 9 S-parameters = 10 expressions
    ASSERT_EQ(expressions.size(), 10);
    auto names = result.value()->expression_manager().expression_names();
    ASSERT_EQ(names[0], "frequency");
    ASSERT_EQ(names[1], "S11");
    ASSERT_EQ(names[2], "S12");
    ASSERT_EQ(names[3], "S13");
    ASSERT_EQ(names[4], "S21");
    ASSERT_EQ(names[5], "S22");
    // check S11
    auto& s11 = std::get<Expression<std::complex<double>>>(*expressions[1]);
    auto s11_data = s11.step_data(0);
    EXPECT_DOUBLE_EQ(s11_data[0].real(), 0.5);
    EXPECT_DOUBLE_EQ(s11_data[0].imag(), 0.1);
    // check S22 = (0.7, 0.05)
    auto& s22 = std::get<Expression<std::complex<double>>>(*expressions[5]);
    auto s22_data = s22.step_data(0);
    EXPECT_DOUBLE_EQ(s22_data[0].real(), 0.7);
    EXPECT_DOUBLE_EQ(s22_data[0].imag(), 0.05);
    // check S33 = (0.6, 0.05)
    auto& s33 = std::get<Expression<std::complex<double>>>(*expressions[9]);
    auto s33_data = s33.step_data(0);
    EXPECT_DOUBLE_EQ(s33_data[0].real(), 0.6);
    EXPECT_DOUBLE_EQ(s33_data[0].imag(), 0.05);
}

TEST(TouchstoneFileParserChecks, parses_v1_ghz_frequency) {
    // arrange
    const std::string content = "# GHz S RI R 50\n"
                                "1.0  0.5  0.1  0.8  0.2  0.3  0.4  0.7  0.05\n"
                                "2.0  0.4  0.2  0.7  0.3  0.2  0.5  0.6  0.1\n";
    TempFileRAII temp_file(content, ".s2p");
    // act
    const auto result = touchstone_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    // check frequency is normalized from GHz to Hz
    auto& freq_expr = result.value()->expression_manager().abscissa();
    auto freq_data = freq_expr.step_data(0);
    ASSERT_EQ(freq_data.size(), 2);
    EXPECT_DOUBLE_EQ(freq_data[0], 1e9);
    EXPECT_DOUBLE_EQ(freq_data[1], 2e9);
}

TEST(TouchstoneFileParserChecks, parses_per_port_reference_impedance_v2) {
    // arrange
    const std::string content = "[Version] 2.0\n"
                                "# Hz S RI R 50\n"
                                "[Number of Ports] 2\n"
                                "[Reference] 75 100\n"
                                "[Network Data]\n"
                                "1.0 0.5 0.1 0.8 0.2 0.3 0.4 0.7 0.05\n"
                                "[End]\n";
    TempFileRAII temp_file(content, ".s2p");
    // act
    const auto result = touchstone_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    // check metadata contains per-port reference impedances
    const auto& metadata = result.value()->metadata();
    ASSERT_EQ(metadata.at("reference_impedance"), "75.000000");
    // check per-parameter metadata for reference impedance
    auto expressions = result.value()->expression_manager().expressions();
    ASSERT_EQ(expressions.size(), 5);
    // S11 metadata should have port 1 reference impedance (75)
    {
        auto* expr = expressions[1];
        ASSERT_TRUE(std::holds_alternative<Expression<std::complex<double>>>(*expr));
        auto& s11 = std::get<Expression<std::complex<double>>>(*expr);
        const auto& s11_meta = s11.metadata();
        ASSERT_EQ(s11_meta[0].at("reference_impedance"), "75.000000");
    }
    // S12 metadata should have port 1 reference impedance (75)
    {
        auto* expr = expressions[2];
        auto& s12 = std::get<Expression<std::complex<double>>>(*expr);
        const auto& s12_meta = s12.metadata();
        // S12 is row 0, col 1 → port index 0 (row)
        ASSERT_EQ(s12_meta[0].at("reference_impedance"), "75.000000");
    }
    // S22 metadata should have port 2 reference impedance (100)
    {
        auto* expr = expressions[4];
        auto& s22 = std::get<Expression<std::complex<double>>>(*expr);
        const auto& s22_meta = s22.metadata();
        // S22 is row 1, col 1 → port index 1
        ASSERT_EQ(s22_meta[0].at("reference_impedance"), "100.000000");
    }
}

TEST(TouchstoneFileParserChecks, parses_inline_comments_in_data) {
    // arrange
    const std::string content = "# Hz S RI R 50\n"
                                "1.0  0.5  0.1  0.8  0.2  0.3  0.4  0.7  0.05 ! inline comment\n"
                                "2.0  0.4  0.2  0.7  0.3  0.2  0.5  0.6  0.1\n";
    TempFileRAII temp_file(content, ".s2p");
    // act
    const auto result = touchstone_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    auto expressions = result.value()->expression_manager().expressions();
    ASSERT_EQ(expressions.size(), 5);
    auto& s11 = std::get<Expression<std::complex<double>>>(*expressions[1]);
    auto s11_data = s11.step_data(0);
    EXPECT_DOUBLE_EQ(s11_data[0].real(), 0.5);
    EXPECT_DOUBLE_EQ(s11_data[0].imag(), 0.1);
}

TEST(TouchstoneFileParserChecks, step_information_is_valid) {
    // arrange
    const std::string content = "# Hz S RI R 50\n"
                                "1.0  0.5  0.1  0.8  0.2  0.3  0.4  0.7  0.05\n"
                                "2.0  0.4  0.2  0.7  0.3  0.2  0.5  0.6  0.1\n"
                                "3.0  0.3  0.1  0.6  0.2  0.1  0.4  0.5  0.05\n";
    TempFileRAII temp_file(content, ".s2p");
    // act
    const auto result = touchstone_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    const auto& step_info = result.value()->step_information();
    // single step (no stepping in touchstone files)
    ASSERT_EQ(step_info.length(), 1);
    // check abscissa range
    EXPECT_DOUBLE_EQ(step_info.abscissa_left_value(), 1.0);
    EXPECT_DOUBLE_EQ(step_info.abscissa_right_value(), 3.0);
}

TEST(TouchstoneFileParserChecks, detects_logarithmic_scale) {
    // arrange — 2 points per decade: 1, sqrt(10), 10, 10*sqrt(10), 100
    std::ostringstream content;
    content << "# Hz S RI R 50\n";
    // compute exact sqrt(10) for precise logarithmic spacing
    const double s = std::sqrt(10.0);
    // generate 5 frequency points with exact log spacing
    for (double f : {1.0, s, 10.0, 10.0 * s, 100.0}) {
        content << std::setprecision(15) << f << " 0.5 0.1 0.8 0.2 0.3 0.4 0.7 0.05\n";
    }
    TempFileRAII temp_file(content.str(), ".s2p");
    // act
    const auto result = touchstone_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    // decade scale (10x per step)
    ASSERT_EQ(result.value()->abscissa_scale(), AbscissaScale::DECADE);
}

TEST(TouchstoneFileParserChecks, detects_linear_scale) {
    // arrange — linear 1, 2, 3, 4
    const std::string content = "# Hz S RI R 50\n"
                                "1.0  0.5  0.1  0.8  0.2  0.3  0.4  0.7  0.05\n"
                                "2.0  0.4  0.2  0.7  0.3  0.2  0.5  0.6  0.1\n"
                                "3.0  0.3  0.1  0.6  0.2  0.1  0.4  0.5  0.05\n"
                                "4.0  0.2  0.1  0.5  0.2  0.1  0.4  0.5  0.05\n";
    TempFileRAII temp_file(content, ".s2p");
    // act
    const auto result = touchstone_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    // linear scale
    ASSERT_EQ(result.value()->abscissa_scale(), AbscissaScale::LINEAR);
}

TEST(TouchstoneFileParserChecks, parses_y_parameters) {
    // arrange
    const std::string content = "# Hz Y RI R 50\n"
                                "1.0  0.5  0.1  0.8  0.2  0.3  0.4  0.7  0.05\n"
                                "2.0  0.4  0.2  0.7  0.3  0.2  0.5  0.6  0.1\n";
    TempFileRAII temp_file(content, ".s2p");
    // act
    const auto result = touchstone_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    // check metadata parameter type
    const auto& metadata = result.value()->metadata();
    ASSERT_EQ(metadata.at("parameter_type"), "Y");
    // check expression names use Y prefix
    auto names = result.value()->expression_manager().expression_names();
    ASSERT_EQ(names[1], "Y11");
    ASSERT_EQ(names[2], "Y12");
}

TEST(TouchstoneFileParserChecks, parses_z_parameters) {
    // arrange
    const std::string content = "# Hz Z RI R 50\n"
                                "1.0  0.5  0.1  0.8  0.2  0.3  0.4  0.7  0.05\n";
    TempFileRAII temp_file(content, ".s2p");
    // act
    const auto result = touchstone_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    const auto& metadata = result.value()->metadata();
    ASSERT_EQ(metadata.at("parameter_type"), "Z");
    auto names = result.value()->expression_manager().expression_names();
    ASSERT_EQ(names[1], "Z11");
}

TEST(TouchstoneFileParserChecks, parses_v2_file_with_declared_frequency_count) {
    // arrange — v2.0 bracket headers with a declared frequency count and decade-spaced
    // data rows (values taken from a real Xyce LIN sweep of an RC low-pass filter)
    const std::string content = "[Version] 2.0\n"
                                "# Hz S RI R 50\n"
                                "[Number of Ports] 2\n"
                                "[Two-Port Data Order] 12_21\n"
                                "[Number of Frequencies] 5\n"
                                "[Reference] 50 50\n"
                                "[Network Data]\n"
                                "!      Freq              ReS11             ImS11             ReS12             ImS12             ReS21             ImS21             ReS22             ImS22\n"
                                "   1.00000000e+00    9.99999210e-01   -6.28317787e-04    7.89567729e-07    6.28317787e-04    7.89567729e-07    6.28317787e-04    9.99999210e-01   -6.28317787e-04\n"
                                "   3.16227766e+00    9.99992104e-01   -1.98689412e-03    7.89562118e-06    1.98689412e-03    7.89562118e-06    1.98689412e-03    9.99992104e-01   -1.98689412e-03\n"
                                "   1.00000000e+01    9.99921049e-01   -6.28244121e-03    7.89506014e-05    6.28244121e-03    7.89506014e-05    6.28244121e-03    9.99921049e-01   -6.28244121e-03\n"
                                "   3.16227766e+01    9.99211055e-01   -1.98456599e-02    7.88945303e-04    1.98456599e-02    7.88945303e-04    1.98456599e-02    9.99211055e-01   -1.98456599e-02\n"
                                "   1.00000000e+02    9.92166291e-01   -6.20925718e-02    7.83370894e-03    6.20925718e-02    7.83370894e-03    6.20925718e-02    9.92166291e-01   -6.20925718e-02\n"
                                "[End]\n";
    TempFileRAII temp_file(content);
    // act
    const auto result = touchstone_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    // check metadata
    const auto& metadata = result.value()->metadata();
    ASSERT_EQ(metadata.at("parameter_type"), "S");
    ASSERT_EQ(metadata.at("data_format"), "RI");
    // check expression count (frequency + 4 s-parameters)
    auto expressions = result.value()->expression_manager().expressions();
    ASSERT_EQ(expressions.size(), 5);
    // check frequency
    auto& freq_expr = result.value()->expression_manager().abscissa();
    auto freq_data = freq_expr.step_data(0);
    // [Number of Frequencies] declares 5 points
    ASSERT_EQ(freq_data.size(), 5);
    // first frequency is 1 Hz
    EXPECT_DOUBLE_EQ(freq_data[0], 1.0);
    // last frequency is 100 Hz
    EXPECT_DOUBLE_EQ(freq_data[4], 100.0);
    // check S-parameters are non-zero (the whole point of using Touchstone instead of raw file)
    auto& s11 = std::get<Expression<std::complex<double>>>(*expressions[1]);
    auto s11_data = s11.step_data(0);
    // S11 at 1 Hz should be near 1.0 (matching the circuit)
    EXPECT_NEAR(s11_data[0].real(), 0.99999, 1e-4);
    // S21 at 1 Hz should be near 0 (matching the circuit)
    auto& s21 = std::get<Expression<std::complex<double>>>(*expressions[3]);
    auto s21_data = s21.step_data(0);
    EXPECT_NEAR(s21_data[0].real(), 7.89567729e-07, 1e-10);
    // check abscissa scale is decade (log-spaced frequencies)
    ASSERT_EQ(result.value()->abscissa_scale(), AbscissaScale::DECADE);
}

TEST(TouchstoneFileParserChecks, suggested_plots_split_reflection_and_transmission_for_2port) {
    // arrange
    const std::string content = "# Hz S RI R 50\n"
                                "1.0  0.5  0.1  0.8  0.2  0.3  0.4  0.7  0.05\n";
    TempFileRAII temp_file(content, ".s2p");
    // act
    const auto result = touchstone_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    // two charts: reflection (diagonal, return loss) and transmission
    // (off-diagonal, insertion loss)
    const auto& suggested = result.value()->suggested_plots();
    ASSERT_EQ(suggested.size(), 2);
    const std::vector<std::vector<std::string>> expected = {{"db(S11)", "db(S22)"}, {"db(S12)", "db(S21)"}};
    ASSERT_EQ(suggested, expected);
}

TEST(TouchstoneFileParserChecks, suggested_plots_split_reflection_and_transmission_for_3port) {
    // arrange
    const std::string content = "[Version] 2.0\n"
                                "# Hz S RI R 50\n"
                                "[Number of Ports] 3\n"
                                "[Number of Frequencies] 1\n"
                                "[Reference] 50 50 50\n"
                                "[Network Data]\n"
                                "1.0 0.5 0.1 0.2 0.0 0.3 0.0 0.8 0.2 0.7 0.05 0.2 0.0 0.1 0.0 0.4 0.0 0.6 0.05\n"
                                "[End]\n";
    TempFileRAII temp_file(content, ".s3p");
    // act
    const auto result = touchstone_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    // diagonal entries form the reflection chart, the row-major off-diagonal
    // entries the transmission chart
    const auto& suggested = result.value()->suggested_plots();
    ASSERT_EQ(suggested.size(), 2);
    const std::vector<std::vector<std::string>> expected = {
        {"db(S11)", "db(S22)", "db(S33)"},
        {"db(S12)", "db(S13)", "db(S21)", "db(S23)", "db(S31)", "db(S32)"},
    };
    ASSERT_EQ(suggested, expected);
}

TEST(TouchstoneFileParserChecks, suggested_plots_follow_the_parameter_prefix) {
    // arrange
    const std::string content = "# Hz Y RI R 50\n"
                                "1.0  0.5  0.1  0.8  0.2  0.3  0.4  0.7  0.05\n";
    TempFileRAII temp_file(content, ".s2p");
    // act
    const auto result = touchstone_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    // the Y-parameter file produces the equivalent groups with the Y prefix
    const auto& suggested = result.value()->suggested_plots();
    ASSERT_EQ(suggested.size(), 2);
    const std::vector<std::vector<std::string>> expected = {{"db(Y11)", "db(Y22)"}, {"db(Y12)", "db(Y21)"}};
    ASSERT_EQ(suggested, expected);
}

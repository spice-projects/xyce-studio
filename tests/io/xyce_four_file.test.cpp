#include <chrono>
#include <complex>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "core/step_information.h"
#include "expression/expression.h"
#include "expression/expression_manager.h"
#include "io/xyce_four_file.h"
#include "io/xyce_output_file.h"

namespace
{

    // temporary directory manager for four test files
    class TempDirRAII
    {
    public:
        TempDirRAII() {
            static int counter = 0;
            // build a unique directory path
            m_path = std::filesystem::temp_directory_path() / ("test_xyce_four_gtest_" + std::to_string(counter++) + "_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
            // create the directory
            std::filesystem::create_directories(m_path);
        }

        // destructor removes the temporary directory
        ~TempDirRAII() {
            // remove the temporary directory
            std::filesystem::remove_all(m_path);
        }

        // write a file with the given content
        void write_file(const std::string& name, const std::string& content) const {
            // build the full path
            const std::filesystem::path file_path = m_path / name;
            // open the file stream
            std::ofstream out(file_path, std::ios::binary);
            // write the content
            out.write(content.data(), static_cast<std::streamsize>(content.size()));
        }

        // write raw bytes to a file
        void write_bytes(const std::string& name, const std::vector<char>& content) const {
            // build the full path
            const std::filesystem::path file_path = m_path / name;
            // open the file stream
            std::ofstream out(file_path, std::ios::binary);
            // write the bytes
            out.write(content.data(), static_cast<std::streamsize>(content.size()));
        }

        // directory path getter
        [[nodiscard]] const std::filesystem::path& path() const { return m_path; }

    private:
        // directory path field
        std::filesystem::path m_path;
    };

    // single Fourier harmonic row
    struct DataRow
    {
        // harmonic index field
        std::string index;
        // frequency field
        std::string frequency;
        // magnitude field
        std::string magnitude;
        // phase field in degrees
        std::string phase;
    };

    // build the content of one Fourier analysis block, terminated by a blank line
    std::string make_four_content(const std::string& signal_name, const std::vector<DataRow>& rows, const std::string& thd = "1.000000e-05", const std::string& gridsize = "200", const std::string& interpolation = "Cubic Spline") {
        // result
        std::string content;
        // append the signal header
        content += "Fourier analysis for " + signal_name + ":\n";
        // append the metadata line
        content += "  No. Harmonics: " + std::to_string(rows.size()) + ", THD: " + thd + ", Gridsize: " + gridsize + ", Interpolation Type: " + interpolation + "\n";
        // append the column header
        content += "    Harmonic       Frequency       Magnitude           Phase       Norm. Mag     Norm. Phase\n";
        // append the harmonic rows
        for (const auto& row : rows)
            content += "   " + row.index + "    " + row.frequency + "    " + row.magnitude + "    " + row.phase + "    0.000000e+00    0.000000e+00\n";
        // append the blank line terminating the block
        content += "\n";
        // return the content
        return content;
    }

    // build the step information with the given number of steps
    StepInformation make_step_information(const size_t length) {
        // value ranges
        std::vector<std::pair<double, double>> ranges;
        // reserve capacity
        ranges.reserve(length);
        for (size_t i = 0; i < length; ++i)
            ranges.emplace_back(0.0, 100.0);
        // create the step information
        return StepInformation({}, {}, std::move(ranges));
    }

    // evaluate an expression in the manager as a complex expression
    Expression<std::complex<double>>* evaluate_complex(ExpressionManager& manager, const std::string& name) {
        // evaluate the expression
        AnyExpression* expression = manager.evaluate(name);
        // cast to a complex expression
        return expression ? std::get_if<Expression<std::complex<double>>>(expression) : nullptr;
    }

    // the default block: dc component, fundamental at 1 kHz and a second harmonic
    const std::vector<DataRow> HARMONICS_1K = {{"0", "0.000000e+00", "0.000000e+00", "0.000000e+00"}, {"1", "1.000000e+03", "1.000000e+00", "0.000000e+00"}, {"2", "2.000000e+03", "2.000000e-01", "9.000000e+01"}};

} // namespace

// ========================================================================================
// file matching and loading failures
// ========================================================================================

TEST(XyceFourFileTest, returns_nullopt_when_no_files_match_pattern) {
    // arrange
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_four_file_parser("/nonexistent/path/*.four*", &step_info);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceFourFileTest, returns_nullopt_when_pattern_has_no_directory_separator) {
    // arrange — a bare file name pattern globs the current directory
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_four_file_parser("__xyce_four_absent_pattern_zzz*.four0", &step_info);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceFourFileTest, matches_a_question_mark_wildcard) {
    // arrange — the question mark matches exactly one character
    const TempDirRAII tmp;
    tmp.write_file("wild.four1", make_four_content("V(OUT)", HARMONICS_1K));
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "wild.four?", &step_info);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
}

TEST(XyceFourFileTest, returns_nullopt_when_pattern_is_longer_than_the_file_name) {
    // arrange — the text is consumed before the pattern is exhausted
    const TempDirRAII tmp;
    tmp.write_file("trail.four", make_four_content("V(OUT)", HARMONICS_1K));
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "trail.fourX", &step_info);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceFourFileTest, returns_empty_vector_for_empty_file_without_step_information) {
    // arrange — an empty file with no analysis step information leaves no signals
    const TempDirRAII tmp;
    tmp.write_file("empty_no_step.four0", "");
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "empty_no_step.four*");
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->empty());
}

TEST(XyceFourFileTest, returns_nullopt_when_harmonic_line_has_wrong_column_count) {
    // arrange
    const TempDirRAII tmp;
    // build a block whose first harmonic row is missing columns
    std::string content = "Fourier analysis for V(OUT):\n  No. Harmonics: 3, THD: 1.000000e-05, Gridsize: 200, Interpolation Type: Cubic Spline\n    Harmonic       Frequency       Magnitude           Phase       Norm. Mag     Norm. Phase\n           0    0.000000e+00    0.000000e+00\n";
    tmp.write_file("bad_cols.four0", content);
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "bad_cols.four*", &step_info);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceFourFileTest, returns_nullopt_when_harmonic_index_is_out_of_order) {
    // arrange
    const TempDirRAII tmp;
    const std::vector<DataRow> rows = {{"0", "0.000000e+00", "0.000000e+00", "0.000000e+00"}, {"2", "2.000000e+03", "2.000000e-01", "0.000000e+00"}};
    tmp.write_file("bad_index.four0", make_four_content("V(OUT)", rows));
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "bad_index.four*", &step_info);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceFourFileTest, returns_nullopt_when_column_header_appears_before_signal_header) {
    // arrange
    const TempDirRAII tmp;
    const std::string content = "  No. Harmonics: 3, THD: 1.000000e-05, Gridsize: 200, Interpolation Type: Cubic Spline\n    Harmonic       Frequency       Magnitude           Phase       Norm. Mag     Norm. Phase\n           0    0.000000e+00    0.000000e+00    0.000000e+00    0.000000e+00    0.000000e+00\n";
    tmp.write_file("bad_order.four0", content);
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "bad_order.four*", &step_info);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceFourFileTest, returns_nullopt_when_column_header_appears_without_metadata) {
    // arrange — a column header directly after the signal header has no metadata
    const TempDirRAII tmp;
    const std::string content = "Fourier analysis for V(OUT):\n    Harmonic       Frequency       Magnitude           Phase       Norm. Mag     Norm. Phase\n           0    0.000000e+00    0.000000e+00    0.000000e+00    0.000000e+00    0.000000e+00\n";
    tmp.write_file("no_metadata.four0", content);
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "no_metadata.four*", &step_info);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceFourFileTest, returns_empty_vector_when_only_a_signal_header_is_present) {
    // arrange — a dangling signal header without metadata or data is discarded
    const TempDirRAII tmp;
    tmp.write_file("header_only.four0", "Fourier analysis for V(OUT):\n");
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "header_only.four*", &step_info);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->empty());
}

TEST(XyceFourFileTest, returns_nullopt_when_step_count_mismatches_step_information) {
    // arrange
    const TempDirRAII tmp;
    tmp.write_file("mismatch.four0", make_four_content("V(OUT)", HARMONICS_1K));
    const StepInformation step_info = make_step_information(2);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "mismatch.four*", &step_info);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceFourFileTest, returns_nullopt_when_numeric_field_is_invalid) {
    // arrange
    const TempDirRAII tmp;
    // the row carries six columns but a non numeric magnitude
    const std::string content = "Fourier analysis for V(OUT):\n  No. Harmonics: 1, THD: 1.000000e-05, Gridsize: 200, Interpolation Type: Cubic Spline\n    Harmonic       Frequency       Magnitude           Phase       Norm. Mag     Norm. Phase\n           0    0.000000e+00    not_a_number    0.000000e+00    0.000000e+00    0.000000e+00\n";
    tmp.write_file("bad_number.four0", content);
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "bad_number.four*", &step_info);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceFourFileTest, returns_nullopt_when_file_has_invalid_utf8_bytes) {
    // arrange
    const TempDirRAII tmp;
    const std::string header = make_four_content("V(OUT)", HARMONICS_1K);
    std::vector<char> bytes(header.begin(), header.end());
    // append invalid utf8 bytes
    bytes.push_back(static_cast<char>(0xFF));
    bytes.push_back(static_cast<char>(0xFE));
    bytes.insert(bytes.end(), {' ', 'b', 'a', 'd', '\n'});
    tmp.write_bytes("bad_encoding.four0", bytes);
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "bad_encoding.four*", &step_info);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceFourFileTest, returns_empty_vector_when_file_is_empty) {
    // arrange
    const TempDirRAII tmp;
    tmp.write_file("empty.four0", "");
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "empty.four*", &step_info);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->empty());
}

// ========================================================================================
// basic four parsing
// ========================================================================================

TEST(XyceFourFileTest, parses_single_file_with_one_signal) {
    // arrange
    const TempDirRAII tmp;
    tmp.write_file("sim.four0", make_four_content("V(OUT)", HARMONICS_1K));
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "sim.four*", &step_info);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
    const auto& output_file = result->front();
    ASSERT_EQ(output_file->step_information().length(), 1);
    ASSERT_EQ(output_file->abscissa().step_count(), 1);
    ASSERT_EQ(output_file->abscissa().step_data(0).size(), 3);
    ASSERT_DOUBLE_EQ(output_file->abscissa().step_data(0)[0], 0.0);
    ASSERT_DOUBLE_EQ(output_file->abscissa().step_data(0)[1], 1000.0);
    ASSERT_DOUBLE_EQ(output_file->abscissa().step_data(0)[2], 2000.0);
    auto* spectrum = evaluate_complex(output_file->expression_manager(), "V(OUT)");
    ASSERT_NE(spectrum, nullptr);
    ASSERT_EQ(spectrum->step_count(), 1);
    ASSERT_EQ(spectrum->step_data(0).size(), 3);
    ASSERT_NEAR(spectrum->step_data(0)[0].real(), 0.0, 1e-12);
    ASSERT_NEAR(spectrum->step_data(0)[0].imag(), 0.0, 1e-12);
    ASSERT_NEAR(spectrum->step_data(0)[1].real(), 1.0, 1e-12);
    ASSERT_NEAR(spectrum->step_data(0)[1].imag(), 0.0, 1e-12);
}

TEST(XyceFourFileTest, stores_the_analysis_metadata_per_signal_and_step) {
    // arrange
    const TempDirRAII tmp;
    tmp.write_file("metadata.four0", make_four_content("V(OUT)", HARMONICS_1K, "9.006060e-05", "200", "Cubic Spline"));
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "metadata.four*", &step_info);
    // assert
    ASSERT_TRUE(result.has_value());
    auto* spectrum = evaluate_complex(result->front()->expression_manager(), "V(OUT)");
    ASSERT_NE(spectrum, nullptr);
    ASSERT_EQ(spectrum->metadata().size(), 1);
    const auto& step_metadata = spectrum->metadata()[0];
    ASSERT_EQ(step_metadata.at("THD"), "9.006060e-05");
    ASSERT_EQ(step_metadata.at("Gridsize"), "200");
    ASSERT_EQ(step_metadata.at("Interpolation Type"), "Cubic Spline");
    ASSERT_EQ(step_metadata.at("No. Harmonics"), "3");
}

TEST(XyceFourFileTest, parses_a_non_finite_thd_token) {
    // arrange — Xyce writes `nan` for a block whose fundamental vanishes
    const TempDirRAII tmp;
    tmp.write_file("nan-thd.four0", make_four_content("V(IN)", HARMONICS_1K, "nan"));
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "nan-thd.four*", &step_info);
    // assert — the block parses and the raw token is preserved as metadata
    ASSERT_TRUE(result.has_value());
    auto* spectrum = evaluate_complex(result->front()->expression_manager(), "V(IN)");
    ASSERT_NE(spectrum, nullptr);
    ASSERT_EQ(spectrum->metadata()[0].at("THD"), "nan");
}

TEST(XyceFourFileTest, parses_multiple_signals_in_one_fundamental) {
    // arrange
    const TempDirRAII tmp;
    const std::vector<DataRow> second = {{"0", "0.000000e+00", "0.000000e+00", "0.000000e+00"}, {"1", "1.000000e+03", "5.000000e-01", "0.000000e+00"}, {"2", "2.000000e+03", "1.000000e-01", "9.000000e+01"}};
    tmp.write_file("multi.four0", make_four_content("V(OUT)", HARMONICS_1K) + make_four_content("I(R1)", second));
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "multi.four*", &step_info);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
    auto& manager = result->front()->expression_manager();
    ASSERT_NE(evaluate_complex(manager, "V(OUT)"), nullptr);
    auto* current = evaluate_complex(manager, "I(R1)");
    ASSERT_NE(current, nullptr);
    ASSERT_NEAR(current->step_data(0)[1].real(), 0.5, 1e-12);
    ASSERT_NEAR(current->step_data(0)[2].imag(), 0.1, 1e-12);
}

TEST(XyceFourFileTest, parses_multiple_fundamentals_into_separate_output_files) {
    // arrange
    const TempDirRAII tmp;
    const std::vector<DataRow> second = {{"0", "0.000000e+00", "0.000000e+00", "0.000000e+00"}, {"1", "2.000000e+03", "4.000000e-01", "0.000000e+00"}, {"2", "4.000000e+03", "2.000000e-01", "0.000000e+00"}};
    tmp.write_file("fundamentals.four0", make_four_content("V(OUT)", HARMONICS_1K) + make_four_content("V(OUT)", second));
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "fundamentals.four*", &step_info);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 2);
    ASSERT_EQ(result->at(0)->metadata().at("Fundamental Frequency"), "1000.000000");
    ASSERT_EQ(result->at(1)->metadata().at("Fundamental Frequency"), "2000.000000");
}

TEST(XyceFourFileTest, converts_magnitude_and_phase_degrees_to_complex) {
    // arrange
    const TempDirRAII tmp;
    const std::vector<DataRow> rows = {{"0", "0.000000e+00", "-5.000000e-01", "0.000000e+00"}, {"1", "1.000000e+03", "2.000000e+00", "9.000000e+01"}, {"2", "2.000000e+03", "1.500000e+00", "1.800000e+02"}};
    tmp.write_file("complex.four0", make_four_content("V(OUT)", rows));
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "complex.four*", &step_info);
    // assert
    ASSERT_TRUE(result.has_value());
    auto* spectrum = evaluate_complex(result->front()->expression_manager(), "V(OUT)");
    ASSERT_NE(spectrum, nullptr);
    // dc component keeps the signed magnitude as its real part
    ASSERT_NEAR(spectrum->step_data(0)[0].real(), -0.5, 1e-12);
    ASSERT_NEAR(spectrum->step_data(0)[0].imag(), 0.0, 1e-12);
    // ninety degrees maps to the imaginary axis
    ASSERT_NEAR(spectrum->step_data(0)[1].real(), 0.0, 1e-12);
    ASSERT_NEAR(spectrum->step_data(0)[1].imag(), 2.0, 1e-12);
    // one hundred and eighty degrees negates the magnitude
    ASSERT_NEAR(spectrum->step_data(0)[2].real(), -1.5, 1e-12);
    ASSERT_NEAR(spectrum->step_data(0)[2].imag(), 0.0, 1e-12);
}

TEST(XyceFourFileTest, parses_file_without_four_index_suffix) {
    // arrange
    const TempDirRAII tmp;
    tmp.write_file("sim.four", make_four_content("V(OUT)", HARMONICS_1K));
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "sim.four*", &step_info);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
}

TEST(XyceFourFileTest, file_without_trailing_newline_is_handled) {
    // arrange
    const TempDirRAII tmp;
    std::string content = make_four_content("V(OUT)", HARMONICS_1K);
    // strip the trailing blank line and newline so the last row ends the file
    content.pop_back();
    content.pop_back();
    tmp.write_file("no_newline.four0", content);
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "no_newline.four*", &step_info);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
    auto* spectrum = evaluate_complex(result->front()->expression_manager(), "V(OUT)");
    ASSERT_NE(spectrum, nullptr);
    ASSERT_EQ(spectrum->step_data(0).size(), 3);
}

TEST(XyceFourFileTest, parses_file_with_unexpected_line) {
    // arrange
    const TempDirRAII tmp;
    std::string content = make_four_content("V(OUT)", HARMONICS_1K);
    // insert an unexpected line before the column header
    const std::string marker = "    Harmonic       Frequency";
    content.insert(content.find(marker), "THIS IS AN UNEXPECTED LINE\n");
    tmp.write_file("unexpected.four0", content);
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "unexpected.four*", &step_info);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
}

TEST(XyceFourFileTest, ignores_a_leading_blank_line) {
    // arrange — a blank line before the first block is skipped
    const TempDirRAII tmp;
    tmp.write_file("leading_blank.four0", "\n" + make_four_content("V(OUT)", HARMONICS_1K));
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "leading_blank.four*", &step_info);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
}

TEST(XyceFourFileTest, parses_single_harmonic_block) {
    // arrange — a block carrying only the dc component has no fundamental
    const TempDirRAII tmp;
    const std::vector<DataRow> rows = {{"0", "0.000000e+00", "1.000000e+00", "0.000000e+00"}};
    tmp.write_file("single_harmonic.four0", make_four_content("V(OUT)", rows));
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "single_harmonic.four*", &step_info);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
    ASSERT_EQ(result->front()->metadata().at("Fundamental Frequency"), "0.000000");
}

TEST(XyceFourFileTest, parses_signal_with_utf8_name) {
    // arrange — the omega sign is a two byte utf8 sequence
    const TempDirRAII tmp;
    tmp.write_file("utf8.four0", make_four_content("V(\xCE\xA9)", HARMONICS_1K));
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "utf8.four*", &step_info);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_NE(evaluate_complex(result->front()->expression_manager(), "V(\xCE\xA9)"), nullptr);
}

TEST(XyceFourFileTest, parses_signal_with_three_and_four_byte_utf8_names) {
    // arrange — the euro sign is three bytes and the emoji is four bytes
    const TempDirRAII tmp;
    tmp.write_file("utf8_long.four0", make_four_content("V(\xE2\x82\xAC)", HARMONICS_1K) + make_four_content("V(\xF0\x9F\x98\x80)", HARMONICS_1K));
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "utf8_long.four*", &step_info);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_NE(evaluate_complex(result->front()->expression_manager(), "V(\xE2\x82\xAC)"), nullptr);
    ASSERT_NE(evaluate_complex(result->front()->expression_manager(), "V(\xF0\x9F\x98\x80)"), nullptr);
}

TEST(XyceFourFileTest, returns_nullopt_when_two_byte_utf8_has_invalid_continuation) {
    // arrange — a two byte lead followed by a non continuation byte
    const TempDirRAII tmp;
    tmp.write_bytes("bad_two.four0", {static_cast<char>(0xC3), 'A', '\n'});
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "bad_two.four*", &step_info);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceFourFileTest, returns_nullopt_when_two_byte_utf8_is_truncated) {
    // arrange — a two byte lead at the end of the line has no continuation byte
    const TempDirRAII tmp;
    tmp.write_bytes("trunc_two.four0", {static_cast<char>(0xC3), '\n'});
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "trunc_two.four*", &step_info);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceFourFileTest, returns_nullopt_when_three_byte_utf8_has_invalid_continuation) {
    // arrange — a three byte lead followed by a bad second continuation byte
    const TempDirRAII tmp;
    tmp.write_bytes("bad_three.four0", {static_cast<char>(0xE2), static_cast<char>(0x82), 'A', '\n'});
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "bad_three.four*", &step_info);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceFourFileTest, returns_nullopt_when_three_byte_utf8_has_invalid_first_continuation) {
    // arrange — a three byte lead followed by a bad first continuation byte
    const TempDirRAII tmp;
    tmp.write_bytes("bad_three_first.four0", {static_cast<char>(0xE2), 'A', static_cast<char>(0x80), '\n'});
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "bad_three_first.four*", &step_info);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceFourFileTest, returns_nullopt_when_three_byte_utf8_is_truncated) {
    // arrange — a three byte lead with only one continuation byte before the newline
    const TempDirRAII tmp;
    tmp.write_bytes("trunc_three.four0", {static_cast<char>(0xE2), static_cast<char>(0x82), '\n'});
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "trunc_three.four*", &step_info);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceFourFileTest, returns_nullopt_when_four_byte_utf8_has_invalid_continuation) {
    // arrange — a four byte lead followed by a bad fourth continuation byte
    const TempDirRAII tmp;
    tmp.write_bytes("bad_four.four0", {static_cast<char>(0xF0), static_cast<char>(0x9F), static_cast<char>(0x98), 'A', '\n'});
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "bad_four.four*", &step_info);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceFourFileTest, returns_nullopt_when_four_byte_utf8_has_invalid_first_continuation) {
    // arrange — a four byte lead followed by a bad first continuation byte
    const TempDirRAII tmp;
    tmp.write_bytes("bad_four_first.four0", {static_cast<char>(0xF0), 'A', static_cast<char>(0x80), static_cast<char>(0x80), '\n'});
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "bad_four_first.four*", &step_info);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceFourFileTest, returns_nullopt_when_four_byte_utf8_has_invalid_second_continuation) {
    // arrange — a four byte lead followed by a bad second continuation byte
    const TempDirRAII tmp;
    tmp.write_bytes("bad_four_second.four0", {static_cast<char>(0xF0), static_cast<char>(0x9F), 'A', static_cast<char>(0x80), '\n'});
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "bad_four_second.four*", &step_info);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceFourFileTest, returns_nullopt_when_four_byte_utf8_is_truncated) {
    // arrange — a four byte lead with two continuation bytes before the newline
    const TempDirRAII tmp;
    tmp.write_bytes("trunc_four.four0", {static_cast<char>(0xF0), static_cast<char>(0x9F), static_cast<char>(0x98), '\n'});
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "trunc_four.four*", &step_info);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceFourFileTest, matches_only_the_exact_file_name) {
    // arrange — a non wildcard pattern must reject the unrelated file in the directory
    const TempDirRAII tmp;
    tmp.write_file("exact.four0", make_four_content("V(OUT)", HARMONICS_1K));
    tmp.write_file("other.txt", "not a four file");
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "exact.four0", &step_info);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
}

TEST(XyceFourFileTest, skips_subdirectories_when_globbing) {
    // arrange — a subdirectory sharing the file name pattern must be ignored
    const TempDirRAII tmp;
    tmp.write_file("glob.four0", make_four_content("V(OUT)", HARMONICS_1K));
    std::filesystem::create_directories(tmp.path() / "glob.four1");
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "glob.four*", &step_info);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
}

TEST(XyceFourFileTest, sorts_files_mixing_indexed_and_unindexed_names) {
    // arrange — the unindexed file sorts before the indexed one
    const TempDirRAII tmp;
    tmp.write_file("mix.four", make_four_content("V(OUT)", HARMONICS_1K));
    tmp.write_file("mix.four0", make_four_content("V(OUT)", HARMONICS_1K));
    const StepInformation step_info = make_step_information(2);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "mix.four*", &step_info);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
    ASSERT_EQ(result->front()->step_information().length(), 2);
}

TEST(XyceFourFileTest, returns_nullopt_when_file_cannot_be_mapped) {
    // arrange — the file exists but read permission is withdrawn so the mapping fails
    const TempDirRAII tmp;
    const std::filesystem::path file_path = tmp.path() / "unreadable.four0";
    tmp.write_file("unreadable.four0", make_four_content("V(OUT)", HARMONICS_1K));
    std::error_code ec;
    // remove all permissions
    std::filesystem::permissions(file_path, std::filesystem::perms::none, ec);
    // probe whether the platform really denies the read
    {
        std::ifstream probe(file_path);
        if (probe.is_open()) {
            // restore the permissions so the directory can be removed
            std::filesystem::permissions(file_path, std::filesystem::perms::all, ec);
            GTEST_SKIP() << "platform does not deny reads on permission-none files";
        }
    }
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "unreadable.four*", &step_info);
    // assert
    ASSERT_FALSE(result.has_value());
    // restore the permissions so the destructor can remove the directory
    std::filesystem::permissions(file_path, std::filesystem::perms::all, ec);
}

// ========================================================================================
// multi step four parsing
// ========================================================================================

TEST(XyceFourFileTest, parses_multiple_step_files_and_accumulates_steps) {
    // arrange
    const TempDirRAII tmp;
    const std::vector<DataRow> first = {{"0", "0.000000e+00", "0.000000e+00", "0.000000e+00"}, {"1", "1.000000e+03", "1.000000e+00", "0.000000e+00"}, {"2", "2.000000e+03", "2.000000e-01", "9.000000e+01"}};
    const std::vector<DataRow> second = {{"0", "0.000000e+00", "0.000000e+00", "0.000000e+00"}, {"1", "1.000000e+03", "8.000000e-01", "0.000000e+00"}, {"2", "2.000000e+03", "4.000000e-01", "0.000000e+00"}};
    tmp.write_file("step.four0", make_four_content("V(OUT)", first, "1.000000e-05"));
    tmp.write_file("step.four1", make_four_content("V(OUT)", second, "2.000000e-05"));
    const StepInformation step_info = make_step_information(2);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "step.four*", &step_info);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
    const auto& output_file = result->front();
    ASSERT_EQ(output_file->step_information().length(), 2);
    auto* spectrum = evaluate_complex(output_file->expression_manager(), "V(OUT)");
    ASSERT_NE(spectrum, nullptr);
    ASSERT_EQ(spectrum->step_count(), 2);
    ASSERT_NEAR(spectrum->step_data(0)[1].real(), 1.0, 1e-12);
    ASSERT_NEAR(spectrum->step_data(1)[1].real(), 0.8, 1e-12);
    ASSERT_NEAR(spectrum->step_data(1)[2].real(), 0.4, 1e-12);
    ASSERT_EQ(spectrum->metadata()[0].at("THD"), "1.000000e-05");
    ASSERT_EQ(spectrum->metadata()[1].at("THD"), "2.000000e-05");
}

TEST(XyceFourFileTest, sorts_step_files_by_numeric_suffix) {
    // arrange
    const TempDirRAII tmp;
    const std::vector<DataRow> first = {{"0", "0.000000e+00", "0.000000e+00", "0.000000e+00"}, {"1", "1.000000e+03", "1.000000e-01", "0.000000e+00"}};
    const std::vector<DataRow> second = {{"0", "0.000000e+00", "0.000000e+00", "0.000000e+00"}, {"1", "1.000000e+03", "2.000000e-01", "0.000000e+00"}};
    const std::vector<DataRow> third = {{"0", "0.000000e+00", "0.000000e+00", "0.000000e+00"}, {"1", "1.000000e+03", "3.000000e-01", "0.000000e+00"}};
    tmp.write_file("sorted.four1", make_four_content("V(OUT)", first));
    tmp.write_file("sorted.four2", make_four_content("V(OUT)", second));
    tmp.write_file("sorted.four10", make_four_content("V(OUT)", third));
    const StepInformation step_info = make_step_information(3);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "sorted.four*", &step_info);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
    auto* spectrum = evaluate_complex(result->front()->expression_manager(), "V(OUT)");
    ASSERT_NE(spectrum, nullptr);
    ASSERT_EQ(spectrum->step_count(), 3);
    ASSERT_NEAR(spectrum->step_data(0)[1].real(), 0.1, 1e-12);
    ASSERT_NEAR(spectrum->step_data(1)[1].real(), 0.2, 1e-12);
    ASSERT_NEAR(spectrum->step_data(2)[1].real(), 0.3, 1e-12);
}

TEST(XyceFourFileTest, parses_without_step_information) {
    // arrange — a run without a .PRINT directive produces no analysis output,
    // so there is no step information to map the FOUR files onto
    const TempDirRAII tmp;
    tmp.write_file("no_print.four0", make_four_content("V(OUT)", HARMONICS_1K));
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "no_print.four*");
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
    ASSERT_TRUE(result->front()->step_information().keys().empty());
    ASSERT_TRUE(result->front()->step_information().values().empty());
    auto* spectrum = evaluate_complex(result->front()->expression_manager(), "V(OUT)");
    ASSERT_NE(spectrum, nullptr);
    ASSERT_EQ(spectrum->step_count(), 1);
}

// ========================================================================================
// output file structure and unit inference
// ========================================================================================

TEST(XyceFourFileTest, output_file_contains_metadata) {
    // arrange
    const TempDirRAII tmp;
    tmp.write_file("sim.four0", make_four_content("V(OUT)", HARMONICS_1K));
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "sim.four*", &step_info);
    // assert
    ASSERT_TRUE(result.has_value());
    const auto& output_file = result->front();
    ASSERT_EQ(output_file->title(), "FOUR: 1000 Hz");
    ASSERT_EQ(output_file->filename(), tmp.path() / "sim.four0");
    ASSERT_TRUE(output_file->is_complex());
    ASSERT_EQ(output_file->abscissa_scale(), AbscissaScale::LINEAR);
    ASSERT_EQ(output_file->plot_type(), PlotType::FOURIER);
    ASSERT_EQ(output_file->abscissa().name(), "frequency");
    ASSERT_EQ(output_file->abscissa().unit(), "Hz");
    ASSERT_EQ(output_file->abscissa().source(), "FOUR");
    ASSERT_EQ(output_file->metadata().at("Fundamental Frequency"), "1000.000000");
    ASSERT_EQ(output_file->metadata().at("No. Harmonics"), "3");
}

TEST(XyceFourFileTest, phase_expression_uses_inferred_unit) {
    // arrange
    const TempDirRAII tmp;
    tmp.write_file("infer_unit.four0", make_four_content("V(OUT)", HARMONICS_1K));
    const StepInformation step_info = make_step_information(1);
    // create a raw expression manager with a known unit
    std::vector<double> raw_data = {0.0, 1.0};
    std::vector<std::span<const double>> raw_steps = {{raw_data.data(), raw_data.size()}};
    std::vector<AnyExpression> raw_expressions;
    raw_expressions.emplace_back(Expression<double>("V(OUT)", std::move(raw_data), std::move(raw_steps), "mV"));
    std::vector<std::pair<size_t, size_t>> raw_slices = {{0, 2}};
    ExpressionManager raw_manager(raw_expressions, raw_slices);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "infer_unit.four*", &step_info, &raw_manager);
    // assert
    ASSERT_TRUE(result.has_value());
    auto* spectrum = evaluate_complex(result->front()->expression_manager(), "V(OUT)");
    ASSERT_NE(spectrum, nullptr);
    ASSERT_EQ(spectrum->unit(), "mV");
}

TEST(XyceFourFileTest, strips_braces_before_unit_inference) {
    // arrange
    const TempDirRAII tmp;
    tmp.write_file("strip_braces.four0", make_four_content("{V(OUT)}", HARMONICS_1K));
    const StepInformation step_info = make_step_information(1);
    // create a raw expression manager with a known unit
    std::vector<double> raw_data = {0.0, 1.0};
    std::vector<std::span<const double>> raw_steps = {{raw_data.data(), raw_data.size()}};
    std::vector<AnyExpression> raw_expressions;
    raw_expressions.emplace_back(Expression<double>("V(OUT)", std::move(raw_data), std::move(raw_steps), "V"));
    std::vector<std::pair<size_t, size_t>> raw_slices = {{0, 2}};
    ExpressionManager raw_manager(raw_expressions, raw_slices);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "strip_braces.four*", &step_info, &raw_manager);
    // assert
    ASSERT_TRUE(result.has_value());
    auto* spectrum = evaluate_complex(result->front()->expression_manager(), "V(OUT)");
    ASSERT_NE(spectrum, nullptr);
    ASSERT_EQ(spectrum->unit(), "V");
}

TEST(XyceFourFileTest, strips_leading_and_trailing_opposite_braces) {
    // arrange — the leading opening brace and trailing closing brace are stripped in turn
    const TempDirRAII tmp;
    tmp.write_file("opposite_braces.four0", make_four_content("}V(OUT){", HARMONICS_1K));
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "opposite_braces.four*", &step_info);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_NE(evaluate_complex(result->front()->expression_manager(), "V(OUT)"), nullptr);
}

TEST(XyceFourFileTest, strips_all_braces_from_a_brace_only_name) {
    // arrange — a name made only of braces reduces to an empty expression name
    const TempDirRAII tmp;
    tmp.write_file("brace_only.four0", make_four_content("{", HARMONICS_1K));
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_four_file_parser(tmp.path() / "brace_only.four*", &step_info);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
}

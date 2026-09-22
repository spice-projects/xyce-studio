#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iomanip>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/step_information.h"
#include "expression/expression.h"
#include "expression/expression_manager.h"
#include "io/xyce_fft_file.h"
#include "io/xyce_output_file.h"

namespace
{

    // temporary directory manager for fft test files
    class TempDirRAII
    {
    public:
        TempDirRAII() {
            static int counter = 0;
            // build a unique directory path
            m_path = std::filesystem::temp_directory_path() / ("test_xyce_fft_gtest_" + std::to_string(counter++) + "_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
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

    // single fft data point row
    struct DataRow
    {
        // index field
        std::string index;
        // frequency field
        std::string frequency;
        // magnitude field
        std::string magnitude;
        // phase field
        std::string phase;
    };

    // build the content of a standard fft file
    std::string make_fft_content(const std::string& signal_name = "V(OUT)", const std::string& window = "HANN", const std::string& first_harmonic = "1.000000e+02", const std::string& start_freq = "1.000000e+02", const std::string& stop_freq = "1.000000e+03", const std::string& dc_magnitude = "1.000000e-02", const std::string& dc_phase = "1.800000e+02", const bool normalized = true, const std::vector<DataRow>& data_rows = {{"1", "1.000000e+02", "5.000000e-01", "9.000000e+01"}}, const std::string& metrics = "") {
        // result
        std::string content;
        // append the signal header
        content += "FFT analysis for " + signal_name + ":\n";
        // append the window line
        content += "  Window: " + window + ", Start Time: 0.000000e+00, Stop Time: 1.000000e-02\n";
        // append the harmonic line
        content += "  First Harmonic: " + first_harmonic + ", Start Freq: " + start_freq + ", Stop Freq: " + stop_freq + "\n";
        // append the dc component line
        content += normalized ? "  DC component    Norm. Mag= " + dc_magnitude + "   Phase= " + dc_phase + "\n" : "  DC component    Mag= " + dc_magnitude + "   Phase= " + dc_phase + "\n";
        // append the index header
        content += normalized ? "       Index       Frequency       Norm. Mag           Phase\n" : "       Index       Frequency           Mag           Phase\n";
        // append the data rows
        for (const auto& row : data_rows)
            content += "   " + row.index + "    " + row.frequency + "    " + row.magnitude + "    " + row.phase + "\n";
        // append the metrics section
        content += metrics;
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

    // evaluate an expression in the manager as a real expression
    Expression<double>* evaluate_real(ExpressionManager& manager, const std::string& name) {
        // evaluate the expression
        AnyExpression* expression = manager.evaluate(name);
        // cast to a real expression
        return expression ? std::get_if<Expression<double>>(expression) : nullptr;
    }

    // the default fft content with two harmonics
    const std::vector<DataRow> TWO_HARMONICS = {{"1", "1.000000e+02", "5.000000e-01", "9.000000e+01"}, {"2", "2.000000e+02", "2.500000e-01", "-9.000000e+01"}};

} // namespace

// ========================================================================================
// file matching and loading failures
// ========================================================================================

TEST(XyceFftFileTest, returns_nullopt_when_no_files_match_pattern) {
    // arrange
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_fft_file_parser("/nonexistent/path/*.fft*", &step_info);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceFftFileTest, returns_nullopt_when_data_line_has_wrong_column_count) {
    // arrange
    const TempDirRAII tmp;
    const std::string content = make_fft_content("V(OUT)", "HANN", "1.000000e+02", "1.000000e+02", "1.000000e+03", "1.000000e-02", "1.800000e+02", true, {{"1", "1.000000e+02", "5.000000e-01", ""}});
    tmp.write_file("bad_cols.fft0", content);
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_fft_file_parser(tmp.path() / "bad_cols.fft*", &step_info);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceFftFileTest, returns_nullopt_when_data_line_index_is_out_of_order) {
    // arrange
    const TempDirRAII tmp;
    const std::string content = make_fft_content("V(OUT)", "HANN", "1.000000e+02", "1.000000e+02", "1.000000e+03", "1.000000e-02", "1.800000e+02", true, {{"1", "1.000000e+02", "5.000000e-01", "9.000000e+01"}, {"3", "3.000000e+02", "2.500000e-01", "-9.000000e+01"}});
    tmp.write_file("bad_idx.fft0", content);
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_fft_file_parser(tmp.path() / "bad_idx.fft*", &step_info);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceFftFileTest, returns_nullopt_when_index_header_appears_before_signal_header) {
    // arrange
    const TempDirRAII tmp;
    const std::string content = "FFT analysis for V(OUT):\n  Window: HANN, Start Time: 0.000000e+00, Stop Time: 1.000000e-02\n  First Harmonic: 1.000000e+02, Start Freq: 1.000000e+02, Stop Freq: 1.000000e+03\n       Index       Frequency       Norm. Mag           Phase\n           1    1.000000e+02    5.000000e-01    9.000000e+01\n";
    tmp.write_file("bad_order.fft0", content);
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_fft_file_parser(tmp.path() / "bad_order.fft*", &step_info);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceFftFileTest, returns_nullopt_when_step_count_mismatches_step_information) {
    // arrange
    const TempDirRAII tmp;
    tmp.write_file("mismatch.fft0", make_fft_content());
    const StepInformation step_info = make_step_information(2);
    // act
    const auto result = xyce_fft_file_parser(tmp.path() / "mismatch.fft*", &step_info);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceFftFileTest, returns_nullopt_when_file_has_invalid_utf8_bytes) {
    // arrange
    const TempDirRAII tmp;
    const std::string header = make_fft_content();
    std::vector<char> bytes(header.begin(), header.end());
    // append invalid utf8 bytes
    bytes.push_back(static_cast<char>(0xFF));
    bytes.push_back(static_cast<char>(0xFE));
    bytes.insert(bytes.end(), {' ', 'b', 'a', 'd', ' ', 'd', 'a', 't', 'a', '\n'});
    tmp.write_bytes("bad_encoding.fft0", bytes);
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_fft_file_parser(tmp.path() / "bad_encoding.fft*", &step_info);
    // assert
    ASSERT_FALSE(result.has_value());
}

// ========================================================================================
// basic fft parsing
// ========================================================================================

TEST(XyceFftFileTest, parses_single_fft_file_with_one_signal) {
    // arrange
    const TempDirRAII tmp;
    tmp.write_file("test_sim.fft0", make_fft_content("V(OUT)", "HANN", "1.000000e+02", "1.000000e+02", "1.000000e+03", "1.000000e-02", "1.800000e+02", true, TWO_HARMONICS));
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_fft_file_parser(tmp.path() / "test_sim.fft*", &step_info);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
    const auto& output_file = result->front();
    ASSERT_EQ(output_file->abscissa().step_count(), 1);
    ASSERT_DOUBLE_EQ(output_file->abscissa().step_data(0)[0], 0.0);
    ASSERT_DOUBLE_EQ(output_file->abscissa().step_data(0)[1], 100.0);
    ASSERT_DOUBLE_EQ(output_file->abscissa().step_data(0)[2], 200.0);
    auto* mag_expr = evaluate_real(output_file->expression_manager(), "FFT(V(OUT))");
    ASSERT_NE(mag_expr, nullptr);
    ASSERT_DOUBLE_EQ(mag_expr->step_data(0)[0], 0.01);
    ASSERT_DOUBLE_EQ(mag_expr->step_data(0)[1], 0.5);
    ASSERT_DOUBLE_EQ(mag_expr->step_data(0)[2], 0.25);
    auto* phase_expr = evaluate_real(output_file->expression_manager(), "FFT(phase(V(OUT)))");
    ASSERT_NE(phase_expr, nullptr);
    ASSERT_DOUBLE_EQ(phase_expr->step_data(0)[0], 180.0);
    ASSERT_DOUBLE_EQ(phase_expr->step_data(0)[1], 90.0);
    ASSERT_DOUBLE_EQ(phase_expr->step_data(0)[2], -90.0);
}

TEST(XyceFftFileTest, parses_multiple_files_and_accumulates_steps) {
    // arrange
    const TempDirRAII tmp;
    tmp.write_file("sim.fft0", make_fft_content("V(OUT)", "HANN", "1.000000e+02", "1.000000e+02", "1.000000e+03", "1.000000e-02", "1.800000e+02", true, {{"1", "1.000000e+02", "5.000000e-01", "9.000000e+01"}}));
    tmp.write_file("sim.fft1", make_fft_content("V(OUT)", "HANN", "1.000000e+02", "1.000000e+02", "1.000000e+03", "2.000000e-02", "0.000000e+00", true, {{"1", "1.000000e+02", "8.000000e-01", "-9.000000e+01"}}));
    const StepInformation step_info = make_step_information(2);
    // act
    const auto result = xyce_fft_file_parser(tmp.path() / "sim.fft*", &step_info);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
    const auto& output_file = result->front();
    ASSERT_EQ(output_file->step_information().length(), 2);
    auto* mag_expr = evaluate_real(output_file->expression_manager(), "FFT(V(OUT))");
    ASSERT_NE(mag_expr, nullptr);
    ASSERT_EQ(mag_expr->step_count(), 2);
    ASSERT_DOUBLE_EQ(mag_expr->step_data(0)[0], 0.01);
    ASSERT_DOUBLE_EQ(mag_expr->step_data(0)[1], 0.5);
    ASSERT_DOUBLE_EQ(mag_expr->step_data(1)[0], 0.02);
    ASSERT_DOUBLE_EQ(mag_expr->step_data(1)[1], 0.8);
    auto* phase_expr = evaluate_real(output_file->expression_manager(), "FFT(phase(V(OUT)))");
    ASSERT_NE(phase_expr, nullptr);
    ASSERT_EQ(phase_expr->step_count(), 2);
    ASSERT_DOUBLE_EQ(phase_expr->step_data(0)[0], 180.0);
    ASSERT_DOUBLE_EQ(phase_expr->step_data(0)[1], 90.0);
    ASSERT_DOUBLE_EQ(phase_expr->step_data(1)[0], 0.0);
    ASSERT_DOUBLE_EQ(phase_expr->step_data(1)[1], -90.0);
}

TEST(XyceFftFileTest, parses_fft_metrics_and_stores_in_metadata) {
    // arrange
    const TempDirRAII tmp;
    const std::string metrics = "\n  THD = 1.687485e+01 dB ( 6.978185e+00 )\n SNDR = -1.687485e+01 dB\n ENOB = -3.095490e+00 bit\n  SNR = 2.000000e+02 dB\n SFDR = -1.257423e+00 dB at frequency 1.150000e+03\n";
    tmp.write_file("metrics.fft0", make_fft_content("V(OUT)", "HANN", "1.000000e+02", "1.000000e+02", "1.000000e+03", "1.000000e-02", "1.800000e+02", true, {{"1", "1.000000e+02", "5.000000e-01", "9.000000e+01"}}, metrics));
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_fft_file_parser(tmp.path() / "metrics.fft*", &step_info);
    // assert
    ASSERT_TRUE(result.has_value());
    auto* mag_expr = evaluate_real(result->front()->expression_manager(), "FFT(V(OUT))");
    ASSERT_NE(mag_expr, nullptr);
    ASSERT_EQ(mag_expr->metadata().size(), 1);
    const auto& step_metadata = mag_expr->metadata()[0];
    ASSERT_EQ(step_metadata.at("THD"), "1.687485e+01 dB ( 6.978185e+00 )");
    ASSERT_EQ(step_metadata.at("SNDR"), "-1.687485e+01 dB");
    ASSERT_EQ(step_metadata.at("ENOB"), "-3.095490e+00 bit");
    ASSERT_EQ(step_metadata.at("SNR"), "2.000000e+02 dB");
    ASSERT_EQ(step_metadata.at("SFDR"), "-1.257423e+00 dB at frequency 1.150000e+03");
}

TEST(XyceFftFileTest, parses_multiple_signals_and_resets_header_flags) {
    // arrange
    const TempDirRAII tmp;
    const std::string speaker_block = make_fft_content("V(SPEAKER)", "HANN", "1.000000e+02", "1.000000e+02", "1.000000e+03", "1.000000e-02", "1.800000e+02", true, {{"1", "1.000000e+02", "5.000000e-01", "9.000000e+01"}}, "\n  THD = 1.000000e+01 dB ( 3.000000e+00 )\n");
    const std::string input_block = make_fft_content("V(INPUT)", "RECT", "2.000000e+02", "2.000000e+02", "2.000000e+03", "5.000000e-02", "0.000000e+00", true, {{"1", "2.000000e+02", "8.000000e-01", "-9.000000e+01"}}, "\n  THD = 2.000000e+01 dB ( 4.000000e+00 )\n");
    tmp.write_file("multi_signals.fft0", speaker_block + "\n" + input_block);
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_fft_file_parser(tmp.path() / "multi_signals.fft*", &step_info);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 2);
    // locate the output file containing the speaker signal
    const auto speaker_file = std::find_if(result->begin(), result->end(), [](const auto& file) { return evaluate_real(file->expression_manager(), "FFT(V(SPEAKER))") != nullptr; });
    ASSERT_NE(speaker_file, result->end());
    auto* speaker_mag = evaluate_real((*speaker_file)->expression_manager(), "FFT(V(SPEAKER))");
    ASSERT_DOUBLE_EQ(speaker_mag->step_data(0)[0], 0.01);
    ASSERT_DOUBLE_EQ(speaker_mag->step_data(0)[1], 0.5);
    ASSERT_EQ(speaker_mag->metadata()[0].at("THD"), "1.000000e+01 dB ( 3.000000e+00 )");
    // locate the output file containing the input signal
    const auto input_file = std::find_if(result->begin(), result->end(), [](const auto& file) { return evaluate_real(file->expression_manager(), "FFT(V(INPUT))") != nullptr; });
    ASSERT_NE(input_file, result->end());
    auto* input_mag = evaluate_real((*input_file)->expression_manager(), "FFT(V(INPUT))");
    ASSERT_DOUBLE_EQ(input_mag->step_data(0)[0], 0.05);
    ASSERT_DOUBLE_EQ(input_mag->step_data(0)[1], 0.8);
    ASSERT_EQ(input_mag->metadata()[0].at("THD"), "2.000000e+01 dB ( 4.000000e+00 )");
}

TEST(XyceFftFileTest, parses_two_signals_with_same_abscissa) {
    // arrange
    const TempDirRAII tmp;
    const std::string first_block = make_fft_content("I(L1)", "HANN", "1.000000e+02", "1.000000e+02", "1.000000e+03", "4.000000e-01", "0.000000e+00", true, {{"1", "1.000000e+02", "5.000000e-01", "0.000000e+00"}});
    const std::string second_block = make_fft_content("V(OUT)", "HANN", "1.000000e+02", "1.000000e+02", "1.000000e+03", "2.000000e-01", "1.800000e+02", true, {{"1", "1.000000e+02", "6.000000e-01", "9.000000e+01"}});
    tmp.write_file("same_abscissa.fft0", first_block + "\n" + second_block);
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_fft_file_parser(tmp.path() / "same_abscissa.fft*", &step_info);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
    const auto& output_file = result->front();
    ASSERT_NE(evaluate_real(output_file->expression_manager(), "FFT(I(L1))"), nullptr);
    ASSERT_NE(evaluate_real(output_file->expression_manager(), "FFT(V(OUT))"), nullptr);
    auto* l1_mag = evaluate_real(output_file->expression_manager(), "FFT(I(L1))");
    ASSERT_DOUBLE_EQ(l1_mag->step_data(0)[0], 0.4);
    ASSERT_DOUBLE_EQ(l1_mag->step_data(0)[1], 0.5);
    auto* out_mag = evaluate_real(output_file->expression_manager(), "FFT(V(OUT))");
    ASSERT_DOUBLE_EQ(out_mag->step_data(0)[0], 0.2);
    ASSERT_DOUBLE_EQ(out_mag->step_data(0)[1], 0.6);
}

TEST(XyceFftFileTest, parses_file_with_multiple_abscissas) {
    // arrange
    const TempDirRAII tmp;
    const std::string first_block = make_fft_content("V(OUT1)", "HANN", "1.000000e+02", "1.000000e+02", "1.000000e+03", "1.000000e-02", "1.800000e+02", true, {{"1", "1.000000e+02", "5.000000e-01", "9.000000e+01"}});
    const std::string second_block = make_fft_content("V(OUT2)", "RECT", "2.000000e+02", "2.000000e+02", "2.000000e+03", "5.000000e-02", "0.000000e+00", true, {{"1", "2.000000e+02", "8.000000e-01", "-9.000000e+01"}});
    tmp.write_file("multi_abscissa.fft0", first_block + "\n" + second_block);
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_fft_file_parser(tmp.path() / "multi_abscissa.fft*", &step_info);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 2);
}

TEST(XyceFftFileTest, parses_file_without_fft_index_suffix) {
    // arrange
    const TempDirRAII tmp;
    tmp.write_file("sim.fft", make_fft_content());
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_fft_file_parser(tmp.path() / "sim.fft*", &step_info);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
}

TEST(XyceFftFileTest, file_without_trailing_newline_is_handled) {
    // arrange
    const TempDirRAII tmp;
    // strip the trailing newline from the last data line
    std::string content = make_fft_content();
    content.pop_back();
    tmp.write_file("no_newline.fft0", content);
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_fft_file_parser(tmp.path() / "no_newline.fft*", &step_info);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
}

TEST(XyceFftFileTest, parses_file_with_unexpected_line) {
    // arrange
    const TempDirRAII tmp;
    const std::string content = "FFT analysis for V(OUT):\n  Window: HANN, Start Time: 0.000000e+00, Stop Time: 1.000000e-02\n  First Harmonic: 1.000000e+02, Start Freq: 1.000000e+02, Stop Freq: 1.000000e+03\n  DC component    Norm. Mag= 1.000000e-02   Phase= 1.800000e+02\nTHIS IS AN UNEXPECTED LINE\n       Index       Frequency       Norm. Mag           Phase\n           1    1.000000e+02    5.000000e-01    9.000000e+01\n";
    tmp.write_file("unexpected.fft0", content);
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_fft_file_parser(tmp.path() / "unexpected.fft*", &step_info);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
}

TEST(XyceFftFileTest, parses_non_normalized_magnitude) {
    // arrange
    const TempDirRAII tmp;
    tmp.write_file("non_norm.fft0", make_fft_content("V(OUT)", "HANN", "1.000000e+02", "1.000000e+02", "1.000000e+03", "1.000000e-02", "1.800000e+02", false));
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_fft_file_parser(tmp.path() / "non_norm.fft*", &step_info);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->front()->metadata().at("Normalized"), "false");
}

// ========================================================================================
// multi step fft parsing
// ========================================================================================

TEST(XyceFftFileTest, parses_three_step_fft_files_generated) {
    // arrange
    const TempDirRAII tmp;
    const std::vector<double> dc_values = {0.4323973, 0.3312294, 0.2770515};
    const std::vector<std::string> thd_values = {"1.687485e+01 dB ( 6.978185e+00 )", "1.683650e+01 dB ( 6.947444e+00 )", "1.679928e+01 dB ( 6.917735e+00 )"};
    for (size_t step = 0; step < 3; ++step) {
        // format the dc magnitude for this step
        std::ostringstream dc_stream;
        dc_stream << std::setprecision(7) << std::scientific << dc_values[step];
        // build the content with the step specific metrics
        const std::string metrics = "\n  THD = " + thd_values[step] + "\n";
        tmp.write_file("sim_step" + std::to_string(step) + ".fft" + std::to_string(step), make_fft_content("I(L1)", "HANN", "1.000000e+02", "1.000000e+02", "1.000000e+03", dc_stream.str(), "1.800000e+02", true, TWO_HARMONICS, metrics));
    }
    const StepInformation step_info = make_step_information(3);
    // act
    const auto result = xyce_fft_file_parser(tmp.path() / "sim_step*.fft*", &step_info);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
    const auto& output_file = result->front();
    ASSERT_EQ(output_file->step_information().length(), 3);
    ASSERT_EQ(output_file->abscissa().step_count(), 3);
    ASSERT_EQ(output_file->abscissa().step_data(0).size(), 3);
    ASSERT_DOUBLE_EQ(output_file->abscissa().step_data(0)[0], 0.0);
    ASSERT_DOUBLE_EQ(output_file->abscissa().step_data(0)[1], 100.0);
    auto* mag_expr = evaluate_real(output_file->expression_manager(), "FFT(I(L1))");
    ASSERT_NE(mag_expr, nullptr);
    ASSERT_EQ(mag_expr->step_count(), 3);
    for (size_t step = 0; step < 3; ++step) {
        // verify the dc magnitude for this step
        ASSERT_DOUBLE_EQ(mag_expr->step_data(step)[0], dc_values[step]);
        // verify the thd metadata for this step
        ASSERT_EQ(mag_expr->metadata()[step].at("THD"), thd_values[step]);
    }
}

TEST(XyceFftFileTest, parses_synthetic_single_step_fft_file) {
    // arrange
    const TempDirRAII tmp;
    const std::string metrics = "\n  THD = 1.687485e+01 dB ( 6.978185e+00 )\n SNDR = 2.000000e+01 dB\n ENOB = 3.000000e+00 bit\n  SNR = 2.000000e+02 dB\n SFDR = -1.257423e+00 dB at frequency 1.150000e+03\n";
    tmp.write_file("single_step.fft0", make_fft_content("I(L1)", "HANN", "1.000000e+02", "1.000000e+02", "1.000000e+03", "4.323973e-01", "1.800000e+02", true, {{"1", "1.000000e+02", "8.652246e-01", "9.000000e+01"}, {"2", "2.000000e+02", "1.234567e-01", "-9.000000e+01"}}, metrics));
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_fft_file_parser(tmp.path() / "single_step.fft*", &step_info);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
    auto* mag_expr = evaluate_real(result->front()->expression_manager(), "FFT(I(L1))");
    ASSERT_NE(mag_expr, nullptr);
    ASSERT_EQ(mag_expr->step_count(), 1);
    ASSERT_DOUBLE_EQ(mag_expr->step_data(0)[0], 0.4323973);
    ASSERT_DOUBLE_EQ(mag_expr->step_data(0)[1], 0.8652246);
    const auto& step_metadata = mag_expr->metadata()[0];
    ASSERT_EQ(step_metadata.at("THD"), "1.687485e+01 dB ( 6.978185e+00 )");
    ASSERT_EQ(step_metadata.at("SNDR"), "2.000000e+01 dB");
    ASSERT_EQ(step_metadata.at("ENOB"), "3.000000e+00 bit");
    ASSERT_EQ(step_metadata.at("SNR"), "2.000000e+02 dB");
    ASSERT_EQ(step_metadata.at("SFDR"), "-1.257423e+00 dB at frequency 1.150000e+03");
}

TEST(XyceFftFileTest, parses_without_step_information) {
    // arrange — a run without a .PRINT directive produces no analysis output,
    // so there is no step information to map the FFT files onto; the files'
    // own single step drives the parsing
    const TempDirRAII tmp;
    tmp.write_file("no_print.fft0", make_fft_content("I(L1)", "HANN", "1.000000e+02", "1.000000e+02", "1.000000e+03", "4.323973e-01", "1.800000e+02", true, {{"1", "1.000000e+02", "8.652246e-01", "9.000000e+01"}, {"2", "2.000000e+02", "1.234567e-01", "-9.000000e+01"}}, ""));
    // act
    const auto result = xyce_fft_file_parser(tmp.path() / "no_print.fft*");
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
    auto* mag_expr = evaluate_real(result->front()->expression_manager(), "FFT(I(L1))");
    ASSERT_NE(mag_expr, nullptr);
    ASSERT_EQ(mag_expr->step_count(), 1);
    ASSERT_DOUBLE_EQ(mag_expr->step_data(0)[0], 0.4323973);
    ASSERT_DOUBLE_EQ(mag_expr->step_data(0)[1], 0.8652246);
    // the step information carries no keys and no values (no .STEP run)
    ASSERT_TRUE(result->front()->step_information().keys().empty());
    ASSERT_TRUE(result->front()->step_information().values().empty());
}

// ========================================================================================
// output file structure and unit inference
// ========================================================================================

TEST(XyceFftFileTest, output_file_contains_abscissa_metadata) {
    // arrange
    const TempDirRAII tmp;
    tmp.write_file("sim.fft0", make_fft_content());
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_fft_file_parser(tmp.path() / "sim.fft*", &step_info);
    // assert
    ASSERT_TRUE(result.has_value());
    const auto& output_file = result->front();
    ASSERT_EQ(output_file->title(), "FFT: HANN, 100–1000 Hz,fh=100, Norm");
    ASSERT_EQ(output_file->filename(), tmp.path() / "sim.fft0");
    ASSERT_FALSE(output_file->is_complex());
    ASSERT_EQ(output_file->abscissa_scale(), AbscissaScale::LINEAR);
    ASSERT_EQ(output_file->plot_type(), PlotType::FFT);
    ASSERT_EQ(output_file->abscissa().name(), "frequency");
    ASSERT_EQ(output_file->abscissa().unit(), "Hz");
    ASSERT_EQ(output_file->abscissa().source(), "FFT");
    ASSERT_EQ(output_file->metadata().at("Window"), "HANN");
    ASSERT_EQ(output_file->metadata().at("Normalized"), "true");
    ASSERT_EQ(output_file->metadata().at("First Harmonic"), "100.000000");
}

TEST(XyceFftFileTest, phase_expression_uses_degree_unit) {
    // arrange
    const TempDirRAII tmp;
    tmp.write_file("sim.fft0", make_fft_content());
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_fft_file_parser(tmp.path() / "sim.fft*", &step_info);
    // assert
    ASSERT_TRUE(result.has_value());
    auto* phase_expr = evaluate_real(result->front()->expression_manager(), "FFT(phase(V(OUT)))");
    ASSERT_NE(phase_expr, nullptr);
    ASSERT_EQ(phase_expr->unit(), "°");
}

TEST(XyceFftFileTest, infers_magnitude_unit_from_expression_manager) {
    // arrange
    const TempDirRAII tmp;
    tmp.write_file("infer_unit.fft0", make_fft_content());
    const StepInformation step_info = make_step_information(1);
    // create a raw expression manager with a known unit
    std::vector<double> raw_data = {0.0, 1.0};
    std::vector<std::span<const double>> raw_steps = {{raw_data.data(), raw_data.size()}};
    std::vector<AnyExpression> raw_expressions;
    raw_expressions.emplace_back(Expression<double>("V(OUT)", std::move(raw_data), std::move(raw_steps), "mV"));
    std::vector<std::pair<size_t, size_t>> raw_slices = {{0, 2}};
    ExpressionManager raw_manager(raw_expressions, raw_slices);
    // act
    const auto result = xyce_fft_file_parser(tmp.path() / "infer_unit.fft*", &step_info, &raw_manager);
    // assert
    ASSERT_TRUE(result.has_value());
    auto* mag_expr = evaluate_real(result->front()->expression_manager(), "FFT(V(OUT))");
    ASSERT_NE(mag_expr, nullptr);
    ASSERT_EQ(mag_expr->unit(), "mV");
}

TEST(XyceFftFileTest, strips_braces_before_unit_inference) {
    // arrange
    const TempDirRAII tmp;
    tmp.write_file("strip_braces.fft0", make_fft_content("{V(OUT)}"));
    const StepInformation step_info = make_step_information(1);
    // create a raw expression manager with a known unit
    std::vector<double> raw_data = {0.0, 1.0};
    std::vector<std::span<const double>> raw_steps = {{raw_data.data(), raw_data.size()}};
    std::vector<AnyExpression> raw_expressions;
    raw_expressions.emplace_back(Expression<double>("V(OUT)", std::move(raw_data), std::move(raw_steps), "V"));
    std::vector<std::pair<size_t, size_t>> raw_slices = {{0, 2}};
    ExpressionManager raw_manager(raw_expressions, raw_slices);
    // act
    const auto result = xyce_fft_file_parser(tmp.path() / "strip_braces.fft*", &step_info, &raw_manager);
    // assert
    ASSERT_TRUE(result.has_value());
    auto* mag_expr = evaluate_real(result->front()->expression_manager(), "FFT(V(OUT))");
    ASSERT_NE(mag_expr, nullptr);
    ASSERT_EQ(mag_expr->unit(), "V");
    ASSERT_NE(evaluate_real(result->front()->expression_manager(), "FFT(V(OUT))"), nullptr);
}

// ========================================================================================
// suggested plots
// ========================================================================================

TEST(XyceFftFileTest, suggests_magnitude_and_phase_plots_for_single_signal) {
    // arrange
    const TempDirRAII tmp;
    tmp.write_file("suggest.fft0", make_fft_content());
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_fft_file_parser(tmp.path() / "suggest.fft*", &step_info);
    // assert
    ASSERT_TRUE(result.has_value());
    const auto& suggested_plots = result->front()->suggested_plots();
    ASSERT_EQ(suggested_plots.size(), 1);
    ASSERT_EQ(suggested_plots[0], (std::vector<std::string>{"FFT(V(OUT))", "FFT(phase(V(OUT)))"}));
    // the suggested plot names resolve to expressions in the output manager
    ASSERT_NE(evaluate_real(result->front()->expression_manager(), suggested_plots[0][0]), nullptr);
    ASSERT_NE(evaluate_real(result->front()->expression_manager(), suggested_plots[0][1]), nullptr);
}

TEST(XyceFftFileTest, suggests_magnitude_and_phase_plots_for_each_signal) {
    // arrange
    const TempDirRAII tmp;
    const std::string first_block = make_fft_content("I(L1)", "HANN", "1.000000e+02", "1.000000e+02", "1.000000e+03", "4.000000e-01", "0.000000e+00", true, {{"1", "1.000000e+02", "5.000000e-01", "0.000000e+00"}});
    const std::string second_block = make_fft_content("V(OUT)", "HANN", "1.000000e+02", "1.000000e+02", "1.000000e+03", "2.000000e-01", "1.800000e+02", true, {{"1", "1.000000e+02", "6.000000e-01", "9.000000e+01"}});
    tmp.write_file("suggest_multi.fft0", first_block + "\n" + second_block);
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_fft_file_parser(tmp.path() / "suggest_multi.fft*", &step_info);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
    const auto& suggested_plots = result->front()->suggested_plots();
    ASSERT_EQ(suggested_plots.size(), 2);
    // collect the suggested plot names
    std::set<std::vector<std::string>> plot_groups(suggested_plots.begin(), suggested_plots.end());
    ASSERT_NE(plot_groups.find({"FFT(I(L1))", "FFT(phase(I(L1)))"}), plot_groups.end());
    ASSERT_NE(plot_groups.find({"FFT(V(OUT))", "FFT(phase(V(OUT)))"}), plot_groups.end());
}

TEST(XyceFftFileTest, caps_suggested_plots_at_three_signals) {
    // arrange
    const TempDirRAII tmp;
    const std::vector<std::string> signals = {"V(A)", "V(B)", "V(C)", "V(D)"};
    std::string content;
    for (const auto& signal : signals)
        content += make_fft_content(signal) + "\n";
    tmp.write_file("suggest_cap.fft0", content);
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_fft_file_parser(tmp.path() / "suggest_cap.fft*", &step_info);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
    const auto& suggested_plots = result->front()->suggested_plots();
    // only three signal groups are suggested regardless of the signal count
    ASSERT_EQ(suggested_plots.size(), 3);
    // every suggested group carries a magnitude and a phase expression resolving in the manager
    auto& manager = result->front()->expression_manager();
    for (const auto& group : suggested_plots) {
        ASSERT_EQ(group.size(), 2);
        ASSERT_NE(evaluate_real(manager, group[0]), nullptr);
        ASSERT_NE(evaluate_real(manager, group[1]), nullptr);
    }
}

TEST(XyceFftFileTest, strips_braces_in_suggested_plot_names) {
    // arrange
    const TempDirRAII tmp;
    tmp.write_file("suggest_braces.fft0", make_fft_content("{V(OUT)}"));
    const StepInformation step_info = make_step_information(1);
    // act
    const auto result = xyce_fft_file_parser(tmp.path() / "suggest_braces.fft*", &step_info);
    // assert
    ASSERT_TRUE(result.has_value());
    const auto& suggested_plots = result->front()->suggested_plots();
    ASSERT_EQ(suggested_plots.size(), 1);
    ASSERT_EQ(suggested_plots[0], (std::vector<std::string>{"FFT(V(OUT))", "FFT(phase(V(OUT)))"}));
}

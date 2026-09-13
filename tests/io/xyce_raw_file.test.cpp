#include <chrono>
#include <cmath>
#include <complex>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "expression/expression.h"
#include "expression/expression_manager.h"
#include "io/xyce_raw_file.h"

namespace
{

    // temp file manager helper class
    class TempFileRAII
    {
    public:
        // constructor
        explicit TempFileRAII(const std::string& content) {
            static int counter = 0;
            // build unique path
            m_path = std::filesystem::temp_directory_path() / ("test_xyce_gtest_" + std::to_string(counter++) + "_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".raw");
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

    // variable definition helper structure
    struct TestVarDef
    {
        // index field
        int index;
        // name field
        std::string name;
        // type field
        std::string type;
    };

    // helper to generate raw bytes
    std::string make_raw_bytes(const std::string& title = "Test Circuit", const std::string& plotname = "Transient Analysis", const std::string& flags = "real", const std::vector<TestVarDef>& variable_defs = {{0, "time", "time"}, {1, "V(1)", "voltage"}}, const std::vector<std::vector<double>>& data_matrix = {{0.0, 0.0}, {1e-9, 1.0}}, bool is_ascii = false, std::optional<size_t> num_points_override = std::nullopt) {
        // get variables count
        size_t num_variables = variable_defs.size();
        // get points count
        size_t num_points = num_points_override.value_or(data_matrix.size());
        // initialize stream
        std::ostringstream ss;
        // write title
        ss << "Title: " << title << "\n";
        // write plotname
        ss << "Plotname: " << plotname << "\n";
        // write flags
        ss << "Flags: " << flags << "\n";
        // write variables count
        ss << "No. Variables: " << num_variables << "\n";
        // write points count
        ss << "No. Points: " << num_points << "\n";
        // write variables header
        ss << "Variables:\n";
        // loop variables
        for (const auto& [index, name, type] : variable_defs) {
            // write variable line
            ss << "\t" << index << "\t" << name << "\t" << type << "\n";
        }
        if (is_ascii) {
            // write values header
            ss << "Values:\n";
            for (size_t r = 0; r < data_matrix.size(); ++r) {
                // write row index
                ss << " " << r << "  ";
                const auto& row = data_matrix[r];
                for (size_t col = 0; col < row.size(); ++col) {
                    if (col > 0) {
                        // write spacing
                        ss << "  ";
                    }
                    // write value
                    ss << row[col];
                }
                // write newline
                ss << "\n";
            }
            // return string
            return ss.str();
        }
        // write binary header
        ss << "Binary:\n";
        std::string header = ss.str();
        std::string payload;
        for (const auto& row : data_matrix) {
            for (double val : row) {
                payload.append(reinterpret_cast<const char*>(&val), sizeof(double));
            }
        }
        return header + payload;
    }

    // helper to generate multi block raw bytes
    std::string make_multi_block_raw_bytes(const std::string& title = "Stepped Circuit", const std::vector<TestVarDef>& variable_defs = {{0, "sweep", "voltage"}, {1, "V(2)", "voltage"}}, const std::vector<std::vector<std::vector<double>>>& step_matrices = {{{0.0, 0.5}, {1.0, 1.0}, {2.0, 1.5}, {3.0, 2.0}}, {{0.0, 1.0}, {1.0, 1.5}, {2.0, 2.0}, {3.0, 2.5}}, {{0.0, 1.5}, {1.0, 2.0}, {2.0, 2.5}, {3.0, 3.0}}}, const std::string& param_name = "R1", const std::vector<double>& param_values = {1000.0, 2000.0, 3000.0}) {
        size_t num_steps = step_matrices.size();
        size_t num_variables = variable_defs.size();
        std::string result;
        for (size_t step_index = 0; step_index < num_steps; ++step_index) {
            const auto& data_matrix = step_matrices[step_index];
            double param_value = param_values[step_index];
            size_t num_points = data_matrix.size();
            std::ostringstream ss;
            ss << "Title: " << title << "\n";
            ss << "Plotname: Step Analysis: Step " << (step_index + 1) << " of " << num_steps << " params:  name = " << param_name << " value = " << param_value << "  DC transfer characteristic\n";
            ss << "Flags: real\n";
            ss << "No. Variables: " << num_variables << "\n";
            ss << "No. Points: " << num_points << "\n";
            ss << "Variables:\n";
            for (const auto& var : variable_defs) {
                ss << "\t" << var.index << "\t" << var.name << "\t" << var.type << "\n";
            }
            ss << "Binary:\n";
            std::string header = ss.str();
            std::string payload;
            for (const auto& row : data_matrix) {
                for (double val : row) {
                    payload.append(reinterpret_cast<const char*>(&val), sizeof(double));
                }
            }
            result += header + payload;
        }
        return result;
    }

    Expression<double>* evaluate_real(ExpressionManager& manager, const std::string& expression_name) {
        // evaluate expression in manager
        AnyExpression* expression = manager.evaluate(expression_name);
        // cast to real expression
        return expression ? std::get_if<Expression<double>>(expression) : nullptr;
    }

    Expression<std::complex<double>>* evaluate_complex(ExpressionManager& manager, const std::string& expression_name) {
        // evaluate expression in manager
        AnyExpression* expression = manager.evaluate(expression_name);
        // cast to complex expression
        return expression ? std::get_if<Expression<std::complex<double>>>(expression) : nullptr;
    }

} // namespace

// ========================================================================================
// file parsing failure scenarios
// ========================================================================================

TEST(XyceRawFileTest, load_returns_nullopt_when_file_not_found) {
    // arrange
    const std::filesystem::path path = "/tmp/nonexistent_xyce_raw_file_abc123.raw";
    // act
    const auto result = xyce_raw_file_parser(path);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceRawFileTest, load_returns_nullopt_when_file_is_empty) {
    // arrange
    const TempFileRAII temp_file("");
    // act
    const auto result = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceRawFileTest, load_returns_nullopt_when_data_section_missing) {
    // arrange
    const std::string content = "Title: Test\nDate: Mon Jan 1 00:00:00 2024\nPlotname: Transient Analysis\nFlags: real\nNo. Variables: 2\nNo. Points: 1\nVariables:\n\t0\ttime\ttime\n\t1\tV(1)\tvoltage\n";
    const TempFileRAII temp_file(content);
    // act
    const auto result = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceRawFileTest, load_returns_nullopt_when_variable_count_header_mismatch) {
    // arrange
    const std::string content = "Title: Test\nDate: Mon Jan 1 00:00:00 2024\nPlotname: Transient Analysis\nFlags: real\nNo. Variables: 3\nNo. Points: 1\nVariables:\n\t0\ttime\ttime\n\t1\tV(1)\tvoltage\nBinary:\n";
    const TempFileRAII temp_file(content);
    // act
    const auto result = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceRawFileTest, load_returns_nullopt_when_binary_payload_is_truncated) {
    // arrange
    const std::string header = "Title: Test\nPlotname: Transient Analysis\nFlags: real\nNo. Variables: 2\nNo. Points: 2\nVariables:\n\t0\ttime\ttime\n\t1\tV(1)\tvoltage\nBinary:\n";
    const std::vector<double> one_row = {0.0, 1.0};
    std::string content = header;
    for (double val : one_row) {
        // append payload value
        content.append(reinterpret_cast<const char*>(&val), sizeof(double));
    }
    const TempFileRAII temp_file(content);
    // act
    const auto result = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceRawFileTest, load_returns_nullopt_when_ascii_parse_produces_no_data) {
    // arrange
    const std::string content = "Title: Test\nDate: Mon Jan 1 00:00:00 2024\nPlotname: Transient Analysis\nFlags: real\nNo. Variables: 2\nNo. Points: 0\nVariables:\n\t0\ttime\ttime\n\t1\tV(1)\tvoltage\nValues:\nNO_NUMERIC_DATA\n";
    const TempFileRAII temp_file(content);
    // act
    const auto result = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceRawFileTest, load_returns_nullopt_when_file_has_no_trailing_newline) {
    // arrange
    const std::string content = "Title: Test\nDate: Mon Jan 1 00:00:00 2024";
    const TempFileRAII temp_file(content);
    // act
    const auto result = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceRawFileTest, ascii_values_unexpected_index) {
    // arrange
    const std::string content = "Title: Test Circuit\nPlotname: Transient Analysis\nFlags: real\nNo. Variables: 2\nNo. Points: 2\nVariables:\n\t0\ttime\ttime\n\t1\tV(1)\tvoltage\nValues:\n 0  0.0  1.0\n 2  1.0  2.0\n";
    const TempFileRAII temp_file(content);
    // act
    const auto result = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceRawFileTest, ascii_values_invalid_token_count) {
    // arrange
    const std::string content = "Title: Test Circuit\nPlotname: Transient Analysis\nFlags: real\nNo. Variables: 2\nNo. Points: 2\nVariables:\n\t0\ttime\ttime\n\t1\tV(1)\tvoltage\nValues:\n 0  0.0  1.0\n 1  1.0\n";
    const TempFileRAII temp_file(content);
    // act
    const auto result = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceRawFileTest, ascii_values_parsing_exception) {
    // arrange
    const std::string content = "Title: Test Circuit\nPlotname: Transient Analysis\nFlags: real\nNo. Variables: 2\nNo. Points: 2\nVariables:\n\t0\ttime\ttime\n\t1\tV(1)\tvoltage\nValues:\n 0  0.0  1.0\n 1  abc  2.0\n";
    const TempFileRAII temp_file(content);
    // act
    const auto result = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceRawFileTest, ascii_values_point_count_mismatch) {
    // arrange
    const std::string content = "Title: Test Circuit\nPlotname: Transient Analysis\nFlags: real\nNo. Variables: 2\nNo. Points: 3\nVariables:\n\t0\ttime\ttime\n\t1\tV(1)\tvoltage\nValues:\n 0  0.0  1.0\n 1  1.0  2.0\n";
    const TempFileRAII temp_file(content);
    // act
    const auto result = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceRawFileTest, multi_block_variables_mismatch) {
    // arrange
    const std::string content0 = make_multi_block_raw_bytes("Stepped Circuit", {{0, "sweep", "voltage"}, {1, "V(2)", "voltage"}}, {{{0.0, 1.0}, {1.0, 2.0}}}, "R1", {1000.0});
    const std::string content1 = make_multi_block_raw_bytes("Stepped Circuit", {{0, "sweep", "voltage"}, {1, "V(3)", "voltage"}}, {{{0.0, 1.5}, {1.0, 2.5}}}, "R1", {2000.0});
    const size_t marker = content1.find("Title: Stepped Circuit");
    const std::string content = content0 + content1.substr(marker);
    const TempFileRAII temp_file(content);
    // act
    const auto result = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_FALSE(result.has_value());
}

// ========================================================================================
// real binary data parsing
// ========================================================================================

TEST(XyceRawFileTest, load_real_binary_title) {
    // arrange
    const std::string content = make_raw_bytes("RC Circuit");
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_EQ(raw.value()->title(), "RC Circuit");
}

TEST(XyceRawFileTest, load_real_binary_filename) {
    // arrange
    const std::string content = make_raw_bytes();
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_EQ(raw.value()->filename(), temp_file.path());
}

TEST(XyceRawFileTest, load_real_binary_complex_flag_false) {
    // arrange
    const std::string content = make_raw_bytes("RC Circuit", "Transient Analysis", "real");
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_FALSE(raw.value()->is_complex());
}

TEST(XyceRawFileTest, load_real_binary_abscissa_values) {
    // arrange
    const std::vector<std::vector<double>> data_matrix = {{0.0, 1.0}, {1e-9, 1.1}, {2e-9, 1.2}};
    const std::string content = make_raw_bytes("Test Circuit", "Transient Analysis", "real", {{0, "time", "time"}, {1, "V(1)", "voltage"}}, data_matrix);
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    const auto& abscissa_data = raw.value()->abscissa().step_data(0);
    ASSERT_EQ(abscissa_data.size(), 3);
    ASSERT_DOUBLE_EQ(abscissa_data[0], 0.0);
    ASSERT_DOUBLE_EQ(abscissa_data[1], 1e-9);
    ASSERT_DOUBLE_EQ(abscissa_data[2], 2e-9);
}

TEST(XyceRawFileTest, load_real_binary_abscissa_scale_is_linear) {
    // arrange
    const std::string content = make_raw_bytes();
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_EQ(raw.value()->abscissa_scale(), AbscissaScale::LINEAR);
}

// ========================================================================================
// plot type classification
// ========================================================================================

TEST(XyceRawFileTest, load_plot_type_transient) {
    // arrange
    const std::string content = make_raw_bytes("RC Circuit", "Transient Analysis");
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_EQ(raw.value()->plot_type(), PlotType::TRANSIENT);
}

TEST(XyceRawFileTest, load_plot_type_ac) {
    // arrange
    const std::vector<TestVarDef> variable_definitions = {{0, "frequency", "frequency"}, {1, "V(out)", "voltage"}};
    const std::vector<std::vector<double>> data_matrix = {{1e3, 1.0}, {1e4, 2.0}};
    const std::string content = make_raw_bytes("AC Sweep Test", "AC Analysis", "real", variable_definitions, data_matrix);
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_EQ(raw.value()->plot_type(), PlotType::AC);
}

TEST(XyceRawFileTest, load_plot_type_noise) {
    // arrange
    const std::vector<TestVarDef> variable_definitions = {{0, "frequency", "frequency"}, {1, "V(out)", "voltage"}};
    const std::vector<std::vector<double>> data_matrix = {{1e3, 1.0}, {1e4, 2.0}};
    const std::string content = make_raw_bytes("Noise Sweep Test", "Noise Analysis", "real", variable_definitions, data_matrix);
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_EQ(raw.value()->plot_type(), PlotType::NOISE);
}

TEST(XyceRawFileTest, load_plot_type_dc) {
    // arrange
    const std::vector<TestVarDef> variable_definitions = {{0, "sweep", "voltage"}, {1, "V(out)", "voltage"}};
    const std::vector<std::vector<double>> data_matrix = {{0.0, 0.0}, {1.0, 0.5}, {2.0, 1.0}};
    const std::string content = make_raw_bytes("DC Sweep Test", "DC transfer characteristic", "real", variable_definitions, data_matrix);
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_EQ(raw.value()->plot_type(), PlotType::DC);
}

TEST(XyceRawFileTest, load_plot_type_dc_operating_point) {
    // arrange
    const std::vector<TestVarDef> variable_definitions = {{0, "sweep", "voltage"}, {1, "V(out)", "voltage"}};
    const std::vector<std::vector<double>> data_matrix = {{0.5, 1.0}};
    const std::string content = make_raw_bytes("DCOP Test", "DC operating point", "real", variable_definitions, data_matrix);
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_EQ(raw.value()->plot_type(), PlotType::DC_OPERATING_POINT);
}

TEST(XyceRawFileTest, load_stepped_plotname_classified_as_dc) {
    // arrange
    const std::vector<TestVarDef> variable_definitions = {{0, "sweep", "voltage"}, {1, "V(out)", "voltage"}};
    const std::vector<std::vector<double>> data_matrix = {{0.0, 0.0}, {1.0, 0.5}, {2.0, 1.0}};
    const std::string plotname = "Step Analysis: Step 1 of 2 params:  name = R1_VAL value = 1000  DC transfer characteristic";
    const std::string content = make_raw_bytes("Stepped DC Sweep", plotname, "real", variable_definitions, data_matrix);
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_EQ(raw.value()->plot_type(), PlotType::DC);
}

TEST(XyceRawFileTest, load_stepped_plotname_classified_as_transient) {
    // arrange
    const std::vector<std::vector<double>> data_matrix = {{0.0, 0.0}, {1e-9, 1.0}, {2e-9, 1.1}};
    const std::string plotname = "Step Analysis: Step 1 of 3 params:  name = L1 value = 0.01  Transient Analysis";
    const std::string content = make_raw_bytes("Stepped Transient", plotname, "real", {{0, "time", "time"}, {1, "V(1)", "voltage"}}, data_matrix);
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_EQ(raw.value()->plot_type(), PlotType::TRANSIENT);
}

TEST(XyceRawFileTest, load_unknown_plotname_is_unknown) {
    // arrange
    const std::string content = make_raw_bytes("Weird Circuit", "Custom Analysis Output");
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_EQ(raw.value()->plot_type(), PlotType::UNKNOWN);
}

TEST(XyceRawFileTest, load_missing_plotname_is_unknown) {
    // arrange
    const std::string header = "Title: Test\nFlags: real\nNo. Variables: 2\nNo. Points: 2\nVariables:\n\t0\ttime\ttime\n\t1\tV(1)\tvoltage\nBinary:\n";
    const std::vector<double> payload = {0.0, 1.0, 1e-9, 1.1};
    std::string content = header;
    for (double value : payload) {
        // append payload value
        content.append(reinterpret_cast<const char*>(&value), sizeof(double));
    }
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_EQ(raw.value()->plot_type(), PlotType::UNKNOWN);
    ASSERT_EQ(raw.value()->abscissa_scale(), AbscissaScale::LINEAR);
}

// ========================================================================================
// abscissa scale detection
// ========================================================================================

TEST(XyceRawFileTest, load_ac_decade_abscissa_scale) {
    // arrange
    const std::vector<TestVarDef> variable_definitions = {{0, "frequency", "frequency"}, {1, "V(out)", "voltage"}};
    std::vector<std::vector<double>> data_matrix;
    for (size_t i = 0; i <= 20; ++i) {
        // decade spacing with ten points per decade
        data_matrix.push_back({std::pow(10.0, static_cast<double>(i) / 10.0), 1.0});
    }
    const std::string content = make_raw_bytes("AC Decade Sweep", "AC Analysis", "real", variable_definitions, data_matrix);
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_EQ(raw.value()->abscissa_scale(), AbscissaScale::DECADE);
}

TEST(XyceRawFileTest, load_ac_octave_abscissa_scale) {
    // arrange
    const std::vector<TestVarDef> variable_definitions = {{0, "frequency", "frequency"}, {1, "V(out)", "voltage"}};
    std::vector<std::vector<double>> data_matrix;
    for (size_t i = 0; i <= 20; ++i) {
        // octave spacing with twenty points per octave
        data_matrix.push_back({std::pow(2.0, static_cast<double>(i) / 20.0), 1.0});
    }
    const std::string content = make_raw_bytes("AC Octave Sweep", "AC Analysis", "real", variable_definitions, data_matrix);
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_EQ(raw.value()->abscissa_scale(), AbscissaScale::OCTAVE);
}

TEST(XyceRawFileTest, load_ac_linear_abscissa_scale) {
    // arrange
    const std::vector<TestVarDef> variable_definitions = {{0, "frequency", "frequency"}, {1, "V(out)", "voltage"}};
    std::vector<std::vector<double>> data_matrix;
    for (size_t i = 0; i <= 20; ++i) {
        // linear spacing
        data_matrix.push_back({static_cast<double>(i), 1.0});
    }
    const std::string content = make_raw_bytes("AC Linear Sweep", "AC Analysis", "real", variable_definitions, data_matrix);
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_EQ(raw.value()->abscissa_scale(), AbscissaScale::LINEAR);
}

TEST(XyceRawFileTest, load_ac_two_points_abscissa_scale_is_linear) {
    // arrange
    const std::vector<TestVarDef> variable_definitions = {{0, "frequency", "frequency"}, {1, "V(out)", "voltage"}};
    const std::vector<std::vector<double>> data_matrix = {{1e3, 1.0}, {1e4, 2.0}};
    const std::string content = make_raw_bytes("AC Short Sweep", "AC Analysis", "real", variable_definitions, data_matrix);
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_EQ(raw.value()->abscissa_scale(), AbscissaScale::LINEAR);
}

TEST(XyceRawFileTest, load_transient_geometric_data_scale_is_linear) {
    // arrange
    std::vector<std::vector<double>> data_matrix;
    for (size_t i = 0; i <= 20; ++i) {
        // geometric time spacing would be misdetected without the transient domain gate
        data_matrix.push_back({std::pow(10.0, static_cast<double>(i) / 10.0), 1.0});
    }
    const std::string content = make_raw_bytes("Transient Gate", "Transient Analysis", "real", {{0, "time", "time"}, {1, "V(1)", "voltage"}}, data_matrix);
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_EQ(raw.value()->plot_type(), PlotType::TRANSIENT);
    ASSERT_EQ(raw.value()->abscissa_scale(), AbscissaScale::LINEAR);
}

TEST(XyceRawFileTest, load_dc_operating_point_geometric_data_scale_is_linear) {
    // arrange
    std::vector<std::vector<double>> data_matrix;
    for (size_t i = 0; i <= 20; ++i) {
        // geometric spacing would be misdetected without the DC operating point domain gate
        data_matrix.push_back({std::pow(10.0, static_cast<double>(i) / 10.0), 1.0});
    }
    const std::string content = make_raw_bytes("DCOP Gate", "DC operating point", "real", {{0, "sweep", "voltage"}, {1, "V(out)", "voltage"}}, data_matrix);
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_EQ(raw.value()->plot_type(), PlotType::DC_OPERATING_POINT);
    ASSERT_EQ(raw.value()->abscissa_scale(), AbscissaScale::LINEAR);
}

TEST(XyceRawFileTest, load_dc_decade_abscissa_scale) {
    // arrange
    const std::vector<TestVarDef> variable_definitions = {{0, "sweep", "voltage"}, {1, "V(out)", "voltage"}};
    std::vector<std::vector<double>> data_matrix;
    for (size_t i = 0; i <= 20; ++i) {
        // decade spacing with ten points per decade
        data_matrix.push_back({std::pow(10.0, static_cast<double>(i) / 10.0), 1.0});
    }
    const std::string content = make_raw_bytes("DC Decade Sweep", "DC transfer characteristic", "real", variable_definitions, data_matrix);
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_EQ(raw.value()->plot_type(), PlotType::DC);
    ASSERT_EQ(raw.value()->abscissa_scale(), AbscissaScale::DECADE);
}

TEST(XyceRawFileTest, load_dc_current_source_decade_abscissa_scale) {
    // arrange
    const std::vector<TestVarDef> variable_definitions = {{0, "sweep", "current"}, {1, "V(out)", "voltage"}};
    std::vector<std::vector<double>> data_matrix;
    for (size_t i = 0; i <= 20; ++i) {
        // decade spacing with ten points per decade
        data_matrix.push_back({std::pow(10.0, static_cast<double>(i) / 10.0), 1.0});
    }
    const std::string content = make_raw_bytes("DC Current Source Sweep", "DC transfer characteristic", "real", variable_definitions, data_matrix);
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_EQ(raw.value()->plot_type(), PlotType::DC);
    ASSERT_EQ(raw.value()->abscissa_scale(), AbscissaScale::DECADE);
}

TEST(XyceRawFileTest, load_real_binary_single_step) {
    // arrange
    const std::vector<std::vector<double>> data_matrix = {{0.0, 1.0}, {1e-9, 1.1}, {2e-9, 1.2}};
    const std::string content = make_raw_bytes("RC Circuit", "Transient Analysis", "real", {{0, "time", "time"}, {1, "V(1)", "voltage"}}, data_matrix);
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_EQ(raw.value()->step_information().length(), 1);
}

TEST(XyceRawFileTest, load_chart_type_transient) {
    // arrange
    const std::string content = make_raw_bytes();
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_EQ(raw.value()->abscissa().variable_type(), "time");
}

TEST(XyceRawFileTest, load_chart_type_dc) {
    // arrange
    const std::vector<TestVarDef> variable_definitions = {{0, "v(v-sweep)", "voltage"}, {1, "V(out)", "voltage"}};
    const std::vector<std::vector<double>> data_matrix = {{0.0, 0.0}, {1.0, 0.5}, {2.0, 1.0}};
    const std::string content = make_raw_bytes("DC Sweep Test", "DC transfer characteristic", "real", variable_definitions, data_matrix);
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_EQ(raw.value()->abscissa().variable_type(), "voltage");
}

TEST(XyceRawFileTest, load_dc_sweep_abscissa_is_unknown) {
    // arrange
    const std::vector<TestVarDef> variable_definitions = {{0, "sweep", "voltage"}, {1, "V(out)", "voltage"}};
    const std::vector<std::vector<double>> data_matrix = {{0.0, 0.0}, {1.0, 0.5}, {2.0, 1.0}};
    const std::string content = make_raw_bytes("DC Sweep Test", "DC transfer characteristic", "real", variable_definitions, data_matrix);
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_EQ(raw.value()->abscissa().variable_type(), "unknown");
    ASSERT_TRUE(raw.value()->abscissa().unit().empty());
}

TEST(XyceRawFileTest, load_power_variable_classified_as_power) {
    // arrange
    const std::vector<TestVarDef> variable_definitions = {{0, "time", "time"}, {1, "P(L1)", "unknown"}, {2, "V(out)", "voltage"}};
    const std::vector<std::vector<double>> data_matrix = {{0.0, 0.0, 0.0}, {1e-9, 1.0, 0.5}};
    const std::string content = make_raw_bytes("Power Test", "Transient Analysis", "real", variable_definitions, data_matrix);
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    auto* power = evaluate_real(raw.value()->expression_manager(), "P(L1)");
    ASSERT_NE(power, nullptr);
    ASSERT_EQ(power->variable_type(), "power");
    ASSERT_EQ(power->unit(), "W");
}

TEST(XyceRawFileTest, load_power_variable_with_explicit_type_still_power) {
    // arrange
    const std::vector<TestVarDef> variable_definitions = {{0, "time", "time"}, {1, "P(L1)", "voltage"}};
    const std::vector<std::vector<double>> data_matrix = {{0.0, 0.0}, {1e-9, 1.0}};
    const std::string content = make_raw_bytes("Power Test", "Transient Analysis", "real", variable_definitions, data_matrix);
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    auto* power = evaluate_real(raw.value()->expression_manager(), "P(L1)");
    ASSERT_NE(power, nullptr);
    ASSERT_EQ(power->variable_type(), "power");
    ASSERT_EQ(power->unit(), "W");
}

TEST(XyceRawFileTest, load_binary_with_trailing_content_ignored) {
    // arrange
    std::string content = make_raw_bytes();
    content += "\nSome extra CSV junk\n1,2,3\n4,5,6\n";
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_EQ(raw.value()->abscissa().step_data(0).size(), 2);
}

TEST(XyceRawFileTest, load_skips_malformed_variable_lines) {
    // arrange
    const std::string header = "Title: Test\nDate: Mon Jan 1 00:00:00 2024\nPlotname: Transient Analysis\nFlags: real\nNo. Variables: 2\nNo. Points: 1\nVariables:\n\t0\ttime\ttime\n\tBAD LINE\n\t1\tV(1)\tvoltage\nBinary:\n";
    const std::vector<double> row = {0.0, 1.0};
    std::string content = header;
    for (double val : row) {
        content.append(reinterpret_cast<const char*>(&val), sizeof(double));
    }
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_EQ(raw.value()->expression_manager().expressions().size(), 2);
}

TEST(XyceRawFileTest, load_expression_manager_contains_all_variables) {
    // arrange
    const std::vector<TestVarDef> variable_definitions = {{0, "time", "time"}, {1, "V(1)", "voltage"}, {2, "I(R1)", "current"}};
    const std::vector<std::vector<double>> data_matrix = {{0.0, 1.0, 2.0}, {1e-9, 1.1, 2.1}};
    const std::string content = make_raw_bytes("RC Circuit", "Transient Analysis", "real", variable_definitions, data_matrix);
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_NE(raw.value()->expression_manager().evaluate("V(1)"), nullptr);
    ASSERT_NE(raw.value()->expression_manager().evaluate("I(R1)"), nullptr);
    ASSERT_NE(raw.value()->expression_manager().evaluate("time"), nullptr);
}

TEST(XyceRawFileTest, load_unknown_variable_type_still_loaded) {
    // arrange
    const std::vector<TestVarDef> variable_definitions = {{0, "time", "time"}, {1, "CUSTOM_SIG", "custom_type"}};
    const std::vector<std::vector<double>> data_matrix = {{0.0, 5.0}, {1.0, 6.0}};
    const std::string content = make_raw_bytes("Circuit", "Transient", "real", variable_definitions, data_matrix);
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_NE(raw.value()->expression_manager().evaluate("CUSTOM_SIG"), nullptr);
}

TEST(XyceRawFileTest, load_step_information_abscissa_range) {
    // arrange
    const std::vector<std::vector<double>> data_matrix = {{0.0, 1.0}, {1e-9, 1.1}, {2e-9, 1.2}};
    const std::string content = make_raw_bytes("RC Circuit", "Transient Analysis", "real", {{0, "time", "time"}, {1, "V(1)", "voltage"}}, data_matrix);
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_DOUBLE_EQ(raw.value()->step_information().step_abscissa_left_value(0), 0.0);
    ASSERT_DOUBLE_EQ(raw.value()->step_information().step_abscissa_right_value(0), 2e-9);
}

TEST(XyceRawFileTest, load_utf8_encoded_header) {
    // arrange
    const std::string content = make_raw_bytes("RC Schéma");
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_EQ(raw.value()->title(), "RC Schéma");
}

// ========================================================================================
// complex binary data parsing
// ========================================================================================

TEST(XyceRawFileTest, load_complex_binary_complex_flag_true) {
    // arrange
    const std::vector<TestVarDef> variable_definitions = {{0, "frequency", "frequency"}, {1, "V(out)", "voltage"}};
    const std::vector<std::vector<double>> data_matrix = {{1e3, 0.0, 0.5, 0.5}, {1e4, 0.0, 0.7, 0.3}};
    const std::string content = make_raw_bytes("AC Sweep Test", "AC Analysis", "complex", variable_definitions, data_matrix);
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_TRUE(raw.value()->is_complex());
}

TEST(XyceRawFileTest, load_complex_binary_mixed_case_flag_true) {
    // arrange
    const std::vector<TestVarDef> variable_definitions = {{0, "frequency", "frequency"}, {1, "V(out)", "voltage"}};
    const std::vector<std::vector<double>> data_matrix = {{1e3, 0.0, 0.5, 0.5}, {1e4, 0.0, 0.7, 0.3}};
    const std::string content = make_raw_bytes("AC Sweep Test", "AC Analysis", "CoMpLeX", variable_definitions, data_matrix);
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_TRUE(raw.value()->is_complex());
}

TEST(XyceRawFileTest, load_chart_type_ac) {
    // arrange
    const std::vector<TestVarDef> variable_definitions = {{0, "frequency", "frequency"}, {1, "V(out)", "voltage"}};
    const std::vector<std::vector<double>> data_matrix = {{1e3, 0.0, 0.5, 0.5}, {1e4, 0.0, 0.7, 0.3}};
    const std::string content = make_raw_bytes("AC Sweep Test", "AC Analysis", "complex", variable_definitions, data_matrix);
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_EQ(raw.value()->abscissa().variable_type(), "frequency");
}

TEST(XyceRawFileTest, load_complex_ac_abscissa_is_frequency) {
    // arrange
    const std::vector<TestVarDef> variable_definitions = {{0, "frequency", "frequency"}, {1, "V(out)", "voltage"}};
    const std::vector<std::vector<double>> data_matrix = {{1e3, 0.0, 1.0, 0.0}, {1e4, 0.0, 0.7, 0.7}, {1e5, 0.0, 0.0, 1.0}};
    const std::string content = make_raw_bytes("AC Sweep Test", "AC Analysis", "complex", variable_definitions, data_matrix);
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    const auto& abscissa_data = raw.value()->abscissa().step_data(0);
    ASSERT_EQ(abscissa_data.size(), 3);
    ASSERT_DOUBLE_EQ(abscissa_data[0], 1e3);
    ASSERT_DOUBLE_EQ(abscissa_data[1], 1e4);
    ASSERT_DOUBLE_EQ(abscissa_data[2], 1e5);
}

TEST(XyceRawFileTest, load_complex_ac_signal_is_complex) {
    // arrange
    const std::vector<TestVarDef> variable_definitions = {{0, "frequency", "frequency"}, {1, "V(out)", "voltage"}};
    const std::vector<std::vector<double>> data_matrix = {{1e3, 0.0, 0.5, 0.5}, {1e4, 0.0, 0.7, 0.3}};
    const std::string content = make_raw_bytes("AC Sweep Test", "AC Analysis", "complex", variable_definitions, data_matrix);
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    auto* v_out = evaluate_complex(raw.value()->expression_manager(), "V(out)");
    ASSERT_NE(v_out, nullptr);
    ASSERT_DOUBLE_EQ(v_out->step_data(0)[0].real(), 0.5);
    ASSERT_DOUBLE_EQ(v_out->step_data(0)[0].imag(), 0.5);
}

// ========================================================================================
// ascii format data parsing
// ========================================================================================

TEST(XyceRawFileTest, load_ascii_values_section) {
    // arrange
    const std::vector<std::vector<double>> data_matrix = {{0.0, 1.0}, {1e-9, 1.1}, {2e-9, 1.2}};
    const std::string content = make_raw_bytes("RC Circuit", "Transient Analysis", "real", {{0, "time", "time"}, {1, "V(1)", "voltage"}}, data_matrix, true);
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    const auto& abscissa_data = raw.value()->abscissa().step_data(0);
    ASSERT_EQ(abscissa_data.size(), 3);
    ASSERT_DOUBLE_EQ(abscissa_data[0], 0.0);
    ASSERT_DOUBLE_EQ(abscissa_data[1], 1e-9);
    ASSERT_DOUBLE_EQ(abscissa_data[2], 2e-9);
}

TEST(XyceRawFileTest, load_ascii_values_variable_data_correct) {
    // arrange
    const std::vector<std::vector<double>> data_matrix = {{0.0, 1.0}, {1e-9, 1.5}, {2e-9, 2.0}};
    const std::string content = make_raw_bytes("RC Circuit", "Transient Analysis", "real", {{0, "time", "time"}, {1, "V(out)", "voltage"}}, data_matrix, true);
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    auto* v_out = evaluate_real(raw.value()->expression_manager(), "V(out)");
    ASSERT_NE(v_out, nullptr);
    ASSERT_DOUBLE_EQ(v_out->step_data(0)[0], 1.0);
    ASSERT_DOUBLE_EQ(v_out->step_data(0)[1], 1.5);
    ASSERT_DOUBLE_EQ(v_out->step_data(0)[2], 2.0);
}

TEST(XyceRawFileTest, load_ascii_complex_ac) {
    // arrange
    const std::vector<TestVarDef> variable_definitions = {{0, "frequency", "frequency"}, {1, "V(out)", "voltage"}};
    const std::vector<std::vector<double>> data_matrix = {{1e3, 0.0, 0.5, 0.5}, {1e4, 0.0, 0.7, 0.3}};
    const std::string content = make_raw_bytes("AC Sweep Test", "AC Analysis", "complex", variable_definitions, data_matrix, true);
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_TRUE(raw.value()->is_complex());
    ASSERT_EQ(raw.value()->abscissa().variable_type(), "frequency");
    ASSERT_EQ(raw.value()->abscissa().step_data(0).size(), 2);
}

TEST(XyceRawFileTest, load_ascii_no_points_zero_reads_all) {
    // arrange
    const std::vector<std::vector<double>> data_matrix = {{0.0, 1.0}, {1e-9, 1.1}, {2e-9, 1.2}};
    const std::string content = make_raw_bytes("RC Circuit", "Transient Analysis", "real", {{0, "time", "time"}, {1, "V(1)", "voltage"}}, data_matrix, true, 0);
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_EQ(raw.value()->abscissa().step_data(0).size(), 3);
}

TEST(XyceRawFileTest, ascii_values_with_blank_lines) {
    // arrange
    const std::string content = "Title: Test Circuit\nPlotname: Transient Analysis\nFlags: real\nNo. Variables: 2\nNo. Points: 2\nVariables:\n\t0\ttime\ttime\n\t1\tV(1)\tvoltage\nValues:\n\n 0  0.0  1.0\n\n   \n 1  1.0  2.0\n";
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_EQ(raw.value()->abscissa().step_data(0).size(), 2);
}

// ========================================================================================
// multi-block (stepped simulation) data parsing
// ========================================================================================

TEST(XyceRawFileTest, multi_block_step_count) {
    // arrange
    const std::string content = make_multi_block_raw_bytes("Stepped Circuit", {{0, "sweep", "voltage"}, {1, "V(2)", "voltage"}}, {{{0.0, 1.0}, {1.0, 2.0}}, {{0.0, 1.5}, {1.0, 2.5}}}, "R1", {1000.0, 2000.0});
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_EQ(raw.value()->step_information().length(), 2);
}

TEST(XyceRawFileTest, multi_block_three_steps) {
    // arrange
    const std::string content = make_multi_block_raw_bytes();
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_EQ(raw.value()->step_information().length(), 3);
}

TEST(XyceRawFileTest, multi_block_step_parameter_keys) {
    // arrange
    const std::string content = make_multi_block_raw_bytes("Stepped Circuit", {{0, "sweep", "voltage"}, {1, "V(2)", "voltage"}}, {{{0.0, 1.0}, {1.0, 2.0}}, {{0.0, 1.5}, {1.0, 2.5}}}, "R1_VAL", {1000.0, 2000.0});
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_EQ(raw.value()->step_information().keys().size(), 1);
    ASSERT_EQ(raw.value()->step_information().keys()[0], "R1_VAL");
}

TEST(XyceRawFileTest, multi_block_step_parameter_values) {
    // arrange
    const std::string content = make_multi_block_raw_bytes("Stepped Circuit", {{0, "sweep", "voltage"}, {1, "V(2)", "voltage"}}, {{{0.0, 1.0}, {1.0, 2.0}}, {{0.0, 1.5}, {1.0, 2.5}}}, "R1", {1000.0, 2000.0});
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_EQ(raw.value()->step_information().values().size(), 2);
    ASSERT_DOUBLE_EQ(raw.value()->step_information().values()[0][0], 1000.0);
    ASSERT_DOUBLE_EQ(raw.value()->step_information().values()[1][0], 2000.0);
}

TEST(XyceRawFileTest, multi_block_expression_step_count) {
    // arrange
    const std::vector<std::vector<double>> matrix = {{0.0, 1.0}, {1.0, 2.0}, {2.0, 3.0}};
    const std::string content = make_multi_block_raw_bytes("Stepped Circuit", {{0, "sweep", "voltage"}, {1, "V(2)", "voltage"}}, {matrix, matrix}, "R1", {1000.0, 2000.0});
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_EQ(raw.value()->abscissa().step_count(), 2);
}

TEST(XyceRawFileTest, multi_block_step_data_correct_values) {
    // arrange
    const std::vector<std::vector<double>> m0 = {{0.0, 1.0}, {1.0, 2.0}, {2.0, 3.0}};
    const std::vector<std::vector<double>> m1 = {{0.0, 4.0}, {1.0, 5.0}, {2.0, 6.0}};
    const std::string content = make_multi_block_raw_bytes("Stepped Circuit", {{0, "sweep", "voltage"}, {1, "V(2)", "voltage"}}, {m0, m1}, "R1", {1000.0, 2000.0});
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    auto* v2_expr = evaluate_real(raw.value()->expression_manager(), "V(2)");
    ASSERT_NE(v2_expr, nullptr);
    ASSERT_DOUBLE_EQ(v2_expr->step_data(0)[0], 1.0);
    ASSERT_DOUBLE_EQ(v2_expr->step_data(0)[1], 2.0);
    ASSERT_DOUBLE_EQ(v2_expr->step_data(0)[2], 3.0);
    ASSERT_DOUBLE_EQ(v2_expr->step_data(1)[0], 4.0);
    ASSERT_DOUBLE_EQ(v2_expr->step_data(1)[1], 5.0);
    ASSERT_DOUBLE_EQ(v2_expr->step_data(1)[2], 6.0);
}

TEST(XyceRawFileTest, multi_block_abscissa_step_data_zero_copy) {
    // arrange
    const std::vector<std::vector<double>> m0 = {{0.0, 1.0}, {1.0, 2.0}};
    const std::vector<std::vector<double>> m1 = {{0.0, 3.0}, {1.0, 4.0}};
    const std::string content = make_multi_block_raw_bytes("Stepped Circuit", {{0, "sweep", "voltage"}, {1, "V(2)", "voltage"}}, {m0, m1}, "R1", {1000.0, 2000.0});
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_EQ(raw.value()->abscissa().step_count(), 2);
    ASSERT_EQ(raw.value()->abscissa().step_data(0).size(), 2);
    ASSERT_EQ(raw.value()->abscissa().step_data(1).size(), 2);
}

TEST(XyceRawFileTest, multi_block_data_property_concatenates) {
    // arrange
    const std::vector<std::vector<double>> m0 = {{0.0, 1.0}, {1.0, 2.0}};
    const std::vector<std::vector<double>> m1 = {{0.0, 3.0}, {1.0, 4.0}};
    const std::string content = make_multi_block_raw_bytes("Stepped Circuit", {{0, "sweep", "voltage"}, {1, "V(2)", "voltage"}}, {m0, m1}, "R1", {1000.0, 2000.0});
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    auto* v2_expr = evaluate_real(raw.value()->expression_manager(), "V(2)");
    ASSERT_NE(v2_expr, nullptr);
    ASSERT_EQ(v2_expr->data().size(), 4);
    ASSERT_DOUBLE_EQ(v2_expr->data()[0], 1.0);
    ASSERT_DOUBLE_EQ(v2_expr->data()[1], 2.0);
    ASSERT_DOUBLE_EQ(v2_expr->data()[2], 3.0);
    ASSERT_DOUBLE_EQ(v2_expr->data()[3], 4.0);
}

TEST(XyceRawFileTest, multi_block_title_from_first_block) {
    // arrange
    const std::string content = make_multi_block_raw_bytes("DC Stepped Test");
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_EQ(raw.value()->title(), "DC Stepped Test");
}

TEST(XyceRawFileTest, multi_block_abscissa_value_ranges) {
    // arrange
    const std::vector<std::vector<double>> m = {{0.0, 1.0}, {1.0, 2.0}, {2.0, 3.0}};
    const std::string content = make_multi_block_raw_bytes("Stepped Circuit", {{0, "sweep", "voltage"}, {1, "V(2)", "voltage"}}, {m, m}, "R1", {1000.0, 2000.0});
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_DOUBLE_EQ(raw.value()->step_information().step_abscissa_left_value(0), 0.0);
    ASSERT_DOUBLE_EQ(raw.value()->step_information().step_abscissa_right_value(0), 2.0);
    ASSERT_DOUBLE_EQ(raw.value()->step_information().step_abscissa_left_value(1), 0.0);
    ASSERT_DOUBLE_EQ(raw.value()->step_information().step_abscissa_right_value(1), 2.0);
}

TEST(XyceRawFileTest, load_real_binary_last_variable_materialization_does_not_read_past_file_end) {
    // arrange: a real binary RAW file whose single data block ends exactly at EOF;
    // materializing the last variable (index 599) must not sweep the contiguous
    // tile window past the last valid element, otherwise the copy reads
    // 599 * sizeof(double) = 4792 bytes past the end of the mapped file, which
    // exceeds any mmap page tail and faults (issue #219)
    constexpr size_t num_variables = 600;
    constexpr size_t num_points = 64;
    // variable definition lines
    std::string variable_lines;
    for (size_t i = 0; i < num_variables; ++i)
        variable_lines += "\t" + std::to_string(i) + "\t" + (i == 0 ? "time" : "V(" + std::to_string(i) + ")") + "\t" + (i == 0 ? "time" : "voltage") + "\n";
    // header followed by interleaved binary payload: point-major, value p * 1000 + variable index
    std::string content = "Title: Test Circuit\nPlotname: Transient Analysis\nFlags: real\nNo. Variables: 600\nNo. Points: 64\nVariables:\n" + variable_lines + "Binary:\n";
    for (size_t p = 0; p < num_points; ++p)
        for (size_t i = 0; i < num_variables; ++i) {
            const double value = static_cast<double>(p) * 1000.0 + static_cast<double>(i);
            content.append(reinterpret_cast<const char*>(&value), sizeof(value));
        }
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    auto* last_expr = evaluate_real(raw.value()->expression_manager(), "V(599)");
    ASSERT_NE(last_expr, nullptr);
    const auto data = last_expr->data();
    ASSERT_EQ(data.size(), num_points);
    for (size_t p = 0; p < num_points; ++p)
        ASSERT_DOUBLE_EQ(data[p], static_cast<double>(p) * 1000.0 + 599.0);
    ASSERT_EQ(last_expr->step_data(0).size(), num_points);
    ASSERT_DOUBLE_EQ(last_expr->step_data(0)[0], 599.0);
    // the abscissa (variable index 0) materializes from the same mapping
    ASSERT_EQ(raw.value()->abscissa().step_data(0).size(), num_points);
    ASSERT_DOUBLE_EQ(raw.value()->abscissa().step_data(0)[num_points - 1], 63000.0);
}

TEST(XyceRawFileTest, load_complex_binary_last_variable_materialization_does_not_read_past_file_end) {
    // arrange: a complex binary RAW file whose single data block ends exactly at
    // EOF; materializing the last complex variable (index 599) must not sweep
    // the contiguous tile window past the last valid element, otherwise the
    // copy reads 599 * sizeof(complex<double>) = 9584 bytes past the end of the
    // mapped file, which exceeds any mmap page tail and faults (issue #219)
    constexpr size_t num_variables = 600;
    constexpr size_t num_points = 64;
    // variable definition lines
    std::string variable_lines;
    for (size_t i = 0; i < num_variables; ++i)
        variable_lines += "\t" + std::to_string(i) + "\t" + (i == 0 ? "frequency" : "V(" + std::to_string(i) + ")") + "\t" + (i == 0 ? "frequency" : "voltage") + "\n";
    // header followed by interleaved binary payload: every variable slot (the
    // abscissa included) occupies one complex slot of real then imaginary part
    std::string content = "Title: AC Sweep Test\nPlotname: AC Analysis\nFlags: complex\nNo. Variables: 600\nNo. Points: 64\nVariables:\n" + variable_lines + "Binary:\n";
    for (size_t p = 0; p < num_points; ++p) {
        const double frequency = static_cast<double>(p) * 1000.0;
        const double zero = 0.0;
        content.append(reinterpret_cast<const char*>(&frequency), sizeof(frequency));
        content.append(reinterpret_cast<const char*>(&zero), sizeof(zero));
        for (size_t i = 1; i < num_variables; ++i) {
            const double real = static_cast<double>(p) * 1000.0 + static_cast<double>(i);
            const double imag = static_cast<double>(i) + 0.5;
            content.append(reinterpret_cast<const char*>(&real), sizeof(real));
            content.append(reinterpret_cast<const char*>(&imag), sizeof(imag));
        }
    }
    const TempFileRAII temp_file(content);
    // act
    auto raw = xyce_raw_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(raw.has_value());
    ASSERT_TRUE(raw.value()->is_complex());
    auto* last_expr = evaluate_complex(raw.value()->expression_manager(), "V(599)");
    ASSERT_NE(last_expr, nullptr);
    const auto data = last_expr->data();
    ASSERT_EQ(data.size(), num_points);
    for (size_t p = 0; p < num_points; ++p)
        ASSERT_EQ(data[p], std::complex<double>(static_cast<double>(p) * 1000.0 + 599.0, 599.5));
    ASSERT_EQ(last_expr->step_data(0).size(), num_points);
    ASSERT_EQ(last_expr->step_data(0)[0], std::complex<double>(599.0, 599.5));
    // the abscissa (variable index 0) materializes from the same mapping
    ASSERT_EQ(raw.value()->abscissa().step_data(0).size(), num_points);
    ASSERT_DOUBLE_EQ(raw.value()->abscissa().step_data(0)[num_points - 1], 63000.0);
}

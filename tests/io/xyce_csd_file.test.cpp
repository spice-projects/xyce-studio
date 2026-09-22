#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <vector>

#include "expression/expression.h"
#include "expression/expression_manager.h"
#include "io/xyce_csd_file.h"

namespace
{

    // temp file manager helper class
    class TempCsdFileRAII
    {
    public:
        // constructor
        explicit TempCsdFileRAII(const std::string& content) {
            static int counter = 0;
            // build unique path
            m_path = std::filesystem::temp_directory_path() / ("test_xyce_csd_" + std::to_string(counter++) + "_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".csd");
            // open file stream
            std::ofstream out(m_path, std::ios::binary);
            // write content
            out.write(content.data(), static_cast<std::streamsize>(content.size()));
            // close file
            out.close();
        }

        // destructor
        ~TempCsdFileRAII() {
            // delete the file when it exists
            if (std::filesystem::exists(m_path))
                std::filesystem::remove(m_path);
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

    // format a number the way Xyce writes probe values, in scientific notation
    auto format_number = [](double value) {
        // buffer for the formatted number
        char buffer[32];
        // format with nine decimals like the Xyce probe writers
        std::snprintf(buffer, sizeof(buffer), "%.9e", value);
        // return the formatted string
        return std::string(buffer);
    };

    // build a single real-valued csd block, mirroring the output of
    // N_IO_OutputterTimeProbe: #H header with quoted key/value fields, #N
    // variable names, #C sweep lines with count and value:column tokens in rows
    // of up to four entries, and a #; terminator
    std::string make_csd_real_block(const std::string& title, const std::string& analysis, const std::string& sweep_var, const std::vector<std::string>& var_names, const std::vector<double>& sweep_values, const std::vector<std::vector<double>>& var_data, const std::string& subtitle = "") {
        // initialize output string
        std::string s;
        // write block header
        s += "#H\n";
        // write source and version
        s += "SOURCE='Xyce' VERSION='7.10'\n";
        // write title
        s += "TITLE='" + title + "'\n";
        // write subtitle
        s += "SUBTITLE='" + subtitle + "'\n";
        // write time and date stamp
        s += "TIME='10:36:52 AM' DATE='Jun 21, 2026' \n";
        // write temperature
        s += "TEMPERATURE='27.0'\n";
        // write analysis type
        s += "ANALYSIS='" + analysis + "' SERIALNO='12345'\n";
        // write allvalues, complexvalues and nodes fields
        s += "ALLVALUES='NO' COMPLEXVALUES='NO' NODES='" + std::to_string(var_names.size()) + "'\n";
        // write sweep variable
        s += "SWEEPVAR='" + sweep_var + "' SWEEPMODE='VAR_STEP'\n";
        // write format field
        s += "FORMAT='0 VOLTSorAMPS;EFLOAT : NODEorBRANCH;NODE  '\n";
        // write digital data flag
        s += "DGTLDATA='NO'\n";
        // write variable names section
        s += "#N\n";
        // write variable names line
        std::string names_line;
        // loop names
        for (const auto& name : var_names)
            names_line += "'" + name + "' ";
        // append names line
        s += names_line + "\n";
        // write data points
        for (size_t i = 0; i < sweep_values.size(); ++i) {
            // write #C line with sweep value and count
            s += "#C " + format_number(sweep_values[i]) + " " + std::to_string(var_names.size()) + "\n";
            // write one value per variable, with column index
            for (size_t v = 0; v < var_names.size(); ++v) {
                // check data is available for the variable
                if (i < var_data[v].size())
                    s += format_number(var_data[v][i]) + ":" + std::to_string(v + 1) + "   ";
            }
            // end the row
            s += "\n";
        }
        // write block terminator
        s += "#;\n";
        // return content
        return s;
    }

    // build a single complex-valued csd block, mirroring the output of
    // N_IO_OutputterFrequencyProbe: COMPLEXVALUES='YES' and real/imag values
    std::string make_csd_complex_block(const std::string& title, const std::string& analysis, const std::string& sweep_var, const std::vector<std::string>& var_names, const std::vector<double>& sweep_values, const std::vector<std::vector<std::complex<double>>>& var_data) {
        // initialize output string
        std::string s;
        // write block header
        s += "#H\n";
        // write source and version
        s += "SOURCE='Xyce' VERSION='7.10'\n";
        // write title
        s += "TITLE='" + title + "'\n";
        // write subtitle
        s += "SUBTITLE='Xyce data'\n";
        // write analysis type
        s += "ANALYSIS='" + analysis + "' SERIALNO='12345'\n";
        // write allvalues, complexvalues and nodes fields
        s += "ALLVALUES='NO' COMPLEXVALUES='YES' NODES='" + std::to_string(var_names.size()) + "'\n";
        // write sweep variable
        s += "SWEEPVAR='" + sweep_var + "' SWEEPMODE='VAR_STEP'\n";
        // write variable names section
        s += "#N\n";
        // write variable names line
        std::string names_line;
        // append names
        for (const auto& name : var_names)
            names_line += "'" + name + "' ";
        // append names line
        s += names_line + "\n";
        // write data points
        for (size_t i = 0; i < sweep_values.size(); ++i) {
            // write #C line with sweep value and count
            s += "#C " + format_number(sweep_values[i]) + " " + std::to_string(var_names.size()) + "\n";
            // write real/imag values per variable
            for (size_t v = 0; v < var_names.size(); ++v) {
                // check data exists for the variable
                if (i < var_data[v].size()) {
                    // get the complex value
                    const auto& c = var_data[v][i];
                    // append the value with column index
                    s += format_number(c.real()) + "/" + format_number(c.imag()) + ":" + std::to_string(v + 1) + "   ";
                }
            }
            // end the row
            s += "\n";
        }
        // write block terminator
        s += "#;\n";
        // return content
        return s;
    }

    // evaluate a real expression from a file by name
    Expression<double>* evaluate_real(ExpressionManager& manager, const std::string& name) {
        // evaluate expression by name
        AnyExpression* expr = manager.evaluate(name);
        // cast to real
        return expr ? std::get_if<Expression<double>>(expr) : nullptr;
    }

    // evaluate a complex expression from a file by name
    Expression<std::complex<double>>* evaluate_complex(ExpressionManager& manager, const std::string& name) {
        // evaluate expression by name
        AnyExpression* expr = manager.evaluate(name);
        // cast to complex
        return expr ? std::get_if<Expression<std::complex<double>>>(expr) : nullptr;
    }

} // namespace

// ========================================================================================
// file parsing failure scenarios
// ========================================================================================

TEST(XyceCsdFileTest, returns_nullopt_when_file_not_found) {
    // arrange
    const std::filesystem::path path = "/tmp/nonexistent_xyce_csd_abc123.csd";
    // act
    const auto result = xyce_csd_file_parser(path);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceCsdFileTest, returns_nullopt_when_file_is_empty) {
    // arrange
    const TempCsdFileRAII temp_file("");
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceCsdFileTest, returns_nullopt_when_file_has_no_block_header) {
    // arrange — content with no #H marker at all
    const std::string content = "TITLE='Test'\nSOME=VALUE\n";
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceCsdFileTest, returns_nullopt_when_block_has_no_names_section) {
    // arrange — #H present but the file ends before #N
    const std::string content = "#H\nTITLE='Test'\nANALYSIS='Transient Analysis'\n";
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceCsdFileTest, returns_nullopt_when_block_has_no_variable_names) {
    // arrange — #H/#N structure but the names line holds no quoted names
    const std::string content = "#H\nTITLE='Test'\nANALYSIS='Transient Analysis'\nSWEEPVAR='Time'\nCOMPLEXVALUES='NO'\n#N\ngarbage\n#C 0.0 1\n1.0:1\n#;\n";
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceCsdFileTest, returns_nullopt_when_variable_name_is_unterminated) {
    // arrange — the variable names line holds an unterminated quote
    const std::string content = "#H\nTITLE='Test'\nANALYSIS='Transient Analysis'\nSWEEPVAR='Time'\nCOMPLEXVALUES='NO'\n#N\n'V(out\n#C 0.0 1\n1.0:1\n#;\n";
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceCsdFileTest, returns_nullopt_when_block_has_no_data) {
    // arrange — valid #H/#N structure but the file ends before any data point
    const std::string content = "#H\nTITLE='Test'\nANALYSIS='Transient Analysis'\nSWEEPVAR='Time'\nCOMPLEXVALUES='NO'\n#N\n'V(out)'\n";
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceCsdFileTest, empty_block_before_valid_block_still_parses) {
    // arrange — a header block terminated without data, followed by a valid block
    const std::string empty_block = "#H\nTITLE='Test'\nANALYSIS='Transient Analysis'\nSWEEPVAR='Time'\nCOMPLEXVALUES='NO'\n#N\n'V(out)'\n#;\n";
    const std::string valid_block = make_csd_real_block("Test", "Transient Analysis", "Time", {"V(out)"}, {0.0, 1e-9}, {{1.0, 1.1}});
    const TempCsdFileRAII temp_file(empty_block + valid_block);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value()->step_information().length(), 1);
}

// ========================================================================================
// real data parsing
// ========================================================================================

TEST(XyceCsdFileTest, real_block_returns_valid_result) {
    // arrange
    const std::string content = make_csd_real_block("RC Circuit", "Transient Analysis", "Time", {"V(out)"}, {0.0, 1e-9, 2e-9}, {{1.0, 1.1, 1.2}});
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
}

TEST(XyceCsdFileTest, real_block_title_strips_comment_marker) {
    // arrange — Xyce writes the netlist name with a leading "* " comment marker
    const std::string content = make_csd_real_block("* RC Circuit", "Transient Analysis", "Time", {"V(out)"}, {0.0, 1e-9}, {{1.0, 1.1}});
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value()->title(), "RC Circuit");
}

TEST(XyceCsdFileTest, real_block_filename_is_set) {
    // arrange
    const std::string content = make_csd_real_block("Test", "Transient Analysis", "Time", {"V(out)"}, {0.0, 1e-9}, {{1.0, 1.1}});
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value()->filename(), temp_file.path());
}

TEST(XyceCsdFileTest, real_block_is_not_complex) {
    // arrange
    const std::string content = make_csd_real_block("Test", "Transient Analysis", "Time", {"V(out)"}, {0.0, 1e-9}, {{1.0, 1.1}});
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_FALSE(result.value()->is_complex());
}

TEST(XyceCsdFileTest, real_block_abscissa_values_are_correct) {
    // arrange
    const std::string content = make_csd_real_block("Test", "Transient Analysis", "Time", {"V(out)"}, {0.0, 1e-9, 2e-9}, {{1.0, 1.1, 1.2}});
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    // get the abscissa of the single step
    const auto& abscissa = result.value()->abscissa().step_data(0);
    // check the sweep values
    ASSERT_EQ(abscissa.size(), 3);
    ASSERT_DOUBLE_EQ(abscissa[0], 0.0);
    ASSERT_DOUBLE_EQ(abscissa[1], 1e-9);
    ASSERT_DOUBLE_EQ(abscissa[2], 2e-9);
}

TEST(XyceCsdFileTest, real_block_variable_data_is_correct) {
    // arrange
    const std::string content = make_csd_real_block("Test", "Transient Analysis", "Time", {"V(out)"}, {0.0, 1e-9, 2e-9}, {{3.0, 4.0, 5.0}});
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    // evaluate the output variable
    auto* v_out = evaluate_real(result.value()->expression_manager(), "V(out)");
    // check the values
    ASSERT_NE(v_out, nullptr);
    ASSERT_DOUBLE_EQ(v_out->step_data(0)[0], 3.0);
    ASSERT_DOUBLE_EQ(v_out->step_data(0)[1], 4.0);
    ASSERT_DOUBLE_EQ(v_out->step_data(0)[2], 5.0);
}

TEST(XyceCsdFileTest, real_block_expression_manager_contains_all_variables) {
    // arrange — transient output with a voltage, a current, a lead current, a power and an expression variable
    const std::string content = make_csd_real_block("Test", "Transient Analysis", "Time", {"V(out)", "I(R1)", "IC(Q1)", "P(R1)", "{abs(V(out))}"}, {0.0, 1e-9}, {{1.0, 1.1}, {0.1, 0.2}, {0.3, 0.4}, {0.5, 0.6}, {0.7, 0.8}});
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_NE(result.value()->expression_manager().evaluate("V(out)"), nullptr);
    ASSERT_NE(result.value()->expression_manager().evaluate("I(R1)"), nullptr);
    ASSERT_NE(result.value()->expression_manager().evaluate("IC(Q1)"), nullptr);
    ASSERT_NE(result.value()->expression_manager().evaluate("P(R1)"), nullptr);
    ASSERT_NE(result.value()->expression_manager().evaluate("{abs(V(out))}"), nullptr);
    ASSERT_NE(result.value()->expression_manager().evaluate("Time"), nullptr);
}

TEST(XyceCsdFileTest, real_block_variable_units_are_classified) {
    // arrange
    const std::string content = make_csd_real_block("Test", "Transient Analysis", "Time", {"V(out)", "I(R1)", "P(R1)"}, {0.0, 1e-9}, {{1.0, 1.1}, {0.1, 0.2}, {0.5, 0.6}});
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    // check the units of the expressions
    ASSERT_EQ(evaluate_real(result.value()->expression_manager(), "V(out)")->unit(), "V");
    ASSERT_EQ(evaluate_real(result.value()->expression_manager(), "I(R1)")->unit(), "A");
    ASSERT_EQ(evaluate_real(result.value()->expression_manager(), "P(R1)")->unit(), "W");
}

TEST(XyceCsdFileTest, real_block_abscissa_unit_is_seconds) {
    // arrange
    const std::string content = make_csd_real_block("Test", "Transient Analysis", "Time", {"V(out)"}, {0.0, 1e-9}, {{1.0, 1.1}});
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value()->abscissa().unit(), "s");
}

TEST(XyceCsdFileTest, single_block_step_count_is_one) {
    // arrange
    const std::string content = make_csd_real_block("Test", "Transient Analysis", "Time", {"V(out)"}, {0.0, 1e-9}, {{1.0, 1.1}});
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value()->step_information().length(), 1);
}

// ========================================================================================
// plot type classification
// ========================================================================================

TEST(XyceCsdFileTest, transient_analysis_produces_transient_plot_type) {
    // arrange
    const std::string content = make_csd_real_block("Test", "Transient Analysis", "Time", {"V(out)"}, {0.0, 1e-9}, {{1.0, 1.1}});
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value()->plot_type(), PlotType::TRANSIENT);
}

TEST(XyceCsdFileTest, dc_analysis_produces_dc_plot_type) {
    // arrange — a DC sweep of the source V1 carries the source name as sweep variable
    const std::string content = make_csd_real_block("Test", "DC Sweep", "V1", {"V(out)"}, {0.0, 1.0, 2.0}, {{0.0, 0.5, 1.0}});
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value()->plot_type(), PlotType::DC);
}

TEST(XyceCsdFileTest, dc_abscissa_of_voltage_sweep_is_voltage) {
    // arrange — sweeping source V1 sweeps a node voltage
    const std::string content = make_csd_real_block("Test", "DC Sweep", "V1", {"V(out)"}, {0.0, 1.0}, {{0.0, 0.5}});
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value()->abscissa().unit(), "V");
}

TEST(XyceCsdFileTest, dc_abscissa_of_current_sweep_is_current) {
    // arrange — sweeping source current I1
    const std::string content = make_csd_real_block("Test", "DC Sweep", "I1", {"V(out)"}, {0.0, 1.0}, {{0.0, 0.5}});
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value()->abscissa().unit(), "A");
}

TEST(XyceCsdFileTest, dc_abscissa_of_parameter_sweep_is_unclassified) {
    // arrange — sweeping a device parameter like R1:R carries no unit
    const std::string content = make_csd_real_block("Test", "DC Sweep", "R1:R", {"V(out)", "R1:R"}, {100.0, 200.0}, {{0.0, 0.5}, {100.0, 200.0}});
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value()->abscissa().unit(), "");
    // the device parameter output variable carries no unit either
    ASSERT_EQ(evaluate_real(result.value()->expression_manager(), "R1:R")->unit(), "");
}

TEST(XyceCsdFileTest, ac_analysis_produces_ac_plot_type) {
    // arrange
    const std::string content = make_csd_complex_block("Test", "AC Sweep", "FREQ", {"V(out)"}, {1e3, 1e4, 1e5}, {{{1.0, 0.0}, {0.9, 0.1}, {0.8, 0.2}}});
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value()->plot_type(), PlotType::AC);
}

TEST(XyceCsdFileTest, ac_ic_analysis_produces_transient_plot_type) {
    // arrange — AC_IC shares the AC Sweep analysis header but sweeps time and writes a .TD.csd file
    const std::string content = make_csd_real_block("Test", "AC Sweep", "Time", {"V(out)"}, {0.0, 1e-9, 2e-9}, {{1.0, 1.1, 1.2}});
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value()->plot_type(), PlotType::TRANSIENT);
}

TEST(XyceCsdFileTest, unknown_analysis_with_time_sweep_produces_transient_plot_type) {
    // arrange — no ANALYSIS header but the sweep variable is Time
    const std::string content = "#H\nTITLE='Test'\nSWEEPVAR='Time'\nCOMPLEXVALUES='NO'\n#N\n'V(out)'\n#C 0.0 1\n1.0:1\n#C 1e-9 1\n1.1:1\n#;\n";
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value()->plot_type(), PlotType::TRANSIENT);
}

TEST(XyceCsdFileTest, unknown_analysis_with_freq_sweep_produces_ac_plot_type) {
    // arrange — no ANALYSIS header but the sweep variable is FREQ
    const std::string content = "#H\nTITLE='Test'\nSWEEPVAR='FREQ'\nCOMPLEXVALUES='YES'\n#N\n'V(out)'\n#C 1e3 1\n1.0/0.0:1\n#C 1e4 1\n0.9/0.1:1\n#;\n";
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value()->plot_type(), PlotType::AC);
    ASSERT_EQ(result.value()->abscissa().unit(), "Hz");
}

TEST(XyceCsdFileTest, unclassified_sweep_var_defaults_time_name) {
    // arrange — no SWEEPVAR header and no recognizable analysis, the abscissa
    // falls back to the Time name with seconds unit
    const std::string content = "#H\nTITLE='Test'\n#N\n'V(out)'\n#C 0.0 1\n1.0:1\n#C 1e-9 1\n1.1:1\n#;\n";
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value()->abscissa().name(), "Time");
    ASSERT_EQ(result.value()->abscissa().unit(), "s");
}

TEST(XyceCsdFileTest, ac_abscissa_unit_is_hz) {
    // arrange
    const std::string content = make_csd_complex_block("Test", "AC Sweep", "FREQ", {"V(out)"}, {1e3, 1e4}, {{{1.0, 0.0}, {0.9, 0.1}}});
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value()->abscissa().unit(), "Hz");
}

TEST(XyceCsdFileTest, ac_variable_units_are_classified) {
    // arrange — AC output carries the real, imaginary, magnitude, decibel and phase operators
    // build the data matrix, one row of two points per variable
    std::vector<std::vector<std::complex<double>>> data;
    // append one row per variable
    for (size_t v = 0; v < 9; ++v)
        data.push_back({{1.0, 0.0}, {1.0, 0.0}});
    const std::string content = make_csd_complex_block("Test", "AC Sweep", "FREQ", {"V(out)", "VR(out)", "VI(out)", "VM(out)", "VDB(out)", "VP(out)", "I(R1)", "IR(R1)", "IP(R1)"}, {1e3, 1e4}, data);
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    // check the units of the complex expressions
    ASSERT_EQ(evaluate_complex(result.value()->expression_manager(), "V(out)")->unit(), "V");
    ASSERT_EQ(evaluate_complex(result.value()->expression_manager(), "VR(out)")->unit(), "V");
    ASSERT_EQ(evaluate_complex(result.value()->expression_manager(), "VI(out)")->unit(), "V");
    ASSERT_EQ(evaluate_complex(result.value()->expression_manager(), "VM(out)")->unit(), "V");
    ASSERT_EQ(evaluate_complex(result.value()->expression_manager(), "VDB(out)")->unit(), "V");
    ASSERT_EQ(evaluate_complex(result.value()->expression_manager(), "VP(out)")->unit(), "\u00b0");
    ASSERT_EQ(evaluate_complex(result.value()->expression_manager(), "I(R1)")->unit(), "A");
    ASSERT_EQ(evaluate_complex(result.value()->expression_manager(), "IR(R1)")->unit(), "A");
    ASSERT_EQ(evaluate_complex(result.value()->expression_manager(), "IP(R1)")->unit(), "\u00b0");
}

// ========================================================================================
// abscissa scale detection
// ========================================================================================

TEST(XyceCsdFileTest, ac_decade_abscissa_scale_is_detected) {
    // arrange — decade-spaced frequency points
    std::vector<double> freqs;
    std::vector<double> vals;
    // build 21 points, 10 per decade
    for (size_t i = 0; i <= 20; ++i) {
        freqs.push_back(std::pow(10.0, static_cast<double>(i) / 10.0));
        vals.push_back(1.0);
    }
    // build the complex data matrix from the real values
    std::vector<std::vector<std::complex<double>>> data;
    // promote each real value to a complex number
    for (double val : vals)
        data.push_back({std::complex<double>(val, 0.0)});
    const std::string content = make_csd_complex_block("AC Decade", "AC Sweep", "FREQ", {"V(out)"}, freqs, data);
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value()->abscissa_scale(), AbscissaScale::DECADE);
}

TEST(XyceCsdFileTest, ac_octave_abscissa_scale_is_detected) {
    // arrange — octave-spaced frequency points
    std::vector<double> freqs;
    std::vector<double> vals;
    // build 21 points, 20 per octave
    for (size_t i = 0; i <= 20; ++i) {
        freqs.push_back(std::pow(2.0, static_cast<double>(i) / 20.0));
        vals.push_back(1.0);
    }
    // build the complex data matrix from the real values
    std::vector<std::vector<std::complex<double>>> data;
    // promote each real value to a complex number
    for (double val : vals)
        data.push_back({std::complex<double>(val, 0.0)});
    const std::string content = make_csd_complex_block("AC Octave", "AC Sweep", "FREQ", {"V(out)"}, freqs, data);
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value()->abscissa_scale(), AbscissaScale::OCTAVE);
}

TEST(XyceCsdFileTest, ac_uniform_ratio_without_decade_or_octave_falls_to_decade) {
    // arrange — uniform log ratio that matches neither decade nor octave spacing
    std::vector<double> freqs;
    std::vector<double> vals;
    // build points with a uniform log ratio of 0.7
    for (size_t i = 0; i < 5; ++i) {
        freqs.push_back(std::pow(10.0, 0.7 * static_cast<double>(i)));
        vals.push_back(1.0);
    }
    // build the complex data matrix from the real values
    std::vector<std::vector<std::complex<double>>> data;
    // promote each real value to a complex number
    for (double val : vals)
        data.push_back({std::complex<double>(val, 0.0)});
    const std::string content = make_csd_complex_block("AC Ratio", "AC Sweep", "FREQ", {"V(out)"}, freqs, data);
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value()->abscissa_scale(), AbscissaScale::DECADE);
}

TEST(XyceCsdFileTest, transient_geometric_points_scale_is_linear) {
    // arrange — geometric time spacing must not be mistaken for a logarithmic sweep
    std::vector<double> times;
    std::vector<double> vals;
    // build 21 geometric points
    for (size_t i = 0; i <= 20; ++i) {
        times.push_back(std::pow(10.0, static_cast<double>(i) / 10.0));
        vals.push_back(1.0);
    }
    const std::string content = make_csd_real_block("TRAN Geo", "Transient Analysis", "Time", {"V(out)"}, times, {vals});
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value()->abscissa_scale(), AbscissaScale::LINEAR);
}

TEST(XyceCsdFileTest, linear_ac_abscissa_scale_is_linear) {
    // arrange — linearly spaced frequency points
    std::vector<double> freqs;
    std::vector<double> vals;
    // build 11 linear points
    for (size_t i = 0; i <= 10; ++i) {
        freqs.push_back(static_cast<double>(i) * 1000.0);
        vals.push_back(1.0);
    }
    // build the complex data matrix from the real values
    std::vector<std::vector<std::complex<double>>> data;
    // promote each real value to a complex number
    for (double val : vals)
        data.push_back({std::complex<double>(val, 0.0)});
    const std::string content = make_csd_complex_block("AC Lin", "AC Sweep", "FREQ", {"V(out)"}, freqs, data);
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value()->abscissa_scale(), AbscissaScale::LINEAR);
}

TEST(XyceCsdFileTest, scale_detection_uses_first_step_only) {
    // arrange — two steps, the first decade-spaced and the second linearly spaced
    std::string block0;
    // write the first block header, carrying the step parameter in the subtitle
    block0 += "#H\nTITLE='AC Steps'\nSUBTITLE='Step param R1 = 1000.0'\nANALYSIS='AC Sweep' SERIALNO='12345'\nCOMPLEXVALUES='YES' NODES='1'\nSWEEPVAR='FREQ' SWEEPMODE='VAR_STEP'\n#N\n'V(out)'\n";
    // build 11 decade-spaced points for the first step
    for (size_t i = 0; i <= 10; ++i) {
        // write the data point with the complex value
        block0 += "#C " + format_number(std::pow(10.0, static_cast<double>(i) / 10.0)) + " 1\n1.0/0.0:1\n";
    }
    // terminate the first block
    block0 += "#;\n";
    // write the second block, linearly spaced frequencies
    std::string block1;
    // write the second block header
    block1 += "#H\nTITLE='AC Steps'\nSUBTITLE='Step param R1 = 2000.0'\nANALYSIS='AC Sweep' SERIALNO='12345'\nCOMPLEXVALUES='YES' NODES='1'\nSWEEPVAR='FREQ' SWEEPMODE='VAR_STEP'\n#N\n'V(out)'\n";
    // build 11 linear points for the second step
    for (size_t i = 0; i <= 10; ++i) {
        // write the data point
        block1 += "#C " + format_number(1000.0 + static_cast<double>(i)) + " 1\n2.0/0.0:1\n";
    }
    // terminate the second block
    block1 += "#;\n";
    const TempCsdFileRAII temp_file(block0 + block1);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value()->abscissa_scale(), AbscissaScale::DECADE);
}

// ========================================================================================
// complex data parsing
// ========================================================================================

TEST(XyceCsdFileTest, complex_block_is_complex_flag_true) {
    // arrange
    const std::string content = make_csd_complex_block("AC Test", "AC Sweep", "FREQ", {"V(out)"}, {1e3, 1e4}, {{{0.5, 0.3}, {0.7, 0.1}}});
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result.value()->is_complex());
}

TEST(XyceCsdFileTest, complex_block_real_parts_are_correct) {
    // arrange
    const std::string content = make_csd_complex_block("AC Test", "AC Sweep", "FREQ", {"V(out)"}, {1e3, 1e4}, {{{0.5, 0.3}, {0.7, 0.1}}});
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    // evaluate the complex expression
    auto* v_out = evaluate_complex(result.value()->expression_manager(), "V(out)");
    // check the real parts
    ASSERT_NE(v_out, nullptr);
    ASSERT_DOUBLE_EQ(v_out->step_data(0)[0].real(), 0.5);
    ASSERT_DOUBLE_EQ(v_out->step_data(0)[1].real(), 0.7);
}

TEST(XyceCsdFileTest, complex_block_imaginary_parts_are_correct) {
    // arrange
    const std::string content = make_csd_complex_block("AC Test", "AC Sweep", "FREQ", {"V(out)"}, {1e3, 1e4}, {{{0.5, 0.3}, {0.7, 0.1}}});
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    // evaluate the complex expression
    auto* v_out = evaluate_complex(result.value()->expression_manager(), "V(out)");
    // check the imaginary parts
    ASSERT_NE(v_out, nullptr);
    ASSERT_DOUBLE_EQ(v_out->step_data(0)[0].imag(), 0.3);
    ASSERT_DOUBLE_EQ(v_out->step_data(0)[1].imag(), 0.1);
}

TEST(XyceCsdFileTest, complex_block_abscissa_values_are_correct) {
    // arrange
    const std::string content = make_csd_complex_block("AC Test", "AC Sweep", "FREQ", {"V(out)"}, {1e3, 1e4, 1e5}, {{{1.0, 0.0}, {0.9, 0.1}, {0.8, 0.2}}});
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    // get the abscissa of the single step
    const auto& abscissa = result.value()->abscissa().step_data(0);
    // check the sweep values
    ASSERT_EQ(abscissa.size(), 3);
    ASSERT_DOUBLE_EQ(abscissa[0], 1e3);
    ASSERT_DOUBLE_EQ(abscissa[1], 1e4);
    ASSERT_DOUBLE_EQ(abscissa[2], 1e5);
}

// ========================================================================================
// probe data token handling
// ========================================================================================

TEST(XyceCsdFileTest, data_tokens_without_column_suffix_are_accepted) {
    // arrange — values written without the :index suffix
    const std::string content = "#H\nTITLE='Test'\nANALYSIS='Transient Analysis'\nSWEEPVAR='Time'\nCOMPLEXVALUES='NO'\n#N\n'V(out)'\n#C 0.0 1\n1.0\n#C 1e-9 1\n1.1\n#;\n";
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    // check the values
    auto* v_out = evaluate_real(result.value()->expression_manager(), "V(out)");
    ASSERT_NE(v_out, nullptr);
    ASSERT_DOUBLE_EQ(v_out->step_data(0)[0], 1.0);
    ASSERT_DOUBLE_EQ(v_out->step_data(0)[1], 1.1);
}

TEST(XyceCsdFileTest, complex_token_without_imaginary_part_gets_zero_imag) {
    // arrange — a complex block token holding only the real part
    const std::string content = "#H\nTITLE='Test'\nANALYSIS='AC Sweep'\nSWEEPVAR='FREQ'\nCOMPLEXVALUES='YES'\n#N\n'V(out)'\n#C 1e3 1\n0.5:1\n#C 1e4 1\n0.7\n#;\n";
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    // check the imaginary parts are zero
    auto* v_out = evaluate_complex(result.value()->expression_manager(), "V(out)");
    ASSERT_NE(v_out, nullptr);
    ASSERT_DOUBLE_EQ(v_out->step_data(0)[0].real(), 0.5);
    ASSERT_DOUBLE_EQ(v_out->step_data(0)[0].imag(), 0.0);
    ASSERT_DOUBLE_EQ(v_out->step_data(0)[1].real(), 0.7);
}

TEST(XyceCsdFileTest, unparsable_data_token_contributes_zero_value) {
    // arrange — a token without a number contributes a zero value, keeping the columns aligned
    const std::string content = "#H\nTITLE='Test'\nANALYSIS='Transient Analysis'\nSWEEPVAR='Time'\nCOMPLEXVALUES='NO'\n#N\n'V(out)' 'I(R1)'\n#C 0.0 2\nabc:1 1.0:2\n#C 1e-9 2\n1.1:1 2.1:2\n#;\n";
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    // the unparsable value became a zero
    auto* v_out = evaluate_real(result.value()->expression_manager(), "V(out)");
    ASSERT_NE(v_out, nullptr);
    ASSERT_DOUBLE_EQ(v_out->step_data(0)[0], 0.0);
    ASSERT_DOUBLE_EQ(v_out->step_data(0)[1], 1.1);
    // the aligned column holds both values
    auto* i_r1 = evaluate_real(result.value()->expression_manager(), "I(R1)");
    ASSERT_NE(i_r1, nullptr);
    ASSERT_DOUBLE_EQ(i_r1->step_data(0)[0], 1.0);
    ASSERT_DOUBLE_EQ(i_r1->step_data(0)[1], 2.1);
}

TEST(XyceCsdFileTest, out_of_range_number_contributes_zero_value) {
    // arrange — a token beyond the double range parses as zero
    const std::string content = "#H\nTITLE='Test'\nANALYSIS='Transient Analysis'\nSWEEPVAR='Time'\nCOMPLEXVALUES='NO'\n#N\n'V(out)'\n#C 0.0 1\n1e999:1\n#C 1e-9 1\n1.1:1\n#;\n";
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    // the out-of-range value became a zero
    auto* v_out = evaluate_real(result.value()->expression_manager(), "V(out)");
    ASSERT_NE(v_out, nullptr);
    ASSERT_DOUBLE_EQ(v_out->step_data(0)[0], 0.0);
    ASSERT_DOUBLE_EQ(v_out->step_data(0)[1], 1.1);
}

TEST(XyceCsdFileTest, data_row_split_over_continuation_lines_is_supported) {
    // arrange — Xyce writes at most four values per row, so a five-variable
    // point continues on a second line
    const std::string content = "#H\nTITLE='Test'\nANALYSIS='Transient Analysis'\nSWEEPVAR='Time'\nCOMPLEXVALUES='NO'\n#N\n'V(1)' 'V(2)' 'V(3)' 'V(4)' 'V(5)'\n#C 0.0 5\n0.0:1   1.0:2   2.0:3   3.0:4\n4.0:5\n#;\n";
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    // check each column received its own value
    ASSERT_DOUBLE_EQ(evaluate_real(result.value()->expression_manager(), "V(1)")->step_data(0)[0], 0.0);
    ASSERT_DOUBLE_EQ(evaluate_real(result.value()->expression_manager(), "V(2)")->step_data(0)[0], 1.0);
    ASSERT_DOUBLE_EQ(evaluate_real(result.value()->expression_manager(), "V(3)")->step_data(0)[0], 2.0);
    ASSERT_DOUBLE_EQ(evaluate_real(result.value()->expression_manager(), "V(4)")->step_data(0)[0], 3.0);
    ASSERT_DOUBLE_EQ(evaluate_real(result.value()->expression_manager(), "V(5)")->step_data(0)[0], 4.0);
}

TEST(XyceCsdFileTest, data_point_with_fewer_values_than_names_pads_zeros) {
    // arrange — the #C line announces fewer values than the names section lists
    const std::string content = "#H\nTITLE='Test'\nANALYSIS='Transient Analysis'\nSWEEPVAR='Time'\nCOMPLEXVALUES='NO'\n#N\n'V(out)' 'I(R1)'\n#C 0.0 1\n1.0:1\n#C 1e-9 2\n1.1:1 2.1:2\n#;\n";
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    // the second variable received a zero for the first point
    auto* i_r1 = evaluate_real(result.value()->expression_manager(), "I(R1)");
    ASSERT_NE(i_r1, nullptr);
    ASSERT_DOUBLE_EQ(i_r1->step_data(0)[0], 0.0);
    ASSERT_DOUBLE_EQ(i_r1->step_data(0)[1], 2.1);
}

TEST(XyceCsdFileTest, data_point_with_surplus_tokens_ignores_the_extras) {
    // arrange — more value tokens arrive than the #C line announces
    const std::string content = "#H\nTITLE='Test'\nANALYSIS='Transient Analysis'\nSWEEPVAR='Time'\nCOMPLEXVALUES='NO'\n#N\n'V(out)'\n#C 0.0 1\n1.0:1 9.9:2\n#C 1e-9 1\n1.1:1\n#;\n";
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    // the surplus token did not corrupt the data
    auto* v_out = evaluate_real(result.value()->expression_manager(), "V(out)");
    ASSERT_NE(v_out, nullptr);
    ASSERT_DOUBLE_EQ(v_out->step_data(0)[0], 1.0);
    ASSERT_DOUBLE_EQ(v_out->step_data(0)[1], 1.1);
}

TEST(XyceCsdFileTest, data_point_with_invalid_count_falls_back_to_variable_count) {
    // arrange — the #C line carries a count token that is not a number
    const std::string content = "#H\nTITLE='Test'\nANALYSIS='Transient Analysis'\nSWEEPVAR='Time'\nCOMPLEXVALUES='NO'\n#N\n'V(out)' 'I(R1)'\n#C 0.0 broken\n1.0:1 2.0:2\n#C 1e-9 2\n1.1:1 2.1:2\n#;\n";
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    // both values of each point arrived
    auto* v_out = evaluate_real(result.value()->expression_manager(), "V(out)");
    ASSERT_NE(v_out, nullptr);
    ASSERT_DOUBLE_EQ(v_out->step_data(0)[0], 1.0);
    ASSERT_DOUBLE_EQ(v_out->step_data(0)[1], 1.1);
}

TEST(XyceCsdFileTest, malformed_point_line_is_skipped) {
    // arrange — a #C line without the sweep value is skipped and the parse continues
    const std::string content = "#H\nTITLE='Test'\nANALYSIS='Transient Analysis'\nSWEEPVAR='Time'\nCOMPLEXVALUES='NO'\n#N\n'V(out)'\n#C 0.0 1\n1.0:1\n#C broken\n#C 1e-9 1\n1.1:1\n#;\n";
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    // only the valid point arrived
    auto* v_out = evaluate_real(result.value()->expression_manager(), "V(out)");
    ASSERT_NE(v_out, nullptr);
    ASSERT_EQ(v_out->step_data(0).size(), 2);
    ASSERT_DOUBLE_EQ(v_out->step_data(0)[0], 1.0);
    ASSERT_DOUBLE_EQ(v_out->step_data(0)[1], 1.1);
}

TEST(XyceCsdFileTest, header_line_without_equals_sign_is_ignored) {
    // arrange — a stray line without key=value fields in the header section
    const std::string content = "#H\nTITLE='Test'\ngarbage line without equals\nANALYSIS='Transient Analysis'\nSWEEPVAR='Time'\nCOMPLEXVALUES='NO'\n#N\n'V(out)'\n#C 0.0 1\n1.0:1\n#;\n";
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value()->plot_type(), PlotType::TRANSIENT);
}

TEST(XyceCsdFileTest, header_with_unterminated_quote_keeps_remaining_text) {
    // arrange — a header value whose closing quote is missing keeps the rest of the line
    const std::string content = "#H\nTITLE='Test\nANALYSIS='Transient Analysis'\nSWEEPVAR='Time'\nCOMPLEXVALUES='NO'\n#N\n'V(out)'\n#C 0.0 1\n1.0:1\n#;\n";
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value()->title(), "Test");
}

TEST(XyceCsdFileTest, header_with_unquoted_value_is_accepted) {
    // arrange — an unquoted header value runs to the next whitespace
    const std::string content = "#H\nTITLE=Test VALUE=2\nANALYSIS='Transient Analysis'\nSWEEPVAR='Time'\nCOMPLEXVALUES='NO'\n#N\n'V(out)'\n#C 0.0 1\n1.0:1\n#;\n";
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
}

TEST(XyceCsdFileTest, empty_key_line_is_ignored) {
    // arrange — a line starting with an equals sign carries no key
    const std::string content = "#H\n=novalue\nTITLE='Test'\nANALYSIS='Transient Analysis'\nSWEEPVAR='Time'\nCOMPLEXVALUES='NO'\n#N\n'V(out)'\n#C 0.0 1\n1.0:1\n#;\n";
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
}

TEST(XyceCsdFileTest, names_line_without_quotes_is_ignored) {
    // arrange — a names line without quoted names contributes nothing but the parse continues
    const std::string content = "#H\nTITLE='Test'\nANALYSIS='Transient Analysis'\nSWEEPVAR='Time'\nCOMPLEXVALUES='NO'\n#N\n'V(out)'\nplain text line\n#C 0.0 1\n1.0:1\n#;\n";
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value()->step_information().length(), 1);
}

TEST(XyceCsdFileTest, variable_names_spanning_multiple_lines_are_collected) {
    // arrange — Xyce wraps the #N section at 128 characters and 12 variables per line
    const std::string content = "#H\nTITLE='Test'\nANALYSIS='Transient Analysis'\nSWEEPVAR='Time'\nCOMPLEXVALUES='NO'\n#N\n'V(1)' 'V(2)' 'V(3)'\n'V(4)' 'V(5)'\n#C 0.0 5\n0.0:1   1.0:2   2.0:3   3.0:4\n4.0:5\n#;\n";
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_NE(result.value()->expression_manager().evaluate("V(5)"), nullptr);
}

TEST(XyceCsdFileTest, names_line_with_partial_quotes_collects_first_name_only) {
    // arrange — the second name is unterminated, only the first is collected
    const std::string content = "#H\nTITLE='Test'\nANALYSIS='Transient Analysis'\nSWEEPVAR='Time'\nCOMPLEXVALUES='NO'\n#N\n'V(1)' 'V(2\n#C 0.0 1\n1.0:1\n#;\n";
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_NE(result.value()->expression_manager().evaluate("V(1)"), nullptr);
}

// ========================================================================================
// stepped simulation parsing
// ========================================================================================

TEST(XyceCsdFileTest, multi_block_step_count_is_correct) {
    // arrange — two blocks with matching topology form one stepped output
    const std::string block0 = make_csd_real_block("Test", "Transient Analysis", "Time", {"V(out)"}, {0.0, 1e-9}, {{1.0, 1.1}}, "Step param R1 = 1000.0");
    const std::string block1 = make_csd_real_block("Test", "Transient Analysis", "Time", {"V(out)"}, {0.0, 1e-9}, {{2.0, 2.1}}, "Step param R1 = 2000.0");
    const TempCsdFileRAII temp_file(block0 + block1);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value()->step_information().length(), 2);
}

TEST(XyceCsdFileTest, multi_block_step_data_is_correct_per_step) {
    // arrange — two stepped blocks; step 0 has V(out) = 1.0/1.1, step 1 = 2.0/2.1
    const std::string block0 = make_csd_real_block("Test", "Transient Analysis", "Time", {"V(out)"}, {0.0, 1e-9}, {{1.0, 1.1}}, "Step param R1 = 1000.0");
    const std::string block1 = make_csd_real_block("Test", "Transient Analysis", "Time", {"V(out)"}, {0.0, 1e-9}, {{2.0, 2.1}}, "Step param R1 = 2000.0");
    const TempCsdFileRAII temp_file(block0 + block1);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    // evaluate the output variable across the steps
    auto* v_out = evaluate_real(result.value()->expression_manager(), "V(out)");
    ASSERT_NE(v_out, nullptr);
    ASSERT_DOUBLE_EQ(v_out->step_data(0)[0], 1.0);
    ASSERT_DOUBLE_EQ(v_out->step_data(0)[1], 1.1);
    ASSERT_DOUBLE_EQ(v_out->step_data(1)[0], 2.0);
    ASSERT_DOUBLE_EQ(v_out->step_data(1)[1], 2.1);
}

TEST(XyceCsdFileTest, multi_block_step_parameter_key_is_extracted) {
    // arrange — the SUBTITLE carries "Step param R1 = value"; the key must be "R1"
    const std::string block0 = make_csd_real_block("Test", "Transient Analysis", "Time", {"V(out)"}, {0.0, 1e-9}, {{1.0, 1.1}}, "Step param R1 = 1000.0");
    const std::string block1 = make_csd_real_block("Test", "Transient Analysis", "Time", {"V(out)"}, {0.0, 1e-9}, {{2.0, 2.1}}, "Step param R1 = 2000.0");
    const TempCsdFileRAII temp_file(block0 + block1);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_FALSE(result.value()->step_information().keys().empty());
    ASSERT_EQ(result.value()->step_information().keys()[0], "R1");
}

TEST(XyceCsdFileTest, multi_block_step_parameter_values_are_extracted) {
    // arrange — two steps with R1 = 1000.0 and R1 = 2000.0
    const std::string block0 = make_csd_real_block("Test", "Transient Analysis", "Time", {"V(out)"}, {0.0, 1e-9}, {{1.0, 1.1}}, "Step param R1 = 1000.0");
    const std::string block1 = make_csd_real_block("Test", "Transient Analysis", "Time", {"V(out)"}, {0.0, 1e-9}, {{2.0, 2.1}}, "Step param R1 = 2000.0");
    const TempCsdFileRAII temp_file(block0 + block1);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    // check the parameter values of both steps
    ASSERT_EQ(result.value()->step_information().values().size(), 2);
    ASSERT_DOUBLE_EQ(result.value()->step_information().values()[0][0], 1000.0);
    ASSERT_DOUBLE_EQ(result.value()->step_information().values()[1][0], 2000.0);
}

TEST(XyceCsdFileTest, multi_block_abscissa_step_count_matches_blocks) {
    // arrange
    const std::string block0 = make_csd_real_block("Test", "Transient Analysis", "Time", {"V(out)"}, {0.0, 1e-9, 2e-9}, {{1.0, 1.1, 1.2}});
    const std::string block1 = make_csd_real_block("Test", "Transient Analysis", "Time", {"V(out)"}, {0.0, 1e-9, 2e-9}, {{2.0, 2.1, 2.2}});
    const TempCsdFileRAII temp_file(block0 + block1);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value()->abscissa().step_count(), 2);
}

TEST(XyceCsdFileTest, multi_block_title_comes_from_first_block) {
    // arrange
    const std::string block0 = make_csd_real_block("First Title", "Transient Analysis", "Time", {"V(out)"}, {0.0, 1e-9}, {{1.0, 1.1}}, "Step param R1 = 1000.0");
    const std::string block1 = make_csd_real_block("Second Title", "Transient Analysis", "Time", {"V(out)"}, {0.0, 1e-9}, {{2.0, 2.1}}, "Step param R1 = 2000.0");
    const TempCsdFileRAII temp_file(block0 + block1);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value()->title(), "First Title");
}

TEST(XyceCsdFileTest, multi_block_abscissa_value_ranges_are_correct) {
    // arrange
    const std::string block0 = make_csd_real_block("Test", "Transient Analysis", "Time", {"V(out)"}, {0.0, 1e-9, 2e-9}, {{1.0, 1.1, 1.2}}, "Step param R1 = 1000.0");
    const std::string block1 = make_csd_real_block("Test", "Transient Analysis", "Time", {"V(out)"}, {3e-9, 4e-9, 5e-9}, {{2.0, 2.1, 2.2}}, "Step param R1 = 2000.0");
    const TempCsdFileRAII temp_file(block0 + block1);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    // check the abscissa range of each step
    ASSERT_DOUBLE_EQ(result.value()->step_information().step_abscissa_left_value(0), 0.0);
    ASSERT_DOUBLE_EQ(result.value()->step_information().step_abscissa_right_value(0), 2e-9);
    ASSERT_DOUBLE_EQ(result.value()->step_information().step_abscissa_left_value(1), 3e-9);
    ASSERT_DOUBLE_EQ(result.value()->step_information().step_abscissa_right_value(1), 5e-9);
}

TEST(XyceCsdFileTest, multi_block_without_subtitle_uses_zero_parameter_values) {
    // arrange — the first block carries a subtitle, the second one does not
    const std::string block0 = make_csd_real_block("Test", "Transient Analysis", "Time", {"V(out)"}, {0.0, 1e-9}, {{1.0, 1.1}}, "Step param R1 = 1000.0");
    const std::string block1 = make_csd_real_block("Test", "Transient Analysis", "Time", {"V(out)"}, {0.0, 1e-9}, {{2.0, 2.1}});
    const TempCsdFileRAII temp_file(block0 + block1);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    // the second step parameter value falls back to zero
    ASSERT_EQ(result.value()->step_information().values().size(), 2);
    ASSERT_DOUBLE_EQ(result.value()->step_information().values()[1][0], 0.0);
}

TEST(XyceCsdFileTest, nested_dc_sweep_parameters_are_extracted) {
    // arrange — a DC sweep over two parameters writes SWEEP2PARM/SWEEP2VALUE in the DGTLDATA line
    const std::string content = "#H\nTITLE='Test'\nANALYSIS='DC Sweep'\nSWEEPVAR='V1'\nCOMPLEXVALUES='NO'\nDGTLDATA='NO'  SWEEP2PARM='R1' SWEEP2VALUE='1.000000000e+03' \n#N\n'V(out)'\n#C 0.0 1\n1.0:1\n#;\n";
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    // the nested sweep parameter was extracted
    ASSERT_FALSE(result.value()->step_information().keys().empty());
    ASSERT_EQ(result.value()->step_information().keys()[0], "R1");
    ASSERT_DOUBLE_EQ(result.value()->step_information().values()[0][0], 1000.0);
}

TEST(XyceCsdFileTest, nested_dc_sweep_without_value_key_is_ignored) {
    // arrange — a SWEEP2PARM without its SWEEP2VALUE pair contributes nothing
    const std::string content = "#H\nTITLE='Test'\nANALYSIS='DC Sweep'\nSWEEPVAR='V1'\nCOMPLEXVALUES='NO'\nDGTLDATA='NO'  SWEEP2PARM='R1' \n#N\n'V(out)'\n#C 0.0 1\n1.0:1\n#;\n";
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result.value()->step_information().keys().empty());
}

TEST(XyceCsdFileTest, subtitle_with_malformed_number_is_ignored) {
    // arrange — a subtitle pair whose value is not a number contributes nothing
    const std::string content = "#H\nTITLE='Test'\nSUBTITLE='Step param R1 = notanumber'\nANALYSIS='Transient Analysis'\nSWEEPVAR='Time'\nCOMPLEXVALUES='NO'\n#N\n'V(out)'\n#C 0.0 1\n1.0:1\n#;\n";
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result.value()->step_information().keys().empty());
}

// ========================================================================================
// block structure tolerance
// ========================================================================================

TEST(XyceCsdFileTest, stray_terminators_are_skipped) {
    // arrange — the AC probe writer may emit two #; in a row at the end of each .STEP
    const std::string content = make_csd_real_block("Test", "Transient Analysis", "Time", {"V(out)"}, {0.0, 1e-9}, {{1.0, 1.1}}) + "#;\n" + make_csd_real_block("Test", "Transient Analysis", "Time", {"V(out)"}, {0.0, 1e-9}, {{2.0, 2.1}}, "Step param R1 = 2000.0");
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value()->step_information().length(), 2);
}

TEST(XyceCsdFileTest, block_without_terminator_is_closed_by_next_header) {
    // arrange — the first block runs into the next #H marker without a #; terminator
    const std::string block0 = make_csd_real_block("Test", "Transient Analysis", "Time", {"V(out)"}, {0.0, 1e-9}, {{1.0, 1.1}});
    // strip the trailing "#;\n" terminator from the first block
    const std::string open_block = block0.substr(0, block0.size() - 3);
    const std::string block1 = make_csd_real_block("Test", "Transient Analysis", "Time", {"V(out)"}, {0.0, 1e-9}, {{2.0, 2.1}}, "Step param R1 = 2000.0");
    const TempCsdFileRAII temp_file(open_block + block1);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value()->step_information().length(), 2);
}

TEST(XyceCsdFileTest, content_before_first_header_is_ignored) {
    // arrange — garbage lines before the first #H marker
    const std::string content = "some log line\nanother line\n" + make_csd_real_block("Test", "Transient Analysis", "Time", {"V(out)"}, {0.0, 1e-9}, {{1.0, 1.1}});
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value()->step_information().length(), 1);
}

TEST(XyceCsdFileTest, blank_lines_are_ignored_everywhere) {
    // arrange — blank lines between the header, names and data lines
    const std::string content = "#H\n\nTITLE='Test'\n\nANALYSIS='Transient Analysis'\nSWEEPVAR='Time'\nCOMPLEXVALUES='NO'\n#N\n\n'V(out)'\n\n#C 0.0 1\n\n1.0:1\n\n#;\n";
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value()->step_information().length(), 1);
}

TEST(XyceCsdFileTest, windows_line_endings_are_supported) {
    // arrange — the file uses CRLF line endings
    const std::string block = make_csd_real_block("Test", "Transient Analysis", "Time", {"V(out)"}, {0.0, 1e-9}, {{1.0, 1.1}});
    // replace every LF with CRLF
    std::string content;
    for (char c : block) {
        // append carriage return before line feeds
        if (c == '\n')
            content += '\r';
        content += c;
    }
    const TempCsdFileRAII temp_file(content);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value()->step_information().length(), 1);
}

TEST(XyceCsdFileTest, topology_mismatch_aborts_the_parse) {
    // arrange — the second block carries a different variable list
    const std::string block0 = make_csd_real_block("Test", "Transient Analysis", "Time", {"V(out)"}, {0.0, 1e-9}, {{1.0, 1.1}}, "Step param R1 = 1000.0");
    const std::string block1 = make_csd_real_block("Test", "Transient Analysis", "Time", {"I(R1)"}, {0.0, 1e-9}, {{2.0, 2.1}}, "Step param R1 = 2000.0");
    const TempCsdFileRAII temp_file(block0 + block1);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceCsdFileTest, topology_mismatch_on_complex_flag_aborts_the_parse) {
    // arrange — the second block flips the complex flag
    const std::string block0 = make_csd_real_block("Test", "Transient Analysis", "Time", {"V(out)"}, {0.0, 1e-9}, {{1.0, 1.1}}, "Step param R1 = 1000.0");
    const std::string block1 = make_csd_complex_block("Test", "AC Sweep", "Time", {"V(out)"}, {0.0, 1e-9}, {{{2.0, 0.0}, {2.1, 0.0}}});
    const TempCsdFileRAII temp_file(block0 + block1);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(XyceCsdFileTest, unreadable_file_returns_nullopt) {
    // arrange — the file exists but cannot be opened for reading
    const TempCsdFileRAII temp_file("#H\nTITLE='Test'\n");
    // drop every permission from the file
    std::filesystem::permissions(temp_file.path(), std::filesystem::perms::none);
    // act
    const auto result = xyce_csd_file_parser(temp_file.path());
    // restore the permissions so the destructor can delete the file
    std::filesystem::permissions(temp_file.path(), std::filesystem::perms::owner_all);
    // assert
    ASSERT_FALSE(result.has_value());
}

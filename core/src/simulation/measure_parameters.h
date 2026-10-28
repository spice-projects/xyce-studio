#pragma once

#include <optional>
#include <string>

// representation of a single .MEASURE entry
struct MeasureEntry
{
    // construct a MeasureEntry with default empty fields
    MeasureEntry(std::string analysis_type = "", std::string result_name = "", std::string measure_type = "", std::string variable = "", std::string from_val = "", std::string to_val = "", std::string td_val = "", std::string rise_val = "", std::string fall_val = "", std::string cross_val = "", std::string minval = "", std::string default_val = "", std::string precision = "", std::string print_val = "", std::string at_val = "", std::string on_val = "", std::string off_val = "", std::string rfc_level = "", std::string output = "", std::string min_thresh = "", std::string max_thresh = "", std::string frac_max = "", std::string when_variable = "", std::string when_condition = "", std::string variable2 = "", std::string trig_variable = "", std::string trig_condition = "", std::string trig_val = "", std::string trig_frac_max = "", std::string trig_td = "", std::string trig_rise = "", std::string trig_fall = "", std::string trig_cross = "", std::string trig_at_val = "", std::string targ_variable = "", std::string targ_condition = "", std::string targ_val = "", std::string targ_frac_max = "", std::string targ_td = "", std::string targ_rise = "", std::string targ_fall = "", std::string targ_cross = "", std::string targ_at_val = "", std::string error_file = "", std::string indepvarcol = "", std::string depvarcol = "", std::string comp_function = "", std::string numfreq = "", std::string gridsize = "", std::string binsiz = "", std::string maxfreq = "", std::string minfreq = "", std::string nbharm = "", std::string goal = "", std::string weight = "");

    // parses a single directive into a MeasureEntry
    [[nodiscard]] static std::optional<MeasureEntry> from_xyce_statement(const std::string& statement);

    // serializes this entry into a .MEASURE statement string
    [[nodiscard]] std::string to_xyce_statement() const;

    // equality operator
    bool operator==(const MeasureEntry& other) const;

    std::string analysis_type;
    std::string result_name;
    std::string measure_type;
    std::string variable;

    // common qualifiers
    std::string from_val;
    std::string to_val;
    std::string td_val;
    std::string rise_val;
    std::string fall_val;
    std::string cross_val;
    std::string minval;
    std::string default_val;
    std::string precision;
    std::string print_val;

    // type-specific qualifiers
    std::string at_val;
    std::string on_val;
    std::string off_val;
    std::string rfc_level;
    std::string output;
    std::string min_thresh;
    std::string max_thresh;
    std::string frac_max;

    // WHEN clause support
    std::string when_variable;
    std::string when_condition;

    // second variable for ERR1/ERR2
    std::string variable2;

    // TRIG-TARG qualifiers
    std::string trig_variable;
    std::string trig_condition;
    std::string trig_val;
    std::string trig_frac_max;
    std::string trig_td;
    std::string trig_rise;
    std::string trig_fall;
    std::string trig_cross;
    std::string trig_at_val;
    std::string targ_variable;
    std::string targ_condition;
    std::string targ_val;
    std::string targ_frac_max;
    std::string targ_td;
    std::string targ_rise;
    std::string targ_fall;
    std::string targ_cross;
    std::string targ_at_val;

    // ERROR-specific qualifiers
    std::string error_file;
    std::string indepvarcol;
    std::string depvarcol;
    std::string comp_function;

    // FOUR-specific qualifiers
    std::string numfreq;
    std::string gridsize;

    // FFT-specific qualifiers
    std::string binsiz;
    std::string maxfreq;
    std::string minfreq;
    std::string nbharm;

    // HSPICE compatibility
    std::string goal;
    std::string weight;
};

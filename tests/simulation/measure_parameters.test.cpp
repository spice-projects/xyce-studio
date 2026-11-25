#include <string>

#include <gtest/gtest.h>

#include "simulation/measure_parameters.h"

// from_xyce_statement

TEST(MeasureEntryChecks, minimal_avg_measure) {
    // arrange
    const std::string statement = ".MEASURE TRAN avgAll AVG V(1)";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->analysis_type, "TRAN");
    ASSERT_EQ(result->result_name, "avgAll");
    ASSERT_EQ(result->measure_type, "AVG");
    ASSERT_EQ(result->variable, "V(1)");
    ASSERT_EQ(result->to_xyce_statement(), ".MEASURE TRAN avgAll AVG V(1)");
}

TEST(MeasureEntryChecks, minimal_max_measure) {
    // arrange
    const std::string statement = ".MEASURE TRAN maxV1 MAX V(1)";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->analysis_type, "TRAN");
    ASSERT_EQ(result->result_name, "maxV1");
    ASSERT_EQ(result->measure_type, "MAX");
    ASSERT_EQ(result->variable, "V(1)");
    ASSERT_EQ(result->to_xyce_statement(), ".MEASURE TRAN maxV1 MAX V(1)");
}

TEST(MeasureEntryChecks, minimal_min_measure) {
    // arrange
    const std::string statement = ".MEASURE TRAN minV1 MIN V(1)";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->analysis_type, "TRAN");
    ASSERT_EQ(result->result_name, "minV1");
    ASSERT_EQ(result->measure_type, "MIN");
    ASSERT_EQ(result->variable, "V(1)");
    ASSERT_EQ(result->to_xyce_statement(), ".MEASURE TRAN minV1 MIN V(1)");
}

TEST(MeasureEntryChecks, minimal_rms_measure) {
    // arrange
    const std::string statement = ".MEASURE TRAN rmsV1 RMS V(1)";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->analysis_type, "TRAN");
    ASSERT_EQ(result->result_name, "rmsV1");
    ASSERT_EQ(result->measure_type, "RMS");
    ASSERT_EQ(result->variable, "V(1)");
    ASSERT_EQ(result->to_xyce_statement(), ".MEASURE TRAN rmsV1 RMS V(1)");
}

TEST(MeasureEntryChecks, minimal_pp_measure) {
    // arrange
    const std::string statement = ".MEASURE TRAN ppV1 PP V(1)";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->analysis_type, "TRAN");
    ASSERT_EQ(result->result_name, "ppV1");
    ASSERT_EQ(result->measure_type, "PP");
    ASSERT_EQ(result->variable, "V(1)");
    ASSERT_EQ(result->to_xyce_statement(), ".MEASURE TRAN ppV1 PP V(1)");
}

TEST(MeasureEntryChecks, minimal_integ_measure) {
    // arrange
    const std::string statement = ".MEASURE TRAN integV1 INTEG V(1)";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->analysis_type, "TRAN");
    ASSERT_EQ(result->result_name, "integV1");
    ASSERT_EQ(result->measure_type, "INTEG");
    ASSERT_EQ(result->variable, "V(1)");
    ASSERT_EQ(result->to_xyce_statement(), ".MEASURE TRAN integV1 INTEG V(1)");
}

TEST(MeasureEntryChecks, minimal_eqn_measure) {
    // arrange
    const std::string statement = ".MEASURE TRAN eqn1 EQN V(1)";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->analysis_type, "TRAN");
    ASSERT_EQ(result->result_name, "eqn1");
    ASSERT_EQ(result->measure_type, "EQN");
    ASSERT_EQ(result->variable, "V(1)");
    ASSERT_EQ(result->to_xyce_statement(), ".MEASURE TRAN eqn1 EQN V(1)");
}

TEST(MeasureEntryChecks, avg_with_from_to) {
    // arrange
    const std::string statement = ".MEASURE TRAN avgAll AVG V(1) FROM=1m TO=5m";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->analysis_type, "TRAN");
    ASSERT_EQ(result->result_name, "avgAll");
    ASSERT_EQ(result->measure_type, "AVG");
    ASSERT_EQ(result->variable, "V(1)");
    ASSERT_EQ(result->from_val, "1m");
    ASSERT_EQ(result->to_val, "5m");
    ASSERT_EQ(result->to_xyce_statement(), ".MEASURE TRAN avgAll AVG V(1) FROM=1m TO=5m");
}

TEST(MeasureEntryChecks, avg_with_td) {
    // arrange
    const std::string statement = ".MEASURE TRAN avgAll AVG V(1) TD=10n";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->td_val, "10n");
    ASSERT_EQ(result->to_xyce_statement(), ".MEASURE TRAN avgAll AVG V(1) TD=10n");
}

TEST(MeasureEntryChecks, avg_with_rise_fall_cross) {
    // arrange
    const std::string statement = ".MEASURE TRAN avgAll AVG V(1) RISE=1 FALL=2 CROSS=3";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->rise_val, "1");
    ASSERT_EQ(result->fall_val, "2");
    ASSERT_EQ(result->cross_val, "3");
}

TEST(MeasureEntryChecks, avg_with_minval_default) {
    // arrange
    const std::string statement = ".MEASURE TRAN avgAll AVG V(1) MINVAL=0.5 DEFAULT=0";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->minval, "0.5");
    ASSERT_EQ(result->default_val, "0");
}

TEST(MeasureEntryChecks, avg_with_precision_print) {
    // arrange
    const std::string statement = ".MEASURE TRAN avgAll AVG V(1) PRECISION=6 PRINT=1";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->precision, "6");
    ASSERT_EQ(result->print_val, "1");
}

TEST(MeasureEntryChecks, avg_with_at_on_off) {
    // arrange
    const std::string statement = ".MEASURE TRAN avgAll AVG V(1) AT=1m ON=0.5 OFF=0.1";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->at_val, "1m");
    ASSERT_EQ(result->on_val, "0.5");
    ASSERT_EQ(result->off_val, "0.1");
}

TEST(MeasureEntryChecks, avg_with_rfc_level) {
    // arrange
    const std::string statement = ".MEASURE TRAN avgAll AVG V(1) RFC_LEVEL=0.5";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->rfc_level, "0.5");
}

TEST(MeasureEntryChecks, avg_with_output) {
    // arrange
    const std::string statement = ".MEASURE TRAN avgAll AVG V(1) OUTPUT=avg_result";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->output, "avg_result");
}

TEST(MeasureEntryChecks, avg_with_min_thresh_max_thresh) {
    // arrange
    const std::string statement = ".MEASURE TRAN avgAll AVG V(1) MIN_THRESH=0.1 MAX_THRESH=1.0";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->min_thresh, "0.1");
    ASSERT_EQ(result->max_thresh, "1.0");
}

TEST(MeasureEntryChecks, avg_with_frac_max) {
    // arrange
    const std::string statement = ".MEASURE TRAN avgAll AVG V(1) FRAC_MAX=0.9";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->frac_max, "0.9");
}

TEST(MeasureEntryChecks, avg_with_when_clause) {
    // arrange
    const std::string statement = ".MEASURE TRAN avgAll AVG V(1) WHEN V(IN)>0.5";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->when_variable, "V(IN)");
    ASSERT_EQ(result->when_condition, ">0.5");
}

TEST(MeasureEntryChecks, avg_with_variable2) {
    // arrange
    const std::string statement = ".MEASURE TRAN err ERR V(OUT) V(IN)";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->measure_type, "ERR");
    ASSERT_EQ(result->variable, "V(OUT)");
    ASSERT_EQ(result->variable2, "V(IN)");
}

TEST(MeasureEntryChecks, avg_with_trig_targ) {
    // arrange
    const std::string statement = ".MEASURE TRAN delay TRIG V(IN) VAL=0.5 TARG V(OUT) VAL=0.5";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->trig_variable, "V(IN)");
    ASSERT_EQ(result->trig_val, "0.5");
    ASSERT_EQ(result->targ_variable, "V(OUT)");
    ASSERT_EQ(result->targ_val, "0.5");
}

TEST(MeasureEntryChecks, avg_with_error_file) {
    // arrange
    const std::string statement = ".MEASURE TRAN avgAll AVG V(1) ERROR_FILE=error.txt";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->error_file, "error.txt");
}

TEST(MeasureEntryChecks, avg_with_indepvarcol_depvarcol) {
    // arrange
    const std::string statement = ".MEASURE TRAN avgAll AVG V(1) INDEPVARCOL=time DEPVARCOL=vout";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->indepvarcol, "time");
    ASSERT_EQ(result->depvarcol, "vout");
}

TEST(MeasureEntryChecks, avg_with_comp_function) {
    // arrange
    const std::string statement = ".MEASURE TRAN avgAll AVG V(1) COMP_FUNCTION=abs";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->comp_function, "abs");
}

TEST(MeasureEntryChecks, avg_with_numfreq_gridsize) {
    // arrange
    const std::string statement = ".MEASURE TRAN avgAll AVG V(1) NUMFREQ=10 GRIDSIZE=100";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->numfreq, "10");
    ASSERT_EQ(result->gridsize, "100");
}

TEST(MeasureEntryChecks, avg_with_binsiz_maxfreq_minfreq) {
    // arrange
    const std::string statement = ".MEASURE TRAN avgAll AVG V(1) BINSIZ=10 MAXFREQ=1MEG MINFREQ=1k";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->binsiz, "10");
    ASSERT_EQ(result->maxfreq, "1MEG");
    ASSERT_EQ(result->minfreq, "1k");
}

TEST(MeasureEntryChecks, avg_with_deriv_find_when_duty_freq_on_time_off_time) {
    // arrange
    const std::string statement = ".MEASURE TRAN deriv1 DERIV V(1) FIND V(1) WHEN V(IN)>0.5 DUTY V(OUT) ON=1m OFF=0.5m FREQ=1K ON_TIME=1m OFF_TIME=0.5m";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->measure_type, "DERIV");
    ASSERT_EQ(result->variable, "V(1)");
    ASSERT_EQ(result->when_variable, "V(IN)");
    ASSERT_EQ(result->when_condition, ">0.5");
    ASSERT_EQ(result->on_val, "1m");
    ASSERT_EQ(result->off_val, "0.5m");
}

TEST(MeasureEntryChecks, avg_with_fourier) {
    // arrange
    const std::string statement = ".MEASURE TRAN four1 FOUR V(1) WIN=rect FREQ=1K HARM=1";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->measure_type, "FOUR");
    ASSERT_EQ(result->variable, "V(1)");
    ASSERT_EQ(result->nbharm, "1");
}

TEST(MeasureEntryChecks, avg_with_err_trig_targ) {
    // arrange
    const std::string statement = ".MEASURE TRAN err1 ERR V(OUT) V(IN) TRIG V(IN) VAL=0.5 TARG V(OUT) VAL=0.5";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->measure_type, "ERR");
    ASSERT_EQ(result->variable, "V(OUT)");
    ASSERT_EQ(result->variable2, "V(IN)");
    ASSERT_EQ(result->trig_variable, "V(IN)");
    ASSERT_EQ(result->trig_val, "0.5");
    ASSERT_EQ(result->targ_variable, "V(OUT)");
    ASSERT_EQ(result->targ_val, "0.5");
}

TEST(MeasureEntryChecks, avg_with_multiple_extra_options) {
    // arrange
    const std::string statement = ".MEASURE TRAN avgAll AVG V(1) FROM=1m TO=5m TD=10n RISE=1 PRECISION=6 PRINT=1";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->from_val, "1m");
    ASSERT_EQ(result->to_val, "5m");
    ASSERT_EQ(result->td_val, "10n");
    ASSERT_EQ(result->rise_val, "1");
    ASSERT_EQ(result->precision, "6");
    ASSERT_EQ(result->print_val, "1");
}

TEST(MeasureEntryChecks, avg_with_all_extra_options) {
    // arrange
    const std::string statement = ".MEASURE TRAN avgAll AVG V(1) FROM=1m TO=5m TD=10n RISE=1 FALL=2 CROSS=3 MINVAL=0.5 DEFAULT=0 PRECISION=6 PRINT=1 AT=1m ON=0.5 OFF=0.1 RFC_LEVEL=0.5 OUTPUT=avg_result MIN_THRESH=0.1 MAX_THRESH=1.0 FRAC_MAX=0.9";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->from_val, "1m");
    ASSERT_EQ(result->to_val, "5m");
    ASSERT_EQ(result->td_val, "10n");
    ASSERT_EQ(result->rise_val, "1");
    ASSERT_EQ(result->fall_val, "2");
    ASSERT_EQ(result->cross_val, "3");
    ASSERT_EQ(result->minval, "0.5");
    ASSERT_EQ(result->default_val, "0");
    ASSERT_EQ(result->precision, "6");
    ASSERT_EQ(result->print_val, "1");
    ASSERT_EQ(result->at_val, "1m");
    ASSERT_EQ(result->on_val, "0.5");
    ASSERT_EQ(result->off_val, "0.1");
    ASSERT_EQ(result->rfc_level, "0.5");
    ASSERT_EQ(result->output, "avg_result");
    ASSERT_EQ(result->min_thresh, "0.1");
    ASSERT_EQ(result->max_thresh, "1.0");
    ASSERT_EQ(result->frac_max, "0.9");
}

TEST(MeasureEntryChecks, avg_with_when_clause_and_variable2) {
    // arrange
    const std::string statement = ".MEASURE TRAN avgAll AVG V(1) WHEN V(IN)>0.5 V(IN)";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->when_variable, "V(IN)");
    ASSERT_EQ(result->when_condition, ">0.5");
    ASSERT_EQ(result->variable2, "V(IN)");
}

TEST(MeasureEntryChecks, avg_with_trig_targ_and_extra_options) {
    // arrange
    const std::string statement = ".MEASURE TRAN delay TRIG V(IN) VAL=0.5 TARG V(OUT) VAL=0.5 FROM=1m TO=5m";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->trig_variable, "V(IN)");
    ASSERT_EQ(result->trig_val, "0.5");
    ASSERT_EQ(result->targ_variable, "V(OUT)");
    ASSERT_EQ(result->targ_val, "0.5");
    ASSERT_EQ(result->from_val, "1m");
    ASSERT_EQ(result->to_val, "5m");
}

TEST(MeasureEntryChecks, avg_with_error_file_and_indepvarcol_depvarcol) {
    // arrange
    const std::string statement = ".MEASURE TRAN avgAll AVG V(1) ERROR_FILE=error.txt INDEPVARCOL=time DEPVARCOL=vout";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->error_file, "error.txt");
    ASSERT_EQ(result->indepvarcol, "time");
    ASSERT_EQ(result->depvarcol, "vout");
}

TEST(MeasureEntryChecks, avg_with_comp_function_and_numfreq_gridsize) {
    // arrange
    const std::string statement = ".MEASURE TRAN avgAll AVG V(1) COMP_FUNCTION=abs NUMFREQ=10 GRIDSIZE=100";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->comp_function, "abs");
    ASSERT_EQ(result->numfreq, "10");
    ASSERT_EQ(result->gridsize, "100");
}

TEST(MeasureEntryChecks, avg_with_binsiz_maxfreq_minfreq_and_harm) {
    // arrange
    const std::string statement = ".MEASURE TRAN avgAll AVG V(1) BINSIZ=10 MAXFREQ=1MEG MINFREQ=1K HARM=1";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->binsiz, "10");
    ASSERT_EQ(result->maxfreq, "1MEG");
    ASSERT_EQ(result->minfreq, "1K");
    ASSERT_EQ(result->nbharm, "1");
}

TEST(MeasureEntryChecks, avg_with_all_measure_types) {
    // arrange
    const std::string statement = ".MEASURE TRAN avgAll AVG V(1) MAX=maxV1 MIN=minV1 RMS=rmsV1 PP=ppV1 INTEG=integV1";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->measure_type, "AVG");
    ASSERT_EQ(result->variable, "V(1)");
    ASSERT_EQ(result->result_name, "avgAll");
}

TEST(MeasureEntryChecks, avg_with_round_trip) {
    // arrange
    const std::string statement = ".MEASURE TRAN avgAll AVG V(1) FROM=1m TO=5m PRECISION=6";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->to_xyce_statement(), statement);
}

TEST(MeasureEntryChecks, avg_with_empty_statement) {
    // arrange
    const std::string statement = "";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(MeasureEntryChecks, avg_with_non_measure_statement) {
    // arrange
    const std::string statement = ".TRAN 1u 1m";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(MeasureEntryChecks, avg_with_invalid_measure_type) {
    // arrange
    const std::string statement = ".MEASURE TRAN avgAll INVALID V(1)";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(MeasureEntryChecks, avg_with_missing_variable) {
    // arrange
    const std::string statement = ".MEASURE TRAN avgAll AVG";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(MeasureEntryChecks, avg_with_missing_result_name) {
    // arrange
    const std::string statement = ".MEASURE TRAN AVG V(1)";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(MeasureEntryChecks, avg_with_missing_analysis_type) {
    // arrange
    const std::string statement = ".MEASURE avgAll AVG V(1)";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_FALSE(result.has_value());
}

TEST(MeasureEntryChecks, avg_with_lowercase_analysis_type) {
    // arrange
    const std::string statement = ".measure tran avgAll AVG V(1)";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->analysis_type, "tran");
    ASSERT_EQ(result->result_name, "avgAll");
    ASSERT_EQ(result->measure_type, "AVG");
    ASSERT_EQ(result->variable, "V(1)");
}

TEST(MeasureEntryChecks, avg_with_mixed_case_analysis_type) {
    // arrange
    const std::string statement = ".MeAsUrE TrAn avgAll AVG V(1)";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->analysis_type, "TrAn");
    ASSERT_EQ(result->result_name, "avgAll");
    ASSERT_EQ(result->measure_type, "AVG");
    ASSERT_EQ(result->variable, "V(1)");
}

TEST(MeasureEntryChecks, avg_with_nbharm) {
    // arrange
    const std::string statement = ".MEASURE TRAN avgAll AVG V(1) NBHARM=10";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->nbharm, "10");
}

TEST(MeasureEntryChecks, avg_with_goal_weight) {
    // arrange
    const std::string statement = ".MEASURE TRAN avgAll AVG V(1) GOAL=1.0 WEIGHT=0.5";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->goal, "1.0");
    ASSERT_EQ(result->weight, "0.5");
}

// to_xyce_statement

TEST(MeasureEntryChecks, to_xyce_statement_minimal) {
    // arrange
    const MeasureEntry entry("TRAN", "avgAll", "AVG", "V(1)");
    // act
    const std::string statement = entry.to_xyce_statement();
    // assert
    ASSERT_EQ(statement, ".MEASURE TRAN avgAll AVG V(1)");
}

TEST(MeasureEntryChecks, to_xyce_statement_with_all_fields) {
    // arrange
    const MeasureEntry entry("TRAN", "avgAll", "AVG", "V(1)", "1m", "5m", "10n", "1", "2", "3", "0.5", "0", "6", "1", "1m", "0.5", "0.1", "0.5", "avg_result", "0.1", "1.0", "0.9", "V(IN)", ">0.5", "V(IN)", "V(IN)", ">0.5", "0.5", "0.5", "10n", "1", "2", "3", "1m", "V(OUT)", "V(OUT)", "0.5", "0.5", "10n", "1", "2", "3", "1m", "error.txt", "time", "vout", "abs", "10", "100", "10", "1MEG", "1k", "10", "1.0", "0.5");
    // act
    const std::string statement = entry.to_xyce_statement();
    // assert
    ASSERT_EQ(statement, ".MEASURE TRAN avgAll AVG V(1) V(IN) WHEN V(IN)>0.5 FROM=1m TO=5m TD=10n RISE=1 FALL=2 CROSS=3 MINVAL=0.5 DEFAULT_VAL=0 PRECISION=6 FRAC_MAX=0.9 PRINT=1 AT=1m ON=0.5 OFF=0.1 RFC_LEVEL=0.5 OUTPUT=avg_result MIN_THRESH=0.1 MAX_THRESH=1.0 FILE=error.txt INDEPVARCOL=time DEPVARCOL=vout COMP_FUNCTION=abs NUMFREQ=10 GRIDSIZE=100 BINSIZ=10 MAXFREQ=1MEG MINFREQ=1k NBHARM=10 GOAL=1.0 WEIGHT=0.5");
}

// equality operator

TEST(MeasureEntryChecks, equality_operator_equal_entries) {
    // arrange
    const MeasureEntry entry1("TRAN", "avgAll", "AVG", "V(1)");
    const MeasureEntry entry2("TRAN", "avgAll", "AVG", "V(1)");
    // act
    const bool result = entry1 == entry2;
    // assert
    ASSERT_TRUE(result);
}

TEST(MeasureEntryChecks, equality_operator_different_analysis_type) {
    // arrange
    const MeasureEntry entry1("TRAN", "avgAll", "AVG", "V(1)");
    const MeasureEntry entry2("AC", "avgAll", "AVG", "V(1)");
    // act
    const bool result = entry1 == entry2;
    // assert
    ASSERT_FALSE(result);
}

TEST(MeasureEntryChecks, equality_operator_different_result_name) {
    // arrange
    const MeasureEntry entry1("TRAN", "avgAll", "AVG", "V(1)");
    const MeasureEntry entry2("TRAN", "maxAll", "AVG", "V(1)");
    // act
    const bool result = entry1 == entry2;
    // assert
    ASSERT_FALSE(result);
}

TEST(MeasureEntryChecks, equality_operator_different_measure_type) {
    // arrange
    const MeasureEntry entry1("TRAN", "avgAll", "AVG", "V(1)");
    const MeasureEntry entry2("TRAN", "avgAll", "MAX", "V(1)");
    // act
    const bool result = entry1 == entry2;
    // assert
    ASSERT_FALSE(result);
}

TEST(MeasureEntryChecks, equality_operator_different_variable) {
    // arrange
    const MeasureEntry entry1("TRAN", "avgAll", "AVG", "V(1)");
    const MeasureEntry entry2("TRAN", "avgAll", "AVG", "V(2)");
    // act
    const bool result = entry1 == entry2;
    // assert
    ASSERT_FALSE(result);
}

TEST(MeasureEntryChecks, parses_fft_analysis_type) {
    // arrange
    const std::string statement = ".MEASURE FFT enobval ENOB V(1)";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->analysis_type, "FFT");
    ASSERT_EQ(result->result_name, "enobval");
    ASSERT_EQ(result->measure_type, "ENOB");
    ASSERT_EQ(result->variable, "V(1)");
}

TEST(MeasureEntryChecks, parses_default_val_qualifier) {
    // arrange
    const std::string statement = ".MEASURE TRAN vout MAX V(out) DEFAULT_VAL=0";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->default_val, "0");
}

TEST(MeasureEntryChecks, parses_ymin_and_ymax_qualifiers) {
    // arrange
    const std::string statement = ".MEASURE TRAN errval ERR1 V(1) V(2) YMIN=1e-3 YMAX=10";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->ymin, "1e-3");
    ASSERT_EQ(result->ymax, "10");
}

TEST(MeasureEntryChecks, fft_analysis_type_round_trips) {
    // arrange
    const std::string statement = ".MEASURE FFT e ENOB V(1)";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->to_xyce_statement(), statement);
}

TEST(MeasureEntryChecks, default_val_round_trips_to_documented_spelling) {
    // arrange
    const std::string statement = ".MEASURE TRAN vout MAX V(out) DEFAULT_VAL=0";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->to_xyce_statement(), statement);
}

TEST(MeasureEntryChecks, trig_targ_round_trips_default_val_and_from_to) {
    // arrange
    const std::string statement = ".MEASURE TRAN td TRIG V(in) VAL=0.5 RISE=1 TARG V(out) VAL=0.5 RISE=1 FROM=1m TO=5m DEFAULT_VAL=0";
    // act
    const auto result = MeasureEntry::from_xyce_statement(statement);
    // assert
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->to_xyce_statement(), statement);
}

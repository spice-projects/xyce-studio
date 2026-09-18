#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "netlist/netlist.h"
#include "simulation/option_parameters.h"

// ========================================================================================
// from_xyce_directives
// ========================================================================================

TEST(OptionParametersChecks, parse_option_directives) {
    // arrange
    const std::vector<std::string> directives = {
        ".OPTIONS DEVICE TEMP=25 GMIN=1e-12", ".OPTIONS TIMEINT RELTOL=1e-3 ABSTOL=1e-12", ".OPTIONS NONLIN MAXSTEP=10", ".OPTIONS LINSOL TYPE=AZTECOO", ".OPTIONS FFT FFT_ACCURATE=1 FFTOUT=1 FFT_MODE=0", ".OPTIONS DIAGNOSTIC DEBUGLEVEL=3",
    };
    // act
    const auto params = OptionParameters::from_xyce_directives(directives);
    // assert
    ASSERT_EQ(params.device.size(), 2);
    ASSERT_EQ(params.device.at("TEMP"), "25");
    ASSERT_EQ(params.device.at("GMIN"), "1e-12");
    ASSERT_EQ(params.timeint.size(), 2);
    ASSERT_EQ(params.timeint.at("RELTOL"), "1e-3");
    ASSERT_EQ(params.timeint.at("ABSTOL"), "1e-12");
    ASSERT_EQ(params.nonlin.size(), 1);
    ASSERT_EQ(params.nonlin.at("MAXSTEP"), "10");
    ASSERT_EQ(params.linsol.size(), 1);
    ASSERT_EQ(params.linsol.at("TYPE"), "AZTECOO");
    ASSERT_EQ(params.fft.size(), 3);
    ASSERT_EQ(params.fft.at("FFT_ACCURATE"), "1");
    ASSERT_EQ(params.fft.at("FFTOUT"), "1");
    ASSERT_EQ(params.fft.at("FFT_MODE"), "0");
    ASSERT_EQ(params.diagnostic.size(), 1);
    ASSERT_EQ(params.diagnostic.at("DEBUGLEVEL"), "3");
}

TEST(OptionParametersChecks, parse_diagnostic_flag_style_option) {
    // arrange
    const std::vector<std::string> directives = {
        ".OPTIONS DIAGNOSTIC DEBUGLEVEL=2",
    };
    // act
    const auto params = OptionParameters::from_xyce_directives(directives);
    // assert
    ASSERT_EQ(params.device.size(), 0);
    ASSERT_EQ(params.diagnostic.size(), 1);
    ASSERT_EQ(params.diagnostic.at("DEBUGLEVEL"), "2");
}

TEST(OptionParametersChecks, parse_fft_options) {
    // arrange
    const std::vector<std::string> directives = {
        ".OPTIONS fft ffT_ACCURATE=0 fFtOut=0",
    };
    // act
    const auto params = OptionParameters::from_xyce_directives(directives);
    // assert
    ASSERT_EQ(params.fft.size(), 2);
    ASSERT_EQ(params.fft.at("FFT_ACCURATE"), "0");
    ASSERT_EQ(params.fft.at("FFTOUT"), "0");
}

TEST(OptionParametersChecks, parse_fft_flag_style_option) {
    // arrange
    const std::vector<std::string> directives = {
        ".OPTIONS FFT FFTOUT",
    };
    // act
    const auto params = OptionParameters::from_xyce_directives(directives);
    // assert
    ASSERT_EQ(params.fft.size(), 1);
    ASSERT_EQ(params.fft.at("FFTOUT"), "");
}

TEST(OptionParametersChecks, parse_single_option_directive) {
    // arrange
    const std::vector<std::string> directives = {
        ".OPTIONS DEVICE TEMP=25",
    };
    // act
    const auto params = OptionParameters::from_xyce_directives(directives);
    // assert
    ASSERT_EQ(params.device.size(), 1);
    ASSERT_EQ(params.device.at("TEMP"), "25");
}

TEST(OptionParametersChecks, parse_empty_directives) {
    // arrange
    const std::vector<std::string> directives = {};
    // act
    const auto params = OptionParameters::from_xyce_directives(directives);
    // assert
    ASSERT_EQ(params.device.size(), 0);
    ASSERT_EQ(params.timeint.size(), 0);
    ASSERT_EQ(params.nonlin.size(), 0);
    ASSERT_EQ(params.linsol.size(), 0);
    ASSERT_EQ(params.fft.size(), 0);
    ASSERT_EQ(params.nonlin_tran.size(), 0);
    ASSERT_EQ(params.output.size(), 0);
    ASSERT_EQ(params.restart.size(), 0);
}

TEST(OptionParametersChecks, parse_non_option_directive) {
    // arrange
    const std::vector<std::string> directives = {
        ".TRAN 1u 1m",
        ".OPTIONS FFT FFTOUT=1",
    };
    // act
    const auto params = OptionParameters::from_xyce_directives(directives);
    // assert
    ASSERT_EQ(params.device.size(), 0);
    ASSERT_EQ(params.fft.size(), 1);
    ASSERT_EQ(params.fft.at("FFTOUT"), "1");
}

TEST(OptionParametersChecks, parse_parser_package_options) {
    // arrange
    const std::vector<std::string> directives = {
        ".OPTIONS PARSER MODEL_BINNING=0 SCALE=2.5",
    };
    // act
    const auto params = OptionParameters::from_xyce_directives(directives);
    // assert
    ASSERT_EQ(params.parser.size(), 2);
    ASSERT_EQ(params.parser.at("MODEL_BINNING"), "0");
    ASSERT_EQ(params.parser.at("SCALE"), "2.5");
}

TEST(OptionParametersChecks, parse_parser_package_options_case_insensitive) {
    // arrange
    const std::vector<std::string> directives = {
        ".OPTIONS Parser model_binning=TRUE",
    };
    // act
    const auto params = OptionParameters::from_xyce_directives(directives);
    // assert
    ASSERT_EQ(params.parser.size(), 1);
    ASSERT_EQ(params.parser.at("MODEL_BINNING"), "TRUE");
}

TEST(OptionParametersChecks, parse_linsol_ac_options) {
    // arrange
    const std::vector<std::string> directives = {
        ".OPTIONS LINSOL-AC TYPE=KLU",
    };
    // act
    const auto params = OptionParameters::from_xyce_directives(directives);
    // assert
    ASSERT_EQ(params.linsol_ac.size(), 1);
    ASSERT_EQ(params.linsol_ac.at("TYPE"), "KLU");
    // the AC-scoped package must not leak into the generic LINSOL package
    ASSERT_EQ(params.linsol.size(), 0);
}

TEST(OptionParametersChecks, parse_loca_options) {
    // arrange
    const std::vector<std::string> directives = {
        ".OPTIONS LOCA Max_Num_Starts=4 Min_Start=0.1",
    };
    // act
    const auto params = OptionParameters::from_xyce_directives(directives);
    // assert
    ASSERT_EQ(params.loca.size(), 2);
    ASSERT_EQ(params.loca.at("MAX_NUM_STARTS"), "4");
    ASSERT_EQ(params.loca.at("MIN_START"), "0.1");
}

TEST(OptionParametersChecks, parse_dist_strategy_option) {
    // arrange
    const std::vector<std::string> directives = {
        ".OPTIONS DIST STRATEGY=2",
    };
    // act
    const auto params = OptionParameters::from_xyce_directives(directives);
    // assert
    ASSERT_EQ(params.dist.size(), 1);
    ASSERT_EQ(params.dist.at("STRATEGY"), "2");
}

TEST(OptionParametersChecks, parse_measure_options) {
    // arrange
    const std::vector<std::string> directives = {
        ".OPTIONS MEASURE DEFAULT_VAL=0 MEASDGT=8 MEASFAIL=0 MEASOUT=1 MEASPRINT=ALL",
    };
    // act
    const auto params = OptionParameters::from_xyce_directives(directives);
    // assert
    ASSERT_EQ(params.measure.size(), 5);
    ASSERT_EQ(params.measure.at("DEFAULT_VAL"), "0");
    ASSERT_EQ(params.measure.at("MEASDGT"), "8");
    ASSERT_EQ(params.measure.at("MEASFAIL"), "0");
    ASSERT_EQ(params.measure.at("MEASOUT"), "1");
    ASSERT_EQ(params.measure.at("MEASPRINT"), "ALL");
}

TEST(OptionParametersChecks, parse_measure_options_case_insensitive) {
    // arrange
    const std::vector<std::string> directives = {
        ".OPTIONS measure measdgt=10",
    };
    // act
    const auto params = OptionParameters::from_xyce_directives(directives);
    // assert
    ASSERT_EQ(params.measure.size(), 1);
    ASSERT_EQ(params.measure.at("MEASDGT"), "10");
}

TEST(OptionParametersChecks, parse_nonlin_tran_options) {
    // arrange
    const std::vector<std::string> directives = {
        ".OPTIONS NONLIN-TRAN NOX=0 NLSTRATEGY=1 ABSTOL=1e-6 RELTOL=1e-2 MAXSTEP=20",
    };
    // act
    const auto params = OptionParameters::from_xyce_directives(directives);
    // assert
    ASSERT_EQ(params.nonlin_tran.size(), 5);
    ASSERT_EQ(params.nonlin_tran.at("NOX"), "0");
    ASSERT_EQ(params.nonlin_tran.at("NLSTRATEGY"), "1");
    ASSERT_EQ(params.nonlin_tran.at("ABSTOL"), "1e-6");
    ASSERT_EQ(params.nonlin_tran.at("RELTOL"), "1e-2");
    ASSERT_EQ(params.nonlin_tran.at("MAXSTEP"), "20");
    // the transient package must not leak into the generic NONLIN package
    ASSERT_EQ(params.nonlin.size(), 0);
}

TEST(OptionParametersChecks, parse_nonlin_tran_options_case_insensitive) {
    // arrange
    const std::vector<std::string> directives = {
        ".OPTIONS nonlin-tran maxstep=15",
    };
    // act
    const auto params = OptionParameters::from_xyce_directives(directives);
    // assert
    ASSERT_EQ(params.nonlin_tran.size(), 1);
    ASSERT_EQ(params.nonlin_tran.at("MAXSTEP"), "15");
}

TEST(OptionParametersChecks, parse_output_options) {
    // arrange
    const std::vector<std::string> directives = {
        ".OPTIONS OUTPUT PRINTHEADER=false PRINTFOOTER=false SNAPSHOTS=true ADD_STEPNUM_COL=true PHASE_OUTPUT_RADIANS=true",
    };
    // act
    const auto params = OptionParameters::from_xyce_directives(directives);
    // assert
    ASSERT_EQ(params.output.size(), 5);
    ASSERT_EQ(params.output.at("PRINTHEADER"), "false");
    ASSERT_EQ(params.output.at("PRINTFOOTER"), "false");
    ASSERT_EQ(params.output.at("SNAPSHOTS"), "true");
    ASSERT_EQ(params.output.at("ADD_STEPNUM_COL"), "true");
    ASSERT_EQ(params.output.at("PHASE_OUTPUT_RADIANS"), "true");
}

TEST(OptionParametersChecks, parse_output_time_points_option) {
    // arrange
    const std::vector<std::string> directives = {
        ".OPTIONS OUTPUT OUTPUTTIMEPOINTS=1e-3,2e-3,3e-3",
    };
    // act
    const auto params = OptionParameters::from_xyce_directives(directives);
    // assert
    ASSERT_EQ(params.output.size(), 1);
    // the comma separated list is preserved verbatim in the value
    ASSERT_EQ(params.output.at("OUTPUTTIMEPOINTS"), "1e-3,2e-3,3e-3");
}

TEST(OptionParametersChecks, parse_output_initial_interval_with_change_points) {
    // arrange
    const std::vector<std::string> directives = {
        ".OPTIONS OUTPUT INITIAL_INTERVAL=0.1us 1.0us 0.5us 10us 0.1us",
    };
    // act
    const auto params = OptionParameters::from_xyce_directives(directives);
    // assert
    ASSERT_EQ(params.output.size(), 1);
    // bare tokens following INITIAL_INTERVAL are consumed as interval change points
    ASSERT_EQ(params.output.at("INITIAL_INTERVAL"), "0.1us 1.0us 0.5us 10us 0.1us");
}

TEST(OptionParametersChecks, parse_output_initial_interval_alone) {
    // arrange
    const std::vector<std::string> directives = {
        ".OPTIONS OUTPUT INITIAL_INTERVAL=0.1us",
    };
    // act
    const auto params = OptionParameters::from_xyce_directives(directives);
    // assert
    ASSERT_EQ(params.output.size(), 1);
    ASSERT_EQ(params.output.at("INITIAL_INTERVAL"), "0.1us");
}

TEST(OptionParametersChecks, parse_output_bare_token_without_interval_is_flag) {
    // arrange
    const std::vector<std::string> directives = {
        ".OPTIONS OUTPUT PRINTHEADER",
    };
    // act
    const auto params = OptionParameters::from_xyce_directives(directives);
    // assert
    ASSERT_EQ(params.output.size(), 1);
    ASSERT_EQ(params.output.at("PRINTHEADER"), "");
}

TEST(OptionParametersChecks, parse_restart_checkpoint_options) {
    // arrange
    const std::vector<std::string> directives = {
        ".OPTIONS RESTART PACK=1 JOB=checkpt",
    };
    // act
    const auto params = OptionParameters::from_xyce_directives(directives);
    // assert
    ASSERT_EQ(params.restart.size(), 2);
    ASSERT_EQ(params.restart.at("PACK"), "1");
    ASSERT_EQ(params.restart.at("JOB"), "checkpt");
}

TEST(OptionParametersChecks, parse_restart_initial_interval_with_change_points) {
    // arrange
    const std::vector<std::string> directives = {
        ".OPTIONS RESTART JOB=checkpt INITIAL_INTERVAL=0.1us 1.0us 0.5us 10us 0.1us",
    };
    // act
    const auto params = OptionParameters::from_xyce_directives(directives);
    // assert
    ASSERT_EQ(params.restart.size(), 2);
    // bare tokens following INITIAL_INTERVAL are consumed as interval change points
    ASSERT_EQ(params.restart.at("INITIAL_INTERVAL"), "0.1us 1.0us 0.5us 10us 0.1us");
    ASSERT_EQ(params.restart.at("JOB"), "checkpt");
}

TEST(OptionParametersChecks, parse_restart_from_job_and_start_time) {
    // arrange
    const std::vector<std::string> directives = {
        ".OPTIONS RESTART JOB=checkpt START_TIME=0.133us",
    };
    // act
    const auto params = OptionParameters::from_xyce_directives(directives);
    // assert
    ASSERT_EQ(params.restart.size(), 2);
    ASSERT_EQ(params.restart.at("JOB"), "checkpt");
    ASSERT_EQ(params.restart.at("START_TIME"), "0.133us");
}

TEST(OptionParametersChecks, parse_restart_from_file) {
    // arrange
    const std::vector<std::string> directives = {
        ".OPTIONS RESTART FILE=checkpt0.000000133",
    };
    // act
    const auto params = OptionParameters::from_xyce_directives(directives);
    // assert
    ASSERT_EQ(params.restart.size(), 1);
    ASSERT_EQ(params.restart.at("FILE"), "checkpt0.000000133");
}

TEST(OptionParametersChecks, parse_restart_from_file_with_continued_checkpointing) {
    // arrange
    const std::vector<std::string> directives = {
        ".OPTIONS RESTART FILE=checkpt0.000000133 JOB=checkpt_again INITIAL_INTERVAL=0.1us",
    };
    // act
    const auto params = OptionParameters::from_xyce_directives(directives);
    // assert
    ASSERT_EQ(params.restart.size(), 3);
    ASSERT_EQ(params.restart.at("FILE"), "checkpt0.000000133");
    ASSERT_EQ(params.restart.at("JOB"), "checkpt_again");
    ASSERT_EQ(params.restart.at("INITIAL_INTERVAL"), "0.1us");
}

// ========================================================================================
// to_xyce_directives
// ========================================================================================

TEST(OptionParametersChecks, generate_directives) {
    // arrange
    const OptionParameters params({{"TEMP", "25"}}, {{"RELTOL", "1e-3"}}, {{"MAXSTEP", "10"}}, {{"TYPE", "AZTECOO"}}, {{"FFTOUT", "1"}});
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 5);
    ASSERT_EQ(directives[0], ".OPTIONS DEVICE TEMP=25");
    ASSERT_EQ(directives[1], ".OPTIONS TIMEINT RELTOL=1e-3");
    ASSERT_EQ(directives[2], ".OPTIONS NONLIN MAXSTEP=10");
    ASSERT_EQ(directives[3], ".OPTIONS LINSOL TYPE=AZTECOO");
    ASSERT_EQ(directives[4], ".OPTIONS FFT FFTOUT=1");
}

TEST(OptionParametersChecks, generate_fft_directive) {
    // arrange
    const OptionParameters params({}, {}, {}, {}, {{"FFT_ACCURATE", "0"}, {"FFTOUT", "1"}, {"FFT_MODE", "1"}});
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    // keys are emitted in sorted map order (FFTOUT < FFT_ACCURATE < FFT_MODE)
    ASSERT_EQ(directives[0], ".OPTIONS FFT FFTOUT=1 FFT_ACCURATE=0 FFT_MODE=1");
}

TEST(OptionParametersChecks, generate_flag_style_option) {
    // arrange
    const OptionParameters params({}, {}, {}, {}, {{"FFTOUT", ""}});
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    ASSERT_EQ(directives[0], ".OPTIONS FFT FFTOUT");
}

TEST(OptionParametersChecks, generate_diagnostic_directive) {
    // arrange
    const OptionParameters params({}, {}, {}, {}, {}, {{"DEBUGLEVEL", "2"}});
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    ASSERT_EQ(directives[0], ".OPTIONS DIAGNOSTIC DEBUGLEVEL=2");
}

TEST(OptionParametersChecks, generate_parser_directive) {
    // arrange
    const OptionParameters params({}, {}, {}, {}, {}, {}, {{"MODEL_BINNING", "0"}, {"SCALE", "2.5"}});
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    ASSERT_EQ(directives[0], ".OPTIONS PARSER MODEL_BINNING=0 SCALE=2.5");
}

TEST(OptionParametersChecks, generate_linsol_ac_directive) {
    // arrange
    const OptionParameters params({}, {}, {}, {}, {}, {}, {}, {{"TYPE", "KLU"}});
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    ASSERT_EQ(directives[0], ".OPTIONS LINSOL-AC TYPE=KLU");
}

TEST(OptionParametersChecks, generate_loca_directive) {
    // arrange
    const OptionParameters params({}, {}, {}, {}, {}, {}, {}, {}, {{"MAX_NUM_STARTS", "4"}});
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    ASSERT_EQ(directives[0], ".OPTIONS LOCA MAX_NUM_STARTS=4");
}

TEST(OptionParametersChecks, generate_dist_directive) {
    // arrange
    const OptionParameters params({}, {}, {}, {}, {}, {}, {}, {}, {}, {{"STRATEGY", "2"}});
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    ASSERT_EQ(directives[0], ".OPTIONS DIST STRATEGY=2");
}

TEST(OptionParametersChecks, generate_measure_directive) {
    // arrange
    const OptionParameters params({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {{"MEASDGT", "8"}, {"MEASFAIL", "0"}});
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    ASSERT_EQ(directives[0], ".OPTIONS MEASURE MEASDGT=8 MEASFAIL=0");
}

TEST(OptionParametersChecks, generate_nonlin_tran_directive) {
    // arrange
    const OptionParameters params({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {{"NOX", "0"}, {"MAXSTEP", "20"}});
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    // keys are emitted in sorted map order (MAXSTEP < NOX)
    ASSERT_EQ(directives[0], ".OPTIONS NONLIN-TRAN MAXSTEP=20 NOX=0");
}

TEST(OptionParametersChecks, generate_output_directive) {
    // arrange
    const OptionParameters params({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {{"PRINTHEADER", "false"}, {"PRINTFOOTER", "false"}, {"SNAPSHOTS", "true"}});
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    // keys are emitted in sorted map order (PRINTFOOTER < PRINTHEADER < SNAPSHOTS)
    ASSERT_EQ(directives[0], ".OPTIONS OUTPUT PRINTFOOTER=false PRINTHEADER=false SNAPSHOTS=true");
}

TEST(OptionParametersChecks, generate_output_initial_interval_with_change_points) {
    // arrange
    const OptionParameters params({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {{"INITIAL_INTERVAL", "0.1us 1.0us 0.5us 10us 0.1us"}});
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    // the interval change points are emitted inline after INITIAL_INTERVAL
    ASSERT_EQ(directives[0], ".OPTIONS OUTPUT INITIAL_INTERVAL=0.1us 1.0us 0.5us 10us 0.1us");
}

TEST(OptionParametersChecks, generate_restart_directive) {
    // arrange
    const OptionParameters params({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {{"PACK", "0"}, {"JOB", "checkpt"}, {"INITIAL_INTERVAL", "0.1us"}});
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 1);
    // keys are emitted in sorted map order (INITIAL_INTERVAL < JOB < PACK)
    ASSERT_EQ(directives[0], ".OPTIONS RESTART INITIAL_INTERVAL=0.1us JOB=checkpt PACK=0");
}

TEST(OptionParametersChecks, generate_new_packages_in_deterministic_order) {
    // arrange
    const OptionParameters params({}, {}, {}, {}, {}, {}, {{"MODEL_BINNING", "0"}}, {{"TYPE", "KLU"}}, {{"MAX_NUM_STARTS", "4"}}, {{"STRATEGY", "1"}});
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 4);
    ASSERT_EQ(directives[0], ".OPTIONS LINSOL-AC TYPE=KLU");
    ASSERT_EQ(directives[1], ".OPTIONS PARSER MODEL_BINNING=0");
    ASSERT_EQ(directives[2], ".OPTIONS LOCA MAX_NUM_STARTS=4");
    ASSERT_EQ(directives[3], ".OPTIONS DIST STRATEGY=1");
}

TEST(OptionParametersChecks, generate_empty_directives) {
    // arrange
    const OptionParameters params({}, {}, {}, {}, {});
    // act
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(directives.size(), 0);
}

// ========================================================================================
// round trip
// ========================================================================================

TEST(OptionParametersChecks, round_trip_directives) {
    // arrange
    const std::vector<std::string> directives = {
        ".OPTIONS DEVICE TEMP=25",
        ".OPTIONS NONLIN MAXSTEP=10",
        ".OPTIONS FFT FFTOUT=1",
    };
    // act
    const auto params = OptionParameters::from_xyce_directives(directives);
    const auto round_trip = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(round_trip.size(), 3);
    ASSERT_EQ(round_trip[0], ".OPTIONS DEVICE TEMP=25");
    ASSERT_EQ(round_trip[1], ".OPTIONS NONLIN MAXSTEP=10");
    ASSERT_EQ(round_trip[2], ".OPTIONS FFT FFTOUT=1");
}

TEST(OptionParametersChecks, round_trip_fft_options) {
    // arrange
    const std::vector<std::string> directives = {
        ".OPTIONS FFT FFT_ACCURATE=0 FFTOUT=1 FFT_MODE=1",
    };
    // act
    const auto params = OptionParameters::from_xyce_directives(directives);
    const auto round_trip = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(round_trip.size(), 1);
    // keys are emitted in sorted map order (FFTOUT < FFT_ACCURATE < FFT_MODE)
    ASSERT_EQ(round_trip[0], ".OPTIONS FFT FFTOUT=1 FFT_ACCURATE=0 FFT_MODE=1");
}

TEST(OptionParametersChecks, round_trip_diagnostic_options) {
    // arrange
    const std::vector<std::string> directives = {
        ".OPTIONS DIAGNOSTIC DEBUGLEVEL=2",
    };
    // act
    const auto params = OptionParameters::from_xyce_directives(directives);
    const auto round_trip = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(round_trip.size(), 1);
    ASSERT_EQ(round_trip[0], ".OPTIONS DIAGNOSTIC DEBUGLEVEL=2");
}

TEST(OptionParametersChecks, round_trip_new_package_options) {
    // arrange
    const std::vector<std::string> directives = {
        ".OPTIONS PARSER MODEL_BINNING=0 SCALE=2.5", ".OPTIONS LINSOL-AC TYPE=KLU", ".OPTIONS LOCA MAX_NUM_STARTS=4 MIN_START=0.1", ".OPTIONS DIST STRATEGY=2", ".OPTIONS MEASURE MEASDGT=8 MEASFAIL=0",
    };
    // act
    const auto params = OptionParameters::from_xyce_directives(directives);
    const auto round_trip = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(round_trip.size(), 5);
    ASSERT_EQ(round_trip[0], ".OPTIONS LINSOL-AC TYPE=KLU");
    ASSERT_EQ(round_trip[1], ".OPTIONS PARSER MODEL_BINNING=0 SCALE=2.5");
    ASSERT_EQ(round_trip[2], ".OPTIONS LOCA MAX_NUM_STARTS=4 MIN_START=0.1");
    ASSERT_EQ(round_trip[3], ".OPTIONS DIST STRATEGY=2");
    ASSERT_EQ(round_trip[4], ".OPTIONS MEASURE MEASDGT=8 MEASFAIL=0");
}

TEST(OptionParametersChecks, round_trip_measure_options) {
    // arrange
    const std::vector<std::string> directives = {
        ".OPTIONS MEASURE MEASDGT=10 MEASOUT=0",
    };
    // act
    const auto params = OptionParameters::from_xyce_directives(directives);
    const auto round_trip = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(round_trip.size(), 1);
    ASSERT_EQ(round_trip[0], ".OPTIONS MEASURE MEASDGT=10 MEASOUT=0");
}

TEST(OptionParametersChecks, round_trip_nonlin_tran_options) {
    // arrange
    const std::vector<std::string> directives = {
        ".OPTIONS NONLIN-TRAN NOX=0 NLSTRATEGY=1 ABSTOL=1e-6 MAXSTEP=20",
    };
    // act
    const auto params = OptionParameters::from_xyce_directives(directives);
    const auto round_trip = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(round_trip.size(), 1);
    // keys are emitted in sorted map order (ABSTOL < MAXSTEP < NLSTRATEGY < NOX)
    ASSERT_EQ(round_trip[0], ".OPTIONS NONLIN-TRAN ABSTOL=1e-6 MAXSTEP=20 NLSTRATEGY=1 NOX=0");
}

TEST(OptionParametersChecks, round_trip_output_options) {
    // arrange
    const std::vector<std::string> directives = {
        ".OPTIONS OUTPUT OUTPUTTIMEPOINTS=1e-3,2e-3 PRINTHEADER=false SNAPSHOTS=true",
    };
    // act
    const auto params = OptionParameters::from_xyce_directives(directives);
    const auto round_trip = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(round_trip.size(), 1);
    // keys are emitted in sorted map order (OUTPUTTIMEPOINTS < PRINTHEADER < SNAPSHOTS)
    ASSERT_EQ(round_trip[0], ".OPTIONS OUTPUT OUTPUTTIMEPOINTS=1e-3,2e-3 PRINTHEADER=false SNAPSHOTS=true");
}

TEST(OptionParametersChecks, round_trip_output_initial_interval_with_change_points) {
    // arrange
    const std::vector<std::string> directives = {
        ".OPTIONS OUTPUT INITIAL_INTERVAL=0.1us 1.0us 0.5us 10us 0.1us",
    };
    // act
    const auto params = OptionParameters::from_xyce_directives(directives);
    const auto round_trip = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(round_trip.size(), 1);
    ASSERT_EQ(round_trip[0], ".OPTIONS OUTPUT INITIAL_INTERVAL=0.1us 1.0us 0.5us 10us 0.1us");
}

TEST(OptionParametersChecks, round_trip_restart_checkpoint_options) {
    // arrange
    const std::vector<std::string> directives = {
        ".OPTIONS RESTART PACK=1 JOB=checkpt INITIAL_INTERVAL=0.1us",
    };
    // act
    const auto params = OptionParameters::from_xyce_directives(directives);
    const auto round_trip = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(round_trip.size(), 1);
    // keys are emitted in sorted map order (INITIAL_INTERVAL < JOB < PACK)
    ASSERT_EQ(round_trip[0], ".OPTIONS RESTART INITIAL_INTERVAL=0.1us JOB=checkpt PACK=1");
}

TEST(OptionParametersChecks, round_trip_restart_initial_interval_with_change_points) {
    // arrange
    const std::vector<std::string> directives = {
        ".OPTIONS RESTART JOB=checkpt INITIAL_INTERVAL=0.1us 1.0us 0.5us 10us 0.1us",
    };
    // act
    const auto params = OptionParameters::from_xyce_directives(directives);
    const auto round_trip = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(round_trip.size(), 1);
    // keys are emitted in sorted map order (INITIAL_INTERVAL < JOB)
    ASSERT_EQ(round_trip[0], ".OPTIONS RESTART INITIAL_INTERVAL=0.1us 1.0us 0.5us 10us 0.1us JOB=checkpt");
}

TEST(OptionParametersChecks, round_trip_restart_from_file) {
    // arrange
    const std::vector<std::string> directives = {
        ".OPTIONS RESTART FILE=checkpt0.000000133 JOB=checkpt_again INITIAL_INTERVAL=0.1us",
    };
    // act
    const auto params = OptionParameters::from_xyce_directives(directives);
    const auto round_trip = params.to_xyce_directives(NetlistTopology{});
    // assert
    ASSERT_EQ(round_trip.size(), 1);
    // keys are emitted in sorted map order (FILE < INITIAL_INTERVAL < JOB)
    ASSERT_EQ(round_trip[0], ".OPTIONS RESTART FILE=checkpt0.000000133 INITIAL_INTERVAL=0.1us JOB=checkpt_again");
}

// ========================================================================================
// equality
// ========================================================================================

TEST(OptionParametersChecks, equal_instances_compare_equal) {
    // arrange
    const OptionParameters a({{"TEMP", "25"}}, {}, {}, {}, {{"FFTOUT", "1"}});
    const OptionParameters b({{"TEMP", "25"}}, {}, {}, {}, {{"FFTOUT", "1"}});
    // act / assert
    ASSERT_TRUE(a == b);
}

TEST(OptionParametersChecks, differing_fft_options_compare_unequal) {
    // arrange
    const OptionParameters a({}, {}, {}, {}, {{"FFTOUT", "1"}});
    const OptionParameters b({}, {}, {}, {}, {{"FFTOUT", "0"}});
    // act / assert
    ASSERT_FALSE(a == b);
}

TEST(OptionParametersChecks, differing_diagnostic_options_compare_unequal) {
    // arrange
    const OptionParameters a({}, {}, {}, {}, {}, {{"DEBUGLEVEL", "1"}});
    const OptionParameters b({}, {}, {}, {}, {}, {{"DEBUGLEVEL", "2"}});
    // act / assert
    ASSERT_FALSE(a == b);
}

TEST(OptionParametersChecks, equal_instances_with_new_packages_compare_equal) {
    // arrange
    const OptionParameters a({}, {}, {}, {}, {}, {}, {{"MODEL_BINNING", "0"}}, {{"TYPE", "KLU"}}, {{"MAX_NUM_STARTS", "4"}}, {{"STRATEGY", "1"}});
    const OptionParameters b({}, {}, {}, {}, {}, {}, {{"MODEL_BINNING", "0"}}, {{"TYPE", "KLU"}}, {{"MAX_NUM_STARTS", "4"}}, {{"STRATEGY", "1"}});
    // act / assert
    ASSERT_TRUE(a == b);
}

TEST(OptionParametersChecks, differing_parser_options_compare_unequal) {
    // arrange
    const OptionParameters a({}, {}, {}, {}, {}, {}, {{"SCALE", "1.0"}});
    const OptionParameters b({}, {}, {}, {}, {}, {}, {{"SCALE", "2.0"}});
    // act / assert
    ASSERT_FALSE(a == b);
}

TEST(OptionParametersChecks, differing_linsol_ac_options_compare_unequal) {
    // arrange
    const OptionParameters a({}, {}, {}, {}, {}, {}, {}, {{"TYPE", "KLU"}});
    const OptionParameters b({}, {}, {}, {}, {}, {}, {}, {{"TYPE", "AZTECOO"}});
    // act / assert
    ASSERT_FALSE(a == b);
}

TEST(OptionParametersChecks, differing_loca_options_compare_unequal) {
    // arrange
    const OptionParameters a({}, {}, {}, {}, {}, {}, {}, {}, {{"MAX_NUM_STARTS", "4"}});
    const OptionParameters b({}, {}, {}, {}, {}, {}, {}, {}, {{"MAX_NUM_STARTS", "5"}});
    // act / assert
    ASSERT_FALSE(a == b);
}

TEST(OptionParametersChecks, differing_dist_options_compare_unequal) {
    // arrange
    const OptionParameters a({}, {}, {}, {}, {}, {}, {}, {}, {}, {{"STRATEGY", "0"}});
    const OptionParameters b({}, {}, {}, {}, {}, {}, {}, {}, {}, {{"STRATEGY", "1"}});
    // act / assert
    ASSERT_FALSE(a == b);
}

TEST(OptionParametersChecks, equal_measure_options_compare_equal) {
    // arrange
    const OptionParameters a({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {{"MEASDGT", "8"}, {"MEASFAIL", "0"}});
    const OptionParameters b({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {{"MEASDGT", "8"}, {"MEASFAIL", "0"}});
    // act / assert
    ASSERT_TRUE(a == b);
}

TEST(OptionParametersChecks, differing_measure_options_compare_unequal) {
    // arrange
    const OptionParameters a({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {{"MEASDGT", "8"}});
    const OptionParameters b({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {{"MEASDGT", "10"}});
    // act / assert
    ASSERT_FALSE(a == b);
}

TEST(OptionParametersChecks, equal_nonlin_tran_options_compare_equal) {
    // arrange
    const OptionParameters a({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {{"MAXSTEP", "20"}, {"NOX", "0"}});
    const OptionParameters b({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {{"MAXSTEP", "20"}, {"NOX", "0"}});
    // act / assert
    ASSERT_TRUE(a == b);
}

TEST(OptionParametersChecks, differing_nonlin_tran_options_compare_unequal) {
    // arrange
    const OptionParameters a({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {{"MAXSTEP", "20"}});
    const OptionParameters b({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {{"MAXSTEP", "40"}});
    // act / assert
    ASSERT_FALSE(a == b);
}

TEST(OptionParametersChecks, differing_nonlin_and_nonlin_tran_options_compare_unequal) {
    // arrange
    const OptionParameters a({}, {}, {{"MAXSTEP", "200"}}, {}, {}, {}, {}, {}, {}, {}, {}, {{"MAXSTEP", "20"}});
    const OptionParameters b({}, {}, {{"MAXSTEP", "200"}}, {}, {}, {}, {}, {}, {}, {}, {}, {{"MAXSTEP", "200"}});
    // act / assert
    ASSERT_FALSE(a == b);
}

TEST(OptionParametersChecks, equal_output_options_compare_equal) {
    // arrange
    const OptionParameters a({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {{"PRINTHEADER", "false"}, {"SNAPSHOTS", "true"}});
    const OptionParameters b({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {{"PRINTHEADER", "false"}, {"SNAPSHOTS", "true"}});
    // act / assert
    ASSERT_TRUE(a == b);
}

TEST(OptionParametersChecks, differing_output_options_compare_unequal) {
    // arrange
    const OptionParameters a({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {{"INITIAL_INTERVAL", "0.1us 1.0us 0.5us"}});
    const OptionParameters b({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {{"INITIAL_INTERVAL", "0.1us 1.0us"}});
    // act / assert
    ASSERT_FALSE(a == b);
}

TEST(OptionParametersChecks, equal_restart_options_compare_equal) {
    // arrange
    const OptionParameters a({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {{"JOB", "checkpt"}, {"START_TIME", "0.133us"}});
    const OptionParameters b({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {{"JOB", "checkpt"}, {"START_TIME", "0.133us"}});
    // act / assert
    ASSERT_TRUE(a == b);
}

TEST(OptionParametersChecks, differing_restart_options_compare_unequal) {
    // arrange
    const OptionParameters a({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {{"JOB", "checkpt"}});
    const OptionParameters b({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {{"JOB", "checkpt_again"}});
    // act / assert
    ASSERT_FALSE(a == b);
}

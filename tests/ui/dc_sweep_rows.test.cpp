#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "simulation/dc_simulation_parameters.h"
#include "ui/dc_sweep_rows.h"

// ========================================================================================
// dc_sweep_rows_from_sweeps
// ========================================================================================

TEST(DcSweepRowsChecks, empty_sweeps_project_to_no_rows) {
    // act
    const auto rows = dc_sweep_rows_from_sweeps({});
    // assert
    ASSERT_EQ(rows.size(), 0u);
}

TEST(DcSweepRowsChecks, single_lin_sweep_projects_all_tuple_fields) {
    // arrange
    const std::vector<DcSweep> sweeps = {DcSweep{"VIN", "0", "5", "0.1", "", {}}};
    // act
    const auto rows = dc_sweep_rows_from_sweeps(sweeps);
    // assert
    ASSERT_EQ(rows.size(), 1u);
    ASSERT_EQ(rows[0].variable, "VIN");
    ASSERT_EQ(rows[0].start, "0");
    ASSERT_EQ(rows[0].stop, "5");
    ASSERT_EQ(rows[0].step, "0.1");
    ASSERT_EQ(rows[0].points, "");
    ASSERT_EQ(rows[0].list_values, "");
}

TEST(DcSweepRowsChecks, nested_sweeps_keep_every_entry_beyond_index_one) {
    // arrange — three nested LIN sweeps stress the 3rd+ entry the dialog used to drop
    const std::vector<DcSweep> sweeps = {
        DcSweep{"V1", "0", "5", "0.5", "", {}},
        DcSweep{"R1", "1k", "10k", "1k", "", {}},
        DcSweep{"TEMP", "25", "125", "25", "", {}},
    };
    // act
    const auto rows = dc_sweep_rows_from_sweeps(sweeps);
    // assert
    ASSERT_EQ(rows.size(), 3u);
    ASSERT_EQ(rows[2].variable, "TEMP");
    ASSERT_EQ(rows[2].start, "25");
    ASSERT_EQ(rows[2].stop, "125");
    ASSERT_EQ(rows[2].step, "25");
}

TEST(DcSweepRowsChecks, list_values_join_into_one_space_separated_cell) {
    // arrange — two nested LIST sweeps, each carrying its own values
    const std::vector<DcSweep> sweeps = {
        DcSweep{"VDS", "", "", "", "", {"0", "3.5", "0.05"}},
        DcSweep{"VGS", "", "", "", "", {"0", "3.5", "0.5"}},
    };
    // act
    const auto rows = dc_sweep_rows_from_sweeps(sweeps);
    // assert
    ASSERT_EQ(rows.size(), 2u);
    ASSERT_EQ(rows[0].list_values, "0 3.5 0.05");
    ASSERT_EQ(rows[1].variable, "VGS");
    ASSERT_EQ(rows[1].list_values, "0 3.5 0.5");
}

TEST(DcSweepRowsChecks, large_sweep_counts_survive_projection) {
    // arrange — 100 nested sweeps
    std::vector<DcSweep> sweeps;
    for (int i = 0; i < 100; ++i) {
        const auto index = std::to_string(i);
        sweeps.push_back(DcSweep{"V" + index, index, index + "0", "1", "", {}});
    }
    // act
    const auto rows = dc_sweep_rows_from_sweeps(sweeps);
    // assert
    ASSERT_EQ(rows.size(), 100u);
    ASSERT_EQ(rows[99].variable, "V99");
}

// ========================================================================================
// dc_sweeps_from_rows
// ========================================================================================

TEST(DcSweepRowsChecks, no_rows_produce_no_sweeps) {
    // act
    const auto sweeps = dc_sweeps_from_rows({}, "LIN");
    // assert
    ASSERT_EQ(sweeps.size(), 0u);
}

TEST(DcSweepRowsChecks, blank_rows_are_dropped) {
    // arrange — a user added a row but never filled it
    const std::vector<DcSweepRowFields> rows = {
        DcSweepRowFields{"V1", "0", "5", "0.5", "", ""},
        DcSweepRowFields{"", "", "", "", "", ""},
        DcSweepRowFields{" ", "", "", "", " ", ""},
    };
    // act
    const auto sweeps = dc_sweeps_from_rows(rows, "LIN");
    // assert
    ASSERT_EQ(sweeps.size(), 1u);
    ASSERT_EQ(sweeps[0].variable, "V1");
}

TEST(DcSweepRowsChecks, fields_are_trimmed) {
    // arrange
    const std::vector<DcSweepRowFields> rows = {DcSweepRowFields{"  V1 ", " 0 ", " 5 ", " 0.5 ", "", ""}};
    // act
    const auto sweeps = dc_sweeps_from_rows(rows, "LIN");
    // assert
    ASSERT_EQ(sweeps.size(), 1u);
    ASSERT_EQ(sweeps[0].variable, "V1");
    ASSERT_EQ(sweeps[0].start, "0");
    ASSERT_EQ(sweeps[0].stop, "5");
    ASSERT_EQ(sweeps[0].step, "0.5");
}

TEST(DcSweepRowsChecks, lin_mode_carries_step_from_points_when_step_blank) {
    // arrange — the user switched DEC -> LIN after typing points
    const std::vector<DcSweepRowFields> rows = {DcSweepRowFields{"V1", "0", "5", "", "0.25", ""}};
    // act
    const auto sweeps = dc_sweeps_from_rows(rows, "LIN");
    // assert
    ASSERT_EQ(sweeps[0].step, "0.25");
    ASSERT_EQ(sweeps[0].points, "");
}

TEST(DcSweepRowsChecks, dec_mode_carries_points_from_step_when_points_blank) {
    // arrange — the user switched LIN -> DEC after typing a step
    const std::vector<DcSweepRowFields> rows = {DcSweepRowFields{"V1", "1", "100", "5", "", ""}};
    // act
    const auto sweeps = dc_sweeps_from_rows(rows, "DEC");
    // assert
    ASSERT_EQ(sweeps[0].points, "5");
    ASSERT_EQ(sweeps[0].step, "");
}

TEST(DcSweepRowsChecks, oct_mode_keeps_points_field) {
    // arrange
    const std::vector<DcSweepRowFields> rows = {DcSweepRowFields{"V1", "0.125", "64", "", "2", ""}};
    // act
    const auto sweeps = dc_sweeps_from_rows(rows, "OCT");
    // assert
    ASSERT_EQ(sweeps[0].points, "2");
}

TEST(DcSweepRowsChecks, list_mode_splits_values_and_clears_range_fields) {
    // arrange
    const std::vector<DcSweepRowFields> rows = {DcSweepRowFields{"VDS", "ignored", "ignored", "", "", " 0 3.5 0.05 "}};
    // act
    const auto sweeps = dc_sweeps_from_rows(rows, "LIST");
    // assert
    ASSERT_EQ(sweeps.size(), 1u);
    ASSERT_EQ(sweeps[0].variable, "VDS");
    ASSERT_EQ(sweeps[0].start, "");
    ASSERT_EQ(sweeps[0].stop, "");
    ASSERT_EQ(sweeps[0].list_values, (std::vector<std::string>{"0", "3.5", "0.05"}));
}

TEST(DcSweepRowsChecks, data_mode_produces_no_sweeps) {
    // arrange — rows left over from an earlier mode must not leak into DATA
    const std::vector<DcSweepRowFields> rows = {DcSweepRowFields{"V1", "0", "5", "0.5", "", ""}};
    // act
    const auto sweeps = dc_sweeps_from_rows(rows, "DATA");
    // assert
    ASSERT_EQ(sweeps.size(), 0u);
}

// ========================================================================================
// dialog round-trip (parse -> project -> rebuild -> serialize)
// ========================================================================================

TEST(DcSweepRowsChecks, lin_dialog_roundtrip_preserves_nested_entries) {
    // arrange — 3 LIN sweeps straight from a parsed directive list
    const auto parsed = DCSimulationParameters::from_xyce_directives({".DC V1 0 5 0.5 R1 1k 10k 1k TEMP 25 125 25"});
    ASSERT_TRUE(parsed.has_value());
    // act — simulate a dialog open (project) and a save (rebuild)
    const auto rebuilt = dc_sweeps_from_rows(dc_sweep_rows_from_sweeps(parsed->sweeps), parsed->sweep_mode);
    // assert — every tuple survives, including index 2
    ASSERT_EQ(rebuilt, parsed->sweeps);
    const DCSimulationParameters params(parsed->sweep_mode, rebuilt, "", std::nullopt, {}, std::nullopt, std::nullopt);
    ASSERT_EQ(params.to_xyce_directives(NetlistTopology{}), parsed->to_xyce_directives(NetlistTopology{}));
}

TEST(DcSweepRowsChecks, dec_dialog_roundtrip_preserves_nested_entries) {
    // arrange — 3 DEC sweeps
    const auto parsed = DCSimulationParameters::from_xyce_directives({".DC DEC VIN 1 100 2 DEC R1 1 10 5 DEC C1 10 1000 1"});
    ASSERT_TRUE(parsed.has_value());
    // act
    const auto rebuilt = dc_sweeps_from_rows(dc_sweep_rows_from_sweeps(parsed->sweeps), parsed->sweep_mode);
    // assert
    ASSERT_EQ(rebuilt.size(), 3u);
    ASSERT_EQ(rebuilt, parsed->sweeps);
}

TEST(DcSweepRowsChecks, list_dialog_roundtrip_preserves_every_sweep) {
    // arrange — a nested LIST analysis parsed from the RG §2.1.3.4 example form
    const auto parsed = DCSimulationParameters::from_xyce_directives({".DC VDS LIST 0 3.5 0.05 VGS LIST 0 3.5 0.5"});
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->sweeps.size(), 2u);
    // act — dialog open then save
    const auto rebuilt = dc_sweeps_from_rows(dc_sweep_rows_from_sweeps(parsed->sweeps), parsed->sweep_mode);
    // assert — the secondary LIST values survive the dialog and pass validation
    ASSERT_EQ(rebuilt, parsed->sweeps);
    const DCSimulationParameters params(parsed->sweep_mode, rebuilt, "", std::nullopt, {}, std::nullopt, std::nullopt);
    ASSERT_EQ(params.validate(), std::nullopt);
    const auto directives = params.to_xyce_directives(NetlistTopology{});
    ASSERT_EQ(directives[0], ".DC VDS LIST 0 3.5 0.05 VGS LIST 0 3.5 0.5");
}

TEST(DcSweepRowsChecks, list_dialog_roundtrip_handles_three_sweeps) {
    // arrange
    const auto parsed = DCSimulationParameters::from_xyce_directives({".DC V1 LIST 1 2 V2 LIST 3 4 V3 LIST 5 6"});
    ASSERT_TRUE(parsed.has_value());
    // act
    const auto rebuilt = dc_sweeps_from_rows(dc_sweep_rows_from_sweeps(parsed->sweeps), parsed->sweep_mode);
    // assert
    ASSERT_EQ(rebuilt.size(), 3u);
    ASSERT_EQ(rebuilt, parsed->sweeps);
}

TEST(DcSweepRowsChecks, single_and_absent_sweep_counts_roundtrip) {
    // arrange — zero sweeps
    const auto zero_rows = dc_sweep_rows_from_sweeps({});
    // act / assert
    ASSERT_EQ(dc_sweeps_from_rows(zero_rows, "LIN").size(), 0u);
    // arrange — one sweep
    const std::vector<DcSweep> one = {DcSweep{"VIN", "0", "5", "0.1", "", {}}};
    // act / assert
    ASSERT_EQ(dc_sweeps_from_rows(dc_sweep_rows_from_sweeps(one), "LIN"), one);
}

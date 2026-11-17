#include <algorithm>
#include <cstddef>
#include <iterator>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "netlist/netlist.h"
#include "simulation/option_catalog.h"
#include "simulation/option_editor.h"
#include "simulation/option_parameters.h"

TEST(OptionEditorChecks, the_device_rows_follow_the_catalog_order) {
    // arrange
    const OptionPackage package = OptionPackage::DEVICE;
    // act
    const std::vector<option_editor::OptionRow> rows = option_editor::build_rows(package, {});
    // assert
    const std::vector<std::string>& catalog = option_package_catalog(package);
    ASSERT_EQ(rows.size(), catalog.size());
    EXPECT_EQ(rows.front().key, catalog.front());
    EXPECT_EQ(rows.back().key, catalog.back());
    for (const option_editor::OptionRow& row : rows) {
        EXPECT_TRUE(row.value.empty());
        EXPECT_FALSE(row.flag);
    }
}

TEST(OptionEditorChecks, a_loaded_value_fills_its_row) {
    // arrange
    const std::map<std::string, std::string> loaded{{"GMIN", "1e-9"}};
    // act
    const std::vector<option_editor::OptionRow> rows = option_editor::build_rows(OptionPackage::DEVICE, loaded);
    // assert
    const auto gmin = std::find_if(rows.begin(), rows.end(), [](const option_editor::OptionRow& row) { return row.key == "GMIN"; });
    ASSERT_NE(gmin, rows.end());
    EXPECT_EQ(gmin->value, "1e-9");
    EXPECT_FALSE(gmin->flag);
    EXPECT_EQ(gmin->choice_index, -1);
    EXPECT_EQ(gmin->default_value, "1.0E-12");
}

TEST(OptionEditorChecks, a_loaded_bare_flag_offers_the_flag_marker) {
    // arrange
    const std::map<std::string, std::string> loaded{{"VOLTLIM", ""}};
    // act
    const std::vector<option_editor::OptionRow> rows = option_editor::build_rows(OptionPackage::DEVICE, loaded);
    // assert
    const auto voltlim = std::find_if(rows.begin(), rows.end(), [](const option_editor::OptionRow& row) { return row.key == "VOLTLIM"; });
    ASSERT_NE(voltlim, rows.end());
    EXPECT_TRUE(voltlim->flag);
    EXPECT_TRUE(voltlim->value.empty());
    ASSERT_EQ(voltlim->choices.size(), 4U);
    EXPECT_EQ(voltlim->choices[0], std::string(option_editor::DEFAULT_CHOICE));
    EXPECT_EQ(voltlim->choices[1], std::string(option_editor::FLAG_CHOICE));
    EXPECT_EQ(voltlim->choices[2], "0");
    EXPECT_EQ(voltlim->choices[3], "1");
    EXPECT_EQ(voltlim->choice_index, 1);
}

TEST(OptionEditorChecks, a_documented_choice_selects_its_entry) {
    // arrange
    const std::map<std::string, std::string> loaded{{"VOLTLIM", "1"}};
    // act
    const std::vector<option_editor::OptionRow> rows = option_editor::build_rows(OptionPackage::DEVICE, loaded);
    // assert
    const auto voltlim = std::find_if(rows.begin(), rows.end(), [](const option_editor::OptionRow& row) { return row.key == "VOLTLIM"; });
    ASSERT_NE(voltlim, rows.end());
    EXPECT_FALSE(voltlim->flag);
    EXPECT_EQ(voltlim->choice_index, 2);
}

TEST(OptionEditorChecks, an_undocumented_choice_value_falls_back_to_free_text) {
    // arrange
    const std::map<std::string, std::string> loaded{{"VOLTLIM", "9"}};
    // act
    const std::vector<option_editor::OptionRow> rows = option_editor::build_rows(OptionPackage::DEVICE, loaded);
    // assert
    const auto voltlim = std::find_if(rows.begin(), rows.end(), [](const option_editor::OptionRow& row) { return row.key == "VOLTLIM"; });
    ASSERT_NE(voltlim, rows.end());
    EXPECT_EQ(voltlim->value, "9");
    EXPECT_EQ(voltlim->choice_index, -1);
}

TEST(OptionEditorChecks, a_netlist_key_the_catalog_omits_follows_the_documented_keys) {
    // arrange
    const std::map<std::string, std::string> loaded{{"GMIN", "1e-9"}, {"CUSTOMOPT", "7"}};
    // act
    const std::vector<option_editor::OptionRow> rows = option_editor::build_rows(OptionPackage::DEVICE, loaded);
    // assert
    const std::vector<std::string>& catalog = option_package_catalog(OptionPackage::DEVICE);
    ASSERT_EQ(rows.size(), catalog.size() + 1);
    EXPECT_EQ(rows[catalog.size()].key, "CUSTOMOPT");
    EXPECT_EQ(rows[catalog.size()].value, "7");
    EXPECT_EQ(rows[catalog.size()].choice_index, -1);
}

TEST(OptionEditorChecks, row_choice_index_resolves_the_choice_inside_the_row_list) {
    // arrange
    option_editor::OptionRow row;
    row.key = "VOLTLIM";
    row.value = "";
    row.flag = true;
    row.choices = {std::string(option_editor::DEFAULT_CHOICE), std::string(option_editor::FLAG_CHOICE), "0", "1"};
    // act
    const int flag_index = option_editor::row_choice_index(row);
    row.value = "1";
    row.flag = false;
    const int value_index = option_editor::row_choice_index(row);
    // assert
    EXPECT_EQ(flag_index, 1);
    EXPECT_EQ(value_index, 3);
}

TEST(OptionEditorChecks, package_name_names_the_hyphenated_packages) {
    // act and assert
    EXPECT_EQ(option_editor::package_name(OptionPackage::NONLIN_TRAN), "NONLIN-TRAN");
    EXPECT_EQ(option_editor::package_name(OptionPackage::LINSOL_AC), "LINSOL-AC");
    EXPECT_EQ(option_editor::package_name(OptionPackage::RESTART), "RESTART");
}

TEST(OptionEditorChecks, apply_rows_trims_uppercases_and_keeps_bare_flags) {
    // arrange
    std::vector<option_editor::OptionRow> rows;
    option_editor::OptionRow valued;
    valued.key = " gmin ";
    valued.value = " 1e-9 ";
    rows.push_back(valued);
    option_editor::OptionRow flag;
    flag.key = "temp";
    flag.flag = true;
    rows.push_back(flag);
    option_editor::OptionRow unnamed;
    unnamed.key = "  ";
    unnamed.value = "1";
    rows.push_back(unnamed);
    // act
    const std::map<std::string, std::string> options = option_editor::apply_rows(rows);
    // assert
    ASSERT_EQ(options.size(), 2U);
    EXPECT_EQ(options.at("GMIN"), "1e-9");
    EXPECT_EQ(options.at("TEMP"), "");
}

TEST(OptionEditorChecks, apply_rows_drops_a_valued_key_cleared_to_empty) {
    // arrange
    option_editor::OptionRow row;
    row.key = "GMIN";
    row.value = "";
    row.flag = false;
    // act
    const std::map<std::string, std::string> options = option_editor::apply_rows({row});
    // assert
    EXPECT_TRUE(options.empty());
}

TEST(OptionEditorChecks, assemble_rewrites_only_the_packages_the_editor_shows) {
    // arrange
    const OptionParameters current = OptionParameters::from_xyce_directives({".OPTIONS DEVICE GMIN=1e-12", ".OPTIONS TIMEINT ABSTOL=1e-6"});
    std::vector<std::vector<option_editor::OptionRow>> rows;
    for (const OptionPackage package : option_editor::dialog_packages())
        rows.push_back(option_editor::build_rows(package, package_options(current, package)));
    // act
    const std::size_t device = static_cast<std::size_t>(std::distance(option_editor::dialog_packages().begin(), std::find(option_editor::dialog_packages().begin(), option_editor::dialog_packages().end(), OptionPackage::DEVICE)));
    const auto gmin = std::find_if(rows[device].begin(), rows[device].end(), [](const option_editor::OptionRow& row) { return row.key == "GMIN"; });
    ASSERT_NE(gmin, rows[device].end());
    gmin->value = "1e-9";
    const OptionParameters result = option_editor::assemble(current, rows);
    // assert
    EXPECT_EQ(package_options(result, OptionPackage::DEVICE).at("GMIN"), "1e-9");
    EXPECT_EQ(package_options(result, OptionPackage::TIMEINT).at("ABSTOL"), "1e-6");
}

TEST(OptionEditorChecks, assemble_keeps_the_packages_without_a_row_list) {
    // arrange
    const OptionParameters current = OptionParameters::from_xyce_directives({".OPTIONS DEVICE GMIN=1e-12", ".OPTIONS PARSER SCALE=2"});
    std::vector<std::vector<option_editor::OptionRow>> rows;
    rows.push_back(option_editor::build_rows(OptionPackage::DEVICE, package_options(current, OptionPackage::DEVICE)));
    // act
    const OptionParameters result = option_editor::assemble(current, rows);
    // assert
    EXPECT_EQ(package_options(result, OptionPackage::DEVICE).at("GMIN"), "1e-12");
    EXPECT_EQ(package_options(result, OptionPackage::PARSER).at("SCALE"), "2");
}

TEST(OptionEditorChecks, the_edited_options_serialize_as_one_directive) {
    // arrange
    const OptionParameters current = OptionParameters::from_xyce_directives({});
    std::vector<std::vector<option_editor::OptionRow>> rows;
    for (const OptionPackage package : option_editor::dialog_packages())
        rows.push_back(option_editor::build_rows(package, package_options(current, package)));
    // act
    const std::size_t device = static_cast<std::size_t>(std::distance(option_editor::dialog_packages().begin(), std::find(option_editor::dialog_packages().begin(), option_editor::dialog_packages().end(), OptionPackage::DEVICE)));
    const auto gmin = std::find_if(rows[device].begin(), rows[device].end(), [](const option_editor::OptionRow& row) { return row.key == "GMIN"; });
    ASSERT_NE(gmin, rows[device].end());
    gmin->value = "1e-9";
    const OptionParameters result = option_editor::assemble(current, rows);
    // assert
    const std::vector<std::string> directives = result.to_xyce_directives(NetlistTopology{});
    ASSERT_EQ(directives.size(), 1U);
    EXPECT_EQ(directives.front(), ".OPTIONS DEVICE GMIN=1e-9");
}

TEST(OptionEditorChecks, rewrite_directives_replaces_only_the_editor_packages) {
    // arrange
    const std::string netlist = "* divider\nR1 1 0 1k\n.TRAN 1m 10m\n.OPTIONS DEVICE GMIN=1e-12\n.OPTIONS TIMEINT ABSTOL=1e-3\n.OPTIONS PARSER SCALE=2\n.END\n";
    const OptionParameters options = OptionParameters::from_xyce_directives({".OPTIONS DEVICE GMIN=1e-9", ".OPTIONS TIMEINT ABSTOL=1e-3", ".OPTIONS PARSER SCALE=2"});
    // act
    const std::string rewritten = option_editor::rewrite_directives(netlist, options);
    // assert
    EXPECT_NE(rewritten.find(".OPTIONS DEVICE GMIN=1e-9"), std::string::npos);
    EXPECT_EQ(rewritten.find("GMIN=1e-12"), std::string::npos);
    EXPECT_NE(rewritten.find("* divider"), std::string::npos);
    EXPECT_NE(rewritten.find(".TRAN 1m 10m"), std::string::npos);
    EXPECT_NE(rewritten.find(".OPTIONS TIMEINT ABSTOL=1e-3"), std::string::npos);
    EXPECT_NE(rewritten.find(".OPTIONS PARSER SCALE=2"), std::string::npos);
}

TEST(OptionEditorChecks, rewrite_directives_removes_a_package_the_editor_cleared) {
    // arrange
    const std::string netlist = "R1 1 0 1k\n.OPTIONS DEVICE GMIN=1e-9\n.OPTIONS PARSER SCALE=2\n.END\n";
    const OptionParameters options = OptionParameters::from_xyce_directives({".OPTIONS PARSER SCALE=2"});
    // act
    const std::string rewritten = option_editor::rewrite_directives(netlist, options);
    // assert
    EXPECT_EQ(rewritten.find("DEVICE"), std::string::npos);
    // the kept package appears exactly once
    std::size_t occurrences = 0;
    for (std::size_t at = rewritten.find("PARSER"); at != std::string::npos; at = rewritten.find("PARSER", at + 1))
        occurrences++;
    EXPECT_EQ(occurrences, 1U);
}

TEST(OptionEditorChecks, rewrite_directives_takes_a_multi_line_directive_with_it) {
    // arrange
    const std::string netlist = "R1 1 0 1k\n.OPTIONS DEVICE GMIN=1e-12\n+ TEMP=25\n.OPTIONS PARSER SCALE=2\n.END\n";
    const OptionParameters options = OptionParameters::from_xyce_directives({".OPTIONS DEVICE GMIN=1e-9", ".OPTIONS PARSER SCALE=2"});
    // act
    const std::string rewritten = option_editor::rewrite_directives(netlist, options);
    // assert
    EXPECT_NE(rewritten.find(".OPTIONS DEVICE GMIN=1e-9"), std::string::npos);
    EXPECT_EQ(rewritten.find("TEMP=25"), std::string::npos);
    EXPECT_EQ(rewritten.find("GMIN=1e-12"), std::string::npos);
    EXPECT_NE(rewritten.find(".OPTIONS PARSER SCALE=2"), std::string::npos);
}

TEST(OptionEditorChecks, rewrite_directives_closes_a_netlist_without_an_end_marker) {
    // arrange
    const std::string netlist = "R1 1 0 1k\n";
    const OptionParameters options = OptionParameters::from_xyce_directives({".OPTIONS DEVICE GMIN=1e-9"});
    // act
    const std::string rewritten = option_editor::rewrite_directives(netlist, options);
    // assert
    EXPECT_NE(rewritten.find("R1 1 0 1k"), std::string::npos);
    EXPECT_NE(rewritten.find(".OPTIONS DEVICE GMIN=1e-9"), std::string::npos);
    EXPECT_TRUE(rewritten.ends_with(".END\n"));
}

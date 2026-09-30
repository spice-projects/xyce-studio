#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/util.h"
#include "ui/options_dialog_view.h"

TEST(OptionsDialogViewChecks, catalogs_cover_every_package_with_unique_uppercase_keys) {
    // arrange — every panel package index
    const std::vector<size_t> packages = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    for (const auto index : packages) {
        // act — read the catalog for the package
        const auto& catalog = options_dialog_view::option_package_catalog(index);
        // assert — a non-empty list of unique uppercase keys
        ASSERT_FALSE(catalog.empty());
        const std::set<std::string> unique(catalog.begin(), catalog.end());
        EXPECT_EQ(unique.size(), catalog.size());
        for (const auto& key : catalog)
            EXPECT_EQ(key, to_upper(key));
    }
}

TEST(OptionsDialogViewChecks, unknown_package_index_is_rejected) {
    // arrange — a config for the member lookup
    OptionParameters options({}, {}, {}, {}, {});
    // act / assert — both lookups throw past the last package
    ASSERT_THROW(static_cast<void>(options_dialog_view::option_package_catalog(16)), std::out_of_range);
    ASSERT_THROW(static_cast<void>(options_dialog_view::package_options(options, 16)), std::out_of_range);
}

TEST(OptionsDialogViewChecks, package_options_maps_panel_index_to_package_members) {
    // arrange — a config to write into through the panel indexes
    OptionParameters options({}, {}, {}, {}, {});
    // act — write through the first, middle and last package indexes
    options_dialog_view::package_options(options, 0)["GMIN"] = "1e-12";
    options_dialog_view::package_options(options, 3)["MAXSTEP"] = "20";
    options_dialog_view::package_options(options, 15)["NUMSAMPLES"] = "100";
    // assert — the writes landed on the matching members
    EXPECT_EQ(options.device.at("GMIN"), "1e-12");
    EXPECT_EQ(options.nonlin_tran.at("MAXSTEP"), "20");
    EXPECT_EQ(options.embeddedsamples.at("NUMSAMPLES"), "100");
}

TEST(OptionsDialogViewChecks, build_option_rows_places_catalog_keys_then_netlist_only_keys) {
    // arrange — a two-key catalog and a netlist carrying one catalog key plus one unknown key
    const std::vector<std::string> catalog = {"RELTOL", "ABSTOL"};
    const std::map<std::string, std::string> loaded = {{"RELTOL", "1e-4"}, {"EXTRA", "5"}};
    // act — build the editor rows
    const auto rows = options_dialog_view::build_option_rows(catalog, loaded);
    // assert — catalog order first with the loaded value, then the netlist-only key
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_EQ(std::string(rows[0].key), "RELTOL");
    EXPECT_EQ(std::string(rows[0].value), "1e-4");
    EXPECT_FALSE(rows[0].flag);
    EXPECT_EQ(std::string(rows[1].key), "ABSTOL");
    EXPECT_EQ(std::string(rows[1].value), "");
    EXPECT_FALSE(rows[1].flag);
    EXPECT_EQ(std::string(rows[2].key), "EXTRA");
    EXPECT_EQ(std::string(rows[2].value), "5");
    EXPECT_FALSE(rows[2].flag);
}

TEST(OptionsDialogViewChecks, build_option_rows_marks_bare_flags) {
    // arrange — a catalog key the netlist carries as a bare flag without a value
    const std::vector<std::string> catalog = {"FFTOUT"};
    const std::map<std::string, std::string> loaded = {{"FFTOUT", ""}};
    // act — build the editor rows
    const auto rows = options_dialog_view::build_option_rows(catalog, loaded);
    // assert — the row keeps the empty value and carries the flag marker
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(std::string(rows[0].value), "");
    EXPECT_TRUE(rows[0].flag);
}

TEST(OptionsDialogViewChecks, apply_option_rows_writes_values_and_keeps_flags) {
    // arrange — one valued row, one empty row without a flag, one empty flag row
    const std::vector<main_window::OptionRow> rows = {
        main_window::OptionRow{slint::SharedString("RELTOL"), slint::SharedString("1e-3"), false},
        main_window::OptionRow{slint::SharedString("ABSTOL"), slint::SharedString(""), false},
        main_window::OptionRow{slint::SharedString("FFTOUT"), slint::SharedString(""), true},
    };
    // act — fold the rows back into one package map
    const auto options = options_dialog_view::apply_option_rows(rows);
    // assert — the valued row is written, the empty non-flag row is omitted, the flag row stays
    ASSERT_EQ(options.size(), 2u);
    EXPECT_EQ(options.at("RELTOL"), "1e-3");
    EXPECT_EQ(options.count("ABSTOL"), 0u);
    EXPECT_TRUE(options.contains("FFTOUT"));
    EXPECT_EQ(options.at("FFTOUT"), "");
}

TEST(OptionsDialogViewChecks, apply_option_rows_trims_values_and_uppercases_keys) {
    // arrange — a row with surrounding whitespace in the key and the value
    const std::vector<main_window::OptionRow> rows = {
        main_window::OptionRow{slint::SharedString(" reltol "), slint::SharedString(" 1e-3 "), false},
    };
    // act — fold the rows back into one package map
    const auto options = options_dialog_view::apply_option_rows(rows);
    // assert — the key is normalized and the value is written without padding
    ASSERT_EQ(options.size(), 1u);
    EXPECT_EQ(options.at("RELTOL"), "1e-3");
}

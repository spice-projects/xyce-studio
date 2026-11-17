#include <algorithm>
#include <cstddef>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "netlist/netlist.h"
#include "presenters/options_presenter.h"
#include "simulation/option_catalog.h"
#include "simulation/option_editor.h"
#include "simulation/option_parameters.h"

TEST(StudioOptionsPresenterChecks, begin_builds_one_row_list_per_shown_package) {
    // arrange: an editor over a netlist without options
    studio::OptionsPresenter presenter;
    // act: seed the editor
    presenter.begin(OptionParameters::from_xyce_directives({}));
    // assert: the editor shows the simulation-agnostic packages in card order
    EXPECT_EQ(presenter.packages(), option_editor::dialog_packages());
    ASSERT_EQ(presenter.packages().size(), 7U);
    EXPECT_EQ(presenter.rows(OptionPackage::DEVICE).size(), option_package_catalog(OptionPackage::DEVICE).size());
}

TEST(StudioOptionsPresenterChecks, a_netlist_option_seeds_its_row) {
    // arrange: a netlist that sets one device option
    studio::OptionsPresenter presenter;
    // act: seed the editor from the directives the netlist carries
    presenter.begin(OptionParameters::from_xyce_directives({".OPTIONS DEVICE GMIN=1e-9"}));
    // assert: the documented row carries the loaded value
    const std::vector<option_editor::OptionRow>& rows = presenter.rows(OptionPackage::DEVICE);
    bool found = false;
    for (const option_editor::OptionRow& row : rows) {
        // the GMIN row is the one the netlist set
        if (row.key != "GMIN")
            continue;
        found = true;
        EXPECT_EQ(row.value, "1e-9");
        EXPECT_FALSE(row.flag);
    }
    EXPECT_TRUE(found);
}

TEST(StudioOptionsPresenterChecks, an_edited_value_reaches_the_result) {
    // arrange: an editor seeded from a netlist without options
    studio::OptionsPresenter presenter;
    presenter.begin(OptionParameters::from_xyce_directives({}));
    // the row the edit names
    const std::vector<option_editor::OptionRow>& rows = presenter.rows(OptionPackage::DEVICE);
    const auto gmin = std::find_if(rows.begin(), rows.end(), [](const option_editor::OptionRow& row) { return row.key == "GMIN"; });
    ASSERT_NE(gmin, rows.end());
    const std::size_t index = static_cast<std::size_t>(std::distance(rows.begin(), gmin));
    // act: commit the value the user typed
    ASSERT_TRUE(presenter.edit(OptionPackage::DEVICE, index, "1e-9", false));
    // assert: the result declares the edited option
    const OptionParameters result = presenter.result();
    EXPECT_EQ(package_options(result, OptionPackage::DEVICE).at("GMIN"), "1e-9");
}

TEST(StudioOptionsPresenterChecks, choosing_the_flag_marker_keeps_a_bare_flag) {
    // arrange: an editor seeded from a netlist that carries a bare flag
    studio::OptionsPresenter presenter;
    presenter.begin(OptionParameters::from_xyce_directives({".OPTIONS DEVICE VOLTLIM"}));
    // the row the edit names
    const std::vector<option_editor::OptionRow>& rows = presenter.rows(OptionPackage::DEVICE);
    const auto voltlim = std::find_if(rows.begin(), rows.end(), [](const option_editor::OptionRow& row) { return row.key == "VOLTLIM"; });
    ASSERT_NE(voltlim, rows.end());
    const std::size_t index = static_cast<std::size_t>(std::distance(rows.begin(), voltlim));
    // act: keep the flag marker, then switch the row to a documented choice
    ASSERT_TRUE(presenter.edit(OptionPackage::DEVICE, index, "", true));
    // assert: the bare flag survives the round trip
    EXPECT_EQ(package_options(presenter.result(), OptionPackage::DEVICE).at("VOLTLIM"), "");
    ASSERT_TRUE(presenter.edit(OptionPackage::DEVICE, index, "1", false));
    // assert: a chosen value replaces the flag
    EXPECT_EQ(package_options(presenter.result(), OptionPackage::DEVICE).at("VOLTLIM"), "1");
    // assert: the selection names the value inside the list the row shows
    EXPECT_EQ(presenter.rows(OptionPackage::DEVICE)[index].choice_index, 3);
}

TEST(StudioOptionsPresenterChecks, a_package_the_editor_does_not_show_is_refused) {
    // arrange: an editor seeded from a netlist that sets an analysis option
    studio::OptionsPresenter presenter;
    presenter.begin(OptionParameters::from_xyce_directives({".OPTIONS TIMEINT ABSTOL=1e-6"}));
    // act: edit the package the parameters dialog owns
    EXPECT_FALSE(presenter.edit(OptionPackage::TIMEINT, 0, "1e-3", false));
    // assert: the editor shows no rows for it
    EXPECT_TRUE(presenter.rows(OptionPackage::TIMEINT).empty());
    // act: edit a row past the list
    EXPECT_FALSE(presenter.edit(OptionPackage::DEVICE, 9999, "1", false));
}

TEST(StudioOptionsPresenterChecks, the_result_keeps_the_packages_the_editor_does_not_show) {
    // arrange: a netlist that sets one analysis option and one editor option
    studio::OptionsPresenter presenter;
    presenter.begin(OptionParameters::from_xyce_directives({".OPTIONS TIMEINT ABSTOL=1e-6", ".OPTIONS PARSER SCALE=2.5"}));
    // the device row the edit names
    const std::vector<option_editor::OptionRow>& rows = presenter.rows(OptionPackage::DEVICE);
    const auto gmin = std::find_if(rows.begin(), rows.end(), [](const option_editor::OptionRow& row) { return row.key == "GMIN"; });
    ASSERT_NE(gmin, rows.end());
    // act: set a device option
    ASSERT_TRUE(presenter.edit(OptionPackage::DEVICE, static_cast<std::size_t>(std::distance(rows.begin(), gmin)), "1e-9", false));
    // assert: the analysis package keeps the value the netlist carried
    const OptionParameters result = presenter.result();
    EXPECT_EQ(package_options(result, OptionPackage::TIMEINT).at("ABSTOL"), "1e-6");
    // assert: the editor package keeps the value the netlist carried too
    EXPECT_EQ(package_options(result, OptionPackage::PARSER).at("SCALE"), "2.5");
}

TEST(StudioOptionsPresenterChecks, the_accepted_options_serialize_the_edited_packages) {
    // arrange: an editor seeded from a netlist without options
    studio::OptionsPresenter presenter;
    presenter.begin(OptionParameters::from_xyce_directives({}));
    // the device row the edit names
    const std::vector<option_editor::OptionRow>& rows = presenter.rows(OptionPackage::DEVICE);
    const auto gmin = std::find_if(rows.begin(), rows.end(), [](const option_editor::OptionRow& row) { return row.key == "GMIN"; });
    ASSERT_NE(gmin, rows.end());
    ASSERT_TRUE(presenter.edit(OptionPackage::DEVICE, static_cast<std::size_t>(std::distance(rows.begin(), gmin)), "1e-9", false));
    // act: serialize the accepted options
    const std::vector<std::string> directives = presenter.result().to_xyce_directives(NetlistTopology{});
    // assert: the netlist rewrites as one device directive
    ASSERT_EQ(directives.size(), 1U);
    EXPECT_EQ(directives.front(), ".OPTIONS DEVICE GMIN=1e-9");
}

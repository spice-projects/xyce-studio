#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/util.h"
#include "simulation/option_catalog.h"

TEST(OptionCatalogChecks, catalogs_cover_every_package_with_unique_uppercase_keys) {
    // arrange — every managed package index
    const std::vector<size_t> packages = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    for (const auto index : packages) {
        // act — read the catalog for the package
        const auto& catalog = option_package_catalog(static_cast<OptionPackage>(index));
        // assert — a non-empty list of unique uppercase keys
        ASSERT_FALSE(catalog.empty());
        const std::set<std::string> unique(catalog.begin(), catalog.end());
        EXPECT_EQ(unique.size(), catalog.size());
        for (const auto& key : catalog)
            EXPECT_EQ(key, to_upper(key));
    }
}

TEST(OptionCatalogChecks, unknown_package_is_rejected) {
    // arrange — out-of-range values cast onto the enum
    const auto above = static_cast<OptionPackage>(16);
    const auto below = static_cast<OptionPackage>(-1);
    OptionParameters options({}, {}, {}, {}, {});
    // act / assert — every accessor throws past the enum range
    ASSERT_THROW(static_cast<void>(option_package_catalog(above)), std::out_of_range);
    ASSERT_THROW(static_cast<void>(option_package_catalog(below)), std::out_of_range);
    ASSERT_THROW(static_cast<void>(package_options(options, above)), std::out_of_range);
    ASSERT_THROW(static_cast<void>(package_options(static_cast<const OptionParameters&>(options), below)), std::out_of_range);
    ASSERT_THROW(static_cast<void>(option_key_info(above, "METHOD")), std::out_of_range);
}

TEST(OptionCatalogChecks, key_info_reports_choices_and_defaults) {
    // act — read the metadata of a closed-choice key, a free-text key and an unknown key
    const auto method = option_key_info(OptionPackage::TIMEINT, "METHOD");
    const auto reltol = option_key_info(OptionPackage::TIMEINT, "RELTOL");
    const auto unknown = option_key_info(OptionPackage::TIMEINT, "NO_SUCH_KEY");
    // assert — the choice key lists the documented values and the default
    EXPECT_EQ(method.choices, (std::vector<std::string>{"trap", "7", "gear", "8"}));
    EXPECT_EQ(method.default_value, "trap");
    // assert — the free-text key reports only the reference default
    EXPECT_TRUE(reltol.choices.empty());
    EXPECT_EQ(reltol.default_value, "1.0E-03");
    // assert — an unknown key reports empty metadata
    EXPECT_TRUE(unknown.choices.empty());
    EXPECT_TRUE(unknown.default_value.empty());
}

TEST(OptionCatalogChecks, package_options_maps_package_to_members) {
    // arrange — a config to write into through the package enum
    OptionParameters options({}, {}, {}, {}, {});
    // act — write through the first, middle and last packages
    package_options(options, OptionPackage::DEVICE)["GMIN"] = "1e-12";
    package_options(options, OptionPackage::NONLIN_TRAN)["MAXSTEP"] = "20";
    package_options(options, OptionPackage::EMBEDDEDSAMPLES)["NUMSAMPLES"] = "100";
    // assert — the writes landed on the matching members
    EXPECT_EQ(options.device.at("GMIN"), "1e-12");
    EXPECT_EQ(options.nonlin_tran.at("MAXSTEP"), "20");
    EXPECT_EQ(options.embeddedsamples.at("NUMSAMPLES"), "100");
    // act — read through the const overload
    const auto& timeint = package_options(static_cast<const OptionParameters&>(options), OptionPackage::TIMEINT);
    // assert — the const reference resolves the timeint member
    EXPECT_TRUE(timeint.empty());
}

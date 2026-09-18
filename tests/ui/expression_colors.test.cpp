#include <string>

#include <gtest/gtest.h>

#include "../../src/ui/expression_colors.h"

TEST(ExpressionColorsChecks, empty_unit_is_gray_misc) {
    // arrange / act
    const auto color = expression_colors::expression_unit_color("");
    // assert
    ASSERT_EQ(color.as_argb_encoded(), 0xff3a3d4a);
}

TEST(ExpressionColorsChecks, curated_base_units_keep_classic_colors) {
    // arrange / act
    const auto voltage = expression_colors::expression_unit_color("V");
    const auto current = expression_colors::expression_unit_color("A");
    const auto time = expression_colors::expression_unit_color("s");
    const auto frequency = expression_colors::expression_unit_color("Hz");
    const auto power = expression_colors::expression_unit_color("W");
    // assert
    ASSERT_EQ(voltage.as_argb_encoded(), 0xff5b9bd5);
    ASSERT_EQ(current.as_argb_encoded(), 0xff7cb342);
    ASSERT_EQ(time.as_argb_encoded(), 0xffba68c8);
    ASSERT_EQ(frequency.as_argb_encoded(), 0xffe57373);
    ASSERT_EQ(power.as_argb_encoded(), 0xffffb74d);
}

TEST(ExpressionColorsChecks, compound_units_have_curated_colors) {
    // arrange / act
    const auto omega = expression_colors::expression_unit_color("\u03A9");
    const auto siemens = expression_colors::expression_unit_color("S");
    const auto phase = expression_colors::expression_unit_color("\u00B0");
    // assert
    ASSERT_EQ(omega.as_argb_encoded(), 0xff26c6da);
    ASSERT_EQ(siemens.as_argb_encoded(), 0xff8d6e63);
    ASSERT_EQ(phase.as_argb_encoded(), 0xfff06292);
}

TEST(ExpressionColorsChecks, decibel_family_shares_the_decibel_color) {
    // arrange / act
    const auto db = expression_colors::expression_unit_color("dB");
    const auto dbv = expression_colors::expression_unit_color("dBV");
    const auto dbw = expression_colors::expression_unit_color("dBW");
    const auto dba = expression_colors::expression_unit_color("dBA");
    // assert
    ASSERT_EQ(db.as_argb_encoded(), 0xffffd54f);
    ASSERT_EQ(dbv.as_argb_encoded(), 0xffffd54f);
    ASSERT_EQ(dbw.as_argb_encoded(), 0xffffd54f);
    ASSERT_EQ(dba.as_argb_encoded(), 0xffffd54f);
}

TEST(ExpressionColorsChecks, metric_prefixed_units_keep_the_base_color) {
    // arrange / act
    const auto millivolt = expression_colors::expression_unit_color("mV");
    const auto microvolt = expression_colors::expression_unit_color("\u00B5V");
    const auto milliampere = expression_colors::expression_unit_color("mA");
    const auto kilohertz = expression_colors::expression_unit_color("kHz");
    const auto millisecond = expression_colors::expression_unit_color("ms");
    // assert
    ASSERT_EQ(millivolt.as_argb_encoded(), 0xff5b9bd5);
    ASSERT_EQ(microvolt.as_argb_encoded(), 0xff5b9bd5);
    ASSERT_EQ(milliampere.as_argb_encoded(), 0xff7cb342);
    ASSERT_EQ(kilohertz.as_argb_encoded(), 0xffe57373);
    ASSERT_EQ(millisecond.as_argb_encoded(), 0xffba68c8);
}

TEST(ExpressionColorsChecks, arbitrary_unit_label_gets_stable_hash_color) {
    // arrange
    const std::string unit = "dBJ";
    // act
    const auto first = expression_colors::expression_unit_color(unit);
    const auto second = expression_colors::expression_unit_color(unit);
    // assert: the color is not the misc gray and repeated calls return the
    // same color
    ASSERT_NE(first.as_argb_encoded(), 0xff3a3d4a);
    ASSERT_EQ(first.as_argb_encoded(), second.as_argb_encoded());
}

TEST(ExpressionColorsChecks, case_variants_of_a_unit_share_the_hash_color) {
    // arrange / act
    const auto lower = expression_colors::expression_unit_color("xunit");
    const auto upper = expression_colors::expression_unit_color("XUNIT");
    // assert
    ASSERT_EQ(lower.as_argb_encoded(), upper.as_argb_encoded());
}

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include <slint.h>

namespace expression_colors
{
    namespace
    {
        // curated palette entries; the first five mirror the classic
        // voltage/current/frequency/time/power colors
        struct UnitColor
        {
            std::string_view unit;
            std::uint32_t rgb;
        };

        // units with a curated color; compound units (dB family, ohms,
        // siemens, phase) appear as the data requires
        const std::array<UnitColor, 12> CURATED{{
            {"V", 0x5b9bd5},
            {"A", 0x7cb342},
            {"s", 0xba68c8},
            {"Hz", 0xe57373},
            {"W", 0xffb74d},
            {"\u03A9", 0x26c6da},
            {"S", 0x8d6e63},
            {"\u00B0", 0xf06292},
            {"dB", 0xffd54f},
            {"dBV", 0xffd54f},
            {"dBW", 0xffd54f},
            {"dBA", 0xffd54f},
        }};

        // base units accepted after a metric prefix
        const std::array<std::string_view, 7> BASE_UNITS{{"V", "A", "s", "Hz", "W", "\u03A9", "S"}};

        // metric prefixes accepted in front of a base unit
        const std::array<std::string_view, 10> METRIC_PREFIXES{{"m", "k", "M", "G", "T", "n", "p", "f", "u", "\u00B5"}};

        // fallback palette for arbitrary unit labels; the fnv-1a hash of the
        // unit picks one entry, keeping colors stable across runs
        const std::array<std::uint32_t, 10> FALLBACK{{
            0xef9a9a,
            0xa5d6a7,
            0x90caf9,
            0xffe082,
            0xce93d8,
            0x80cbc4,
            0xbcaaa4,
            0x9fa8da,
            0xf48fb1,
            0xaed581,
        }};

        // build a slint color from a packed 0xRRGGBB value
        inline slint::Color packed_color(const std::uint32_t rgb) { return slint::Color::from_rgb_uint8((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff); }
    } // namespace

    // map a measurement unit to a stable color; the curated table covers the
    // common units, a metric prefix in front of a base unit keeps the base
    // color, dB family units share the decibel color, unknown units hash into
    // the fallback palette, and the empty unit is gray misc
    inline slint::Color expression_unit_color(std::string_view unit) {
        // empty unit means misc
        if (unit.empty())
            return packed_color(0x3a3d4a);
        // curated exact matches
        for (const auto& [curated_unit, rgb] : CURATED) {
            if (unit == curated_unit)
                return packed_color(rgb);
        }
        // decibel family shares the decibel color
        if (unit.starts_with("dB"))
            return packed_color(0xffd54f);
        // strip a metric prefix and retry the base units
        for (const auto& prefix : METRIC_PREFIXES) {
            if (!unit.starts_with(prefix))
                continue;
            // search the curated table for the base unit behind the prefix
            const auto base = unit.substr(prefix.size());
            const auto it = std::ranges::find(CURATED, base, &UnitColor::unit);
            if (it != CURATED.end())
                return packed_color(it->rgb);
        }
        // fnv-1a hash of the unit label picks a stable fallback color
        std::uint32_t hash = 0x811c9dc5;
        for (const char c : unit) {
            // hash the case-normalized byte so case variants share a color
            hash ^= static_cast<std::uint32_t>(static_cast<unsigned char>(c | 0x20));
            hash *= 0x01000193;
        }
        return packed_color(FALLBACK[hash % FALLBACK.size()]);
    }

    // one legend row: display label and its color
    struct LegendEntry
    {
        std::string label;
        slint::Color color;
    };

    // build the legend rows for a dataset: distinct units in first-seen
    // order, misc when any unit is empty, and the scope kinds that are
    // present in the dataset
    inline std::vector<LegendEntry> expression_legend_entries(const std::vector<std::string>& units, const bool has_subcircuit, const bool has_sheet) {
        // result rows
        std::vector<LegendEntry> entries;
        // distinct units already emitted, preserving first-seen order
        std::vector<std::string> seen;
        // loop units, deduplicating while keeping first-seen order
        for (const auto& unit : units) {
            // empty units share the single misc row
            if (unit.empty()) {
                continue;
            }
            if (std::ranges::find(seen, unit) == seen.end()) {
                seen.push_back(unit);
            }
        }
        // emit a row per distinct unit
        for (const auto& unit : seen)
            entries.push_back({unit, expression_unit_color(unit)});
        // misc row when the dataset has expressions without a unit
        if (std::ranges::find(units, std::string{}) != units.end())
            entries.push_back({"Misc", expression_unit_color("")});
        // subcircuit scope row when the dataset has subcircuit probes
        if (has_subcircuit)
            entries.push_back({"Subcircuit", packed_color(0x8e6dd9)});
        // sheet scope row when the dataset has sheet probes
        if (has_sheet)
            entries.push_back({"Sheet", packed_color(0x4dd0e1)});
        return entries;
    }
} // namespace expression_colors

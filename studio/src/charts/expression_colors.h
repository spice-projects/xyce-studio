#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "albor/core/paint.h"

namespace studio
{
    namespace expression_colors
    {
        namespace
        {
            // curated palette entries; the first five mirror the classic voltage/current/frequency/time/power colors
            struct UnitColor
            {
                std::string_view unit;
                std::uint32_t rgb;
            };

            // units with a curated color, so the common signals keep their identity ink
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

            // the fnv-1a hash of an unknown unit picks a stable fallback color
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

            // build an albor color from a packed 0xRRGGBB value
            inline albor::Color packed_color(const std::uint32_t rgb) { return albor::Color::rgba(static_cast<float>((rgb >> 16) & 0xff) / 255.0f, static_cast<float>((rgb >> 8) & 0xff) / 255.0f, static_cast<float>(rgb & 0xff) / 255.0f, 1.0f); }
        } // namespace

        // map a measurement unit to a stable color, with unknown units hashed into the fallback palette
        inline albor::Color expression_unit_color(std::string_view unit) {
            // the empty unit means misc
            if (unit.empty())
                return packed_color(0x3a3d4a);
            // the curated exact matches win
            for (const auto& [curated_unit, rgb] : CURATED) {
                if (unit == curated_unit)
                    return packed_color(rgb);
            }
            // the decibel family shares the decibel color
            if (unit.starts_with("dB"))
                return packed_color(0xffd54f);
            // strip a metric prefix and retry the base units
            for (const auto& prefix : METRIC_PREFIXES) {
                // only a unit that starts with the prefix can hide its base
                if (!unit.starts_with(prefix))
                    continue;
                // the base unit behind the prefix
                const std::string_view base = unit.substr(prefix.size());
                // the curated entry the base unit names
                const auto it = std::ranges::find(CURATED, base, &UnitColor::unit);
                // a curated base unit keeps its color
                if (it != CURATED.end())
                    return packed_color(it->rgb);
            }
            // the fnv-1a hash of the unit label picks a stable fallback color
            std::uint32_t hash = 0x811c9dc5;
            for (const char c : unit) {
                // hash the case-normalized byte so case variants share a color
                hash ^= static_cast<std::uint32_t>(static_cast<unsigned char>(c | 0x20));
                hash *= 0x01000193;
            }
            // the hashed fallback entry is the color
            return packed_color(FALLBACK[hash % FALLBACK.size()]);
        }

        // the legend label a measurement unit reads as, which groups the raw units into signal categories
        inline std::string legend_label(std::string_view unit) {
            // the empty unit is misc
            if (unit.empty())
                return "Misc";
            // the common signal categories keep the design's names
            if (unit == "V")
                return "Voltage";
            if (unit == "A")
                return "Current";
            if (unit == "s")
                return "Time";
            if (unit == "Hz")
                return "Frequency";
            if (unit == "W")
                return "Power";
            if (unit == "\u03A9")
                return "Impedance";
            if (unit == "S")
                return "Admittance";
            if (unit == "\u00B0")
                return "Phase";
            if (unit.starts_with("dB"))
                return "dB";
            // every other unit keeps its own spelling
            return std::string(unit);
        }

        // one legend row: display label and its color
        struct LegendEntry
        {
            std::string label;
            albor::Color color;
        };

        // build the legend rows for the dataset units and the scope kinds it carries
        inline std::vector<LegendEntry> expression_legend_entries(const std::vector<std::string>& units, const bool has_subcircuit, const bool has_sheet) {
            // result rows
            std::vector<LegendEntry> entries;
            // distinct legend labels already emitted, preserving first-seen order
            std::vector<std::string> seen;
            // loop units, deduplicating the category labels while keeping first-seen order
            for (const std::string& unit : units) {
                // empty units share the single misc row
                if (unit.empty())
                    continue;
                // the category the unit reads as
                const std::string label = legend_label(unit);
                // a category seen for the first time joins the list
                if (std::ranges::find(seen, label) == seen.end())
                    seen.push_back(label);
            }
            // emit a row per distinct category
            for (const std::string& label : seen) {
                // the first unit of the category decides the dot color
                const auto unit = std::ranges::find_if(units, [&label](const std::string& candidate) { return !candidate.empty() && legend_label(candidate) == label; });
                // the row takes the category label and its color
                entries.push_back(LegendEntry{.label = label, .color = expression_unit_color(unit != units.end() ? *unit : std::string{})});
            }
            // the misc row appears when the dataset has expressions without a unit
            if (std::ranges::find(units, std::string{}) != units.end())
                entries.push_back(LegendEntry{.label = "Misc", .color = expression_unit_color("")});
            // the subcircuit row appears when the dataset has subcircuit probes
            if (has_subcircuit)
                entries.push_back(LegendEntry{.label = "Subcircuit", .color = packed_color(0x8e6dd9)});
            // the sheet row appears when the dataset has sheet probes
            if (has_sheet)
                entries.push_back(LegendEntry{.label = "Sheet", .color = packed_color(0x4dd0e1)});
            // hand the rows to the legend bar
            return entries;
        }
    } // namespace expression_colors
} // namespace studio

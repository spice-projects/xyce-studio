#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "simulation/option_catalog.h"
#include "simulation/option_parameters.h"

namespace option_editor
{
    // the entry that clears a valued option when it is selected
    inline constexpr std::string_view DEFAULT_CHOICE = "<default>";

    // the entry that keeps a bare flag, which the netlist writes without a value
    inline constexpr std::string_view FLAG_CHOICE = "flag is set";

    // one editable .OPTIONS key, as the options editor presents it
    struct OptionRow
    {
        // the option key, uppercased as the catalog and the netlist parser name it
        std::string key;

        // the value the netlist carries, empty for an unset key or a bare flag
        std::string value;

        // whether the netlist carries the key as a bare flag (no value)
        bool flag = false;

        // the choices the editor offers, empty when the key is free text
        std::vector<std::string> choices;

        // the reference default the editor shows as a hint, empty when none is documented
        std::string default_value;

        // the index of the current value inside the choices, or -1 when the row edits free text
        int choice_index = -1;
    };

    // the simulation-agnostic packages the options editor lists, in card order
    [[nodiscard]] const std::vector<OptionPackage>& dialog_packages();

    // the directive name a package writes, e.g. DEVICE or NONLIN-TRAN
    [[nodiscard]] std::string_view package_name(OptionPackage package);

    // build the editor rows of one package from the documented keys and the loaded values
    [[nodiscard]] std::vector<OptionRow> build_rows(OptionPackage package, const std::map<std::string, std::string>& loaded);

    // resolve the choice an edited row selects inside the list it shows, or -1 when it edits free text
    [[nodiscard]] int row_choice_index(const OptionRow& row);

    // fold one package's edited rows back into the options they declare
    [[nodiscard]] std::map<std::string, std::string> apply_rows(const std::vector<OptionRow>& rows);

    // rewrite the edited packages onto the options the editor opened with
    [[nodiscard]] OptionParameters assemble(const OptionParameters& current, const std::vector<std::vector<OptionRow>>& rows);

    // replace the .OPTIONS directives the editor owns in the netlist, leaving every other line untouched
    [[nodiscard]] std::string rewrite_directives(std::string_view netlist, const OptionParameters& options);
} // namespace option_editor

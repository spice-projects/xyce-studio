#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "simulation/option_catalog.h"
#include "simulation/option_editor.h"
#include "simulation/option_parameters.h"

namespace studio
{
    // owns the option rows the editor shows and folds the accepted rows back into the options
    class OptionsPresenter final
    {
    public:
        // seed the editor from the options the netlist carries
        void begin(const OptionParameters& current);

        // hand out the packages the editor shows, in card order
        [[nodiscard]] const std::vector<OptionPackage>& packages() const;

        // hand out the rows of one package, empty for a package the editor does not show
        [[nodiscard]] const std::vector<option_editor::OptionRow>& rows(OptionPackage package) const;

        // commit an edited row, returning false when the package or row is not shown
        bool edit(OptionPackage package, std::size_t row, std::string value, bool flag);

        // fold the edited rows back into the options the editor opened with
        [[nodiscard]] OptionParameters result() const;

    private:
        // the position of one package in the card order, or a size past the end when it is not shown
        [[nodiscard]] std::size_t position_of(OptionPackage package) const;

        // the options the editor opened with, which the untouched packages keep
        OptionParameters m_current = OptionParameters::from_xyce_directives({});

        // the edited rows, one list per package in card order
        std::vector<std::vector<option_editor::OptionRow>> m_rows;
    };
} // namespace studio

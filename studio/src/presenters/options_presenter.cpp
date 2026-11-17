#include "presenters/options_presenter.h"

#include <algorithm>
#include <utility>

void studio::OptionsPresenter::begin(const OptionParameters& current) {
    // the options the editor opened with decide the packages it leaves alone
    m_current = current;
    // rebuild one row list per package the editor shows
    m_rows.clear();
    for (const OptionPackage package : packages())
        m_rows.push_back(option_editor::build_rows(package, package_options(current, package)));
}

const std::vector<OptionPackage>& studio::OptionsPresenter::packages() const {
    // the packages the analysis dialog does not edit
    return option_editor::dialog_packages();
}

const std::vector<option_editor::OptionRow>& studio::OptionsPresenter::rows(const OptionPackage package) const {
    // a package the editor does not show has no rows
    static const std::vector<option_editor::OptionRow> EMPTY;
    // the row list the card shows
    const std::size_t position = position_of(package);
    // hand back the rows of the package the editor shows
    return position < m_rows.size() ? m_rows[position] : EMPTY;
}

bool studio::OptionsPresenter::edit(const OptionPackage package, const std::size_t row, std::string value, const bool flag) {
    // the package the edit names
    const std::size_t position = position_of(package);
    // a package the editor does not show has no row to edit
    if (position >= m_rows.size())
        return false;
    // a row past the list names nothing
    if (row >= m_rows[position].size())
        return false;
    // the edited row keeps the key, the choices and the hint it was seeded with
    option_editor::OptionRow& edited = m_rows[position][row];
    edited.value = std::move(value);
    edited.flag = flag;
    // the selection follows the value inside the list the row shows
    edited.choice_index = option_editor::row_choice_index(edited);
    // report the row committed
    return true;
}

OptionParameters studio::OptionsPresenter::result() const {
    // the edited packages rewrite the options the editor opened with
    return option_editor::assemble(m_current, m_rows);
}

std::size_t studio::OptionsPresenter::position_of(const OptionPackage package) const {
    // the card order decides the row list a package owns
    const std::vector<OptionPackage>& order = packages();
    // locate the package among the cards
    const auto found = std::find(order.begin(), order.end(), package);
    // a package the editor does not show sits past the end
    return found == order.end() ? order.size() : static_cast<std::size_t>(std::distance(order.begin(), found));
}

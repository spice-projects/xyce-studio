#include "presenters/plugin_config_presenter.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace
{
    // validation feedback strings
    constexpr const char* REQUIRED_ERROR = "Xyce executable path is required";
    constexpr const char* INVALID_ERROR = "Selected path is not an executable file";

    // trim the surrounding whitespace the field may carry
    std::string trim(std::string_view value) {
        // find the first byte that is not whitespace
        const std::size_t first = value.find_first_not_of(" \t\r\n");
        // an all-whitespace value trims to empty
        if (first == std::string_view::npos)
            return {};
        // find the last byte that is not whitespace
        const std::size_t last = value.find_last_not_of(" \t\r\n");
        // copy the trimmed span into an owning string
        return std::string(value.substr(first, last - first + 1));
    }
} // namespace

void studio::PluginConfigPresenter::show(PluginConfigDialogView& view, const PluginConfig& current, SavedHandler on_saved) {
    // a second open request while the dialog is up is ignored
    if (m_view != nullptr)
        return;
    // remember the surface and the result destination for the dialog lifetime
    m_view = &view;
    m_on_saved = std::move(on_saved);
    // open the dialog seeded with the current configuration
    view.show(current.xyce_executable_path(), *this);
}

void studio::PluginConfigPresenter::on_accept(std::string path) {
    // an accept without an open dialog is stale
    if (m_view == nullptr)
        return;
    // trim the path the field carries
    const std::string trimmed = trim(path);
    // require a non-empty path so the plugin can launch Xyce
    if (trimmed.empty()) {
        m_view->show_error(REQUIRED_ERROR);
        return;
    }
    // build the candidate configuration from the trimmed path
    const PluginConfig candidate(trimmed);
    // reject path values that are not executable files
    if (!candidate.is_xyce_executable_valid()) {
        m_view->show_error(INVALID_ERROR);
        return;
    }
    // persist the validated configuration before delivering it
    candidate.save();
    // take the view and the callback out before closing, so the closed notification is a no-op
    PluginConfigDialogView* view = m_view;
    SavedHandler on_saved = std::move(m_on_saved);
    m_view = nullptr;
    m_on_saved = nullptr;
    // close the dialog before the shell observes the new configuration
    view->close();
    // deliver the accepted configuration to the shell
    if (on_saved)
        on_saved(candidate);
}

void studio::PluginConfigPresenter::on_cancel() {
    // a cancel without an open dialog is stale
    if (m_view == nullptr)
        return;
    // take the view out before closing, so the closed notification is a no-op
    PluginConfigDialogView* view = m_view;
    m_view = nullptr;
    m_on_saved = nullptr;
    // close the dialog without persisting anything
    view->close();
}

void studio::PluginConfigPresenter::on_dialog_closed() {
    // the window is gone, so the next show is free to open a new one
    m_view = nullptr;
    m_on_saved = nullptr;
}

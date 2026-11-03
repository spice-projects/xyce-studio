#include "presenters/main_window_presenter.h"

#include "netlist/editor_netlist_source.h"

studio::MainWindowPresenter::MainWindowPresenter(MainWindowViewDef& view) :
    m_view(&view),
    // the shell starts from an untitled source, which reads live editor text and
    // defers every write until a save-as names the file
    m_netlist_source(std::make_unique<EditorNetlistSource>([this]() { return m_view->netlist_text(); }, std::filesystem::path{})) {
    // initialize the toolbar action states before the window is shown
    refresh_action_states();
}

void studio::MainWindowPresenter::on_open_file(const std::filesystem::path& path) {
    // netlist file extension; the analysis output formats arrive with the
    // dataset slice and are refused until then
    if (path.extension().string() != ".cir")
        return;
    // create an editor netlist source backed by the selected file, reading live editor text
    m_netlist_source = std::make_unique<EditorNetlistSource>([this]() { return m_view->netlist_text(); }, path);
    // update the window title from the source
    set_base_title(m_netlist_source->title());
    // the netlist editor is now editable
    m_view->set_netlist_read_only(false);
    // load the netlist content
    const auto [reloaded, content] = m_netlist_source->load_netlist();
    // set the editor content to the loaded netlist
    update_netlist_editor_content(content, false);
    // refresh toolbar/menu states
    refresh_action_states();
}

void studio::MainWindowPresenter::on_save() {
    // an untitled netlist has no file behind it: ask the user for a location and filename before anything can be written
    if (!m_netlist_source->has_backing_file()) {
        // run the save-as flow through the view (native dialog)
        const auto path = m_view->request_netlist_save_path();
        // user canceled: the netlist stays dirty for the next save attempt
        if (!path.has_value())
            return;
        // default to the netlist format when the chosen name carries no extension
        std::filesystem::path target = path.value();
        if (target.extension().empty())
            target += ".cir";
        // rebind the source to the chosen file so later saves write in place
        m_netlist_source = std::make_unique<EditorNetlistSource>([this]() { return m_view->netlist_text(); }, target);
        // the chosen file becomes the window title (clears the dirty marker)
        set_base_title(m_netlist_source->title());
        // consume the file seed now: the just-created source would otherwise read the file on its first load and miss later editor edits
        static_cast<void>(m_netlist_source->load_netlist());
    }
    // save content in the netlist source
    m_netlist_source->save_netlist();
    // reset the dirty flag and refresh states (set_base_title may have cleared the flag already, so the refresh is unconditional)
    set_netlist_editor_dirty(false);
    refresh_action_states();
}

void studio::MainWindowPresenter::on_netlist_edited() {
    // track whether the editor still holds content
    m_netlist_has_content = !m_view->netlist_text().empty();
    // mark the editor dirty and refresh states when the flag changed
    if (set_netlist_editor_dirty(true))
        refresh_action_states();
}

void studio::MainWindowPresenter::refresh_action_states() {
    // open stays available while no simulation is running; save needs unsaved
    // changes on netlist content that a writable source can write
    m_view->set_open_enabled(true);
    m_view->set_save_enabled(m_netlist_editor_dirty && m_netlist_has_content && !m_netlist_source->is_read_only());
}

bool studio::MainWindowPresenter::set_netlist_editor_dirty(bool flag) {
    // previous dirty state
    const bool previous_dirty_state = m_netlist_editor_dirty;
    // store the dirty state
    m_netlist_editor_dirty = flag;
    // prefix the base title while the editor holds unsaved changes
    m_view->set_title(flag ? "* " + m_base_title : m_base_title);
    // return true if the dirty state changed, false if it remained the same
    return previous_dirty_state != m_netlist_editor_dirty;
}

void studio::MainWindowPresenter::set_base_title(const std::string& title) {
    // store the clean base title
    m_base_title = title;
    // re-render the title with the current dirty marker preserved
    set_netlist_editor_dirty(m_netlist_editor_dirty);
}

bool studio::MainWindowPresenter::update_netlist_editor_content(const std::string& content, bool dirty_flag) {
    // set the editor content through the view
    m_view->set_netlist_text(content);
    // track whether the editor holds content
    m_netlist_has_content = !content.empty();
    // return whether the dirty flag changed
    return set_netlist_editor_dirty(dirty_flag);
}

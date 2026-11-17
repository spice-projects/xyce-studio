#include "views/options/options_dialog_view.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "albor/application/application.h"
#include "albor/application/window_stage.h"
#include "albor/core/font_weight.h"
#include "albor/layout/column_layout.h"
#include "albor/layout/row_layout.h"
#include "albor/layout/sized_box_layout.h"
#include "albor/ui/ui_node.h"
#include "albor/widgets/combo_box.h"
#include "albor/widgets/label.h"
#include "albor/widgets/panel.h"
#include "albor/widgets/text_field.h"
#include "albor/widgets/theme_controller.h"
#include "albor/window/window.h"
#include "app/theme.h"
#include "options_dialog.h"
#include "presenters/options_presenter.h"

namespace
{
    // the size the dialog window opens with
    constexpr float DIALOG_WIDTH = 780.0f;

    constexpr float DIALOG_HEIGHT = 640.0f;

    // the width the option key column keeps
    constexpr float KEY_COLUMN_WIDTH = 190.0f;

    // the height of one editor row
    constexpr float ROW_HEIGHT = 28.0f;

    // the gap between the key cell and its editor
    constexpr float ROW_GAP = 8.0f;

    // the inset and the gap a package card applies
    constexpr float CARD_PADDING = 10.0f;

    constexpr float CARD_SPACING = 6.0f;

    // find a node by the author id a compiled tree carries
    albor::UiNode* find_node(albor::UiNode& node, const std::string_view explicit_id) {
        // the node itself can be the match
        if (node.identity().explicit_id == explicit_id)
            return &node;
        // the match can sit anywhere below this node
        for (const auto& child : node.children()) {
            // the first match wins, because author ids are unique
            if (albor::UiNode* found = find_node(*child, explicit_id))
                return found;
        }
        // no node below this one carries the id
        return nullptr;
    }
} // namespace

studio::AlborOptionsDialogView::AlborOptionsDialogView(albor::Application& application, albor::WindowHandle parent) :
    m_app(&application), m_parent(parent) {}

studio::AlborOptionsDialogView::~AlborOptionsDialogView() {
    // a posted focus task drops once the view is gone
    *m_alive = false;
    // a dialog still up at teardown asks the application to release it
    if (m_handle.valid())
        (void)m_app->request_close(m_handle);
}

void studio::AlborOptionsDialogView::show(OptionsPresenter& presenter, AcceptedHandler accepted) {
    // a second show while the dialog is up is ignored
    if (is_open())
        return;
    // remember the rows and the sink the dialog works from
    m_presenter = &presenter;
    m_accepted = std::move(accepted);
    // describe a modal dialog owned by the shell
    albor::WindowOptions options;
    options.title = "Xyce Options";
    options.size = albor::Size{.width = DIALOG_WIDTH, .height = DIALOG_HEIGHT};
    options.role = albor::WindowRole::Dialog;
    options.modality = albor::Modality::WindowModal;
    options.parent = m_parent;
    options.resizable = true;
    options.visible = false;
    // build the compiled dialog tree in a fresh window
    m_handle = m_app->create_window(options, m_root);
    // a refused window leaves the caller without a dialog
    if (!m_handle.valid() || m_root == nullptr) {
        m_handle = {};
        m_root = nullptr;
        m_presenter = nullptr;
        m_accepted = nullptr;
        return;
    }
    // the host-built cards borrow the stage engines
    albor::WindowStage* stage = m_app->stage(m_handle);
    // a stage that is not wired cannot host the cards
    if (stage == nullptr) {
        (void)m_app->request_close(m_handle);
        m_handle = {};
        m_root = nullptr;
        m_presenter = nullptr;
        m_accepted = nullptr;
        return;
    }
    // remember the engines the cards read
    m_engine = &stage->binding_engine();
    m_text = &stage->text_engine();
    // mount the column the cards fill
    m_cards = dynamic_cast<albor::ColumnLayout*>(find_node(*m_root, "options_cards"));
    // build the cards before the theme walks the tree
    populate();
    // theme the dialog from the same system appearance as the shell
    m_theme = install_theme(*m_app, m_handle);
    // the footer hands the edits back or drops them
    m_root->set_on_accepted([this] { accept(); });
    m_root->set_on_dismissed([this] { dismiss(); });
    // show the fully seeded window, which avoids a blank first frame
    if (albor::Window* window = m_app->window(m_handle))
        window->show();
    // the first editor takes the caret once the first frame has arranged the page
    m_app->post([this, alive = m_alive] {
        // a dialog that closed or died before the task ran has nothing to focus
        if (!*alive || !m_handle.valid() || m_first_editor == nullptr)
            return;
        // the field is the first value the user can edit
        if (albor::WindowStage* posted_stage = m_app->stage(m_handle))
            posted_stage->focus().request_focus(m_first_editor);
    });
}

void studio::AlborOptionsDialogView::populate() {
    // a host that failed to mount cannot show a card
    if (m_cards == nullptr || m_presenter == nullptr)
        return;
    // drop the cards a previous show built
    while (m_cards->child_count() > 0) {
        // the column owns the card until the removal hands it back
        std::unique_ptr<albor::UiNode> removed = m_cards->remove_child(m_cards->child_at(m_cards->child_count() - 1));
        // the removed card releases here
        (void)removed;
    }
    // one card per package the presenter shows
    for (const OptionPackage package : m_presenter->packages())
        m_cards->add_child(build_card(package));
}

std::unique_ptr<albor::UiNode> studio::AlborOptionsDialogView::build_card(const OptionPackage package) {
    // the card frames one .OPTIONS package
    const std::string name(option_editor::package_name(package));
    auto card = std::make_unique<albor::Panel>(m_engine, "options_card_" + name);
    card->set_padding(CARD_PADDING);
    card->set_spacing(CARD_SPACING);
    // the header names the statement the card edits
    auto header = std::make_unique<albor::Label>(m_engine, m_text, ".OPTIONS " + name + " package");
    header->set_font_size(13.0f);
    header->set_font_weight(albor::FontWeight::Semibold);
    card->add_child(std::move(header));
    // the rows the presenter seeded from the netlist
    const std::vector<option_editor::OptionRow>& rows = m_presenter->rows(package);
    for (std::size_t index = 0; index < rows.size(); ++index) {
        // the row pairs the key with its editor
        auto row = std::make_unique<albor::RowLayout>();
        row->set_gap(ROW_GAP);
        row->set_horizontal_align(albor::Align::Stretch);
        // the key cell keeps the column width so the editors line up
        auto key_cell = std::make_unique<albor::SizedBoxLayout>();
        key_cell->set_width(KEY_COLUMN_WIDTH);
        key_cell->set_height(ROW_HEIGHT);
        // the label names the key the editor edits
        auto key = std::make_unique<albor::Label>(m_engine, m_text, rows[index].key);
        key->set_font_size(13.0f);
        key->set_vertical_align(albor::Align::Center);
        key_cell->add_child(std::move(key));
        row->add_child(std::move(key_cell));
        // the editor the row shows
        const std::string id = "options_row_" + name + "_" + rows[index].key;
        // a documented choice row opens the choice list
        if (rows[index].choice_index >= 0) {
            // the items the trigger shows, kept for the commit
            const std::vector<std::string> items = rows[index].choices;
            auto combo = std::make_unique<albor::ComboBox>(m_engine, m_text, id);
            // the first editor of the page takes the caret
            if (m_first_editor == nullptr)
                m_first_editor = combo.get();
            combo->set_items(items);
            combo->set_selected(rows[index].choice_index);
            combo->set_label(rows[index].key);
            combo->set_horizontal_align(albor::Align::Stretch);
            // the trigger sits on the row's centre line
            combo->set_vertical_align(albor::Align::Center);
            // a selection rewrites the row it names
            combo->set_on_selection_changed([this, package, index, items](const int selected) {
                // the entry before the choices clears the option
                if (selected <= 0) {
                    commit(package, index, {}, false);
                    return;
                }
                // a selection past the list names nothing
                if (static_cast<std::size_t>(selected) >= items.size())
                    return;
                // the marker entry keeps a bare flag, which writes the key without a value
                if (items[static_cast<std::size_t>(selected)] == option_editor::FLAG_CHOICE) {
                    commit(package, index, {}, true);
                    return;
                }
                // a documented choice writes its value
                commit(package, index, items[static_cast<std::size_t>(selected)], false);
            });
            row->add_child(std::move(combo));
        }
        // every other row edits free text and hints at the reference default
        else {
            auto field = std::make_unique<albor::TextField>(m_engine, m_text, rows[index].value, id);
            // the first editor of the page takes the caret
            if (m_first_editor == nullptr)
                m_first_editor = field.get();
            field->set_placeholder(rows[index].default_value);
            field->set_horizontal_align(albor::Align::Stretch);
            // the field sits on the row's centre line
            field->set_vertical_align(albor::Align::Center);
            // a typed value rewrites the row it names
            field->set_on_change([this, package, index](const std::string& text) { commit(package, index, text, false); });
            row->add_child(std::move(field));
        }
        // the row joins the card
        card->add_child(std::move(row));
    }
    // hand the card to the column
    return card;
}

void studio::AlborOptionsDialogView::commit(const OptionPackage package, const std::size_t row, std::string value, const bool flag) {
    // a dialog without a presenter has no row to rewrite
    if (m_presenter == nullptr)
        return;
    // the presenter owns the row state the accept folds back
    (void)m_presenter->edit(package, row, std::move(value), flag);
}

void studio::AlborOptionsDialogView::accept() {
    // the options the edited rows declare
    OptionParameters options = OptionParameters::from_xyce_directives({});
    // a dialog without a presenter has nothing to apply
    if (m_presenter != nullptr)
        options = m_presenter->result();
    // the sink belongs to this show
    AcceptedHandler accepted = std::move(m_accepted);
    // the dialog leaves the screen before the result reaches the shell
    close();
    // deliver the options once the dialog is gone
    if (accepted)
        accepted(std::move(options));
}

void studio::AlborOptionsDialogView::dismiss() { close(); }

void studio::AlborOptionsDialogView::close() {
    // a dialog that is already closed has nothing to release
    if (!m_handle.valid())
        return;
    // ask the application to close the window
    (void)m_app->request_close(m_handle);
    // the borrowed facade dies with the window
    m_handle = {};
    m_root = nullptr;
    m_cards = nullptr;
    m_first_editor = nullptr;
    m_presenter = nullptr;
    m_accepted = nullptr;
    // the theme bindings and the engines belong to the closed window
    m_theme.reset();
    m_engine = nullptr;
    m_text = nullptr;
}

bool studio::AlborOptionsDialogView::is_open() const { return m_handle.valid(); }

void studio::AlborOptionsDialogView::on_window_closed(const albor::WindowHandle handle) {
    // a window the dialog did not open is not its concern
    if (!m_handle.valid() || !(handle == m_handle))
        return;
    // the facade and the cards die with the window
    m_handle = {};
    m_root = nullptr;
    m_cards = nullptr;
    m_first_editor = nullptr;
    m_presenter = nullptr;
    m_accepted = nullptr;
    // the theme bindings and the engines belong to the closed window
    m_theme.reset();
    m_engine = nullptr;
    m_text = nullptr;
}

#include "views/charts/step_tool_dialog_view.h"

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
#include "albor/style/theme.h"
#include "albor/ui/ui_node.h"
#include "albor/widgets/checkbox.h"
#include "albor/widgets/label.h"
#include "albor/widgets/theme_controller.h"
#include "albor/window/window.h"
#include "app/theme.h"
#include "step_tool_dialog.h"

namespace
{
    // the size the dialog window opens with
    constexpr float DIALOG_WIDTH = 560.0f;

    constexpr float DIALOG_HEIGHT = 460.0f;

    // the width of the checkbox column
    constexpr float CHECK_COLUMN_WIDTH = 24.0f;

    // the width of the step index column
    constexpr float INDEX_COLUMN_WIDTH = 60.0f;

    // the height of one row
    constexpr float ROW_HEIGHT = 20.0f;

    // the gap between two cells and between two rows
    constexpr float CELL_GAP = 6.0f;

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

studio::AlborStepToolDialogView::AlborStepToolDialogView(albor::Application& application, albor::WindowHandle parent) :
    m_app(&application), m_parent(parent) {}

studio::AlborStepToolDialogView::~AlborStepToolDialogView() {
    // a dialog still up at teardown asks the application to release it
    if (m_handle.valid())
        (void)m_app->request_close(m_handle);
}

void studio::AlborStepToolDialogView::show(StepToolState state, AcceptedHandler accepted) {
    // a second show while the dialog is up is ignored
    if (is_open())
        return;
    // remember the state and the sink the dialog works from
    m_state = std::move(state);
    m_accepted = std::move(accepted);
    // describe a fixed size modal dialog owned by the shell
    albor::WindowOptions options;
    options.title = "Step Tool";
    options.size = albor::Size{.width = DIALOG_WIDTH, .height = DIALOG_HEIGHT};
    options.role = albor::WindowRole::Dialog;
    options.modality = albor::Modality::WindowModal;
    options.parent = m_parent;
    options.resizable = false;
    options.visible = false;
    // build the compiled dialog tree in a fresh window
    m_handle = m_app->create_window(options, m_root);
    // a refused window leaves the caller without a dialog
    if (!m_handle.valid() || m_root == nullptr) {
        m_handle = {};
        m_root = nullptr;
        m_accepted = nullptr;
        return;
    }
    // the host-built rows borrow the stage engines
    albor::WindowStage* stage = m_app->stage(m_handle);
    // a stage that is not wired cannot host the rows
    if (stage == nullptr) {
        (void)m_app->request_close(m_handle);
        return;
    }
    // remember the engines the rows read
    m_engine = &stage->binding_engine();
    m_text = &stage->text_engine();
    // mount the column the header and rows fill
    albor::UiNode* rows_host = find_node(*m_root, "step_tool_rows");
    // the typed pin keeps the host as the column layout it is
    m_rows = dynamic_cast<albor::ColumnLayout*>(rows_host);
    // the footer carries the selected count
    albor::UiNode* count_host = find_node(*m_root, "step_tool_count");
    // the typed pin keeps the readout as the label it is
    m_count = dynamic_cast<albor::Label*>(count_host);
    // build the header and the rows before the theme walks the tree
    populate();
    // theme the dialog from the same system appearance as the shell
    m_theme = install_theme(*m_app, m_handle);
    // the bulk actions rewrite the selection in place
    m_root->set_on_select_all([this] { select_all(true); });
    // clear every step
    m_root->set_on_clear_all([this] { select_all(false); });
    // flip every step
    m_root->set_on_invert_selection([this] { invert_selection(); });
    // the footer commits the selection
    m_root->set_on_accepted([this] { accept(); });
    // the footer dismisses the dialog
    m_root->set_on_dismissed([this] { dismiss(); });
    // the count starts from the chart's current selection
    update_count();
    // show the fully seeded window, which avoids a blank first frame
    if (albor::Window* window = m_app->window(m_handle))
        window->show();
    // the first checkbox takes the focus once the first frame has arranged the rows
    m_app->post([this] {
        // a dialog that closed before the task ran has nothing to focus
        if (!m_handle.valid() || m_boxes.empty() || m_boxes.front() == nullptr)
            return;
        // the first row is the first choice the user can make
        if (albor::WindowStage* posted_stage = m_app->stage(m_handle))
            posted_stage->focus().request_focus(m_boxes.front());
    });
}

void studio::AlborStepToolDialogView::populate() {
    // a host that failed to mount cannot show a row
    if (m_rows == nullptr || m_engine == nullptr || m_text == nullptr)
        return;
    // drop the rows a previous show built
    while (m_rows->child_count() > 0) {
        // the column owns the row until the removal hands it back
        std::unique_ptr<albor::UiNode> removed = m_rows->remove_child(m_rows->child_at(m_rows->child_count() - 1));
        // the removed row releases here
        (void)removed;
    }
    // the checkboxes of the rebuilt rows
    m_boxes.clear();
    // the header row names the checkbox, index and value columns
    auto header = std::make_unique<albor::RowLayout>();
    // the cells keep a small gap
    header->set_gap(CELL_GAP);
    // the header spans the dialog width
    header->set_horizontal_align(albor::Align::Stretch);
    // the checkbox column carries no header text
    auto spacer = std::make_unique<albor::SizedBoxLayout>();
    // the spacer keeps the checkbox column width
    spacer->set_width(CHECK_COLUMN_WIDTH);
    // the row height keeps the header aligned with the data rows
    spacer->set_height(ROW_HEIGHT);
    // the spacer joins the header
    header->add_child(std::move(spacer));
    // the index column names the step
    add_label_cell(*header, INDEX_COLUMN_WIDTH, "Step", 12.0f, albor::FontWeight::Semibold);
    // every sweep parameter names its column
    for (const std::string& key : m_state.keys)
        add_label_cell(*header, 0.0f, key, 12.0f, albor::FontWeight::Semibold);
    // the header joins the column
    m_rows->add_child(std::move(header));
    // one checkbox, index and value row per step
    for (std::size_t index = 0; index < m_state.rows.size(); ++index) {
        // the row starts empty
        auto row = std::make_unique<albor::RowLayout>();
        // the cells keep a small gap
        row->set_gap(CELL_GAP);
        // the row spans the dialog width
        row->set_horizontal_align(albor::Align::Stretch);
        // the checkbox cell keeps the checkbox column width
        auto cell = std::make_unique<albor::SizedBoxLayout>();
        // the cell keeps the checkbox column width
        cell->set_width(CHECK_COLUMN_WIDTH);
        // the cell keeps the row height
        cell->set_height(ROW_HEIGHT);
        // the checkbox toggles this step
        auto box = std::make_unique<albor::CheckBox>(m_engine, "step_tool_check_" + std::to_string(index));
        // the row keeps its own selected state
        box->set_checked(m_state.rows[index].selected);
        // a toggle rewrites the row and the count
        box->set_on_toggle([this, index](const bool checked) {
            // keep the row in step with the box
            m_state.rows[index].selected = checked;
            // the count follows the selection
            update_count();
        });
        // keep the box so the bulk actions reach it
        m_boxes.push_back(box.get());
        // the cell owns the box
        cell->add_child(std::move(box));
        // the cell joins the row
        row->add_child(std::move(cell));
        // the index cell names the step
        add_label_cell(*row, INDEX_COLUMN_WIDTH, std::to_string(m_state.rows[index].index), 13.0f, albor::FontWeight::Regular);
        // one value cell per key
        for (std::size_t value = 0; value < m_state.keys.size(); ++value) {
            // a dataset without a value for the key shows a dash
            const std::string text = value < m_state.rows[index].values.size() ? m_state.rows[index].values[value] : std::string("-");
            // append the value cell
            add_label_cell(*row, 0.0f, text, 13.0f, albor::FontWeight::Regular);
        }
        // the row joins the column
        m_rows->add_child(std::move(row));
    }
}

void studio::AlborStepToolDialogView::add_label_cell(albor::RowLayout& row, const float width, std::string text, const float font_size, const albor::FontWeight weight) {
    // a host that failed to mount cannot hold a cell
    if (m_engine == nullptr || m_text == nullptr)
        return;
    // the cell is a plain label
    auto label = std::make_unique<albor::Label>(m_engine, m_text, std::move(text));
    // the header and the values differ only in the stroke and size
    label->set_font_size(font_size);
    // the header is emphasized
    label->set_font_weight(weight);
    // a zero width means the cell stretches to its share of the row
    if (width <= 0.0f) {
        // the value column takes its share of the leftover
        label->set_horizontal_align(albor::Align::Stretch);
        // the label joins the row
        row.add_child(std::move(label));
        // the stretching cell is complete
        return;
    }
    // a fixed width cell keeps the column aligned across rows
    auto cell = std::make_unique<albor::SizedBoxLayout>();
    // the cell keeps the column width
    cell->set_width(width);
    // the cell keeps the row height
    cell->set_height(ROW_HEIGHT);
    // the cell owns the label
    cell->add_child(std::move(label));
    // the cell joins the row
    row.add_child(std::move(cell));
}

void studio::AlborStepToolDialogView::select_all(const bool selected) {
    // every row takes the requested state
    for (std::size_t index = 0; index < m_state.rows.size(); ++index) {
        // the selection lives in the row state
        m_state.rows[index].selected = selected;
        // the box shows the row state
        if (index < m_boxes.size() && m_boxes[index] != nullptr)
            m_boxes[index]->set_checked(selected);
    }
    // the count follows the selection
    update_count();
}

void studio::AlborStepToolDialogView::invert_selection() {
    // every row flips its state
    for (std::size_t index = 0; index < m_state.rows.size(); ++index) {
        // flip the row state
        m_state.rows[index].selected = !m_state.rows[index].selected;
        // the box shows the row state
        if (index < m_boxes.size() && m_boxes[index] != nullptr)
            m_boxes[index]->set_checked(m_state.rows[index].selected);
    }
    // the count follows the selection
    update_count();
}

void studio::AlborStepToolDialogView::update_count() {
    // a dialog without the readout shows no count
    if (m_count == nullptr)
        return;
    // count the selected rows
    std::size_t selected = 0;
    for (const StepToolRow& row : m_state.rows)
        selected += row.selected ? 1 : 0;
    // the readout names the selection against the whole set
    m_count->set_text("Selected " + std::to_string(selected) + " / " + std::to_string(m_state.rows.size()));
}

void studio::AlborStepToolDialogView::accept() {
    // the step indexes the chart keeps
    std::vector<std::size_t> steps;
    // every checked row contributes its index
    for (const StepToolRow& row : m_state.rows) {
        // an unchecked row is not plotted
        if (!row.selected)
            continue;
        // keep the step
        steps.push_back(row.index);
    }
    // the sink owns the selection once it is handed back
    if (m_accepted)
        m_accepted(std::move(steps));
    // the dialog is done
    close();
}

void studio::AlborStepToolDialogView::dismiss() { close(); }

void studio::AlborStepToolDialogView::close() {
    // a dialog that is already closed has nothing to release
    if (!m_handle.valid())
        return;
    // ask the application to close the window
    (void)m_app->request_close(m_handle);
    // the borrowed facade dies with the window
    m_handle = {};
    m_root = nullptr;
    m_rows = nullptr;
    m_count = nullptr;
    m_boxes.clear();
    m_accepted = nullptr;
}

bool studio::AlborStepToolDialogView::is_open() const { return m_handle.valid(); }

void studio::AlborStepToolDialogView::on_window_closed(const albor::WindowHandle handle) {
    // a window the dialog did not open is not its concern
    if (!m_handle.valid() || handle != m_handle)
        return;
    // the facade and the rows die with the window
    m_handle = {};
    m_root = nullptr;
    m_rows = nullptr;
    m_count = nullptr;
    m_boxes.clear();
    m_accepted = nullptr;
}

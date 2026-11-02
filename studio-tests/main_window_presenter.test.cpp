#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

#include "presenters/main_window_presenter.h"
#include "views/main_window/main_window_view_def.h"

namespace
{
    // the view double records what the presenter asks the surface to do
    class FakeView final : public studio::MainWindowViewDef
    {
    public:
        void set_event_handler(studio::MainWindowViewEvents*) override {}

        void set_netlist_text(std::string_view content) override {
            editor_text = std::string(content);
            seeded_texts.emplace_back(editor_text);
        }

        [[nodiscard]] std::string netlist_text() const override { return editor_text; }

        void set_netlist_read_only(bool read_only) override {
            read_only_count++;
            read_only_last = read_only;
        }

        void set_status_text(std::string_view text) override { statuses.emplace_back(text); }

        void set_title(std::string_view title) override { titles.emplace_back(title); }

        void set_open_enabled(bool enabled) override { open_enabled = enabled; }

        void set_save_enabled(bool enabled) override { save_enabled = enabled; }

        [[nodiscard]] std::optional<std::filesystem::path> request_netlist_save_path() override {
            save_as_count++;
            // the scripted choice the test made, which an absent value reads as a cancelled panel
            return save_as_path;
        }

        // the text the editor currently edits
        std::string editor_text;

        // every seeded content the presenter pushed
        std::vector<std::string> seeded_texts;

        // every title the presenter rendered
        std::vector<std::string> titles;

        // every status the presenter reported
        std::vector<std::string> statuses;

        // the read only state the presenter last applied and how often it was set
        int read_only_count = 0;
        bool read_only_last = false;

        // the enablement the action states last applied
        bool open_enabled = false;
        bool save_enabled = false;

        // the save-as flow the fake answers
        std::optional<std::filesystem::path> save_as_path;
        int save_as_count = 0;
    };

    // point the filesystem at a clean root for one test and remove it afterwards
    class ScratchDirectory
    {
    public:
        explicit ScratchDirectory(const std::filesystem::path& root) :
            m_root(root) {
            // start from a clean root, so a previous failed run cannot seed this one
            std::error_code ec;
            std::filesystem::remove_all(m_root, ec);
            std::filesystem::create_directories(m_root, ec);
        }

        ScratchDirectory(const ScratchDirectory&) = delete;

        ScratchDirectory& operator=(const ScratchDirectory&) = delete;

        ~ScratchDirectory() {
            // the root holds every file the test created
            std::error_code ec;
            std::filesystem::remove_all(m_root, ec);
        }

        // hand out the root the test writes into
        [[nodiscard]] const std::filesystem::path& path() const { return m_root; }

    private:
        // the root every artifact of the test lives under
        std::filesystem::path m_root;
    };
} // namespace

TEST(StudioMainWindowPresenterChecks, startup_is_untitled_clean_and_save_disabled) {
    // arrange
    FakeView view;
    // act
    studio::MainWindowPresenter presenter(view);
    // assert: the toolbar starts in the untitled state and no title was rendered
    EXPECT_TRUE(view.open_enabled);
    EXPECT_FALSE(view.save_enabled);
    EXPECT_TRUE(view.titles.empty());
    EXPECT_EQ(view.read_only_count, 0);
}

TEST(StudioMainWindowPresenterChecks, open_loads_the_netlist_and_starts_clean) {
    // arrange: a netlist file with known content
    const ScratchDirectory scratch(std::filesystem::temp_directory_path() / "xyce-studio-tests" / "mw-open");
    const std::filesystem::path netlist = scratch.path() / "tran.cir";
    {
        std::ofstream file(netlist);
        file << "* a simple tran netlist\nV1 in 0 1\n.END\n";
    }
    FakeView view;
    studio::MainWindowPresenter presenter(view);
    // act
    presenter.on_open_file(netlist);
    // assert: the file content is in the editor, the title carries the name and the state is clean
    EXPECT_EQ(view.editor_text, "* a simple tran netlist\nV1 in 0 1\n.END\n");
    EXPECT_EQ(view.titles.back(), "tran.cir");
    EXPECT_EQ(view.read_only_count, 1);
    EXPECT_FALSE(view.read_only_last);
    EXPECT_FALSE(view.save_enabled);
}

TEST(StudioMainWindowPresenterChecks, open_ignores_non_netlist_extensions) {
    // arrange
    const ScratchDirectory scratch(std::filesystem::temp_directory_path() / "xyce-studio-tests" / "mw-ignore");
    FakeView view;
    studio::MainWindowPresenter presenter(view);
    // act
    presenter.on_open_file(scratch.path() / "run.raw");
    // assert: nothing was loaded and no title was rendered
    EXPECT_TRUE(view.seeded_texts.empty());
    EXPECT_TRUE(view.titles.empty());
    EXPECT_EQ(view.read_only_count, 0);
}

TEST(StudioMainWindowPresenterChecks, open_missing_file_shows_empty_editor) {
    // arrange: a path no file backs
    const ScratchDirectory scratch(std::filesystem::temp_directory_path() / "xyce-studio-tests" / "mw-missing");
    FakeView view;
    studio::MainWindowPresenter presenter(view);
    // act
    presenter.on_open_file(scratch.path() / "absent.cir");
    // assert: the load failed, so the editor holds nothing and save stays unreachable
    EXPECT_EQ(view.editor_text, "");
    EXPECT_EQ(view.titles.back(), "absent.cir");
    EXPECT_FALSE(view.save_enabled);
}

TEST(StudioMainWindowPresenterChecks, edit_marks_dirty_and_enables_save) {
    // arrange: a loaded netlist
    const ScratchDirectory scratch(std::filesystem::temp_directory_path() / "xyce-studio-tests" / "mw-edit");
    const std::filesystem::path netlist = scratch.path() / "tran.cir";
    {
        std::ofstream file(netlist);
        file << ".END\n";
    }
    FakeView view;
    studio::MainWindowPresenter presenter(view);
    presenter.on_open_file(netlist);
    // act: the user types into the editor
    view.editor_text = ".END\n* typed\n";
    presenter.on_netlist_edited();
    // assert: the title carries the marker and save is available
    EXPECT_EQ(view.titles.back(), "* tran.cir");
    EXPECT_TRUE(view.save_enabled);
}

TEST(StudioMainWindowPresenterChecks, save_writes_the_backing_file_and_clears_dirty) {
    // arrange: a loaded netlist the user edited
    const ScratchDirectory scratch(std::filesystem::temp_directory_path() / "xyce-studio-tests" / "mw-save");
    const std::filesystem::path netlist = scratch.path() / "tran.cir";
    {
        std::ofstream file(netlist);
        file << ".END\n";
    }
    FakeView view;
    studio::MainWindowPresenter presenter(view);
    presenter.on_open_file(netlist);
    view.editor_text = ".END\n* saved line\n";
    presenter.on_netlist_edited();
    // act
    presenter.on_save();
    // assert: the file received the edit, the marker is gone and save is disabled again
    std::ifstream file(netlist);
    const std::string saved((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    EXPECT_EQ(saved, ".END\n* saved line\n");
    EXPECT_EQ(view.titles.back(), "tran.cir");
    EXPECT_FALSE(view.save_enabled);
    EXPECT_EQ(view.save_as_count, 0);
}

TEST(StudioMainWindowPresenterChecks, untitled_save_runs_save_as_and_renames_the_title) {
    // arrange: an untitled netlist the user typed into
    const ScratchDirectory scratch(std::filesystem::temp_directory_path() / "xyce-studio-tests" / "mw-save-as");
    FakeView view;
    studio::MainWindowPresenter presenter(view);
    view.editor_text = "* typed from scratch\n.END\n";
    presenter.on_netlist_edited();
    view.save_as_path = scratch.path() / "newname";
    // act
    presenter.on_save();
    // assert: the missing extension defaulted to the netlist format and the file received the text
    std::ifstream file(scratch.path() / "newname.cir");
    const std::string saved((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    EXPECT_EQ(saved, "* typed from scratch\n.END\n");
    EXPECT_EQ(view.titles.back(), "newname.cir");
    EXPECT_FALSE(view.save_enabled);
    // assert: the rebound source writes in place without asking again
    view.editor_text = "* second edit\n";
    presenter.on_netlist_edited();
    EXPECT_TRUE(view.save_enabled);
    presenter.on_save();
    EXPECT_EQ(view.save_as_count, 1);
    std::ifstream again(scratch.path() / "newname.cir");
    const std::string resaved((std::istreambuf_iterator<char>(again)), std::istreambuf_iterator<char>());
    EXPECT_EQ(resaved, "* second edit\n");
}

TEST(StudioMainWindowPresenterChecks, cancelled_save_as_keeps_the_netlist_dirty) {
    // arrange: an untitled netlist the user typed into
    const ScratchDirectory scratch(std::filesystem::temp_directory_path() / "xyce-studio-tests" / "mw-save-cancel");
    FakeView view;
    studio::MainWindowPresenter presenter(view);
    view.editor_text = "* typed from scratch\n.END\n";
    presenter.on_netlist_edited();
    view.save_as_path = std::nullopt;
    // act
    presenter.on_save();
    // assert: the netlist stays dirty and no file was written
    EXPECT_EQ(view.titles.back(), "* ");
    EXPECT_TRUE(view.save_enabled);
    EXPECT_TRUE(std::filesystem::is_empty(scratch.path()));
}

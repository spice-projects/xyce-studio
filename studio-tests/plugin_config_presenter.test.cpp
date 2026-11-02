#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

#include "config/plugin_config.h"
#include "presenters/plugin_config_presenter.h"
#include "views/plugin_config/plugin_config_dialog_view_def.h"

namespace
{
    // the view double records what the presenter asks the surface to do
    class FakeView final : public studio::PluginConfigDialogView
    {
    public:
        void show(std::string_view current_path, studio::PluginConfigDialogEvents& new_events) override {
            shown_path = std::string(current_path);
            events = &new_events;
            show_count++;
            // a failed surface reports the close through the same seam the real view uses
            if (fail_show)
                events->on_dialog_closed();
        }

        void close() override { close_count++; }

        void show_error(std::string_view message) override { errors.emplace_back(message); }

        // the value the presenter seeded into the view
        std::string shown_path;

        // the presenter that receives the simulated user actions
        studio::PluginConfigDialogEvents* events = nullptr;

        // how many times each surface action ran
        int show_count = 0;
        int close_count = 0;

        // whether the next show fails to create a surface
        bool fail_show = false;

        // every validation message the presenter produced
        std::vector<std::string> errors;
    };

    // point the configuration at a temporary root for one test and restore the previous value afterwards
    class ConfigHome
    {
    public:
        explicit ConfigHome(const std::filesystem::path& config_root) :
            m_test_root(config_root.parent_path()) {
            // start from a clean root, so a previous failed run cannot seed this one
            std::error_code ec;
            std::filesystem::remove_all(m_test_root, ec);
            std::filesystem::create_directories(config_root, ec);
#if defined(_WIN32)
            // windows stores the per user configuration under APPDATA
            const char* previous = std::getenv("APPDATA");
            if (previous != nullptr) {
                m_previous = previous;
                m_had_previous = true;
            }
            _putenv_s("APPDATA", config_root.string().c_str());
#else
            // the portable tests use XDG_CONFIG_HOME on every other platform
            const char* previous = std::getenv("XDG_CONFIG_HOME");
            if (previous != nullptr) {
                m_previous = previous;
                m_had_previous = true;
            }
            setenv("XDG_CONFIG_HOME", config_root.string().c_str(), 1);
#endif
        }

        ConfigHome(const ConfigHome&) = delete;

        ConfigHome& operator=(const ConfigHome&) = delete;

        ~ConfigHome() {
#if defined(_WIN32)
            _putenv_s("APPDATA", m_had_previous ? m_previous.c_str() : "");
#else
            if (m_had_previous)
                setenv("XDG_CONFIG_HOME", m_previous.c_str(), 1);
            else
                unsetenv("XDG_CONFIG_HOME");
#endif
            // the test root holds the executable and the configuration file
            std::error_code ec;
            std::filesystem::remove_all(m_test_root, ec);
        }

    private:
        // the root every artifact of the test lives under
        std::filesystem::path m_test_root;

        // the environment value the test replaced
        std::string m_previous;

        // whether there was an environment value to restore
        bool m_had_previous = false;
    };
} // namespace

TEST(StudioPluginConfigPresenterChecks, accept_saves_the_trimmed_path_and_closes) {
    // arrange: a temporary config root and a regular executable file
    const std::filesystem::path test_root = std::filesystem::temp_directory_path() / "xyce-studio-tests" / "accept-saves";
    ConfigHome config_home(test_root / "config");
    const std::filesystem::path executable = test_root / "Xyce";
    {
        std::ofstream file(executable);
        file << "#!/bin/sh\n";
    }
#if !defined(_WIN32)
    std::error_code permissions_error;
    std::filesystem::permissions(executable, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write | std::filesystem::perms::owner_exec, std::filesystem::perm_options::replace, permissions_error);
#endif
    FakeView view;
    studio::PluginConfigPresenter presenter;
    bool saved = false;
    PluginConfig saved_config;
    // act: open the dialog with an empty path
    presenter.show(view, PluginConfig(""), [&saved, &saved_config](const PluginConfig& config) {
        saved = true;
        saved_config = config;
    });
    // assert: the view received the current path and no action ran yet
    ASSERT_EQ(view.show_count, 1);
    ASSERT_NE(view.events, nullptr);
    EXPECT_EQ(view.shown_path, "");
    EXPECT_EQ(view.close_count, 0);
    // act: the field carries surrounding whitespace
    view.events->on_accept("  " + executable.string() + "  \n");
    // assert: the trimmed path is saved, the callback runs and the view closes
    EXPECT_EQ(view.close_count, 1);
    EXPECT_TRUE(saved);
    EXPECT_EQ(saved_config.xyce_executable_path(), executable.string());
    EXPECT_TRUE(view.errors.empty());
    EXPECT_TRUE(std::filesystem::is_regular_file(test_root / "config" / "xyce-studio" / "config.json"));
    EXPECT_EQ(PluginConfig::load().xyce_executable_path(), executable.string());
}

TEST(StudioPluginConfigPresenterChecks, show_is_ignored_while_the_dialog_is_open) {
    // arrange: two open requests and two callbacks
    FakeView view;
    studio::PluginConfigPresenter presenter;
    int first_calls = 0;
    int second_calls = 0;
    // act: the second request arrives while the first dialog is still open
    presenter.show(view, PluginConfig("/first"), [&first_calls](const PluginConfig&) { first_calls++; });
    presenter.show(view, PluginConfig("/second"), [&second_calls](const PluginConfig&) { second_calls++; });
    // assert: only the first request reached the view and kept its callback
    EXPECT_EQ(view.show_count, 1);
    EXPECT_EQ(view.shown_path, "/first");
    ASSERT_NE(view.events, nullptr);
    // act: cancel the open dialog
    view.events->on_cancel();
    // assert: the dialog closed and the ignored callback never ran
    EXPECT_EQ(view.close_count, 1);
    EXPECT_EQ(first_calls, 0);
    EXPECT_EQ(second_calls, 0);
}

TEST(StudioPluginConfigPresenterChecks, empty_path_shows_required_error) {
    // arrange: an open dialog with no callback
    FakeView view;
    studio::PluginConfigPresenter presenter;
    presenter.show(view, PluginConfig(""), {});
    ASSERT_NE(view.events, nullptr);
    // act: the field only carries whitespace
    view.events->on_accept(" \t\r\n ");
    // assert: the presenter reports the required message and keeps the dialog open
    ASSERT_EQ(view.errors.size(), 1);
    EXPECT_EQ(view.errors[0], "Xyce executable path is required");
    EXPECT_EQ(view.close_count, 0);
}

TEST(StudioPluginConfigPresenterChecks, non_executable_path_shows_invalid_error) {
    // arrange: a path that is guaranteed not to exist
    const std::filesystem::path missing = std::filesystem::temp_directory_path() / "xyce-studio-tests" / "missing-xyce" / "Xyce";
    std::error_code ec;
    std::filesystem::remove_all(missing.parent_path(), ec);
    FakeView view;
    studio::PluginConfigPresenter presenter;
    presenter.show(view, PluginConfig(""), {});
    ASSERT_NE(view.events, nullptr);
    // act: accept a path that is not an executable file
    view.events->on_accept(missing.string());
    // assert: the presenter reports the invalid message and keeps the dialog open
    ASSERT_EQ(view.errors.size(), 1);
    EXPECT_EQ(view.errors[0], "Selected path is not an executable file");
    EXPECT_EQ(view.close_count, 0);
}

TEST(StudioPluginConfigPresenterChecks, cancel_closes_without_saving) {
    // arrange: a temporary config root and an open dialog
    const std::filesystem::path test_root = std::filesystem::temp_directory_path() / "xyce-studio-tests" / "cancel";
    ConfigHome config_home(test_root / "config");
    FakeView view;
    studio::PluginConfigPresenter presenter;
    int saved_calls = 0;
    presenter.show(view, PluginConfig("/configured/path"), [&saved_calls](const PluginConfig&) { saved_calls++; });
    ASSERT_NE(view.events, nullptr);
    // act: cancel the dialog
    view.events->on_cancel();
    // assert: the view closed, no callback ran and no configuration file was written
    EXPECT_EQ(view.close_count, 1);
    EXPECT_EQ(saved_calls, 0);
    EXPECT_FALSE(std::filesystem::exists(test_root / "config" / "xyce-studio" / "config.json"));
}

TEST(StudioPluginConfigPresenterChecks, stale_accept_cancel_and_close_are_ignored) {
    // arrange: a presenter with no open dialog
    FakeView view;
    studio::PluginConfigPresenter presenter;
    // act: deliver every stale notification a late window may raise
    presenter.on_dialog_closed();
    presenter.on_accept("anything");
    presenter.on_cancel();
    // assert: no surface action ran and no validation message was produced
    EXPECT_EQ(view.show_count, 0);
    EXPECT_EQ(view.close_count, 0);
    EXPECT_TRUE(view.errors.empty());
}

TEST(StudioPluginConfigPresenterChecks, dialog_closed_allows_a_new_show) {
    // arrange: an open dialog with a callback
    FakeView view;
    studio::PluginConfigPresenter presenter;
    int first_calls = 0;
    int second_calls = 0;
    presenter.show(view, PluginConfig("/first"), [&first_calls](const PluginConfig&) { first_calls++; });
    ASSERT_NE(view.events, nullptr);
    // act: the window disappears without accept or cancel
    view.events->on_dialog_closed();
    // act: a new dialog opens with a new callback
    presenter.show(view, PluginConfig("/second"), [&second_calls](const PluginConfig&) { second_calls++; });
    // assert: the second request reached the view and replaced the seeded path
    EXPECT_EQ(view.show_count, 2);
    EXPECT_EQ(view.shown_path, "/second");
    ASSERT_NE(view.events, nullptr);
    // act: cancel the second dialog
    view.events->on_cancel();
    // assert: only the second dialog closed and neither callback ran
    EXPECT_EQ(view.close_count, 1);
    EXPECT_EQ(first_calls, 0);
    EXPECT_EQ(second_calls, 0);
}

TEST(StudioPluginConfigPresenterChecks, accept_without_callback_still_closes) {
    // arrange: a temporary config root and an executable
    const std::filesystem::path test_root = std::filesystem::temp_directory_path() / "xyce-studio-tests" / "accept-without-callback";
    ConfigHome config_home(test_root / "config");
    const std::filesystem::path executable = test_root / "Xyce";
    {
        std::ofstream file(executable);
        file << "#!/bin/sh\n";
    }
#if !defined(_WIN32)
    std::error_code permissions_error;
    std::filesystem::permissions(executable, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write | std::filesystem::perms::owner_exec, std::filesystem::perm_options::replace, permissions_error);
#endif
    FakeView view;
    studio::PluginConfigPresenter presenter;
    // act: open without a result callback and accept a valid path
    presenter.show(view, PluginConfig(""), {});
    ASSERT_NE(view.events, nullptr);
    view.events->on_accept(executable.string());
    // assert: the dialog still saves and closes even when nobody listens for the result
    EXPECT_EQ(view.close_count, 1);
    EXPECT_TRUE(view.errors.empty());
    EXPECT_TRUE(std::filesystem::exists(test_root / "config" / "xyce-studio" / "config.json"));
}

TEST(StudioPluginConfigPresenterChecks, failed_show_resets_the_presenter) {
    // arrange: a view whose surface creation fails and reports the close synchronously
    FakeView view;
    view.fail_show = true;
    studio::PluginConfigPresenter presenter;
    // act: the failed show reports the close from inside show
    presenter.show(view, PluginConfig("/path"), {});
    ASSERT_EQ(view.show_count, 1);
    ASSERT_NE(view.events, nullptr);
    // act: a late accept is stale because the presenter already reset
    view.events->on_accept("anything");
    // assert: no surface action ran for the stale accept
    EXPECT_EQ(view.close_count, 0);
    EXPECT_TRUE(view.errors.empty());
    // act: the next show is allowed after the failed one
    view.fail_show = false;
    presenter.show(view, PluginConfig("/second"), {});
    // assert: the second surface opened with the new path
    EXPECT_EQ(view.show_count, 2);
    EXPECT_EQ(view.shown_path, "/second");
    ASSERT_NE(view.events, nullptr);
    // act: close the second surface
    view.events->on_cancel();
    // assert: the second surface closed
    EXPECT_EQ(view.close_count, 1);
}

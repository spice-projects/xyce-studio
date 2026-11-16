#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "albor/application/application.h"
#include "albor/application/task_queue.h"
#include "albor/window/window_handle.h"
#include "app/studio_app.h"
#include "app/window_set.h"
#include "charts/chart_engine.h"
#include "config/plugin_config.h"
#include "io/xyce_csv_file.h"
#include "io/xyce_output_file.h"

namespace
{
    // a shell double that records what the application asks of it
    class FakeShellWindow final : public studio::ShellWindow
    {
    public:
        explicit FakeShellWindow(albor::WindowHandle window = {}) :
            handle_value(window) {}

        ~FakeShellWindow() override {
            // the test may observe the release the set defers
            if (on_destroyed)
                on_destroyed();
        }

        [[nodiscard]] albor::WindowHandle handle() const override { return handle_value; }

        [[nodiscard]] bool alive() const override { return alive_value; }

        bool on_window_closed(const albor::WindowHandle closed) override {
            // every report reaches the shell, whether it owns the handle or not
            closed_handles.push_back(closed);
            // a foreign handle leaves this shell alone
            if (!(closed == handle_value))
                return false;
            // the shell lost its window
            alive_value = false;
            return true;
        }

        bool show_dataset(std::shared_ptr<XyceOutputFile> file, const ChartKind kind, std::string title) override {
            // record the dataset the shell was asked to show
            dataset = std::move(file);
            dataset_kind = kind;
            dataset_title = std::move(title);
            return accept_dataset;
        }

        void open_netlist(const std::filesystem::path& path) override { netlists.push_back(path); }

        bool show_startup_datasets(const std::vector<std::filesystem::path>& paths) override {
            // record the files the command line named
            startup = paths;
            return !paths.empty();
        }

        void set_on_new_window(std::function<void(std::shared_ptr<XyceOutputFile>, ChartKind, std::string)> handler) override { on_new_window = std::move(handler); }

        void request_close() override { close_requests++; }

        // the handle the application names this shell by
        albor::WindowHandle handle_value;

        // whether the shell still owns a live window
        bool alive_value = true;

        // whether the shell shows the dataset it is handed
        bool accept_dataset = true;

        // how many times the shell asked the application to close it
        int close_requests = 0;

        // the dataset the shell was last handed
        std::shared_ptr<XyceOutputFile> dataset;

        // the kind the handed dataset plots on
        ChartKind dataset_kind = ChartKind::XY;

        // the title the handed dataset carries
        std::string dataset_title;

        // the netlists the application opened through the shell
        std::vector<std::filesystem::path> netlists;

        // the startup datasets the application opened
        std::vector<std::filesystem::path> startup;

        // every close the application routed to this shell
        std::vector<albor::WindowHandle> closed_handles;

        // the sink the chart new window command reaches
        std::function<void(std::shared_ptr<XyceOutputFile>, ChartKind, std::string)> on_new_window;

        // the work the release performs
        std::function<void()> on_destroyed;
    };

    // the native loop double, which returns as soon as it is asked to run
    class FakeEventLoop final : public albor::PlatformEventLoop
    {
    public:
        void set_pump_handler(albor::Task pump) override { m_pump = std::move(pump); }

        void run() override { run_count++; }

        void quit() override {}

        [[nodiscard]] bool running() const override { return false; }

        void start_frame_pacing() override {}

        void stop_frame_pacing() override {}

        // the pump the application installed, which the loop never calls here
        albor::Task m_pump;

        // how many times the application ran the loop
        int run_count = 0;
    };
} // namespace

TEST(StudioWindowSetChecks, a_closed_shell_is_released_after_the_deferral_runs) {
    // arrange: a set over a deferral the test drives, holding one shell
    std::vector<std::function<void()>> deferred;
    studio::WindowSet windows([&](std::function<void()> task) { deferred.push_back(std::move(task)); });
    windows.add(std::make_unique<FakeShellWindow>(albor::WindowHandle::from_id(7)));
    ASSERT_EQ(windows.size(), 1U);
    // the shell the set keeps
    FakeShellWindow* shell = static_cast<FakeShellWindow*>(windows.at(0));
    ASSERT_NE(shell, nullptr);
    // act: the application reports the window closed
    windows.route_close(albor::WindowHandle::from_id(7));
    // assert: the shell lost its window and the release waits for the deferral
    ASSERT_EQ(shell->closed_handles.size(), 1U);
    EXPECT_TRUE(shell->closed_handles.front() == albor::WindowHandle::from_id(7));
    EXPECT_FALSE(shell->alive());
    EXPECT_EQ(windows.size(), 1U);
    ASSERT_EQ(deferred.size(), 1U);
    // act: the event loop runs the deferred release
    deferred.front()();
    // assert: the closed shell left the set
    EXPECT_EQ(windows.size(), 0U);
}

TEST(StudioWindowSetChecks, an_unknown_handle_posts_no_release) {
    // arrange: a set holding one live shell
    std::vector<std::function<void()>> deferred;
    studio::WindowSet windows([&](std::function<void()> task) { deferred.push_back(std::move(task)); });
    windows.add(std::make_unique<FakeShellWindow>(albor::WindowHandle::from_id(3)));
    FakeShellWindow* shell = static_cast<FakeShellWindow*>(windows.at(0));
    ASSERT_NE(shell, nullptr);
    // act: a window no shell owns reports its close
    windows.route_close(albor::WindowHandle::from_id(99));
    // assert: every shell saw the report but none claimed it, so nothing is released
    EXPECT_EQ(shell->closed_handles.size(), 1U);
    EXPECT_TRUE(shell->alive());
    EXPECT_EQ(windows.size(), 1U);
    EXPECT_TRUE(deferred.empty());
}

TEST(StudioWindowSetChecks, a_released_shell_leaves_the_live_shells_in_order) {
    // arrange: a set holding three shells
    std::vector<std::function<void()>> deferred;
    studio::WindowSet windows([&](std::function<void()> task) { deferred.push_back(std::move(task)); });
    windows.add(std::make_unique<FakeShellWindow>(albor::WindowHandle::from_id(1)));
    windows.add(std::make_unique<FakeShellWindow>(albor::WindowHandle::from_id(2)));
    windows.add(std::make_unique<FakeShellWindow>(albor::WindowHandle::from_id(3)));
    ASSERT_EQ(windows.size(), 3U);
    // act: the middle window closes and the deferral runs
    windows.route_close(albor::WindowHandle::from_id(2));
    ASSERT_EQ(deferred.size(), 1U);
    deferred.front()();
    // assert: the surviving shells keep the order they were built in
    ASSERT_EQ(windows.size(), 2U);
    EXPECT_TRUE(windows.at(0)->handle() == albor::WindowHandle::from_id(1));
    EXPECT_TRUE(windows.at(1)->handle() == albor::WindowHandle::from_id(3));
    EXPECT_EQ(windows.at(2), nullptr);
}

TEST(StudioWindowSetChecks, a_close_reported_during_the_release_leaves_the_set_consistent) {
    // arrange: a set whose first shell reports another close while it is destroyed
    std::vector<std::function<void()>> deferred;
    studio::WindowSet windows([&](std::function<void()> task) { deferred.push_back(std::move(task)); });
    windows.add(std::make_unique<FakeShellWindow>(albor::WindowHandle::from_id(1)));
    windows.add(std::make_unique<FakeShellWindow>(albor::WindowHandle::from_id(2)));
    ASSERT_EQ(windows.size(), 2U);
    FakeShellWindow* released = static_cast<FakeShellWindow*>(windows.at(0));
    ASSERT_NE(released, nullptr);
    released->on_destroyed = [&windows] { windows.route_close(albor::WindowHandle::from_id(2)); };
    // act: the first shell closes, and its release reports the second one closing
    windows.route_close(albor::WindowHandle::from_id(1));
    ASSERT_EQ(deferred.size(), 1U);
    deferred.front()();
    // assert: the set kept the shell the release reported, and deferred its own release
    ASSERT_EQ(windows.size(), 1U);
    EXPECT_TRUE(windows.at(0)->handle() == albor::WindowHandle::from_id(2));
    EXPECT_EQ(deferred.size(), 2U);
    // act: the second release runs
    deferred.back()();
    // assert: the last shell left the set
    EXPECT_EQ(windows.size(), 0U);
}

TEST(StudioStudioAppChecks, run_builds_the_primary_shell_and_seeds_it) {
    // arrange: a fake loop and an app whose factory records every shell
    auto event_loop = std::make_unique<FakeEventLoop>();
    FakeEventLoop* loop = event_loop.get();
    albor::Application application(std::move(event_loop));
    std::vector<FakeShellWindow*> shells;
    int factory_calls = 0;
    PluginConfig config;
    studio::StudioApp app(config, application, [&](PluginConfig&, albor::Application&) -> std::unique_ptr<studio::ShellWindow> {
        // the shell the application builds for a window
        auto shell = std::make_unique<FakeShellWindow>();
        shells.push_back(shell.get());
        factory_calls++;
        return shell;
    });
    // the files the command line named
    app.set_startup_netlist("/tmp/run.cir");
    app.set_startup_dataset("/tmp/run.csv");
    // act: run, over a loop that returns at once
    const int code = app.run();
    // assert: one shell was built, seeded and wired for the new window command
    EXPECT_EQ(code, 0);
    EXPECT_EQ(factory_calls, 1);
    ASSERT_EQ(shells.size(), 1U);
    EXPECT_EQ(shells[0]->netlists, std::vector<std::filesystem::path>{"/tmp/run.cir"});
    EXPECT_EQ(shells[0]->startup, std::vector<std::filesystem::path>{"/tmp/run.csv"});
    EXPECT_TRUE(static_cast<bool>(shells[0]->on_new_window));
    // assert: the app ran the native loop once
    EXPECT_EQ(loop->run_count, 1);
}

TEST(StudioStudioAppChecks, a_chart_new_window_request_spawns_a_seeded_shell) {
    // arrange: a parsed dataset and an app whose first shell hands it over
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "xyce-studio-tests" / "app-new-window";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    const std::filesystem::path dataset = root / "run.csv";
    {
        std::ofstream file(dataset);
        file << "TIME,V(1)\n0,0\n1,1\n";
    }
    const std::optional<std::shared_ptr<XyceOutputFile>> parsed = xyce_csv_file_parser(dataset);
    ASSERT_TRUE(parsed.has_value());
    auto event_loop = std::make_unique<FakeEventLoop>();
    albor::Application application(std::move(event_loop));
    std::vector<FakeShellWindow*> shells;
    PluginConfig config;
    studio::StudioApp app(config, application, [&](PluginConfig&, albor::Application&) -> std::unique_ptr<studio::ShellWindow> {
        // the shell the application builds for a window
        auto shell = std::make_unique<FakeShellWindow>();
        shells.push_back(shell.get());
        return shell;
    });
    ASSERT_EQ(app.run(), 0);
    ASSERT_EQ(shells.size(), 1U);
    // act: the chart command hands the dataset to the shell
    shells[0]->on_new_window(parsed.value(), ChartKind::SMITH, "mysim.cir");
    // assert: a second shell was built and seeded with the same file, kind and title
    ASSERT_EQ(shells.size(), 2U);
    EXPECT_EQ(shells[1]->dataset, parsed.value());
    EXPECT_EQ(shells[1]->dataset_kind, ChartKind::SMITH);
    EXPECT_EQ(shells[1]->dataset_title, "mysim.cir");
    // assert: the shell that asked keeps its own window
    EXPECT_TRUE(shells[0]->alive());
    EXPECT_EQ(shells[0]->close_requests, 0);
    // release the scratch file
    std::filesystem::remove_all(root, error);
}

TEST(StudioStudioAppChecks, a_refused_dataset_closes_the_spawned_shell) {
    // arrange: an app whose spawned shells refuse the dataset they are handed
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "xyce-studio-tests" / "app-refused-window";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    const std::filesystem::path dataset = root / "run.csv";
    {
        std::ofstream file(dataset);
        file << "TIME,V(1)\n0,0\n1,1\n";
    }
    const std::optional<std::shared_ptr<XyceOutputFile>> parsed = xyce_csv_file_parser(dataset);
    ASSERT_TRUE(parsed.has_value());
    auto event_loop = std::make_unique<FakeEventLoop>();
    albor::Application application(std::move(event_loop));
    std::vector<FakeShellWindow*> shells;
    PluginConfig config;
    studio::StudioApp app(config, application, [&](PluginConfig&, albor::Application&) -> std::unique_ptr<studio::ShellWindow> {
        // the shell the application builds for a window
        auto shell = std::make_unique<FakeShellWindow>();
        // the primary window owns the startup files, a spawned one must show the dataset
        shell->accept_dataset = shells.empty();
        shells.push_back(shell.get());
        return shell;
    });
    ASSERT_EQ(app.run(), 0);
    ASSERT_EQ(shells.size(), 1U);
    // act: the chart command hands the dataset to the shell
    shells[0]->on_new_window(parsed.value(), ChartKind::XY, {});
    // assert: the empty shell closes itself and the window that asked survives
    ASSERT_EQ(shells.size(), 2U);
    EXPECT_EQ(shells[1]->close_requests, 1);
    EXPECT_TRUE(shells[0]->alive());
    // release the scratch file
    std::filesystem::remove_all(root, error);
}

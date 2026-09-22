#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class KiCadSession;
class NetlistSource;
class XyceOutputFile;

// forward-declared window pair owned by the app window registry
class SlintMainWindowView;
class SlintMainWindowPresenter;

// application singleton managing the lifecycle, shared kicad session, and event loop
class App
{
public:
    // access the singleton application instance
    static App& instance();

    // parse command line arguments, configure logging, and run platform setup
    void initialize(int argc, char** argv);

    // create the main window and run the application event loop; returns when the loop exits
    int run();

    // spawn an additional window seeded with the given raw file (charts view).
    // the app creates and wires the view/presenter pair so all window wiring
    // stays in a single place, and keeps the window alive for the event loop
    // lifetime
    void new_window(std::shared_ptr<XyceOutputFile> raw_file);

    // release the shared kicad session
    ~App();

    // the parsed logging level (e.g. "debug", "info", "warn", "error")
    [[nodiscard]] const std::string& log_level() const { return m_log_level; }

    // the shared kicad session for plugin-mode communication, null when standalone
    [[nodiscard]] const std::shared_ptr<KiCadSession>& kicad_session() const { return m_kicad_session; }

    // the netlist file requested through --netlist, unset when not provided
    [[nodiscard]] const std::optional<std::filesystem::path>& netlist_path() const { return m_netlist_path; }

    // the analysis output file requested through --raw (.raw, .prn, .csd or .csv), unset when not provided
    [[nodiscard]] const std::optional<std::filesystem::path>& raw_path() const { return m_raw_path; }

    // the Xyce executable requested through --xyce, unset when not provided
    [[nodiscard]] const std::optional<std::string>& xyce_path() const { return m_xyce_path; }

private:
    App() = default;

    // apply the configured log level to spdlog
    void setup_logger();

    // create a view/presenter pair for a window, wire them together, keep the
    // window alive in the registry, and show it; returns the presenter so the
    // caller can seed the window content (main window netlist or raw file)
    [[nodiscard]] SlintMainWindowPresenter* create_window(std::unique_ptr<NetlistSource> netlist_source, std::shared_ptr<KiCadSession> session);

    // a spawned window pair: the view owns the slint component and the
    // presenter owns the business logic; both must stay alive for the window
    // lifetime
    struct WindowInstance
    {
        std::unique_ptr<SlintMainWindowView> view;
        std::unique_ptr<SlintMainWindowPresenter> presenter;
    };

    std::shared_ptr<KiCadSession> m_kicad_session;
    std::string m_log_level = "info";

    // command line requested files and executable, unset when not provided
    std::optional<std::filesystem::path> m_netlist_path;
    std::optional<std::filesystem::path> m_raw_path;
    std::optional<std::string> m_xyce_path;

    // windows kept alive while the event loop runs; the main window is the
    // first entry
    std::vector<std::unique_ptr<WindowInstance>> m_windows;
};

// platform-specific initialization (e.g. dock icon), implemented per-platform
void platform_initialize();

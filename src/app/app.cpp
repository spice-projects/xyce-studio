#include <cctype>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <slint.h>
#include <spdlog/spdlog.h>

#include "../core/util.h"
#include "app.h"

#include "../kicad/kicad_session.h"
#include "../netlist/editor_netlist_source.h"
#include "../ui/main_window_presenter.h"
#include "../ui/main_window_view.h"

// extract the value of a long option, accepting both "--option VALUE" and "--option=VALUE" forms
static std::optional<std::string> option_value(int argc, char** argv, int& i, std::string_view name) {
    // match the space separated form and consume the following value token
    if (i + 1 < argc && argv[i] == name)
        return std::string(argv[++i]);
    // match the equals separated form
    const std::string_view argument = argv[i];
    // check for the option name followed by the separator
    if (argument.starts_with(name) && argument.size() > name.size() && argument[name.size()] == '=')
        // extract everything after the equals sign
        return std::string(argument.substr(name.size() + 1));
    // the option is not present at this position
    return std::nullopt;
}

// check that the path carries the expected extension, logging a warning and rejecting it otherwise
static bool has_extension(const std::filesystem::path& path, std::string_view extension) {
    // compare the normalized lowercase extension
    if (to_lower(path.extension().string()) == extension)
        return true;
    // warn about the rejected file so the user understands why it was not opened
    spdlog::warn("ignoring {}: {} is not a {} file", path.string(), path.string(), extension);
    // reject the path
    return false;
}

App& App::instance() {
    static App app;
    return app;
}

App::~App() = default;

void App::initialize(int argc, char** argv) {
    // reset the parsed file options so repeated initialization starts fresh
    m_netlist_path.reset();
    m_raw_path.reset();
    m_xyce_path.reset();
    // parse command line arguments
    for (int i = 1; i < argc; ++i) {
        // --log-level VALUE or -l VALUE
        if (i + 1 < argc && (std::string(argv[i]) == "--log-level" || std::string(argv[i]) == "-l"))
            m_log_level = argv[++i];
        // --log-level=VALUE
        else if (std::string(argv[i]).starts_with("--log-level="))
            m_log_level = std::string(argv[i]).substr(12);
        // --netlist VALUE or --netlist=VALUE opens a netlist file at startup
        else if (auto netlist_value = option_value(argc, argv, i, "--netlist")) {
            // only netlist files are accepted through this option
            if (has_extension(*netlist_value, ".cir"))
                m_netlist_path = std::filesystem::path(*netlist_value);
        }
        // --raw VALUE or --raw=VALUE opens a simulation output file at startup
        else if (auto raw_value = option_value(argc, argv, i, "--raw")) {
            // only raw output files are accepted through this option
            if (has_extension(*raw_value, ".raw"))
                m_raw_path = std::filesystem::path(*raw_value);
        }
        // --xyce VALUE or --xyce=VALUE overrides the Xyce executable for this session
        else if (auto xyce_value = option_value(argc, argv, i, "--xyce"))
            m_xyce_path = *xyce_value;
    }
    // normalize to lowercase
    m_log_level = to_lower(m_log_level);
    // apply the configured log level
    setup_logger();
    // run platform-specific initialization (dock icon, etc.)
    platform_initialize();
}

void App::setup_logger() {
    // default log level
    spdlog::set_level(spdlog::level::info);
    // debug
    if (m_log_level == "debug") {
        spdlog::set_level(spdlog::level::debug);
        return;
    }
    // warn
    if (m_log_level == "warn") {
        spdlog::set_level(spdlog::level::warn);
        return;
    }
    // error
    if (m_log_level == "error") {
        spdlog::set_level(spdlog::level::err);
        return;
    }
}

int App::run() {
    // build a session when running as a KiCad plugin
    auto session = KiCadSession::from_environment();
    // share the session with the app when present
    if (session)
        m_kicad_session = std::make_shared<KiCadSession>(std::move(*session));
    // the main window's netlist source: the schematic-backed source when running as a KiCad plugin (taken from the session), or an editable editor source standalone
    std::unique_ptr<NetlistSource> netlist_source = m_kicad_session != nullptr ? m_kicad_session->take_netlist_source() : std::make_unique<EditorNetlistSource>([]() -> std::string { return std::string{}; }, std::filesystem::path{});
    // the main window goes through the same creation path as any spawned window
    auto* main_presenter = create_window(std::move(netlist_source), m_kicad_session);
    // extract the schematic netlist before the first frame (KiCad plugin mode)
    if (m_kicad_session != nullptr)
        main_presenter->on_extract_schematic_netlist();
    // seed the window from the command line when running standalone; the plugin session owns the netlist source
    if (m_kicad_session == nullptr) {
        // open the requested netlist through the same code path as the file selection action
        if (m_netlist_path)
            main_presenter->on_open_xyce_file(*m_netlist_path);
        // load the requested raw output through the same dispatch as the file selection action
        if (m_raw_path)
            main_presenter->on_open_xyce_file(*m_raw_path);
    }
    // run the slint event loop until the last window closes
    slint::run_event_loop();
    // WORKAROUND (see slint-bug.md): slint 1.17.1 caches the native context menu item tree in
    // WinitWindowAdapter::context_menu and never releases it, and that field is declared after the
    // renderer, so destroying a window frees the skia renderer first and the cached menu item tree
    // then calls free_graphics_resources() on it. The result is a use-after-free that aborts on exit
    // as soon as the charts context menu has been opened once. Intentionally leak every window (the
    // main window and any spawned through new_window()) so the window adapters are never destroyed;
    // the process is exiting and the OS reclaims the memory. Remove once the slint bug is fixed.
    // release the gpu context on every window before leaking it; the gpu
    // context must be torn down before static destructors or skia's
    // grmanagedresource trace asserts during teardown
    for (auto& window : m_windows)
        window->view->release_gpu_resources();
    // intentionally leak the windows so the slint workaround stays active
    for (auto& window : m_windows)
        (void)window.release();
    // the event loop exited, end the application
    return 0;
}

void App::new_window(std::shared_ptr<XyceOutputFile> raw_file) {
    // spawned windows are standalone (no kicad session); the window is wired
    // and shown by create_window
    auto* presenter = create_window(std::make_unique<EditorNetlistSource>([]() -> std::string { return std::string{}; }, std::filesystem::path{}), nullptr);
    // seed the new window with the raw file, switching to the charts view; the
    // window was shown by create_window, so the native content view exists when
    // the charts renderer attaches
    presenter->load_raw_file(std::move(raw_file));
}

SlintMainWindowPresenter* App::create_window(std::unique_ptr<NetlistSource> netlist_source, std::shared_ptr<KiCadSession> session) {
    // plugin config; the --xyce command line override replaces the disk configuration for this session
    auto config = m_xyce_path ? PluginConfig(*m_xyce_path) : PluginConfig::load();
    // the view needs only a placeholder netlist source; the presenter owns the real source
    auto instance = std::make_unique<WindowInstance>();
    instance->view = std::make_unique<SlintMainWindowView>(std::make_unique<EditorNetlistSource>([]() -> std::string { return std::string{}; }, std::filesystem::path{}), config);
    instance->presenter = std::make_unique<SlintMainWindowPresenter>(*instance->view, std::move(netlist_source), config, std::move(session));
    // wire the event handler so the view forwards user interactions to the presenter
    instance->view->set_event_handler(*instance->presenter);
    // keep the window alive while the event loop runs
    m_windows.push_back(std::move(instance));
    // show the window; the caller seeds the content afterwards
    m_windows.back()->view->show();
    // hand back the presenter so the caller can seed the window
    return m_windows.back()->presenter.get();
}

#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "albor/window/window_handle.h"
#include "charts/chart_engine.h"

class XyceOutputFile;

namespace studio
{
    // one shell window as the application tracks it, which a test substitutes without the window toolkit
    class ShellWindow
    {
    public:
        virtual ~ShellWindow() = default;

        // hand out the window handle the application names the shell by
        [[nodiscard]] virtual albor::WindowHandle handle() const = 0;

        // report whether the shell still owns a live window
        [[nodiscard]] virtual bool alive() const = 0;

        // route one closed window to the shell that owns it, returning true when it was this one
        virtual bool on_window_closed(albor::WindowHandle closed) = 0;

        // show a dataset another window already parsed as a tab
        virtual bool show_dataset(std::shared_ptr<XyceOutputFile> file, ChartKind kind, std::string title) = 0;

        // open a netlist through the same dispatch the open action uses
        virtual void open_netlist(const std::filesystem::path& path) = 0;

        // open every output file the command line named, returning whether one loaded
        virtual bool show_startup_datasets(const std::vector<std::filesystem::path>& paths) = 0;

        // install the sink the chart new window command reaches
        virtual void set_on_new_window(std::function<void(std::shared_ptr<XyceOutputFile>, ChartKind, std::string)> handler) = 0;

        // close the window the shell owns, which the application tears down
        virtual void request_close() = 0;
    };
} // namespace studio

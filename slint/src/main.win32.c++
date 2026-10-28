// clang-format off
#include <windows.h>
#include <shellapi.h>
// clang-format on

#include <vector>

#include "app/app.h"

// entry point for the Windows GUI subsystem application
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // discard unused winmain parameters
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;
    // parse the windows command line into argc/argv so initialize() can read it
    int argc = 0;
    // accumulate the converted arguments as owning narrow strings
    std::vector<std::string> args;
    // CommandLineToArgvW splits the command line into wide strings
    if (LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        args.reserve(argc);
        for (int i = 0; i < argc; ++i) {
            // measure the utf-8 conversion size for this argument
            int size = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, nullptr, 0, nullptr, nullptr);
            if (size > 0) {
                // allocate a buffer for the narrow result
                std::string arg(size, '\0');
                // perform the wide-to-narrow conversion into the buffer
                int written = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, arg.data(), size, nullptr, nullptr);
                if (written > 0) {
                    // drop the trailing null terminator from the measured length
                    arg.resize(written - 1);
                    // store the converted argument
                    args.push_back(std::move(arg));
                }
            }
        }
        // release the memory allocated by CommandLineToArgW
        LocalFree(wargv);
    }
    // build a null-terminated argv array pointing at the narrow strings
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    // append the chart pointers
    for (auto& arg : args)
        argv.push_back(arg.data());
    // append null pointer
    argv.push_back(nullptr);
    // create application
    auto& application = App::instance();
    // configure the application (command line parsing, logging, platform setup)
    application.initialize(static_cast<int>(args.size()), argv.data());
    // create the main window and run the application event loop
    return application.run();
}

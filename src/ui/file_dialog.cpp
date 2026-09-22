#include "file_dialog.h"

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include <nfd.hpp>
#include <spdlog/spdlog.h>

namespace
{
    // RAII guard that initializes NFD once per thread and shuts it down when
    // the guard goes out of scope; NFD requires init/quit on each thread that
    // uses it
    struct NFDGuard
    {
        NFDGuard() {
            // init the nativefiledialog library for this thread
            if (NFD::Init() != NFD_OKAY) {
                // initialization can fail when the platform backend is missing
                spdlog::error("NFD_Init failed: {}", NFD::GetError());
            }
        }

        ~NFDGuard() {
            // shutdown the nativefiledialog library for this thread
            NFD::Quit();
        }
    };

    // ensures NFD is initialized for the calling thread and stays alive
    // until thread exit
    void ensure_nfd() {
        // thread_local storage guarantees a single init/quit per thread
        thread_local NFDGuard guard;
    }

} // anonymous namespace

std::string FileDialog::open_file_extensions() {
    // the netlist format plus every analysis output format the presenter dispatches by extension in on_open_xyce_file
    return std::string("cir,raw,prn,csd,csv");
}

std::optional<std::filesystem::path> FileDialog::open_xyce_file() {
    // ensure the native file dialog library is initialized
    ensure_nfd();

    // filter list covering the netlist and all loadable analysis output files;
    // the description and extension pair lives for the whole call
    static const std::string extensions = open_file_extensions();
    static const std::array<nfdu8filteritem_t, 1> filters{{nfdu8filteritem_t{"Xyce Files", extensions.c_str()}}};

    // open the native dialog with the filter applied
    nfdu8char_t* outPath = nullptr;
    nfdopendialogu8args_t args{};
    args.filterList = filters.data();
    args.filterCount = static_cast<nfdfiltersize_t>(filters.size());
    const auto result = ::NFD_OpenDialogU8_With(&outPath, &args);
    // user canceled or an error occurred
    if (result != NFD_OKAY) {
        // log the error when the dialog failed programmatically
        if (result == NFD_ERROR) {
            // log information
            spdlog::error("open-xyce-file: NFD error: {}", NFD::GetError());
        }
        // no path to return
        return std::nullopt;
    }

    // NFD returns UTF-8 paths; construct a path from char8_t so the
    // platform-native encoding is UTF-8 on all platforms including Windows
    NFD::UniquePathU8 pathGuard(outPath);
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(outPath)));
}

std::optional<std::filesystem::path> FileDialog::open_xyce_executable() {
    // ensure the native file dialog library is initialized
    ensure_nfd();

    // open the native dialog with no filter so any executable is selectable
    nfdu8char_t* outPath = nullptr;
    const nfdopendialogu8args_t args{};
    const auto result = ::NFD_OpenDialogU8_With(&outPath, &args);
    // user canceled or an error occurred
    if (result != NFD_OKAY) {
        // log the error when the dialog failed programmatically
        if (result == NFD_ERROR) {
            // log information
            spdlog::error("open-xyce-executable: NFD error: {}", NFD::GetError());
        }
        // no path to return
        return std::nullopt;
    }

    // NFD returns UTF-8 paths; construct a path from char8_t so the
    // platform-native encoding is UTF-8 on all platforms including Windows
    NFD::UniquePathU8 pathGuard(outPath);
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(outPath)));
}

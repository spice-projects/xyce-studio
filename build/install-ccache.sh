#!/usr/bin/env bash
set -euo pipefail

# chocolatey returns success when its feed is down, so the executable decides the result
install_choco() {
    local package="$1"
    shift
    for binary in "$@"; do
        if command -v "$binary" >/dev/null 2>&1; then
            return 0
        fi
    done
    # the hosted feed throws transient 503s, so the install gets two more attempts
    for attempt in 1 2 3; do
        if choco install "$package" --no-progress -y > /dev/null 2>&1; then
            for binary in "$@"; do
                if command -v "$binary" >/dev/null 2>&1; then
                    return 0
                fi
            done
        fi
        sleep 10
    done
    return 1
}

if ! install_choco ccache ccache; then
    # the pinned upstream build keeps a feed outage from blocking a windows compile
    version=4.11.3
    archive="ccache-${version}-windows-x86_64.zip"
    curl -fsSL "https://github.com/ccache/ccache/releases/download/v${version}/${archive}" -o "$archive"
    unzip -q -o "$archive" -d ccache-download
    bin="ccache-download/ccache-${version}-windows-x86_64/ccache.exe"
    [ -x "$bin" ] || { echo "ccache ${version} could not be installed" >&2; exit 1; }
    echo "$(pwd)/$(dirname "$bin")" >> "$GITHUB_PATH"
fi

# msvc coverage is collected from the pdb, which needs its own tool on the windows coverage job
if [ "${1:-}" = "--coverage" ] && ! command -v OpenCppCoverage >/dev/null 2>&1; then
    # the installer writes a machine path that git bash does not pick up, so the install directories are checked directly
    log="${RUNNER_TEMP:-/tmp}/opencppcoverage-install.log"
    binary=""
    # the hosted feed throws transient 503s, so the install gets two more attempts
    for attempt in 1 2 3; do
        if choco install opencppcoverage --no-progress -y > "$log" 2>&1; then
            for candidate in "/c/Program Files/OpenCppCoverage/OpenCppCoverage.exe" "/c/Program Files (x86)/OpenCppCoverage/OpenCppCoverage.exe"; do
                if [ -x "$candidate" ]; then
                    binary="$candidate"
                    break
                fi
            done
            if [ -n "$binary" ]; then
                break
            fi
        fi
        sleep 10
    done
    # the binary is published as OpenCppCoverage.exe, so exporting its directory makes both spellings resolve
    if [ -n "$binary" ]; then
        install_dir="$(dirname "$binary")"
        echo "$install_dir" >> "$GITHUB_PATH"
        export PATH="$PATH:$install_dir"
    else
        echo "opencppcoverage could not be installed, last choco output:" >&2
        tail -40 "$log" >&2 2>/dev/null || true
        exit 1
    fi
fi

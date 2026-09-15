# Xyce Studio

Native desktop UI for the Xyce circuit simulator, shipped as a KiCad plugin (launched from the schematic editor over IPC) and as a standalone macOS application for loading netlists, running simulations, and visualizing output.

## Installation

### KiCad plugin

#### Step 1: Enable KiCad API

1. Open KiCad
2. Go to **Preferences** → **Settings** → **API**
3. Enable **KiCad API**

![KiCad Plugin Preferences](docs/kicad-plugin-settings.png)

#### Step 2: Restart KiCad

Restart KiCad so the KiCad API server is available.

#### Step 3: Download the plugin package

https://github.com/spice-projects/xyce-studio/releases/latest

Download the package for your platform: `kicad-xyce-plugin-macos.zip` or `kicad-xyce-plugin-windows.zip`.

#### Step 4: Install the plugin from file

1. Open KiCad
2. Go to **Tools** → **Plugin Manager** (or **Preferences** → **Plugin Manager**)
3. Click **Install from File...** and select the downloaded plugin `.zip` file

![Install from File](docs/kicad-package-manager.png)

#### Step 5: Restart KiCad

Restart KiCad so the newly installed plugin is loaded and registered.

#### Step 6: Open Xyce Studio from the schematic toolbar

Open a schematic project. The plugin appears as a toolbar button in the schematic editor — click it to launch the simulator.

![KiCad Schematic Editor](docs/kicad-schematic-editor.png)

### Standalone app (macOS)

Download `xyce-studio-macos.zip` from the [latest release](https://github.com/spice-projects/xyce-studio/releases/latest), unzip it, and drag `Xyce Studio.app` to your Applications folder. The standalone app does not connect to KiCad: it opens netlist files, runs simulations with Xyce, and visualizes the output, and it can also open Xyce simulation output files directly.

## Current status

- Development status: in progress
- Primary action: Run Xyce Circuit Simulator
- Runtime: native C++23 executable (IPC plugin)

## What this project provides

- Native KiCad plugin action to launch the simulator UI
- Simulation command dialog supporting Transient, AC, DC, Harmonic Balance, Noise, Operating Point, and Linear analyses
- Xyce process runner with streamed stdout and stderr handling
- Interactive Slint desktop UI with native charts and expression plotting
- FFT calculations for transient analysis and STEP visualization
- Persistent plugin configuration for the Xyce executable path
- IPC integration with KiCad via NNG and the vendored KiCad protobuf API

## Repository layout

- `src/`: main source directory containing the application logic and files
  - `src/main.osx.mm`, `src/main.win32.c++`, `src/main.linux.c++`: platform entry points
  - `src/app/`: application lifecycle (singleton, platform initialization)
  - `src/core/`: shared utilities (`util`, `view`, `step_information`)
  - `src/plugin.json`: KiCad Plugin and Content Manager (PCM) executable-plugin metadata
  - `src/kicad/`: KiCad IPC connection (NNG), session handling, and netlist source
  - `src/netlist/`: Xyce netlist generation
  - `src/simulation/`: simulation parameter models
  - `src/expression/`: expression parsing/evaluation
  - `src/dsp/`: FFT computation for spectral analysis
  - `src/io/`: Xyce output/raw/FFT file readers
  - `src/charts/`: chart data model and decimation algorithms
  - `src/config/`: plugin configuration
  - `src/ui/`: Slint desktop UI (flat layout: views/presenters, dialog wrappers, platform backends, charts renderer)
- `netlists/`: sample/test netlists
- `tests/`: C++ unit tests (GoogleTest)
- `xyce-docs/`: vendor-provided Xyce documentation PDFs
- `STYLE-GUIDE.md`: style guidelines for the codebase

## Requirements

- C++23 compiler and CMake 3.25+
- KiCad 10.0 or newer with IPC plugin runtime support
- Xyce executable installed and available on disk
- Dependencies are managed via `vcpkg` (see `vcpkg.json`)

## Local development setup

Configure and build with CMake presets. Dependencies come from vcpkg via the toolchain file.

```bash
cmake --preset debug
cmake --build --preset debug
```

Other presets are available in `CMakePresets.json`: `release` and `profile`.

## Running locally

The binary runs in two modes:

- **Standalone**: run the executable directly (`.build-debug/xyce-studio`); it opens netlist and simulation output files through the file dialog.
- **KiCad plugin mode**: launched by KiCad over IPC using the `KICAD_API_SOCKET` and `KICAD_API_TOKEN` environment variables; the plugin entrypoint/metadata referenced by KiCad lives in `src/plugin.json` and `metadata.json`.

## Building the KiCad package

```bash
./build/create-kicad-package.sh <version>
```

The executable defaults to `.build-debug/xyce-studio` and can be overridden as the second argument. The result is written to `dist/`.

## Testing

### C++

Tests are built alongside the plugin. Run the test executable directly:

```bash
./.build-debug/tests/xyce-studio-tests
```

### Python (including UI integration tests)

```bash
python3 -m unittest discover -v -s . -p "*_test.py"
```

The KiCad plugin mode scenario in `tests_ui/kicad_reconfigure_test.py` requires the `pynng` package (`pip install pynng`); it simulates the KiCad API server with an in-process mock.

## Configuration

At runtime, the plugin expects a valid path to the Xyce executable. Configure it in the plugin UI via the Configuration dialog, along with analysis-specific simulation settings.

## Troubleshooting

- If simulation fails to start, verify the configured Xyce path points to an executable file
- If the plugin does not run from KiCad, verify KiCad plugin discovery and IPC runtime environment configuration
- If the UI fails to initialize, verify the platform graphics backend (Metal/D3D11/OpenGL) is available

## Contributing

1. Open an issue describing the proposed change
2. Implement and test in a feature branch
3. Submit a pull request with a clear summary and validation notes

## License

Project source code is licensed under Apache-2.0. See LICENSE.

This repository also bundles third-party icon assets from KiCad under CC-BY-SA 4.0 in kicad-icons (and `src/ui/kicad-icons`). See:

- kicad-icons/LICENSE
- kicad-icons/COPYING
- THIRD_PARTY_NOTICES.txt

This repository also bundles third-party Xyce documentation PDFs in xyce-docs. See:

- xyce-docs/Xyce_RG.pdf
- xyce-docs/Xyce_UG.pdf
- xyce-docs/COPYING.XYCE
- THIRD_PARTY_NOTICES.txt

When redistributing this project, include the project LICENSE file and all third-party license and notice files listed above.

Dependencies (Slint, spdlog, protobuf, NNG, pocketfft, GoogleTest) are third-party components distributed under their own licenses. See THIRD_PARTY_NOTICES.txt for attribution and redistribution notes.

# Building from Source

This section is for developers and packagers. If you only use the
application, everything you need is in the
[Getting Started](../getting-started/installation.md) section.

## Prerequisites

- C++23-capable compiler and CMake 3.25+
- [vcpkg](https://vcpkg.io) for dependencies (see `vcpkg.json`)
- [ccache](https://ccache.dev) (recommended — enabled by the build
  presets)

## Configure and build

```bash
cmake --preset debug
cmake --build --preset debug
```

Presets available in `CMakePresets.json`: `debug`, `release`, `profile`.
The built binary lands in `.build-debug/` (debug preset).

The vcpkg binary cache (`.vcpkg-cache/`) and ccache speed up rebuilds;
first configuration downloads and builds dependencies (Slint, protobuf,
NNG, spdlog, pocketfft, GoogleTest).

## Run the application

The binary runs in two modes:

- **Standalone**: `.build-debug/xyce-studio` — opens netlist and
  simulation output files through the file dialog.
- **KiCad plugin mode**: launched by KiCad over IPC using the
  `KICAD_API_SOCKET` and `KICAD_API_TOKEN` environment variables; the
  plugin entrypoint metadata lives in `src/plugin.json`.

## UI integration tests

UI tests drive the real debug build through the `slint_automation`
framework and live in `tests_ui/`:

```bash
.venv/bin/python -m unittest discover -s tests_ui -p "*_test.py"
```

The KiCad plugin mode scenario requires `pynng`
(`pip install pynng`); it simulates the KiCad API server in-process.

## Build the KiCad plugin package

```bash
./build/create-kicad-package.sh <version>
```

The result is written to `dist/`. The executable defaults to
`.build-debug/xyce-studio` and can be overridden as the second argument.

## Building this documentation site

The documentation site is built with MkDocs + Material for MkDocs.

```bash
python3 -m venv .venv-docs
.venv-docs/bin/pip install mkdocs-material
.venv-docs/bin/mkdocs serve      # local preview at http://localhost:8000
.venv-docs/bin/mkdocs build      # static output in site/
```

The site is deployed to GitHub Pages by the
[docs workflow](https://github.com/spice-projects/xyce-studio/blob/main/.github/workflows/docs.yml)
on every change under `docs/` or to `mkdocs.yml`.

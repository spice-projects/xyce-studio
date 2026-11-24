# Building from Source

This section is for developers and packagers. If you only use the
application, everything you need is in the
[Getting Started](../getting-started/installation.md) section.

## Prerequisites

- C++20-capable compiler and CMake 3.28+
- [vcpkg](https://vcpkg.io) for dependencies (see `vcpkg.json`)
- [ccache](https://ccache.dev) (recommended — enabled by the build
  presets)

## Configure and build

```bash
cmake --preset debug
cmake --build --preset debug
```

Four presets remain in `CMakePresets.json`:

- `debug` — local development against the sibling `../albor` checkout via
  `ALBOR_LOCAL_SOURCE_DIR`; Debug build of the app plus both test suites
  (`xyce-studio-tests` and `studio-tests`), with the automation host embedded.
  Binary: `.build-debug/xyce-studio`.
- `release` — clean-checkout release build using the vendored Albor port via
  `VCPKG_MANIFEST_FEATURES=albor-package`; Release build of the app with no tests
  and no automation host, and it runs the `link-boundary` check. Binary:
  `.build-release/xyce-studio`.
- `coverage` — CI coverage build using the vendored port; Debug with
  instrumentation.
- `asan` — CI sanitizer build using the vendored port.

Albor is not published in the vcpkg registry. Its published GitHub release is
`v0.0.1`; the overlay port vendored in `.ports/albor` pins that release's commit
`7d2f0c6f52a780762802d667a0a49fa5eed110a7` (version 0.0.1). `.ports` is the single
overlay directory. Local development still uses the sibling `../albor` checkout via
`ALBOR_LOCAL_SOURCE_DIR`, but the root `CMakeLists.txt` no longer adds that
checkout's port to `VCPKG_OVERLAY_PORTS`.

The vcpkg binary cache (`.vcpkg-cache/`) and ccache speed up rebuilds;
first configuration downloads and builds dependencies (Albor, protobuf,
NNG, spdlog, pocketfft, GoogleTest).

## Run the application

The binary runs in two modes:

- **Standalone**: `.build-debug/xyce-studio` — opens netlist and
  simulation output files through the file dialog.
- **KiCad plugin mode**: launched by KiCad over IPC using the
  `KICAD_API_SOCKET` and `KICAD_API_TOKEN` environment variables; the
  plugin entrypoint metadata is the `plugin.json` packaged with the build.

## UI integration tests

UI tests drive the real debug build through the `albor_automation`
framework and live in `tests_studio/`:

```bash
.venv/bin/python -m unittest discover -s tests_studio -p "*_test.py"
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

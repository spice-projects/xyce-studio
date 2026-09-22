# Contributing to Xyce Studio

Thanks for considering a contribution!

## How to contribute

1. Open (or find) an issue describing the proposed change.
2. Implement and test on a feature branch off the latest `main`.
3. Submit a pull request with a clear summary and validation notes.

## Development environment

Xyce Studio uses CMake presets with vcpkg-managed dependencies.

```bash
cmake --preset debug
cmake --build --preset debug
```

See [Building from Source](docs/development/building.md) for details:
presets, running the application in both modes, UI integration tests, and
building the KiCad plugin package.

## Testing

- C++ unit tests: `./.build-debug/tests/xyce-studio-tests`
- UI integration tests: `.venv/bin/python -m unittest discover -s tests_ui -p "*_test.py"`

## Documentation

The documentation site is built with MkDocs + Material for MkDocs and
deployed to GitHub Pages. To preview locally:

```bash
python3 -m venv .venv-docs
.venv-docs/bin/pip install mkdocs-material
.venv-docs/bin/mkdocs serve
```

Documentation changes (`docs/**`, `mkdocs.yml`) are validated with a
`mkdocs build --strict` build in the `docs.yml` workflow.

## Reporting bugs

Open a GitHub issue including: platform and versions (Xyce Studio, KiCad,
Xyce), steps to reproduce, and the contents of the
[Simulation Output panel](https://spice-projects.github.io/xyce-studio/user-guide/main-window/#simulation-output-panel)
when relevant.

## License

By contributing you agree that your contributions are licensed under the
project's [Apache-2.0 license](LICENSE).

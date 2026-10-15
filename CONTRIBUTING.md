# Contributing

Thank you for contributing to `lumina-svg-core`. The project is an independent
SVG geometry library; changes should preserve that boundary.

## Report a problem

Open an [issue](https://github.com/lumina-layer-studio/lumina-svg-core/issues)
with the package version, operating system and architecture, Python version,
analysis arguments, returned status/reason, and expected behavior. Include a
minimal SVG that you have permission to share. Remove personal or confidential
content. Follow [SECURITY.md](SECURITY.md) for sensitive findings.

Discuss substantial API or algorithm changes in an issue before implementing
them. Small fixes and documentation corrections may be submitted directly.

## Local setup

Use CPython 3.12–3.14, Git, and a C++17 compiler. Create a virtual environment as
shown in the [README](README.md), then run:

```bash
python -m pip install --upgrade pip
python -m pip install ".[test]" build
python -m pytest tests -q
```

Reinstall the package after changing C++ source. Dependencies are pinned in
`cmake/Dependencies.cmake` and `pyproject.toml`; a fresh build fetches those
revisions and applies the reviewed patches. Release builds reject arbitrary
dependency source overrides. Do not weaken these checks to work around a local
configuration problem.

## Changes and tests

- Keep SVG semantics, coordinate ownership, topology, color provenance, and
  read-only buffer lifetimes explicit. Do not add caller-specific printer or
  application policy to the engine.
- For algorithm changes and bug fixes, add a small regression fixture and test
  the observable geometry or failure contract. Preserve public signatures
  unless an API change is deliberately proposed.
- Write Python with type hints and save all source/documentation as UTF-8.
  Follow the surrounding C++17 conventions and keep helpers focused.
- Update the API/support documentation when behavior changes. If distributing
  new third-party code, retain its license and attribution.
- Keep independent recorder algorithms in the project-owned modules. See
  [the recorder boundary](docs/recorder-boundary.md) before changing the
  LunaSVG/PlutoVG patch or updating its pinned revision.
- Do not commit build outputs, environments, credentials, private SVGs, or
  machine-specific paths.

Build and inspect distributions before proposing packaging changes:

```bash
python -m build
python tools/verify_wheel.py dist/<actual-wheel-filename>.whl --contents-only
```

The Linux CI job also runs the C++ canonical-boundary regression via CMake and
CTest. The wheel matrix validates installation and the complete Python suite
on Windows x86-64, Linux x86-64, macOS arm64, and macOS x86-64, for Python
3.12, 3.13 and 3.14. [Distribution preparation](docs/DISTRIBUTIONS.md) records
the reviewed source identity and hashes; release preparation requires a clean
checkout and wheels from that exact source.

## Pull requests

Create a topic branch such as `your-name/fix/clip-winding` or
`your-name/chore/document-installation`. Open a pull request against
`dev-refactor`; do not push directly to the long-lived branch. Describe the
problem, the resulting behavior, and the validation performed. Use Conventional
Commits, for example `fix(geometry): preserve nested clip winding`.

CI must pass before merging. Golden geometry fingerprints should change only
when the intended semantics change; explain the reason for each update.

## License

Contributions to project-owned code are provided under **AGPL-3.0-only**, as
described in [LICENSE](LICENSE). Existing third-party notices and licenses
remain in force. Submit only material you are authorized to contribute.

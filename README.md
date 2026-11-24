# lumina-svg-core

[![Build wheels](https://github.com/lumina-layer-studio/lumina-svg-core/actions/workflows/wheels.yml/badge.svg?branch=dev-refactor)](https://github.com/lumina-layer-studio/lumina-svg-core/actions/workflows/wheels.yml)
[![Python 3.12–3.14](https://img.shields.io/badge/python-3.12%E2%80%933.14-blue.svg)](https://www.python.org/downloads/)
[![License: AGPL v3](https://img.shields.io/badge/license-AGPL--3.0--only-blue.svg)](LICENSE)

[English](README.md) · [简体中文](README.zh-CN.md) · [Installation](#installation) · [Quick start](#quick-start) · [API reference](docs/api.md) · [SVG support](docs/support.md)

A native C++17 and Python 2D SVG geometry engine for print workflows.

`lumina-svg-core` resolves SVG paint operations, converts fills and strokes into planar polygons, and exposes read-only NumPy arrays to Python while preserving paint order, holes, clipping, solid-color alpha compositing, and paint provenance.

Originally built for [Lumina Studio](https://github.com/lumina-layer-studio/Lumina-Layers), `lumina-svg-core` lives in this independent repository with its C++ geometry kernel, pybind11 bindings, test suite, and wheel build tooling.

**Current release:** `0.3.4` (alpha) · **Python:** CPython `3.12`, `3.13`, `3.14`

## Installation

### Prebuilt wheels

```bash
python -m pip install lumina-svg-core==0.3.4
```

Prebuilt wheels bundle pinned LunaSVG/PlutoVG and Clipper2 sources (also downloadable from [GitHub Releases](https://github.com/lumina-layer-studio/lumina-svg-core/releases)):

| Platform | Architecture | Baseline |
| --- | --- | --- |
| Windows | x86-64 | 64-bit CPython 3.12–3.14 |
| macOS | Apple Silicon (`arm64`) & Intel (`x86_64`) | macOS 12+ (Python 3.12/3.13), macOS 14+ (Python 3.14) |
| Linux | x86-64 | `manylinux_2_28` (glibc 2.28+) |

### Build from source

Requires CPython 3.12–3.14, Git, network access (for pinned LunaSVG and Clipper2 revisions on first build), and a C++17 compiler (MSVC, Apple Clang, or GCC/Clang):

```bash
git clone https://github.com/lumina-layer-studio/lumina-svg-core.git
cd lumina-svg-core
python3.12 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install .
```

On Windows PowerShell, run `py -3.12 -m venv .venv` and `.venv\Scripts\Activate.ps1` from a Visual Studio Developer PowerShell.

## Quick start

```python
import lumina_svg_core

svg = """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 20 10">
  <rect width="20" height="10" fill="#e63946"/>
  <circle cx="10" cy="5" r="3" fill="#457b9d" fill-opacity="0.5"/>
</svg>"""

result = lumina_svg_core.analyze_svg_svg(
    svg,
    target_width_mm=40.0,
    tolerance_mm=0.02,
)
if result.status != "supported":
    raise RuntimeError(f"SVG analysis failed: {result.status}: {result.reason_code}")

print(result.visible.geometry_count)
print(result.visible.rgba_f64)

# Convert integer working-grid coordinates to millimetres:
coordinates_mm = (
    result.visible.coordinates.astype("float64")
    * result.grid_to_source_unit
    * result.scale_factor
)
```

- **Analyze a file:** `lumina_svg_core.analyze_svg("drawing.svg", target_width_mm=40.0, tolerance_mm=0.02)` (`str` or `pathlib.Path`).
- **Fills only:** Pass `allow_strokes=False` to skip strokes while keeping filled regions.
- **Output views:** `result.raw` holds painted polygons before occlusion, `result.visible` holds non-overlapping visible regions, and `result.silhouette` holds the outer footprint.

Ring traversal, status codes, build provenance (`__version__`, `__source_revision__`, `__source_dirty__`), and lower-level APIs are documented in [docs/api.md](docs/api.md) and [docs/DISTRIBUTIONS.md](docs/DISTRIBUTIONS.md).

## Supported SVG features

| Category | Supported features |
| --- | --- |
| Solid fills and strokes | Fill rules (`nonzero`, `evenodd`), line caps, line joins, dash arrays, and `non-scaling-stroke` |
| Shapes and paths | `<rect>`, `<circle>`, `<ellipse>`, `<polygon>`, `<polyline>`, `<line>`, and `<path>` curves/arcs |
| Layout | Affine `transform`, `viewBox` origins, nested `<svg>` viewports, and percentage lengths |
| References and styles | Supported CSS selectors and same-document `<use>` elements |
| Clipping and markers | `<clipPath>`, marker paints, and viewport clipping |
| Transparency | Solid-paint alpha and object/group `opacity` composited in premultiplied sRGB |

Documents using features outside this 2D solid-paint subset return `status == "unsupported"` with a specific `reason_code`; see [docs/support.md](docs/support.md) for supported boundaries and numerical limits.

## Development and validation

```bash
python -m pip install ".[test]" build
python -m pytest tests -q
python -m build
python tools/verify_wheel.py dist/<wheel-filename>.whl --contents-only
```

See [CONTRIBUTING.md](CONTRIBUTING.md) and [benchmarks/README.md](benchmarks/README.md).

## License and acknowledgements

Licensed under **AGPL-3.0-only**; see [LICENSE](LICENSE).

Third-party attributions and license texts for LunaSVG, PlutoVG, Clipper2, pybind11, and ChromaPrint3D are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and [THIRD_PARTY_LICENSES/](THIRD_PARTY_LICENSES). Report bugs via [GitHub Issues](https://github.com/lumina-layer-studio/lumina-svg-core/issues) and security issues per [SECURITY.md](SECURITY.md).

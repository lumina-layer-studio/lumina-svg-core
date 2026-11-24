# lumina-svg-core

[![Build wheels](https://github.com/lumina-layer-studio/lumina-svg-core/actions/workflows/wheels.yml/badge.svg?branch=dev-refactor)](https://github.com/lumina-layer-studio/lumina-svg-core/actions/workflows/wheels.yml)
[![Python 3.12–3.14](https://img.shields.io/badge/python-3.12%E2%80%933.14-blue.svg)](https://www.python.org/downloads/)
[![License: AGPL v3](https://img.shields.io/badge/license-AGPL--3.0--only-blue.svg)](LICENSE)

[English](README.md) · [简体中文](README.zh-CN.md) · [Installation](#installation) · [Quick start](#quick-start) · [API reference](docs/api.md) · [SVG support](docs/support.md)

A native C++17 and Python 2D SVG geometry engine for print workflows.

`lumina-svg-core` resolves SVG paint operations, converts fills and strokes into planar polygons, and returns the result to Python as read-only NumPy arrays while preserving paint order, holes, clipping, solid-color alpha compositing, and paint provenance.

Originally built for [Lumina Studio](https://github.com/lumina-layer-studio/Lumina-Layers), `lumina-svg-core` is maintained in this independent repository with its C++ geometry kernel, pybind11 bindings, test suite, and cross-platform wheel tooling, so any Python tool can use it directly.

**Current release:** `0.3.4` (alpha) · **Python:** CPython `3.12`, `3.13`, `3.14` · **Geometry schema:** v4

## Installation

### Prebuilt wheels

Install from PyPI:

```bash
python -m pip install lumina-svg-core==0.3.4
```

Or download a matching wheel from [GitHub Releases](https://github.com/lumina-layer-studio/lumina-svg-core/releases) and install it locally:

```bash
python -m pip install /path/to/lumina_svg_core-0.3.4-cp312-cp312-platform.whl
```

Pinned LunaSVG/PlutoVG and Clipper2 sources are compiled directly into the prebuilt wheels:

| Platform | Architecture | Baseline |
| --- | --- | --- |
| Windows | x86-64 | 64-bit CPython 3.12–3.14 |
| macOS | Apple Silicon (`arm64`) & Intel (`x86_64`) | macOS 12+ (Python 3.12/3.13), macOS 14+ (Python 3.14) |
| Linux | x86-64 | `manylinux_2_28` (glibc 2.28+) |

Release artifacts and build checks are also listed on the [PyPI version page](https://pypi.org/project/lumina-svg-core/0.3.4/) and [publishing workflow](https://github.com/lumina-layer-studio/lumina-svg-core/actions/workflows/publish.yml).

### Build from source

Building from source requires CPython 3.12–3.14, Git, network access (to fetch pinned LunaSVG and Clipper2 revisions on first build), and a C++17 compiler — MSVC on Windows, Apple Clang on macOS, or GCC/Clang on Linux:

```bash
git clone https://github.com/lumina-layer-studio/lumina-svg-core.git
cd lumina-svg-core
python3.12 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install .
```

On Windows PowerShell, create the virtual environment with `py -3.12 -m venv .venv`, activate it via `.venv\Scripts\Activate.ps1`, and run from a Visual Studio Developer PowerShell if MSVC is not already on `PATH`.

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

- **Analyze a file:** `lumina_svg_core.analyze_svg("drawing.svg", target_width_mm=40.0, tolerance_mm=0.02)` (accepts `str` or `pathlib.Path`).
- **Fills only:** Pass `allow_strokes=False` to either function to skip strokes while keeping filled regions.
- **Build provenance:** Inspect `lumina_svg_core.__version__`, `lumina_svg_core.__source_revision__`, and `lumina_svg_core.__source_dirty__`. See [docs/DISTRIBUTIONS.md](docs/DISTRIBUTIONS.md) for provenance and release receipt details.

Full ring traversal, status codes, coordinate units, and lower-level recording/grid APIs are documented in [docs/api.md](docs/api.md).

## Supported SVG features

| Category | Supported features |
| --- | --- |
| Solid fills and strokes | Fill rules (`nonzero`, `evenodd`), line caps, line joins, dash arrays (positive lengths), and `non-scaling-stroke` |
| Shapes and paths | `<rect>` (including rounded corners), `<circle>`, `<ellipse>`, `<polygon>`, `<polyline>`, `<line>`, and `<path>` curves/arcs |
| Layout | Affine `transform`, `viewBox` origins, nested `<svg>` viewports, and percentage lengths |
| References and styles | Supported CSS selectors and same-document `<use>` elements |
| Clipping and markers | `<clipPath>`, marker paints, and viewport clipping |
| Transparency | Solid-paint alpha and isolated object/group `opacity` composited with normal `source-over` in premultiplied sRGB |

The SVG canvas starts transparent. Fully opaque documents resolve occlusion in reverse paint order, while translucent documents partition overlapping regions and composite their colors in premultiplied sRGB.

When a document uses features outside the 2D solid-paint subset — such as gradients, patterns, masks, `<text>`, embedded bitmaps, external references, non-normal blend modes, non-sRGB color interpolation, or zero-length dash entries — the analyzer returns `status == "unsupported"` with a specific `reason_code` so callers can handle it explicitly. See [docs/support.md](docs/support.md) for full details and numerical limits.

## Geometry output

- **Three geometry views:** `result.raw` holds area-bearing paint polygons before occlusion, `result.visible` holds the final non-overlapping visible regions, and `result.silhouette` holds their combined outer footprint.
- **Exact working grid & colors:** Coordinates are stored on an `int64` working grid with explicit scale factors (`grid_to_source_unit` and `scale_factor`). `rgba_f64` provides straight-sRGB colors in `[0.0, 1.0]`, alongside byte-packed `rgba` and contributing paint provenance.
- **Degenerate fragments:** Zero-area points, open lines, empty paths, and collapsed slivers are reported separately in `result.unprintable_fragments`.
- **Parallel normalization:** Set `LUMINA_SVG_REPLAY_THREADS` to a positive integer (capped at `16`) to parallelize independent polygon normalization while preserving identical deterministic output order.

## Development and validation

```bash
python -m pip install ".[test]" build
python -m pytest tests -q
python -m build
python tools/verify_wheel.py dist/<wheel-filename>.whl --contents-only
```

CI builds and tests wheels across Python 3.12–3.14 on Windows, macOS (arm64 and x86-64), and Linux, verifies wheel contents and bundled licenses, and tests building from the sdist. See [CONTRIBUTING.md](CONTRIBUTING.md) for contributor guidelines and [benchmarks/README.md](benchmarks/README.md) for benchmark methodology.

## License and acknowledgements

Licensed under **AGPL-3.0-only**; see [LICENSE](LICENSE).

`lumina-svg-core` builds on pinned LunaSVG, PlutoVG, Clipper2, and pybind11 sources, plus adapted hole-selection criteria from ChromaPrint3D. Third-party attributions and license texts are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and [THIRD_PARTY_LICENSES/](THIRD_PARTY_LICENSES).

Report bugs through [GitHub Issues](https://github.com/lumina-layer-studio/lumina-svg-core/issues), and follow [SECURITY.md](SECURITY.md) for security disclosures.

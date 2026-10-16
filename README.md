# lumina-svg-core

[![Build wheels](https://github.com/lumina-layer-studio/lumina-svg-core/actions/workflows/wheels.yml/badge.svg?branch=dev-refactor)](https://github.com/lumina-layer-studio/lumina-svg-core/actions/workflows/wheels.yml)
[![Python 3.12–3.14](https://img.shields.io/badge/python-3.12%E2%80%933.14-blue.svg)](https://www.python.org/downloads/)
[![License: AGPL v3](https://img.shields.io/badge/license-AGPL--3.0--only-blue.svg)](LICENSE)

[English](README.md) | [简体中文](README.zh-CN.md)

A native SVG geometry engine for print workflows. `lumina-svg-core` resolves SVG
paint operations, converts them into polygons, and exposes the result to Python
as read-only NumPy arrays. It preserves paint order, holes, clipping, solid-color
alpha compositing, and the identity of contributing paints.

Originally developed for Lumina Studio, this independent repository contains
the geometry engine, Python bindings, test fixtures, and build tools. It has no
runtime dependency on Lumina Studio, its UI, printer settings, or application
services. It does not generate meshes or 3MF files.

**Status:** version 0.3.3 (alpha). CPython 3.12, 3.13 and 3.14 are supported.
The geometry result contract is schema version 4.

## Installation

### Prebuilt wheels

Install the current public release from PyPI:

```bash
python -m pip install lumina-svg-core==0.3.3
```

See the [PyPI version page](https://pypi.org/project/lumina-svg-core/0.3.3/)
and [publishing workflow](https://github.com/lumina-layer-studio/lumina-svg-core/actions/workflows/publish.yml)
for the released files and their build/installation checks.

No C++ compiler, third-party checkout, manual patch, or local wheelhouse is
needed for a matching prebuilt wheel.
LunaSVG/PlutoVG and Clipper2 are compiled into the extension by maintainers.

Alternatively, download a matching wheel from
[GitHub Releases](https://github.com/lumina-layer-studio/lumina-svg-core/releases)
and install the downloaded file:

```bash
python -m pip install /path/to/lumina_svg_core-0.3.3-cp314-cp314-platform.whl
```

Replace the example filename with the actual downloaded wheel. Wheels are built
for Windows x86-64, macOS Apple Silicon, macOS Intel, and Linux x86-64. macOS
requires version 12 or newer for Python 3.12/3.13, and version 14 or newer for
Python 3.14. Linux wheels use `manylinux_2_28` and require glibc 2.28 or newer;
Alpine/musl, Windows ARM and free-threaded Python are not covered.
Match the wheel's OS, CPU and Python tags.

### Build the native library from source (contributors)

You need CPython 3.12–3.14, Git, and a C++17 compiler: MSVC on Windows, Apple Clang on
macOS, or GCC/Clang on Linux. The build installs its pinned Python build tools
and fetches pinned LunaSVG and Clipper2 source revisions, so network access is
required for a fresh build.

This is the library contributor workflow, not a prerequisite for running
Lumina Studio from its Python/TypeScript source.

```bash
git clone https://github.com/lumina-layer-studio/lumina-svg-core.git
cd lumina-svg-core
python3.12 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install .
```

On Windows, use `py -3.12 -m venv .venv` and activate it with
`.venv\Scripts\Activate.ps1` in PowerShell. Run the build from a Visual Studio
developer shell if MSVC is not already available on your command line.

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

# Coordinates are an integer working grid, not SVG units or millimetres.
coordinates_mm = (
    result.visible.coordinates.astype("float64")
    * result.grid_to_source_unit
    * result.scale_factor
)
```

To read a file, use
`lumina_svg_core.analyze_svg("drawing.svg", target_width_mm=40.0, tolerance_mm=0.02)`.
Paths may also be `pathlib.Path` instances. Both analysis entry points accept
`allow_strokes=False` to omit stroke paints while retaining fills.

Inspect the installed build with `lumina_svg_core.__version__`,
`lumina_svg_core.__source_revision__` and `lumina_svg_core.__source_dirty__`.
Git-free source builds report unknown provenance unless the builder supplies
it explicitly. [Distribution preparation](docs/DISTRIBUTIONS.md) describes
the build identity and checked file receipts.

See the [API reference](docs/api.md) for ring traversal, result statuses, source
identity, coordinate units, and the lower-level recording and grid APIs.

## SVG support

| Supported | Details |
| --- | --- |
| Solid fills and strokes | Fill rules, caps, joins, supported dash arrays, and non-scaling strokes |
| Shapes and paths | Rectangles, rounded rectangles, circles, ellipses, polygons, and SVG path curves/arcs |
| Layout | Affine transforms, viewBox origins, nested SVG viewports, and percentage lengths |
| References and styles | Supported CSS selectors and same-document `<use>` references |
| Clipping and markers | Vector clips, marker paints, and viewport clipping |
| Transparency | Solid-paint alpha and isolated object/group opacity with normal source-over compositing |

The SVG root begins transparent. The engine introduces no white background or
printer-material matte. Fully opaque documents use reverse-order occlusion;
translucent documents are partitioned and composited in premultiplied sRGB.

Masks, gradients, patterns, text, embedded images, external resources, non-normal
blend modes, non-sRGB interpolation, and other unsupported semantics return a
whole-document `unsupported` result. Zero-length dash entries are also
unsupported. See [support and numerical limits](docs/support.md).

The recorder refactor reduces upstream patch additions by 46.5% while preserving
the public API and geometry schema. Its prior comparison of 508 private SVGs at
220 mm / 0.02 mm with strokes enabled found the same 504 supported results and
four `native_geometry_error` failures in both builds, with zero output differences.
Those failures were not counted as passes. Version 0.3.3 addresses the
canonical-boundary arithmetic and incomplete noding mechanisms, without adding
printer policy or a fallback; see [the fix and validation scope](docs/support.md#canonical-boundary-fixes).

## Geometry contract

- `raw` contains area-bearing paint geometry before paint-order occlusion;
  `visible` contains the final non-overlapping painted regions; `silhouette`
  contains their combined footprint.
- Coordinates use an integer working grid with an explicit conversion factor.
  Arrays preserve polygon components, holes, colors, and paint provenance.
- `rgba_f64` is authoritative straight-sRGB RGBA in `[0, 1]`; `rgba` is a
  compatibility byte representation.
- Point-only, line-only, empty, and collapsed fragments are reported separately
  in `unprintable_fragments`. They are not fabricated into printable polygons.
- Unsupported or invalid documents are rejected as a whole. The standalone
  package does not retry them through another parser or silently discard
  unsupported elements.
- The parser performs no morphological closing, printer-grid snapping,
  minimum-area filtering, triangulation, or mesh repair.

Independent normalization work can run in parallel. Set
`LUMINA_SVG_REPLAY_THREADS` to a positive integer to select a worker count; the
engine caps it at 16. Parallel and serial replay preserve the same ordered
geometry and provenance.

## Development and validation

```bash
python -m pip install ".[test]" build
python -m pytest tests -q
python -m build
python tools/verify_wheel.py dist/<actual-wheel-filename>.whl --contents-only
```

CI builds and installs wheels for all three Python versions on all four platform
targets, runs the complete Python suite, checks binary/license contents, and
exercises installed extensions. A separate Linux job builds a wheel from the
source distribution and runs the native canonical-boundary regression.
See [CONTRIBUTING.md](CONTRIBUTING.md) for
the development workflow and [benchmarks/README.md](benchmarks/README.md) for
reproducible measurements.

Performance numbers depend on the input, compiler, CPU, and thread count. The
checked-in benchmark report describes its reference host; it is not a universal
latency guarantee.

## License and acknowledgements

The project is licensed under the **GNU Affero General Public License, version
3 only (`AGPL-3.0-only`)**. See [LICENSE](LICENSE) for the full terms.

The implementation uses pinned LunaSVG/PlutoVG and Clipper2 sources, pybind11
bindings, and adapted hole-selection criteria from ChromaPrint3D. Their
licenses and attributions remain separate and are documented in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and
[THIRD_PARTY_LICENSES/](THIRD_PARTY_LICENSES).

Report reproducible problems through
[GitHub Issues](https://github.com/lumina-layer-studio/lumina-svg-core/issues).
For sensitive reports, follow [SECURITY.md](SECURITY.md).

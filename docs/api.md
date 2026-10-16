# Python API reference

This reference describes `lumina-svg-core` 0.3.1 and result schema 4. The package
requires CPython 3.12–3.14 and NumPy 2.x.

`__version__`, `__source_revision__` and `__source_dirty__` identify the installed
build. Source revision is a full Git SHA or `"unknown"`; dirty status is a
boolean or `None` when unavailable. This is declared build provenance, separate
from geometry result/source identities. See [distribution receipts](DISTRIBUTIONS.md).

## Analyze an SVG

```python
analyze_svg(path, target_width_mm, tolerance_mm, allow_strokes=True)
analyze_svg_svg(svg, target_width_mm, tolerance_mm, allow_strokes=True)
```

`path` accepts a string or `pathlib.Path`; `svg` is an SVG XML string.
`target_width_mm` is the requested physical width. `tolerance_mm` is the total
physical error budget used for curve flattening and coordinate quantization.
Both must be finite and positive. `allow_strokes=False` excludes stroke paint
operations while retaining fill operations.

Both functions return `NativeResult`. Check `status` before using its geometry:

| Status | Meaning | Consumer action |
| --- | --- | --- |
| `supported` | Analysis completed within the supported semantics and numerical contract | Consume the returned collections |
| `unsupported` | SVG semantics or precision cannot be represented under this contract | Inspect `reason_code`; choose a deliberate alternative in the calling application |
| `invalid` | Invalid input or another failed analysis | Inspect `reason_code` and report or correct the input |

Do not infer printable geometry from an unsuccessful result. Invalid Python
argument types can also raise binding exceptions. The package supplies no
automatic alternative parser.

## Result collections and coordinates

`raw` is the collection of area-bearing paints before occlusion. `visible`
contains the non-overlapping final painted regions after clipping and
compositing. `silhouette` contains combined painted coverage.

`bbox` and `viewport_bounds` are `(x, y, width, height)` in SVG source units.
Coordinates are absolute in the source coordinate domain, with the SVG axis
orientation retained. Conversion to physical millimetres is:

```python
coordinates_mm = (
    collection.coordinates.astype("float64")
    * result.grid_to_source_unit
    * result.scale_factor
)
```

`grid_to_source_unit` converts one integer working-grid step to an SVG source
unit. `scale_factor` converts one source unit to millimetres. Apply any desired
origin translation or axis inversion in the caller. The working grid is not
the printer's extrusion grid.

`max_flattening_error_mm` and `max_quantization_error_mm` report numerical error
components. `schema_version` is the result contract version;
`input_paint_count` is the recorded paint census. Stage timings are reported in
seconds, and `replay_worker_count` reports the final geometry replay's worker
count.

The reported bounds concern boundary distance, not exact connectivity below
the numerical grid. Sub-grid topology and fragment-diagnostic limits are
described in [canonical boundaries](support.md#canonical-boundary-fixes).

## GeometryCollection

All exposed arrays are C-contiguous, read-only NumPy views. A view keeps its
underlying native result alive even when the original Python result variable is
deleted. Make an explicit copy before modifying data.

| Field | Type / shape | Meaning |
| --- | --- | --- |
| `coordinates` | `int64`, `(N, 2)` | Closed ring points on the working grid |
| `ring_offsets` | `uint64`, `(R + 1,)` | Point spans for each ring |
| `polygon_offsets` | `uint64`, `(P + 1,)` | Ring spans for each polygon |
| `geometry_offsets` | `uint64`, `(G + 1,)` | Polygon spans for each geometry |
| `hole_parent` | `int64`, `(R,)` | Shell ring index for a hole, or `-1` for a shell |
| `rgba_f64` | `float64`, `(G, 4)` | Authoritative straight-sRGB RGBA in `[0, 1]` |
| `rgba` | `uint8`, `(G, 4)` | Compatibility RGBA bytes |
| `draw_indices` | `int32`, `(G,)` | Stable recorded paint order, or `-1` for a composite |
| `source_indices` | `int32`, `(G,)` | Stable source element identity, or `-1` for a composite |
| `source_kinds` | `int32`, `(G,)` | Fill `0`, stroke `1`, silhouette `2`, or composite `3` |

`geometry_count`, `component_count`, and `hole_count` expose aggregate counts.
Offsets count points, rings, and polygons, never scalar coordinate values.
The first offset is zero; the final offset is the length of the referenced
collection.

To iterate the first visible geometry's polygon rings:

```python
visible = result.visible
if visible.geometry_count:
    polygon_start, polygon_end = map(int, visible.geometry_offsets[:2])
    for polygon_index in range(polygon_start, polygon_end):
        ring_start, ring_end = map(
            int, visible.polygon_offsets[polygon_index:polygon_index + 2]
        )
        for ring_index in range(ring_start, ring_end):
            point_start, point_end = map(
                int, visible.ring_offsets[ring_index:ring_index + 2]
            )
            ring = visible.coordinates[point_start:point_end]
            is_hole = visible.hole_parent[ring_index] >= 0
            print(is_hole, ring.shape)
```

### Composite provenance

A region assembled from several translucent paints has no fabricated single
source owner. Its draw/source indices are `-1`, and its source kind is
`composite`. The contributor arrays describe the full ordered provenance:

- `contributor_offsets`: `uint64` spans for each geometry.
- `contributor_draw_indices`, `contributor_source_indices`, and
  `contributor_source_kinds`: `int32` identities in bottom-to-top paint order.
- `contributor_weights`: `float64` premultiplied source contributions. Their
  sum equals the resulting geometry alpha.

## Non-area fragments

`unprintable_fragments` reports excluded empty, point-only, line-only,
area-collapsed, fill-rule-cancelled, clipped-out, and stroke-collapsed fragments.
It provides `count`, `reason_codes`, `reason_ids`, RGBA, draw/source identity,
source kinds, and subpath indices. These diagnostics do not turn non-area input
into polygons or require otherwise supported paints to fail.

## Paint recording

```python
record_paint_commands(path)
record_paint_commands_svg(svg)
```

These return `RecorderResult`: a status/reason, recorder schema version,
bounds, viewport, and ordered commands. Commands contain copied path verbs and
points, resolved solid paint, affine transforms, fill/stroke styles, clip/group
boundaries, and source identity. They expose no private LunaSVG scene-tree
objects. Use the analysis functions when you need final polygon geometry.

## Integer-grid utilities

```python
project_regions_to_grid(regions, units_per_coordinate)
canonicalize_grid_regions(regions)
```

Each input region is a sequence of rings, and each ring is a sequence of `(x,
y)` pairs. Both return, per input region, polygons expressed as `(shell, holes)`.

`project_regions_to_grid` scales and rounds source coordinates onto an integer
target grid, resolves winding with Clipper2, and exports canonical faces. Ties
round away from zero.

`canonicalize_grid_regions` accepts already-valid integer-grid coverage with
positive shells and negative holes. It performs no scaling, rounding, or second
union; paint ownership and overlaps must already have been resolved. It is not
a replacement for projection or Boolean cleanup.

## Bundled internal extension

Wheels also contain `_lumina_svg_compensation`, used for enclosed-hole
selection. It is an internal extension and is not exported by the package's
top-level public API. Its adapted algorithm and attribution are described in
[THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).

"""Stroke, clip, source-order occlusion, and silhouette semantics."""

from __future__ import annotations

from fractions import Fraction
import json
from pathlib import Path

import numpy as np
import pytest
import shapely
from shapely.geometry import LineString, MultiLineString, Polygon, box
from shapely.affinity import translate

import lumina_svg_core


FIXTURES = Path(__file__).resolve().parent / "fixtures"


def geometry_rings(result, collection, geometry_index: int):
    polygon_start = collection.geometry_offsets[geometry_index]
    polygon_end = collection.geometry_offsets[geometry_index + 1]
    for polygon_index in range(polygon_start, polygon_end):
        ring_start = collection.polygon_offsets[polygon_index]
        ring_end = collection.polygon_offsets[polygon_index + 1]
        for ring_index in range(ring_start, ring_end):
            point_start = collection.ring_offsets[ring_index]
            point_end = collection.ring_offsets[ring_index + 1]
            yield (
                collection.coordinates[point_start:point_end].astype(np.float64)
                * result.grid_to_source_unit
            )


def geometry_area(result, collection, geometry_index: int) -> float:
    area = 0.0
    for ring in geometry_rings(result, collection, geometry_index):
        signed = 0.5 * np.sum(
            ring[:-1, 0] * ring[1:, 1]
            - ring[1:, 0] * ring[:-1, 1]
        )
        area += signed
    return abs(area)


def geometry_bounds(result, collection, geometry_index: int):
    points = np.concatenate(
        list(geometry_rings(result, collection, geometry_index)), axis=0
    )
    return (
        points[:, 0].min(),
        points[:, 1].min(),
        points[:, 0].max(),
        points[:, 1].max(),
    )


def geometry_shape(result, collection, geometry_index: int):
    geometries = shapely.from_ragged_array(
        shapely.GeometryType.MULTIPOLYGON,
        collection.coordinates.astype(np.float64)
        * result.grid_to_source_unit,
        (
            collection.ring_offsets,
            collection.polygon_offsets,
            collection.geometry_offsets,
        ),
    )
    return geometries[geometry_index]


def geometry_contributors(collection, geometry_index: int):
    start = collection.contributor_offsets[geometry_index]
    end = collection.contributor_offsets[geometry_index + 1]
    return [
        (
            int(collection.contributor_draw_indices[index]),
            int(collection.contributor_source_indices[index]),
            int(collection.contributor_source_kinds[index]),
            float(collection.contributor_weights[index]),
        )
        for index in range(start, end)
    ]


def geometry_index_with_bounds(result, collection, expected_bounds):
    matches = [
        index
        for index in range(collection.geometry_count)
        if geometry_bounds(result, collection, index)
        == pytest.approx(expected_bounds, abs=0.02)
    ]
    assert len(matches) == 1
    return matches[0]


def assert_valid_integer_components(collection) -> list[int]:
    """Check topology and return exact signed doubled component areas."""
    shapes = shapely.from_ragged_array(
        shapely.GeometryType.MULTIPOLYGON,
        collection.coordinates.astype(np.float64),
        (
            collection.ring_offsets,
            collection.polygon_offsets,
            collection.geometry_offsets,
        ),
    )
    for shape in shapes:
        assert shape.is_valid, shapely.is_valid_reason(shape)
        polygons = list(shape.geoms)
        assert all(polygon.is_valid for polygon in polygons)
        for index, polygon in enumerate(polygons):
            for other in polygons[index + 1:]:
                assert polygon.intersection(other).area == 0.0

    component_areas = []
    for start, end in zip(
        collection.polygon_offsets[:-1],
        collection.polygon_offsets[1:],
        strict=True,
    ):
        shell_index = int(start)
        twice_component_area = 0
        for ring_index in range(shell_index, int(end)):
            point_start = int(collection.ring_offsets[ring_index])
            point_end = int(collection.ring_offsets[ring_index + 1])
            points = collection.coordinates[point_start:point_end].tolist()
            twice_ring_area = sum(
                first[0] * second[1] - first[1] * second[0]
                for first, second in zip(points, points[1:])
            )
            if ring_index == shell_index:
                assert twice_ring_area > 0
                assert collection.hole_parent[ring_index] == -1
            else:
                assert twice_ring_area < 0
                assert collection.hole_parent[ring_index] == shell_index
            twice_component_area += twice_ring_area
        assert twice_component_area > 0
        component_areas.append(twice_component_area)
    return component_areas


def _exact_ring_winding(
    points: list[list[int]], probe: tuple[Fraction, Fraction],
) -> int:
    winding = 0
    for first, last in zip(points, points[1:]):
        cross = ((last[0] - first[0]) * (probe[1] - first[1])
                 - (last[1] - first[1]) * (probe[0] - first[0]))
        if cross == 0:
            assert not (
                min(first[0], last[0]) <= probe[0] <= max(first[0], last[0])
                and min(first[1], last[1]) <= probe[1] <= max(first[1], last[1])
            ), "probe lies on a ring boundary"
        if first[1] <= probe[1] < last[1] and cross > 0:
            winding += 1
        if last[1] <= probe[1] < first[1] and cross < 0:
            winding -= 1
    return winding


@pytest.mark.parametrize("opacity", [1.0, 0.5])
def test_export_preserves_positive_integer_components_from_one_fill(
    opacity: float,
) -> None:
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 100 100'>"
        f"<path fill='#123456' fill-opacity='{opacity}' "
        "transform='matrix(1 65536 0 1 0 0)' "
        "d='M10 0h1v.0000152587890625h-1Z M20 20h10v10h-10Z'/>"
        "</svg>",
        target_width_mm=100.0,
        tolerance_mm=0.02,
    )

    assert result.status == "supported", result.reason_code
    assert result.reason_code == ""
    assert result.unprintable_fragments.count == 0
    grid_mm = result.grid_to_source_unit * result.scale_factor
    perimeter_mm = 20.0 * np.hypot(1.0, 65536.0) + 20.0
    error_mm = result.max_quantization_error_mm
    area_error_mm2 = perimeter_mm * error_mm + np.pi * error_mm ** 2
    for name in ("raw", "visible", "silhouette"):
        collection = getattr(result, name)
        assert collection.component_count == 2
        assert collection.hole_count == 0
        exact_component_areas = assert_valid_integer_components(collection)
        coarse_area_mm2 = max(exact_component_areas) * 0.5 * grid_mm ** 2
        assert coarse_area_mm2 == pytest.approx(100.0, abs=area_error_mm2)
        count = collection.geometry_count
        np.testing.assert_array_equal(collection.draw_indices, [0] * count)
        if name == "silhouette":
            assert count == 1
            np.testing.assert_array_equal(collection.source_indices, [-1])
            np.testing.assert_array_equal(collection.source_kinds, [2])
            np.testing.assert_array_equal(collection.rgba, [[0, 0, 0, 255]])
            np.testing.assert_array_equal(collection.rgba_f64, [[0, 0, 0, 1]])
            np.testing.assert_array_equal(collection.contributor_offsets, [0, 0])
            assert collection.contributor_draw_indices.size == 0
            assert collection.contributor_source_indices.size == 0
            assert collection.contributor_source_kinds.size == 0
            assert collection.contributor_weights.size == 0
        else:
            assert count >= 1
            np.testing.assert_array_equal(collection.source_indices, [1] * count)
            np.testing.assert_array_equal(collection.source_kinds, [0] * count)
            np.testing.assert_array_equal(
                collection.rgba,
                [[18, 52, 86, round(255 * opacity)]] * count,
            )
            np.testing.assert_allclose(
                collection.rgba_f64,
                [[18 / 255, 52 / 255, 86 / 255, opacity]] * count,
                rtol=0.0,
                atol=1e-15,
            )
            for index in range(count):
                assert geometry_contributors(collection, index) == [
                    (0, 1, 0, opacity)
                ]


@pytest.mark.parametrize("opacity", [1.0, 0.5, 0.25])
def test_nearly_singular_fill_has_nonoverlapping_shell_hierarchy(
    opacity: float,
) -> None:
    result = lumina_svg_core.analyze_svg_svg(
        '<svg xmlns="http://www.w3.org/2000/svg" '
        'viewBox="0 0 100 100" width="100" height="100">'
        '<path transform="matrix(1 1 0 0.0000000001 50 50)" '
        f'fill="#ff0000" fill-opacity="{opacity}" '
        'd="M7 -1L-6 -3L-5 1L6 4L6 -6L-3 2L-1 -1L10 100000000000Z"/>'
        '</svg>',
        target_width_mm=100.0,
        tolerance_mm=0.02,
        allow_strokes=True,
    )

    assert result.status == "supported", result.reason_code
    assert result.reason_code == ""
    assert result.unprintable_fragments.count == 0
    grid_step_mm = result.grid_to_source_unit * result.scale_factor
    source_points = [
        [7, -1], [-6, -3], [-5, 1], [6, 4], [6, -6], [-3, 2],
        [-1, -1], [10, 100000000000],
    ]
    # Exact authored NonZero area: -signed area - the winding -2 face area
    # + twice the winding +1 triangle area. Independent rational face splitting
    # and a vertical sweep agree; the transform determinant is 1/10**10.
    source_area = Fraction(
        358147636428455151200902155802872784667,
        895369090987684418179888164,
    )
    scale = Fraction.from_float(result.scale_factor)
    source_area_mm2 = source_area / 10**10 * scale ** 2
    transformed_source = [
        (Fraction(x + 50), Fraction(x + 50) + Fraction(y, 10**10))
        for x, y in source_points
    ]
    # L1 bounds the eight segment lengths (120.000000001 source units).
    # Their radius-r capsules bound changed area by 2*r*sum(lengths)+N*pi*r²;
    # use pi <= 4, including the reported source/flattening boundary error.
    perimeter_l1_mm = scale * sum(
        abs(b[0] - a[0]) + abs(b[1] - a[1])
        for a, b in zip(transformed_source,
                        transformed_source[1:] + transformed_source[:1],
                        strict=True)
    )
    radius_mm = (Fraction.from_float(result.max_flattening_error_mm)
                 + Fraction.from_float(result.max_quantization_error_mm))
    area_error_mm2 = (2 * radius_mm * perimeter_l1_mm
                      + 4 * len(source_points) * radius_mm ** 2)
    # The independent exact probe is inside the shell and its negative ring.
    # A second export union used to fill this originally empty point.
    hole_probe = (Fraction(1454000000002, 3), Fraction(1454000000002, 3))
    source_probe = (hole_probe[0] / 10**10 - 50, Fraction(0))
    assert _exact_ring_winding(source_points + source_points[:1], source_probe) == 0
    for name in ("raw", "visible", "silhouette"):
        collection = getattr(result, name)
        assert collection.geometry_count == 1
        assert collection.component_count == 1
        assert collection.hole_count == 1
        component_area2 = assert_valid_integer_components(collection)[0]
        actual_area_mm2 = (Fraction(component_area2, 2)
                           * Fraction.from_float(grid_step_mm) ** 2)
        assert abs(actual_area_mm2 - source_area_mm2) <= area_error_mm2
        ring_windings = []
        for start, end in zip(collection.ring_offsets[:-1],
                              collection.ring_offsets[1:], strict=True):
            points = collection.coordinates[int(start):int(end)].tolist()
            ring_windings.append(_exact_ring_winding(points, hole_probe))
        assert ring_windings == [1, -1]
        assert sum(ring_windings) == 0
        hole = collection.coordinates[int(collection.ring_offsets[1]):].tolist()
        assert sum(a[0] * b[1] - a[1] * b[0]
                   for a, b in zip(hole, hole[1:])) == -16000000001
        assert collection.draw_indices.tolist() == [0]
        if name == "silhouette":
            assert collection.source_indices.tolist() == [-1]
            assert collection.source_kinds.tolist() == [2]
            assert collection.rgba.tolist() == [[0, 0, 0, 255]]
            assert collection.rgba_f64[0] == pytest.approx((0, 0, 0, 1))
            assert collection.contributor_offsets.tolist() == [0, 0]
        else:
            assert collection.source_indices.tolist() == [1]
            assert collection.source_kinds.tolist() == [0]
            assert collection.rgba.tolist() == [
                [255, 0, 0, round(255 * opacity)]
            ]
            assert collection.rgba_f64[0] == pytest.approx(
                (1, 0, 0, opacity), abs=1e-15,
            )
            assert geometry_contributors(collection, 0) == [(0, 1, 0, opacity)]
    # The existing noder routes this normalized contact once. The redundant
    # export union filled the empty probe below and hid that routing.
    assert result.max_quantization_error_mm == pytest.approx(
        (1.0 / np.sqrt(2.0) + 1.0) * grid_step_mm,
        rel=1e-15,
        abs=0.0,
    )
    assert (result.max_flattening_error_mm
            + result.max_quantization_error_mm) <= 0.6 * 0.02


@pytest.mark.parametrize("opacity", [1.0, 0.5, 0.25])
def test_self_touch_and_t_contact_keep_independent_shells_and_holes(
    opacity: float,
) -> None:
    # Two holed squares share one corner; a triangle meets an edge interior.
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 60 60'>"
        f"<path fill='red' fill-opacity='{opacity}' "
        "d='M10 10H30V30H10Z M15 15V20H20V15Z "
        "M30 30H50V50H30Z M35 35V40H40V35Z "
        "M20 10L15 0L25 0Z'/></svg>",
        target_width_mm=60.0,
        tolerance_mm=0.02,
    )

    assert result.status == "supported", result.reason_code
    assert result.reason_code == ""
    assert result.unprintable_fragments.count == 0
    assert (result.max_flattening_error_mm
            + result.max_quantization_error_mm) <= 0.6 * 0.02
    for name in ("raw", "visible", "silhouette"):
        collection = getattr(result, name)
        assert_valid_integer_components(collection)
        assert collection.geometry_count == 1
        assert collection.component_count == 3
        assert collection.hole_count == 2
        shape = geometry_shape(result, collection, 0)
        assert sorted(polygon.area for polygon in shape.geoms) == pytest.approx(
            [50.0, 375.0, 375.0], abs=1e-8,
        )
        assert collection.draw_indices.tolist() == [0]
        if name == "silhouette":
            assert collection.source_indices.tolist() == [-1]
            assert collection.source_kinds.tolist() == [2]
            assert collection.rgba_f64[0] == pytest.approx((0, 0, 0, 1))
        else:
            assert collection.source_indices.tolist() == [1]
            assert collection.source_kinds.tolist() == [0]
            assert collection.rgba_f64[0] == pytest.approx(
                (1, 0, 0, opacity), abs=1e-15,
            )
            assert geometry_contributors(collection, 0) == [(0, 1, 0, opacity)]


def test_transparent_canvas_source_over_reference_contract() -> None:
    reference = json.loads(
        (FIXTURES / "alpha_source_over_reference.json").read_text(
            encoding="utf-8"
        )
    )
    result = lumina_svg_core.analyze_svg(
        FIXTURES / "alpha_source_over.svg",
        reference["render"]["target_width_mm"],
        reference["render"]["tolerance_mm"],
    )

    assert result.status == "supported"
    assert result.visible.geometry_count == len(reference["regions"])
    for expected in reference["regions"]:
        matches = [
            index
            for index, rgba in enumerate(result.visible.rgba_f64)
            if np.allclose(rgba, expected["rgba_f64"], rtol=0.0, atol=1e-15)
        ]
        assert len(matches) == 1
        index = matches[0]
        assert result.visible.rgba[index].tolist() == expected["rgba_u8"]
        assert geometry_area(result, result.visible, index) == pytest.approx(
            expected["area_user_units_squared"], abs=1e-9
        )

    silhouette_area = geometry_area(result, result.silhouette, 0)
    canvas = reference["canvas"]
    canvas_area = (
        canvas["width_user_units"] * canvas["height_user_units"]
    )
    assert canvas_area - silhouette_area == pytest.approx(
        reference["transparent_area_user_units_squared"], abs=1e-9
    )


def _cubic_point(
    start: np.ndarray,
    control1: np.ndarray,
    control2: np.ndarray,
    end: np.ndarray,
    parameter: np.ndarray,
) -> np.ndarray:
    complement = 1.0 - parameter
    return (
        complement**3 * start
        + 3.0 * complement**2 * parameter * control1
        + 3.0 * complement * parameter**2 * control2
        + parameter**3 * end
    )


def test_stroke_expands_before_full_affine_transform() -> None:
    result = lumina_svg_core.analyze_svg(
        FIXTURES / "stroke_only_affine.svg", 60.0, 0.02
    )

    assert result.status == "supported"
    assert result.bbox == pytest.approx((0.0, 0.0, 100.0, 60.0), abs=0.01)
    assert result.raw.source_kinds.tolist() == [1]
    assert geometry_bounds(result, result.raw, 0) == pytest.approx(
        (20.0, 35.0, 80.0, 45.0), abs=0.03
    )


def test_clip_and_later_paint_define_visible_geometry() -> None:
    result = lumina_svg_core.analyze_svg(
        FIXTURES / "strokes_clips_occlusion.svg", 100.0, 0.02
    )

    assert result.status == "supported"
    assert result.raw.geometry_count == 4
    assert result.visible.geometry_count == 4
    assert result.raw.rgba.tolist() == [
        [239, 83, 80, 255],
        [30, 136, 229, 255],
        [67, 160, 71, 255],
        [142, 36, 170, 255],
    ]
    assert geometry_area(result, result.raw, 1) == pytest.approx(900.0, abs=0.1)
    assert geometry_area(result, result.visible, 1) == pytest.approx(550.0, abs=0.1)
    assert geometry_bounds(result, result.raw, 3) == pytest.approx(
        (20.0, 35.0, 80.0, 45.0), abs=0.03
    )
    assert result.silhouette.component_count == 1
    assert result.silhouette.hole_count == 0


@pytest.mark.parametrize(
    ("overflow", "expected_maximum"),
    [("", 12.0), ("hidden", 12.0), ("visible", 20.0)],
    ids=["default-hidden", "explicit-hidden", "visible"],
)
def test_marker_viewport_overflow_is_recorded_as_geometry_clip(
    overflow: str,
    expected_maximum: float,
) -> None:
    overflow_attribute = f" overflow='{overflow}'" if overflow else ""
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 30 20'><defs>"
        "<marker id='m' markerWidth='2' markerHeight='2' refX='0' refY='0'"
        f"{overflow_attribute}>"
        "<rect x='0' y='0' width='10' height='10' fill='red'/>"
        "</marker></defs>"
        "<path d='M1 10L10 10' fill='none' stroke='none' "
        "marker-end='url(#m)'/></svg>",
        target_width_mm=30.0,
        tolerance_mm=0.02,
    )

    assert result.status == "supported"
    assert result.raw.geometry_count == 1
    assert geometry_bounds(result, result.raw, 0) == pytest.approx(
        (10.0, 10.0, expected_maximum, expected_maximum), abs=0.02
    )


def test_auto_marker_direction_precision_never_silently_exceeds_budget() -> None:
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 1000000 1000001'><defs>"
        "<marker id='m' markerUnits='userSpaceOnUse' markerWidth='1000000' "
        "markerHeight='10' refX='0' refY='5' orient='auto' "
        "overflow='visible'>"
        "<path d='M0 0L1000000 5L0 10Z' fill='blue'/>"
        "</marker></defs>"
        "<path d='M10 1000000L20 1000000.003' fill='none' stroke='none' "
        "marker-end='url(#m)'/></svg>",
        target_width_mm=100.0,
        tolerance_mm=0.02,
    )

    assert result.status == "unsupported"
    assert result.reason_code == "unsupported_input_precision"


@pytest.mark.parametrize(
    "source_geometry",
    [
        "<line x1='10' y1='1000000' x2='20' y2='1000000.003'/>",
        "<polyline points='10 1000000 20 1000000.003'/>",
    ],
    ids=["line", "polyline"],
)
def test_primitive_auto_marker_direction_precision_fails_closed(
    source_geometry: str,
) -> None:
    source_geometry = source_geometry[:-2] + (
        " fill='none' stroke='none' marker-end='url(#m)'/>"
    )
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 1000000 1000001'><defs>"
        "<marker id='m' markerUnits='userSpaceOnUse' markerWidth='1000000' "
        "markerHeight='10' refX='0' refY='5' orient='auto' "
        "overflow='visible'>"
        "<path d='M0 0L1000000 5L0 10Z' fill='blue'/>"
        "</marker></defs>"
        f"{source_geometry}</svg>",
        target_width_mm=100.0,
        tolerance_mm=0.02,
    )

    assert result.status == "unsupported"
    assert result.reason_code == "unsupported_input_precision"


def test_exact_shared_boundary_does_not_split_the_silhouette() -> None:
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 9 5'>"
        "<rect x='.1' width='5' height='5' fill='red'/>"
        "<path d='M5.1 0H9V5H5.1Z' fill='blue'/></svg>",
        target_width_mm=90.0,
        tolerance_mm=0.02,
    )

    assert result.status == "supported"
    assert result.silhouette.component_count == 1
    assert geometry_area(result, result.silhouette, 0) == pytest.approx(
        44.5, abs=1e-7
    )


@pytest.mark.parametrize("width", ["51%", "5.1px"])
def test_resolved_length_shared_boundary_does_not_split_silhouette(
    width: str,
) -> None:
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 10 5'>"
        f"<rect width='{width}' height='5' fill='red'/>"
        "<path d='M5.1 0H9V5H5.1Z' fill='blue'/></svg>",
        target_width_mm=100.0,
        tolerance_mm=0.02,
    )

    assert result.status == "supported"
    assert result.reason_code == ""
    assert result.silhouette.component_count == 1
    assert geometry_area(result, result.silhouette, 0) == pytest.approx(
        45.0, abs=1e-7
    )


def test_percentage_ellipse_radii_follow_viewport_axes() -> None:
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 200 100'>"
        "<ellipse cx='100' cy='50' rx='50%' ry='50%' fill='red'/>"
        "</svg>",
        target_width_mm=200.0,
        tolerance_mm=0.02,
    )

    assert result.status == "supported"
    assert result.raw.geometry_count == 1
    assert geometry_bounds(result, result.raw, 0) == pytest.approx(
        (0.0, 0.0, 200.0, 100.0), abs=0.02
    )


def test_short_stroke_subpath_is_reported_without_losing_valid_stroke() -> None:
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 10 10'>"
        "<path d='M1 5H9 M1 7h1e-14' fill='none' stroke='blue' "
        "stroke-width='1' stroke-linecap='butt'/></svg>",
        target_width_mm=10.0,
        tolerance_mm=0.02,
    )

    assert result.status == "supported"
    assert result.raw.geometry_count == 1
    assert result.unprintable_fragments.reason_codes == ("stroke_collapse",)
    assert result.unprintable_fragments.subpath_indices.tolist() == [1]


def test_partial_paint_alpha_is_composited_over_a_transparent_root() -> None:
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 15 10'>"
        "<rect width='10' height='10' fill='rgba(255,0,0,.5)'/>"
        "<rect x='5' width='10' height='10' fill='rgba(0,0,255,.5)'/>"
        "</svg>",
        target_width_mm=15.0,
        tolerance_mm=0.02,
    )

    assert result.status == "supported"
    assert result.reason_code == ""
    assert result.input_paint_count == 2
    assert result.raw.geometry_count == 2
    assert result.visible.geometry_count == 3

    red_index = geometry_index_with_bounds(
        result, result.visible, (0.0, 0.0, 5.0, 10.0)
    )
    overlap_index = geometry_index_with_bounds(
        result, result.visible, (5.0, 0.0, 10.0, 10.0)
    )
    blue_index = geometry_index_with_bounds(
        result, result.visible, (10.0, 0.0, 15.0, 10.0)
    )
    assert result.visible.rgba_f64[red_index] == pytest.approx(
        (1.0, 0.0, 0.0, 0.5), abs=1e-15
    )
    assert result.visible.rgba_f64[overlap_index] == pytest.approx(
        (1 / 3, 0.0, 2 / 3, 0.75), abs=1e-15
    )
    assert result.visible.rgba_f64[blue_index] == pytest.approx(
        (0.0, 0.0, 1.0, 0.5), abs=1e-15
    )
    assert geometry_contributors(result.visible, red_index) == [
        (0, int(result.raw.source_indices[0]), 0, 0.5)
    ]
    assert geometry_contributors(result.visible, overlap_index) == [
        (0, int(result.raw.source_indices[0]), 0, 0.25),
        (1, int(result.raw.source_indices[1]), 0, 0.5),
    ]
    assert geometry_contributors(result.visible, blue_index) == [
        (1, int(result.raw.source_indices[1]), 0, 0.5)
    ]
    assert result.visible.source_kinds[overlap_index] == 3
    assert result.visible.draw_indices[overlap_index] == -1
    assert result.visible.source_indices[overlap_index] == -1
    assert result.silhouette.component_count == 1
    assert geometry_area(result, result.silhouette, 0) == pytest.approx(
        150.0, abs=0.1
    )


@pytest.mark.parametrize(
    ("paint", "expected_alpha"),
    [
        ("fill='red' fill-opacity='0.99999999'", 0.99999999),
        ("fill='red' fill-opacity='1e-50'", 1e-50),
        ("fill='rgba(1,2,3,0.99999999)'", 0.99999999),
        ("fill='rgba(1,2,3,1e-50)'", 1e-50),
        ("fill='RGBA(1,2,3,0.99999999)'", 0.99999999),
        ("fill='rGbA(1,2,3,1e-50)'", 1e-50),
        (
            "color='rgba(1,2,3,0.99999999)' fill='currentColor'",
            0.99999999,
        ),
    ],
)
def test_source_partial_alpha_near_endpoints_remains_exact(
    paint: str, expected_alpha: float
) -> None:
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 10 10'>"
        f"<rect width='10' height='10' {paint}/>"
        "</svg>",
        target_width_mm=10.0,
        tolerance_mm=0.02,
    )

    assert result.status == "supported"
    assert result.reason_code == ""
    assert result.raw.geometry_count == 1
    assert result.visible.geometry_count == 1
    assert result.raw.rgba_f64[0, 3] == pytest.approx(
        expected_alpha, rel=0, abs=1e-60
    )
    assert result.visible.rgba_f64[0, 3] == pytest.approx(
        expected_alpha, rel=0, abs=1e-60
    )
    assert result.visible.contributor_weights.tolist() == pytest.approx(
        [expected_alpha], rel=0, abs=1e-60
    )


def test_many_disjoint_translucent_paints_preserve_every_region() -> None:
    paint_count = 14_200
    columns = 120
    rows = (paint_count + columns - 1) // columns
    rectangles = "".join(
        "<rect "
        f"x='{2 * (index % columns)}' y='{2 * (index // columns)}' "
        "width='1' height='1' fill='red' "
        f"fill-opacity='{(index + 1) / (paint_count + 1):.12f}'/>"
        for index in range(paint_count)
    )
    result = lumina_svg_core.analyze_svg_svg(
        f"<svg viewBox='0 0 {2 * columns} {2 * rows}'>{rectangles}</svg>",
        target_width_mm=240.0,
        tolerance_mm=0.02,
    )

    assert result.status == "supported"
    assert result.reason_code == ""
    assert result.raw.geometry_count == paint_count
    assert result.visible.geometry_count == paint_count


def test_complex_translucent_component_keeps_holes_after_spatial_partition() -> None:
    holes = " ".join(
        f"M{1 + column * 5} {1 + row * 5}h1v1h-1Z"
        for row in range(20)
        for column in range(20)
    )
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 100 100'>"
        f"<path d='M0 0H100V100H0Z {holes}' fill='red' "
        "fill-rule='evenodd' fill-opacity='.5'/>"
        "<rect x='50' width='50' height='100' fill='blue' "
        "fill-opacity='.5'/></svg>",
        target_width_mm=100.0,
        tolerance_mm=0.02,
    )

    assert result.status == "supported"
    assert result.raw.geometry_count == 2
    assert result.raw.hole_count == 400
    assert result.visible.geometry_count == 3
    assert geometry_area(result, result.silhouette, 0) == pytest.approx(
        9_800.0, abs=0.1
    )
    visible_area = sum(
        geometry_area(result, result.visible, index)
        for index in range(result.visible.geometry_count)
    )
    assert visible_area == pytest.approx(9_800.0, abs=0.1)


def test_group_opacity_is_applied_after_children_are_composited() -> None:
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 15 10'><g opacity='.5'>"
        "<rect width='10' height='10' fill='red'/>"
        "<rect x='5' width='10' height='10' fill='blue'/>"
        "</g></svg>",
        target_width_mm=15.0,
        tolerance_mm=0.02,
    )

    assert result.status == "supported"
    assert result.raw.geometry_count == 2
    assert result.visible.geometry_count == 2
    red_index = geometry_index_with_bounds(
        result, result.visible, (0.0, 0.0, 5.0, 10.0)
    )
    blue_index = geometry_index_with_bounds(
        result, result.visible, (5.0, 0.0, 15.0, 10.0)
    )
    assert result.visible.rgba_f64[red_index] == pytest.approx(
        (1.0, 0.0, 0.0, 0.5), abs=1e-15
    )
    assert result.visible.rgba_f64[blue_index] == pytest.approx(
        (0.0, 0.0, 1.0, 0.5), abs=1e-15
    )
    assert geometry_contributors(result.visible, red_index) == [
        (0, int(result.raw.source_indices[0]), 0, 0.5)
    ]
    assert geometry_contributors(result.visible, blue_index) == [
        (1, int(result.raw.source_indices[1]), 0, 0.5)
    ]


def test_isolated_group_is_composited_once_over_earlier_content() -> None:
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 10 10'>"
        "<rect width='10' height='10' fill='rgb(0,255,0)'/>"
        "<g opacity='.5'>"
        "<rect width='10' height='10' fill='red'/>"
        "<rect x='5' width='5' height='10' fill='blue'/>"
        "</g></svg>",
        target_width_mm=10.0,
        tolerance_mm=0.02,
    )

    assert result.status == "supported"
    assert result.visible.geometry_count == 2
    left_index = geometry_index_with_bounds(
        result, result.visible, (0.0, 0.0, 5.0, 10.0)
    )
    right_index = geometry_index_with_bounds(
        result, result.visible, (5.0, 0.0, 10.0, 10.0)
    )
    assert result.visible.rgba_f64[left_index] == pytest.approx(
        (0.5, 0.5, 0.0, 1.0), abs=1e-15
    )
    assert result.visible.rgba_f64[right_index] == pytest.approx(
        (0.0, 0.5, 0.5, 1.0), abs=1e-15
    )
    contributor_weights = [
        [contributor[3] for contributor in geometry_contributors(
            result.visible, index
        )]
        for index in (left_index, right_index)
    ]
    assert contributor_weights[0] == pytest.approx([0.5, 0.5])
    assert contributor_weights[1] == pytest.approx([0.5, 0.5])


def test_leaf_opacity_isolates_fill_and_stroke_before_fading_object() -> None:
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 10 10'>"
        "<rect x='2' y='2' width='6' height='6' fill='red' stroke='blue' "
        "stroke-width='2' opacity='.5'/></svg>",
        target_width_mm=10.0,
        tolerance_mm=0.02,
    )

    assert result.status == "supported"
    assert result.raw.geometry_count == 2
    assert result.visible.geometry_count == 2
    assert sorted(result.visible.rgba_f64[:, 3].tolist()) == pytest.approx(
        [0.5, 0.5], abs=1e-15
    )
    assert {
        tuple(color)
        for color in result.visible.rgba_f64[:, :3]
    } == {(1.0, 0.0, 0.0), (0.0, 0.0, 1.0)}
    assert all(
        len(geometry_contributors(result.visible, index)) == 1
        for index in range(result.visible.geometry_count)
    )


def test_nested_group_opacity_and_clip_preserve_hole_hierarchy() -> None:
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 20 20'><defs>"
        "<clipPath id='ring' clip-rule='evenodd'>"
        "<path d='M1 1H19V19H1Z M6 6H14V14H6Z'/>"
        "</clipPath></defs>"
        "<g opacity='.5' clip-path='url(#ring)'><g opacity='.5'>"
        "<rect width='20' height='20' fill='rgb(10,20,30)'/>"
        "</g></g></svg>",
        target_width_mm=20.0,
        tolerance_mm=0.02,
    )

    assert result.status == "supported"
    assert result.visible.geometry_count == 1
    assert result.visible.component_count == 1
    assert result.visible.hole_count == 1
    assert result.visible.rgba_f64[0] == pytest.approx(
        (10 / 255, 20 / 255, 30 / 255, 0.25), abs=1e-15
    )
    assert result.visible.contributor_weights.tolist() == pytest.approx(
        [0.25], abs=1e-15
    )
    assert geometry_area(result, result.visible, 0) == pytest.approx(
        260.0, abs=0.1
    )


def test_dash_array_is_applied_in_local_arclength() -> None:
    result = lumina_svg_core.analyze_svg(
        FIXTURES / "dashed_stroke.svg", 40.0, 0.02
    )

    assert result.status == "supported"
    assert result.raw.geometry_count == 1
    assert result.raw.component_count == 2
    assert geometry_area(result, result.raw, 0) == pytest.approx(80.0, abs=0.1)


@pytest.mark.parametrize("unit_scale", [1e-17, 1.0, 1e17])
def test_dashed_stroke_is_invariant_across_user_unit_scales(
    unit_scale: float,
) -> None:
    result = lumina_svg_core.analyze_svg_svg(
        f"<svg viewBox='0 0 {unit_scale:g} {unit_scale:g}'>"
        f"<path d='M0 {0.5 * unit_scale:g}H{unit_scale:g}' fill='none' "
        f"stroke='#000' stroke-width='{0.1 * unit_scale:g}' "
        f"stroke-linecap='butt' stroke-dasharray="
        f"'{0.2 * unit_scale:g} {0.2 * unit_scale:g}'/>"
        "</svg>",
        target_width_mm=100.0,
        tolerance_mm=0.02,
    )

    assert result.status == "supported"
    assert result.raw.component_count == 3
    physical_area = (
        geometry_area(result, result.raw, 0) * result.scale_factor**2
    )
    assert physical_area == pytest.approx(600.0, abs=0.2)


@pytest.mark.parametrize("unit_scale", [1.0, 1e-2, 1e-4, 1e-6, 1e-17])
def test_round_stroke_bbox_contains_geometry_at_any_user_unit_scale(
    unit_scale: float,
) -> None:
    result = lumina_svg_core.analyze_svg_svg(
        "<svg>"
        f"<path d='M0 0L{unit_scale:g} 0' fill='none' stroke='red' "
        f"stroke-width='{0.2 * unit_scale:g}' stroke-linecap='round'/>"
        "</svg>",
        target_width_mm=100.0,
        tolerance_mm=0.02,
    )

    assert result.status == "supported"
    min_x, min_y, max_x, max_y = geometry_bounds(result, result.raw, 0)
    source_budget = 0.02 / result.scale_factor
    assert min_x >= -source_budget
    assert min_y >= -source_budget
    assert max_x <= result.bbox[2] + source_budget
    assert max_y <= result.bbox[3] + source_budget
    assert (max_x - min_x) * result.scale_factor == pytest.approx(
        100.0, abs=0.02
    )


def test_closed_stroke_remains_a_band_with_an_independent_hole() -> None:
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 40 40'>"
        "<path d='M10 10H30V30H10Z' fill='none' stroke='#000' "
        "stroke-width='4' stroke-linejoin='miter'/>"
        "</svg>",
        target_width_mm=24.0,
        tolerance_mm=0.02,
    )

    assert result.status == "supported"
    assert result.bbox == pytest.approx((0.0, 0.0, 40.0, 40.0), abs=0.01)
    assert result.raw.component_count == 1
    assert result.raw.hole_count == 1
    assert geometry_area(result, result.raw, 0) == pytest.approx(320.0, abs=0.3)


@pytest.mark.parametrize(
    ("linecap", "expected_area"),
    [("round", np.pi), ("square", 4.0)],
)
def test_zero_length_solid_stroke_preserves_visible_cap(
    linecap: str,
    expected_area: float,
) -> None:
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 10 10'>"
        f"<path d='M5 5L5 5' fill='none' stroke='#000' "
        f"stroke-width='2' stroke-linecap='{linecap}'/>"
        "</svg>",
        target_width_mm=10.0,
        tolerance_mm=0.02,
    )

    assert result.status == "supported"
    assert result.raw.component_count == 1
    assert geometry_bounds(result, result.raw, 0) == pytest.approx(
        (4.0, 4.0, 6.0, 6.0), abs=0.03
    )
    assert geometry_area(result, result.raw, 0) == pytest.approx(
        expected_area, abs=0.08
    )


@pytest.mark.parametrize("linecap", ["butt", "round", "square"])
def test_single_moveto_does_not_create_a_stroke_cap(linecap: str) -> None:
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 10 10'>"
        f"<path d='M5 5' fill='none' stroke='#000' stroke-width='2' "
        f"stroke-linecap='{linecap}'/>"
        "<rect width='1' height='1' fill='red'/></svg>",
        target_width_mm=10.0,
        tolerance_mm=0.02,
    )

    assert result.status == "supported"
    assert result.raw.geometry_count == 1
    assert tuple(result.raw.source_kinds) == (0,)
    assert tuple(result.raw.rgba[0]) == (255, 0, 0, 255)


@pytest.mark.parametrize(
    "path_data",
    ["M5 5L5 5", "M5 5Z", "M5 5C5 5 5 5 5 5"],
    ids=["line", "close", "cubic"],
)
def test_zero_length_draw_verbs_keep_their_round_cap(
    path_data: str,
) -> None:
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 10 10'>"
        f"<path d='{path_data}' fill='none' stroke='#000' "
        "stroke-width='2' stroke-linecap='round'/></svg>",
        target_width_mm=10.0,
        tolerance_mm=0.02,
    )

    assert result.status == "supported"
    assert result.raw.component_count == 1
    assert geometry_area(result, result.raw, 0) == pytest.approx(
        np.pi, abs=0.08
    )


@pytest.mark.parametrize(
    "path_data",
    ["M5 5L5 5", "M5 5Z"],
    ids=["line", "close"],
)
@pytest.mark.parametrize(
    ("linecap", "dash_offset", "expected_area"),
    [
        ("round", 0, np.pi),
        ("square", 0, 4.0),
        ("butt", 0, 0.0),
        ("round", 5, np.pi),
        ("square", 5, 4.0),
        ("butt", 5, 0.0),
        ("round", -5, np.pi),
        ("square", 15, 4.0),
        ("round", 4.999, np.pi),
        ("round", 5.001, 0.0),
    ],
    ids=[
        "round-painted",
        "square-painted",
        "butt-painted",
        "round-painted-endpoint",
        "square-painted-endpoint",
        "butt-painted-endpoint",
        "round-negative-painted-endpoint",
        "square-wrapped-painted-endpoint",
        "round-before-painted-endpoint",
        "round-after-painted-endpoint",
    ],
)
def test_zero_length_dashed_subpath_respects_dash_phase_and_cap(
    path_data: str,
    linecap: str,
    dash_offset: float,
    expected_area: float,
) -> None:
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 10 10'>"
        f"<path d='{path_data}' fill='none' stroke='#000' "
        f"stroke-width='2' stroke-linecap='{linecap}' "
        f"stroke-dasharray='5 5' stroke-dashoffset='{dash_offset}'/>"
        "<rect width='1' height='1' fill='red'/></svg>",
        target_width_mm=10.0,
        tolerance_mm=0.02,
    )

    assert result.status == "supported"
    if expected_area:
        assert result.raw.geometry_count == 2
        assert tuple(result.raw.source_kinds) == (1, 0)
        assert geometry_area(result, result.raw, 0) == pytest.approx(
            expected_area, abs=0.08
        )
        assert result.raw.component_count == 2
        assert result.unprintable_fragments.count == 0
    else:
        assert result.raw.geometry_count == 1
        assert tuple(result.raw.source_kinds) == (0,)
        assert result.raw.component_count == 1
        assert result.unprintable_fragments.reason_codes == (
            "stroke_collapse",
        )
        assert result.unprintable_fragments.source_kinds.tolist() == [1]


@pytest.mark.parametrize("linecap", ["round", "square"])
@pytest.mark.parametrize("dash_offset", [-5, 5, 15])
def test_nonzero_dash_keeps_cap_at_exact_painted_interval_endpoint(
    linecap: str,
    dash_offset: int,
) -> None:
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 30 10'>"
        "<path d='M5 5H25' fill='none' stroke='#000' stroke-width='2' "
        f"stroke-linecap='{linecap}' stroke-dasharray='5 5' "
        f"stroke-dashoffset='{dash_offset}'/></svg>",
        target_width_mm=30.0,
        tolerance_mm=0.02,
    )

    assert result.status == "supported"
    assert geometry_bounds(result, result.raw, 0)[0] == pytest.approx(
        4.0, abs=0.03
    )


def test_pure_moveto_is_dropped_without_hiding_a_later_zero_length_line() -> None:
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 10 10'>"
        "<path d='M1 1 M5 5L5 5' fill='none' stroke='#000' "
        "stroke-width='2' stroke-linecap='round'/></svg>",
        target_width_mm=10.0,
        tolerance_mm=0.02,
    )

    assert result.status == "supported"
    assert result.raw.component_count == 1
    assert geometry_bounds(result, result.raw, 0) == pytest.approx(
        (4.0, 4.0, 6.0, 6.0), abs=0.03
    )


@pytest.mark.parametrize("unit_scale", [1e-17, 1.0, 1e17])
def test_closed_dash_wrap_join_is_invariant_across_user_unit_scales(
    unit_scale: float,
) -> None:
    result = lumina_svg_core.analyze_svg_svg(
        f"<svg viewBox='0 0 {14 * unit_scale:g} {14 * unit_scale:g}'>"
        f"<path d='M{2 * unit_scale:g} {2 * unit_scale:g}"
        f"H{12 * unit_scale:g}V{12 * unit_scale:g}"
        f"H{2 * unit_scale:g}Z' "
        "fill='none' stroke='#000' "
        f"stroke-width='{2 * unit_scale:g}' stroke-linecap='butt' "
        "stroke-linejoin='miter' "
        f"stroke-dasharray='{10 * unit_scale:g} {10 * unit_scale:g}' "
        f"stroke-dashoffset='{5 * unit_scale:g}'/>"
        "</svg>",
        target_width_mm=14.0,
        tolerance_mm=0.02,
    )

    assert result.status == "supported"
    assert result.raw.component_count == 2
    physical_area = (
        geometry_area(result, result.raw, 0) * result.scale_factor**2
    )
    # A painted dash that crosses the closed-subpath seam keeps the corner
    # join; the seam is not converted into two artificial caps.
    # The zero-origin root viewport is 14 mm wide after public normalization.
    assert physical_area == pytest.approx(40.0, abs=0.1)


@pytest.mark.parametrize(
    "dash_array",
    ["50 10", "40 10"],
    ids=["covers-perimeter", "equals-perimeter"],
)
def test_closed_dash_covering_perimeter_is_independent_of_linecap(
    dash_array: str,
) -> None:
    geometries = []
    for linecap in ("butt", "round", "square"):
        result = lumina_svg_core.analyze_svg_svg(
            "<svg viewBox='0 0 20 20'>"
            "<path d='M5 5H15V15H5Z' fill='none' stroke='#000' "
            "stroke-width='2' stroke-linejoin='bevel' "
            f"stroke-linecap='{linecap}' stroke-dasharray='{dash_array}'/>"
            "</svg>",
            target_width_mm=20.0,
            tolerance_mm=0.02,
        )
        assert result.status == "supported"
        geometries.append(geometry_shape(result, result.raw, 0))

    assert geometries[0].symmetric_difference(geometries[1]).area < 0.02
    assert geometries[0].symmetric_difference(geometries[2]).area < 0.02


def test_zero_length_dash_entry_fails_closed_before_expansion() -> None:
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 20 10'>"
        "<path d='M0 5H20' fill='none' stroke='#000' stroke-width='2' "
        "stroke-linecap='round' stroke-dasharray='0 5' "
        "stroke-dashoffset='2'/>"
        "</svg>",
        target_width_mm=20.0,
        tolerance_mm=0.02,
    )

    assert result.status == "unsupported"
    assert result.reason_code == "unsupported_zero_length_dash"
    assert result.raw.geometry_count == 0


def test_large_dash_offset_preserves_exact_source_value() -> None:
    svg = (
        "<svg viewBox='0 0 100 10'>"
        "<path d='M0 5H100' fill='none' stroke='#000' stroke-width='2' "
        "stroke-dasharray='3 3' stroke-dashoffset='16777217'/>"
        "</svg>"
    )
    recording = lumina_svg_core.record_paint_commands_svg(svg)
    result = lumina_svg_core.analyze_svg_svg(
        svg,
        target_width_mm=100.0,
        tolerance_mm=0.02,
    )

    assert recording.commands[0].stroke.dash_offset == 16777217.0
    assert result.status == "supported"
    assert result.raw.geometry_count == 1
    assert result.raw.component_count == 17


def test_large_dash_cycle_preserves_exact_decimal_pattern() -> None:
    dash_array = " ".join(["0.100000001"] * 32)
    svg = (
        "<svg viewBox='0 0 100 10'>"
        "<path d='M0 5H100' fill='none' stroke='#000' stroke-width='2' "
        f"stroke-dasharray='{dash_array}' stroke-dashoffset='10000000'/>"
        "</svg>"
    )
    recording = lumina_svg_core.record_paint_commands_svg(svg)
    result = lumina_svg_core.analyze_svg_svg(
        svg,
        target_width_mm=100.0,
        tolerance_mm=0.02,
    )

    assert recording.commands[0].stroke.dash_array == pytest.approx(
        (0.100000001,) * 32,
        abs=0.0,
    )
    assert result.status == "supported"
    assert result.raw.geometry_count == 1
    assert result.raw.component_count > 0


@pytest.mark.parametrize("limit_delta", [-0.05, 0.05])
def test_svg_miter_limit_uses_bevel_fallback_below_threshold(
    limit_delta: float,
) -> None:
    points = [(10.0, 10.0), (20.0, 10.0), (10.603, 13.42)]
    incoming_outgoing_cosine = -9.397 / np.hypot(9.397, 3.42)
    miter_ratio = np.sqrt(2.0 / (incoming_outgoing_cosine + 1.0))
    miter_limit = miter_ratio + limit_delta
    expected_join = "bevel" if limit_delta < 0 else "mitre"
    expected = LineString(points).buffer(
        1.0,
        cap_style="flat",
        join_style=expected_join,
        mitre_limit=miter_limit,
    )
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 40 25'>"
        "<path d='M10 10L20 10L10.603 13.42' fill='none' stroke='#000' "
        "stroke-width='2' stroke-linecap='butt' stroke-linejoin='miter' "
        f"stroke-miterlimit='{miter_limit:.9f}'/></svg>",
        target_width_mm=40.0,
        tolerance_mm=0.02,
    )

    assert result.status == "supported"
    assert geometry_area(result, result.raw, 0) == pytest.approx(
        expected.area, abs=0.08
    )
    assert geometry_bounds(result, result.raw, 0)[2] == pytest.approx(
        expected.bounds[2], abs=0.03
    )


def test_closed_miter_limit_uses_bevel_instead_of_square_fallback() -> None:
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 30 20'>"
        "<path d='M10 10L20 10L10.603 13.42Z' fill='none' stroke='#000' "
        "stroke-width='2' stroke-linejoin='miter' stroke-miterlimit='4'/>"
        "</svg>",
        target_width_mm=30.0,
        tolerance_mm=0.02,
    )

    assert result.status == "supported"
    assert geometry_bounds(result, result.raw, 0)[2] == pytest.approx(
        20.342, abs=0.03
    )


def test_miter_bevel_fallback_is_expanded_before_affine_transform() -> None:
    local_bevel = LineString(
        [(0.0, 0.0), (10.0, 0.0), (0.603, 3.42)]
    ).buffer(1.0, cap_style="flat", join_style="bevel")
    affine_determinant = abs(1.2 * 0.8 - 0.4 * 0.3)
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 60 50'>"
        "<path d='M0 0L10 0L0.603 3.42' fill='none' stroke='#000' "
        "stroke-width='2' stroke-linecap='butt' stroke-linejoin='miter' "
        "stroke-miterlimit='4' transform='matrix(1.2 0.3 0.4 0.8 20 20)'/>"
        "</svg>",
        target_width_mm=60.0,
        tolerance_mm=0.02,
    )

    assert result.status == "supported"
    assert geometry_area(result, result.raw, 0) == pytest.approx(
        local_bevel.area * affine_determinant, abs=0.1
    )


def test_miter_limit_rounding_at_join_threshold_fails_closed() -> None:
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 14 8'>"
        "<path d='M2 2L10 2L3 5.872983346207417' fill='none' "
        "stroke='#000' "
        "stroke-width='2' stroke-linejoin='miter' stroke-miterlimit='4'/>"
        "</svg>",
        target_width_mm=14.0,
        tolerance_mm=0.02,
    )

    assert result.status == "unsupported"
    assert result.reason_code == "unsupported_input_precision"
    assert result.raw.geometry_count == 0


@pytest.mark.parametrize("tolerance_mm", [0.02, 0.002])
def test_acute_miter_centerline_quantization_stays_in_total_error_budget(
    tolerance_mm: float,
) -> None:
    angle = np.deg2rad(0.25)
    length = 40.0
    center = np.array([50.0, 50.0])
    first = center + np.array(
        [length * np.cos(angle), -length * np.sin(angle)]
    )
    last = center + np.array(
        [length * np.cos(angle), length * np.sin(angle)]
    )
    centerline = LineString([first, center, last])
    reference = centerline.buffer(
        0.5,
        cap_style="flat",
        join_style="mitre",
        mitre_limit=1000.0,
    )
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 100 100'>"
        f"<path d='M{first[0]:.15g} {first[1]:.15g} L50 50 "
        f"L{last[0]:.15g} {last[1]:.15g}' fill='none' stroke='#000' "
        "stroke-width='1' stroke-linecap='butt' stroke-linejoin='miter' "
        "stroke-miterlimit='1000'/></svg>",
        target_width_mm=100.0,
        tolerance_mm=tolerance_mm,
    )

    if result.status == "unsupported":
        assert result.reason_code == "unsupported_input_precision"
        return
    actual = translate(
        geometry_shape(result, result.raw, 0),
        xoff=result.bbox[0],
        yoff=result.bbox[1],
    )
    assert (
        actual.boundary.hausdorff_distance(reference.boundary)
        * result.scale_factor
        <= tolerance_mm
    )


@pytest.mark.parametrize("tolerance_mm", [0.002, 0.001, 0.0005])
def test_acute_miter_uses_exact_source_directions(
    tolerance_mm: float,
) -> None:
    angle = np.deg2rad(0.05)
    length = 40.0
    center = np.array([50.0, 50.0])
    first = center + np.array(
        [length * np.cos(angle), -length * np.sin(angle)]
    )
    last = center + np.array(
        [length * np.cos(angle), length * np.sin(angle)]
    )
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 100 100'>"
        f"<path d='M{first[0]:.15g} {first[1]:.15g} L50 50 "
        f"L{last[0]:.15g} {last[1]:.15g}' fill='none' stroke='#000' "
        "stroke-width='1' stroke-linecap='butt' stroke-linejoin='miter' "
        "stroke-miterlimit='5000'/></svg>",
        target_width_mm=100.0,
        tolerance_mm=tolerance_mm,
    )

    assert result.status == "supported"
    assert result.reason_code == ""
    assert result.raw.geometry_count == 1


def test_short_stroke_direction_is_preserved() -> None:
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 100 100'>"
        "<path d='M500.00013 50.00013 L500.00513 50.00023' "
        "transform='translate(-450 0)' fill='none' stroke='black' "
        "stroke-width='10' stroke-linecap='butt'/></svg>",
        target_width_mm=100.0,
        tolerance_mm=0.02,
    )

    assert result.status == "supported"
    assert result.raw.geometry_count == 1
    assert result.unprintable_fragments.count == 0


def test_exact_arc_lowering_error_is_not_amplified_as_endpoint_noise() -> None:
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 100 100'>"
        "<path d='M10 10 A24 23.65 -66.8 0 1 10.01 10.001 "
        "L20 10 L20 20 Z' fill='red' stroke='black' "
        "stroke-width='2' stroke-linecap='round' "
        "stroke-linejoin='round' vector-effect='non-scaling-stroke'/>"
        "</svg>",
        target_width_mm=100.0,
        tolerance_mm=0.02,
    )

    assert result.status == "supported", result.reason_code
    assert result.reason_code == ""
    assert result.raw.geometry_count == 2
    assert result.unprintable_fragments.count == 0


def test_dense_round_stroke_uses_exact_minkowski_bounds() -> None:
    points = " ".join(
        f"L{-10.0 + index * 0.001:.3f} "
        f"{10.0 + (index % 2) * 0.0001:.4f}"
        for index in range(3000)
    )
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 20 20'>"
        f"<path d='M-10 10 {points}' fill='none' stroke='black' "
        "stroke-width='2' stroke-linecap='round' "
        "stroke-linejoin='round'/></svg>",
        target_width_mm=20.0,
        tolerance_mm=0.02,
    )

    assert result.status == "supported", result.reason_code
    assert result.raw.geometry_count == 1
    assert result.bbox[0] == pytest.approx(-11.0, abs=0.001)
    assert result.bbox[2] == pytest.approx(31.0, abs=0.001)


@pytest.mark.parametrize(
    "line_cap",
    ["square", "round", "butt"],
)
def test_exact_short_line_preserves_all_area_bearing_caps(
    line_cap: str,
) -> None:
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 100 100'>"
        "<path d='M500.00013 50.00013 L500.0001301 50.0001301' "
        "transform='translate(-450 0)' fill='none' stroke='black' "
        f"stroke-width='10' stroke-linecap='{line_cap}'/></svg>",
        target_width_mm=100.0,
        tolerance_mm=0.02,
    )

    assert result.status == "supported"
    assert result.reason_code == ""
    assert result.raw.geometry_count == 1
    assert result.unprintable_fragments.count == 0


def test_exact_short_round_dash_phase_remains_printable() -> None:
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 100 100'>"
        "<rect x='0' y='0' width='1' height='1' fill='red'/>"
        "<path d='M500.00013 50.00013 L500.000131 50.000131' "
        "transform='translate(-450 0)' fill='none' stroke='blue' "
        "stroke-width='10' stroke-linecap='round' "
        "stroke-dasharray='1 1' stroke-dashoffset='1.99999988'/></svg>",
        target_width_mm=100.0,
        tolerance_mm=0.02,
    )

    assert result.status == "supported"
    assert result.raw.geometry_count == 2
    assert result.unprintable_fragments.count == 0


@pytest.mark.parametrize(
    "path_data",
    [
        "M500.00013 50.00013 L500.0001301 50.0001301 V70",
        "M500.00013 50.00013 H500.0001301 V70",
        "M500.00013 50.00013 V50.0001301 H520",
        "M500.00013 50.00013 L510 60 L500.0001301 50.0001301 Z",
    ],
    ids=["line", "horizontal", "vertical", "close"],
)
def test_short_line_direction_next_to_other_segments_is_preserved(
    path_data: str,
) -> None:
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 100 100'>"
        f"<path d='{path_data}' transform='translate(-450 0)' "
        "fill='none' stroke='black' stroke-width='10' "
        "stroke-linecap='round' stroke-linejoin='miter'/></svg>",
        target_width_mm=100.0,
        tolerance_mm=0.02,
    )

    assert result.status == "supported"
    assert result.raw.geometry_count == 1
    assert result.unprintable_fragments.count == 0


@pytest.mark.parametrize(
    "path_data",
    [
        "M500.00013 50.00013 "
        "C500.0001301 50.0001301 501 60 510 60",
        "M500 60 C509 60 509.9999999 50.0001301 "
        "510 50.00013",
        "M500.00013 50.00013 "
        "Q500.0001301 50.0001301 510 60",
        "M500 60 Q509.9999999 50.0001301 510 50.00013",
    ],
    ids=[
        "cubic-start-tangent",
        "cubic-end-tangent",
        "quadratic-start-tangent",
        "quadratic-end-tangent",
    ],
)
def test_curve_endpoint_tangent_uses_exact_source_coordinates(
    path_data: str,
) -> None:
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 100 100'>"
        f"<path d='{path_data}' transform='translate(-450 0)' "
        "fill='none' stroke='black' stroke-width='.2' "
        "stroke-linecap='butt' stroke-linejoin='round'/></svg>",
        target_width_mm=100.0,
        tolerance_mm=0.02,
    )

    assert result.status == "supported"
    assert result.reason_code == ""
    assert result.raw.geometry_count == 1


@pytest.mark.parametrize(
    ("line_cap", "line_join"),
    [
        ("butt", "miter"),
        ("round", "miter"),
        ("square", "bevel"),
    ],
)
def test_closed_dash_wrap_uses_join_at_seam(
    line_cap: str,
    line_join: str,
) -> None:
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 40 40'>"
        "<path d='M10 10H30V30H10Z' fill='none' stroke='black' "
        "stroke-width='4' stroke-dasharray='10 10' "
        f"stroke-dashoffset='5' stroke-linecap='{line_cap}' "
        f"stroke-linejoin='{line_join}'/></svg>",
        target_width_mm=40.0,
        tolerance_mm=0.02,
    )

    assert result.status == "supported", result.reason_code
    actual = geometry_shape(result, result.raw, 0)
    centerlines = MultiLineString(
        [
            [(10, 15), (10, 10), (15, 10)],
            [(25, 10), (30, 10), (30, 15)],
            [(30, 25), (30, 30), (25, 30)],
            [(15, 30), (10, 30), (10, 25)],
        ]
    )
    reference = centerlines.buffer(
        2.0,
        cap_style={"butt": "flat"}.get(line_cap, line_cap),
        join_style={"miter": "mitre"}.get(line_join, line_join),
        mitre_limit=4.0,
        quad_segs=64,
    )

    assert actual.boundary.hausdorff_distance(reference.boundary) <= 0.02
    assert actual.symmetric_difference(reference).area <= 0.2


def test_round_point_cap_fails_closed_when_error_needs_over_4096_segments(
) -> None:
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 100 100'>"
        "<path d='M50 50' fill='none' stroke='#000' stroke-width='100' "
        "stroke-linecap='round'/></svg>",
        target_width_mm=100.0,
        tolerance_mm=1.0e-6,
    )

    assert result.status == "unsupported"
    assert result.reason_code in {
        "unsupported_complexity",
        "unsupported_input_precision",
    }
    assert result.raw.geometry_count == 0


def test_collapsed_stroke_fragment_does_not_discard_valid_paint() -> None:
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 100 100'>"
        "<rect width='10' height='10' fill='red'/>"
        "<path d='M20 20L80 20' fill='none' stroke='blue' "
        "stroke-width='1' stroke-linecap='butt' "
        "transform='matrix(1 0 0 0.000000000001 0 0)'/>"
        "</svg>",
        target_width_mm=100.0,
        tolerance_mm=0.02,
    )

    assert result.status == "supported"
    assert result.raw.geometry_count == 1
    assert result.raw.rgba.tolist() == [[255, 0, 0, 255]]
    assert result.unprintable_fragments.reason_codes == (
        "stroke_collapse",
    )
    assert result.unprintable_fragments.rgba.tolist() == [
        [0, 0, 255, 255],
    ]
    assert result.unprintable_fragments.draw_indices.tolist() == [1]
    assert result.unprintable_fragments.source_kinds.tolist() == [1]


def test_dashed_cubic_uses_total_arclength_error_budget() -> None:
    cubic_commands = " ".join(
        f"C{index + 0.25} 2 {index + 0.75} 0 {index + 1} 1"
        for index in range(5)
    )
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 5 2'>"
        f"<path d='M0 1 {cubic_commands}' fill='none' stroke='#000' "
        "stroke-width='.2' stroke-linecap='butt' "
        "stroke-dasharray='7 1000'/></svg>",
        target_width_mm=5.0,
        tolerance_mm=0.02,
    )

    if result.status == "unsupported":
        assert result.reason_code in {
            "unsupported_complexity",
            "unsupported_input_precision",
        }
        assert result.raw.geometry_count == 0
        return
    assert result.status == "supported", result.reason_code
    samples: list[np.ndarray] = [np.array([0.0, 1.0])]
    start = samples[0]
    parameters = np.linspace(0.0, 1.0, 4001)[1:, None]
    for index in range(5):
        control1 = np.array([index + 0.25, 2.0])
        control2 = np.array([index + 0.75, 0.0])
        end = np.array([index + 1.0, 1.0])
        samples.extend(
            _cubic_point(
                start, control1, control2, end, parameters
            )
        )
        start = end
    centerline = np.asarray(samples)
    segment_lengths = np.linalg.norm(
        np.diff(centerline, axis=0), axis=1
    )
    cumulative = np.concatenate(([0.0], np.cumsum(segment_lengths)))
    endpoint_index = int(np.searchsorted(cumulative, 7.0))
    endpoint_fraction = (
        (7.0 - cumulative[endpoint_index - 1])
        / (cumulative[endpoint_index] - cumulative[endpoint_index - 1])
    )
    endpoint = centerline[endpoint_index - 1] + endpoint_fraction * (
        centerline[endpoint_index] - centerline[endpoint_index - 1]
    )
    reference_centerline = np.vstack(
        (centerline[:endpoint_index], endpoint)
    )
    reference = LineString(reference_centerline).buffer(
        0.1,
        cap_style="flat",
        join_style="round",
        quad_segs=16,
    )
    reference = translate(
        reference,
        xoff=-result.bbox[0],
        yoff=-result.bbox[1],
    )
    actual = geometry_shape(result, result.raw, 0)
    physical_boundary_error = (
        actual.boundary.hausdorff_distance(reference.boundary)
        * result.scale_factor
    )

    assert physical_boundary_error <= 0.02


def test_nested_svg_coordinate_semantics_are_clipped_natively() -> None:
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 40 20'>"
        "<svg x='10' y='5' width='10' height='10' viewBox='0 0 10 10'>"
        "<path d='M-10 0H20V10H-10Z' fill='#f00'/>"
        "</svg></svg>",
        target_width_mm=40.0,
        tolerance_mm=0.02,
    )

    assert result.status == "supported"
    assert result.raw.geometry_count == 1
    assert result.raw.component_count == 1


def test_explicit_root_overflow_hidden_is_clipped_natively() -> None:
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 10 10' overflow='hidden'>"
        "<path d='M-10 0H20V10H-10Z' fill='#f00'/>"
        "</svg>",
        target_width_mm=10.0,
        tolerance_mm=0.02,
    )

    assert result.status == "supported"
    assert result.raw.geometry_count == 1
    assert result.raw.component_count == 1


@pytest.mark.parametrize("outlier", [1_000.0, 1_000_000_000.0])
def test_clip_outlier_does_not_expand_bbox_or_change_physical_scale(
    outlier: float,
) -> None:
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 100 100'>"
        "<defs><clipPath id='small'>"
        "<rect x='0' y='0' width='10' height='10'/>"
        "</clipPath></defs>"
        f"<rect x='-{outlier:g}' y='0' width='{2 * outlier:g}' height='10' "
        "clip-path='url(#small)' fill='#f00'/>"
        "</svg>",
        target_width_mm=100.0,
        tolerance_mm=0.02,
    )

    assert result.status == "supported"
    assert result.bbox == pytest.approx((0.0, 0.0, 100.0, 100.0), abs=0.001)
    assert result.scale_factor == pytest.approx(1.0, abs=0.00001)
    assert geometry_bounds(result, result.raw, 0) == pytest.approx(
        (0.0, 0.0, 10.0, 10.0), abs=0.02
    )


def test_clip_bounds_prepass_respects_end_to_end_boundary_budget() -> None:
    result = lumina_svg_core.analyze_svg_svg(
        "<svg xmlns='http://www.w3.org/2000/svg'>"
        "<defs><clipPath id='small'>"
        "<rect x='0' y='0' width='10' height='10'/>"
        "</clipPath></defs>"
        "<rect x='-1000000000' y='0' width='2000000000' height='10' "
        "clip-path='url(#small)' fill='#f00'/>"
        "</svg>",
        target_width_mm=100.0,
        tolerance_mm=0.02,
    )

    assert result.status == "supported"
    assert result.bbox == pytest.approx((0.0, 0.0, 10.0, 10.0), abs=0.001)
    ring = next(geometry_rings(result, result.raw, 0))
    physical = Polygon(ring * result.scale_factor)
    expected = box(0.0, 0.0, 100.0, 100.0)
    assert physical.boundary.hausdorff_distance(expected.boundary) <= 0.02


def test_viewport_clip_bounds_keep_a_tiny_representable_component() -> None:
    result = lumina_svg_core.analyze_svg_svg(
        '<svg viewBox="0 0 6552 4612">'
        '<defs><clipPath id="c"><rect x="10" y="10" width="1" height="1"/>'
        '</clipPath></defs>'
        '<path d="M10.1 10.1h0.0001v0.0001Z" fill="red" clip-path="url(#c)"/>'
        '</svg>', 230.0, 0.02, allow_strokes=False,
    )

    assert result.status == "supported", result.reason_code
    assert result.raw.component_count == result.visible.component_count == 1
    actual = geometry_shape(result, result.raw, 0)
    assert actual.is_valid
    assert actual.area == pytest.approx(5e-9, rel=0.001)
    assert result.bbox == pytest.approx((0.0, 0.0, 6552.0, 4612.0))


def test_viewport_clip_bounds_preserve_cancelled_fill_census() -> None:
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 100 100'>"
        "<svg width='100' height='100' viewBox='0 0 100 100'>"
        "<path d='M10 10Q11 12 12 10Q11 12 10 10Z' fill-rule='evenodd'/>"
        "<rect x='20' y='20' width='5' height='5' fill='red'/>"
        "</svg></svg>", 100.0, 0.02, allow_strokes=False,
    )

    assert result.status == "supported", result.reason_code
    assert result.input_paint_count == 2
    assert result.raw.geometry_count == result.visible.geometry_count == 1
    assert result.raw.draw_indices.tolist() == [1]
    assert result.unprintable_fragments.reason_codes == ("fill_rule_cancelled",)
    assert result.unprintable_fragments.draw_indices.tolist() == [0]
    assert geometry_area(result, result.visible, 0) == pytest.approx(25.0)


def test_viewport_clip_bounds_preserve_narrow_curve_fragment_census() -> None:
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 6552 4612'><defs><clipPath id='c'>"
        "<rect x='145' y='100.01' width='10' height='0.004'/>"
        "</clipPath></defs><path d='M100 100C100 100.02 200 100.02 200 100"
        "L200 0L100 0Z' clip-path='url(#c)'/></svg>",
        230.0, 0.02, allow_strokes=False,
    )

    # Final replay's existing physical tolerance classifies this tiny
    # intersection as a fragment. A discarded bounds pass must not replace
    # that supported result and its paint census with a document rejection.
    assert result.status == "supported", result.reason_code
    assert result.input_paint_count == 1
    assert result.raw.geometry_count == result.visible.geometry_count == 0
    assert result.unprintable_fragments.reason_codes == ("clipped_out",)
    assert result.unprintable_fragments.draw_indices.tolist() == [0]
    assert result.bbox == pytest.approx((0.0, 0.0, 6552.0, 4612.0))


def test_viewport_clip_bounds_keep_transform_origin_roundoff() -> None:
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 100 100'>"
        "<g transform='translate(0 -5e-20)'>"
        "<svg width='100' height='100' viewBox='0 0 100 100'>"
        "<rect width='100' height='100'/>"
        "<path d='M10 10Q11 12 12 10Q11 12 10 10Z' fill-rule='evenodd'/>"
        "</svg></g></svg>", 100.0, 0.02, allow_strokes=False,
    )

    assert result.status == "supported", result.reason_code
    assert result.bbox[1] == pytest.approx(-5e-20, rel=1e-12, abs=0)
    assert result.viewport_bounds[1] == pytest.approx(5e-20, rel=1e-12, abs=0)
    assert result.bbox[2:] == (100.0, 100.0)
    assert result.scale_factor == 1.0
    assert result.raw.geometry_count == result.visible.geometry_count == 1
    assert result.unprintable_fragments.reason_codes == ("fill_rule_cancelled",)


def test_viewport_clip_bounds_preserve_visible_overflow_dimensions() -> None:
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 100 100' overflow='visible'>"
        "<defs><clipPath id='c'><rect x='-1' y='-1' width='102' height='102'/>"
        "</clipPath></defs>"
        "<rect x='-0.25' y='-0.5' width='100.5' height='101' "
        "clip-path='url(#c)'/></svg>", 230.0, 0.02, allow_strokes=False,
    )

    assert result.status == "supported", result.reason_code
    assert result.bbox == pytest.approx((-0.25, -0.5, 100.5, 101.0))
    assert result.scale_factor == pytest.approx(230.0 / 100.5)
    actual = geometry_shape(result, result.visible, 0)
    assert actual.bounds == pytest.approx((0.0, 0.0, 100.5, 101.0))


@pytest.mark.parametrize("extent", [1e8, 1e12, 1e16])
def test_small_viewport_does_not_reject_large_visible_clipped_geometry(
    extent: float,
) -> None:
    result = lumina_svg_core.analyze_svg_svg(
        '<svg viewBox="0 0 1 1" overflow="visible">'
        '<defs><clipPath id="c">'
        f'<rect width="{extent:g}" height="{extent:g}"/>'
        '</clipPath></defs>'
        f'<rect width="{extent:g}" height="{extent:g}" clip-path="url(#c)"/>'
        '</svg>', 100.0, 0.02, allow_strokes=False,
    )

    assert result.status == "supported", result.reason_code
    assert result.raw.component_count == result.visible.component_count == 1
    assert result.bbox == pytest.approx((0.0, 0.0, extent, extent))
    assert result.scale_factor == pytest.approx(100.0 / extent)
    actual = geometry_shape(result, result.raw, 0)
    physical = shapely.affinity.scale(
        actual, result.scale_factor, result.scale_factor, origin=(0, 0),
    )
    assert physical.boundary.hausdorff_distance(box(0, 0, 100, 100).boundary) <= 0.02


@pytest.mark.parametrize(
    "root_matrix",
    [
        (1, 0, 0, 1, 0, 0),
        (0, 1, -1, 0, 240, 0),
        (1.5, 0.2, 0.4, 0.75, -20, 15),
    ],
    ids=["identity", "rotation", "nonuniform-shear"],
)
@pytest.mark.parametrize("paint", ["fill", "stroke", "non-scaling-stroke"])
def test_viewport_clip_bounds_respect_transformed_curve_error_budget(
    root_matrix: tuple[float, ...], paint: str,
) -> None:
    # The root's nonzero viewBox origin and the nested viewport map local
    # points to (x + 20, y + 20), before applying the root transform.
    curve_path = "M-10 40C-10 0 110 0 110 40"
    if paint == "fill":
        path = curve_path + "L110 90L-10 90Z"
        style = "fill='red'"
    else:
        path = curve_path
        style = "fill='none' stroke='red' stroke-width='4' stroke-linecap='round'"
        if paint == "non-scaling-stroke":
            style += " vector-effect='non-scaling-stroke'"
    matrix_text = " ".join(map(str, root_matrix))
    result = lumina_svg_core.analyze_svg_svg(
        "<svg width='240' height='160' viewBox='10 20 120 80' "
        f"preserveAspectRatio='none' transform='matrix({matrix_text})' "
        "overflow='visible'>"
        "<svg x='20' y='30' width='50' height='40' viewBox='0 0 100 80' "
        "preserveAspectRatio='none'>"
        "<defs><clipPath id='c'>"
        "<rect x='-5' y='5' width='85' height='95'/></clipPath></defs>"
        f"<path d='{path}' {style} clip-path='url(#c)'/>"
        "</svg></svg>", 230.0, 0.02,
    )

    assert result.status == "supported", result.reason_code
    assert result.raw.geometry_count == result.visible.geometry_count == 1
    # Independently sample the analytic Bezier, clip with GEOS, and apply the
    # known SVG matrices; no recorder/bounds helper supplies the oracle.
    t = np.linspace(0.0, 1.0, 8193)[:, None]
    points = (
        (1 - t) ** 3 * [-10, 40]
        + 3 * (1 - t) ** 2 * t * [-10, 0]
        + 3 * (1 - t) * t ** 2 * [110, 0]
        + t ** 3 * [110, 40]
    )
    a, b, c, d, e, f = root_matrix
    combined_matrix = [a, c, b, d, e + 20 * a + 20 * c, f + 20 * b + 20 * d]
    local_clip = box(0, 0, 100, 80).intersection(box(-5, 5, 80, 100))
    if paint == "non-scaling-stroke":
        reference = shapely.affinity.affine_transform(
            LineString(points), combined_matrix,
        ).buffer(2.0, quad_segs=128).intersection(
            shapely.affinity.affine_transform(local_clip, combined_matrix),
        )
    else:
        local_paint = (
            Polygon(np.concatenate((points, [[110, 90], [-10, 90]])))
            if paint == "fill"
            else LineString(points).buffer(2.0, quad_segs=128)
        )
        reference = shapely.affinity.affine_transform(
            local_paint.intersection(local_clip), combined_matrix,
        )
    assert reference.is_valid and not reference.is_empty
    for collection in (result.raw, result.visible):
        actual = translate(
            geometry_shape(result, collection, 0),
            xoff=result.bbox[0], yoff=result.bbox[1],
        )
        assert actual.is_valid and not actual.is_empty
        assert (
            actual.boundary.hausdorff_distance(reference.boundary)
            * result.scale_factor <= 0.02
        )


def test_disabled_stroke_policy_omits_strokes_without_losing_fills() -> None:
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 40 20'>"
        "<path d='M2 2H38V18H2Z' fill='#f00' stroke='#00f' "
        "stroke-width='4' vector-effect='non-scaling-stroke'/>"
        "</svg>",
        target_width_mm=40.0,
        tolerance_mm=0.02,
        allow_strokes=False,
    )

    assert result.status == "supported"
    assert result.raw.geometry_count == 1
    assert result.raw.source_kinds.tolist() == [0]
    assert geometry_bounds(result, result.raw, 0) == pytest.approx(
        (2.0, 2.0, 38.0, 18.0), abs=0.02
    )


@pytest.mark.parametrize(
    "vector_effect",
    ["non-scaling-stroke", "non-scaling&#45;stroke"],
    ids=["literal", "decoded-entity"],
)
def test_non_scaling_stroke_is_expanded_when_strokes_are_enabled(
    vector_effect: str,
) -> None:
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 40 20'>"
        "<path d='M2 2H38V18H2Z' fill='#f00' stroke='#00f' "
        f"stroke-width='4' vector-effect='{vector_effect}'/>"
        "</svg>",
        target_width_mm=40.0,
        tolerance_mm=0.02,
        allow_strokes=True,
    )

    assert result.status == "supported"
    assert result.reason_code == ""
    assert result.input_paint_count == 2
    assert result.raw.geometry_count == 2
    assert result.raw.source_kinds.tolist() == [0, 1]
    assert geometry_area(result, result.raw, 1) == pytest.approx(
        416.0, abs=0.02
    )


def test_non_scaling_stroke_expands_after_the_element_transform() -> None:
    result = lumina_svg_core.analyze_svg_svg(
        "<svg viewBox='0 0 30 20'>"
        "<path d='M1 5H6' transform='scale(4 2)' fill='none' "
        "stroke='#00f' stroke-width='2' stroke-linecap='butt' "
        "vector-effect='non-scaling-stroke'/>"
        "</svg>",
        target_width_mm=30.0,
        tolerance_mm=0.02,
    )

    assert result.status == "supported"
    assert result.raw.geometry_count == 1
    assert geometry_area(result, result.raw, 0) == pytest.approx(
        40.0, abs=0.02
    )

"""Canonical target-grid projection retains labels and shared contact nodes."""
import numpy as np
import pytest
import shapely
import lumina_svg_core as native


def _exact_doubled_area(coordinates: np.ndarray) -> int:
    points = [tuple(int(value) for value in point) for point in coordinates]
    return sum(
        first[0] * second[1] - first[1] * second[0]
        for first, second in zip(points, points[1:] + points[:1])
    )


def test_projection_nodes_shared_edges_without_merging_paint_identity():
    regions = [
        [np.array([(0, 0), (4, 0), (4, 4), (0, 4)], dtype=float)],
        [np.array([(4, 1), (6, 1), (6, 3), (4, 3)], dtype=float)],
    ]
    projected = native.project_regions_to_grid(regions, 1.0)
    assert len(projected) == 2
    polygons = [shapely.Polygon(shell, holes) for region in projected for shell, holes in region]
    assert all(p.is_valid for p in polygons)
    assert [p.area for p in polygons] == [16, 4]
    assert {(4, 1), (4, 3)} <= set(map(tuple, projected[0][0][0]))
    assert polygons[0].intersection(polygons[1]).area == 0


def test_projection_half_grid_rule_is_symmetric_and_collapses_zero_area():
    square = np.array([(-0.5, -0.5), (1.5, -0.5), (1.5, 1.5), (-0.5, 1.5)])
    tiny = np.array([(0, 0), (0.1, 0), (0, 0.1)])
    projected = native.project_regions_to_grid([[square], [tiny]], 1.0)
    assert shapely.Polygon(*projected[0][0]).bounds == (-1, -1, 2, 2)
    assert projected[1] == []


def test_projection_preserves_holes_and_point_touch_components():
    shell = np.array([(0, 0), (8, 0), (8, 8), (0, 8)], dtype=float)
    hole = np.array([(2, 2), (2, 6), (6, 6), (6, 2)], dtype=float)
    touching = np.array([(8, 8), (10, 8), (10, 10), (8, 10)], dtype=float)
    result = native.project_regions_to_grid([[shell, hole], [touching]], 1.0)
    assert len(result) == 2
    assert len(result[0][0][1]) == 1
    assert sum(shapely.Polygon(s, h).area for r in result for s, h in r) == pytest.approx(52)


def test_quantized_cross_paint_contact_is_canonical_before_face_export():
    regions = [
        [np.array([(6000, 9586), (5609, 9488), (1898, 992)], dtype=float)],
        [np.array([(5740, 9521), (5877, 9555), (5891, 9581)], dtype=float)],
    ]
    assert all(shapely.Polygon(r[0]).is_valid for r in regions)
    result = native.project_regions_to_grid(regions, 1.0)
    assert len(result) == 2
    for region in result:
        assert region
        assert shapely.MultiPolygon([shapely.Polygon(s, h) for s, h in region]).is_valid


def test_integer_canonicalization_preserves_positive_area_short_edge_triangle():
    triangle = np.array([(0, 0), (1, 0), (0, 1000)], dtype=np.int64)
    rectangle = np.array([(10, 10), (30, 10), (30, 20), (10, 20)], dtype=np.int64)

    result = native.canonicalize_grid_regions([[triangle], [rectangle]])

    assert len(result) == 2
    polygons = [
        shapely.Polygon(shell, holes)
        for region in result
        for shell, holes in region
    ]
    assert [sum(shapely.Polygon(shell, holes).area for shell, holes in region)
            for region in result] == [500, 200]
    assert shapely.Polygon(result[0][0][0]).equals(shapely.Polygon(triangle))
    assert set(map(tuple, result[0][0][0][:-1])) == set(map(tuple, triangle))
    assert all(polygon.is_valid for polygon in polygons)


def test_integer_canonicalization_preserves_holes_islands_and_point_contacts():
    shell = np.array([(0, 0), (10, 0), (10, 10), (0, 10)], dtype=np.int64)
    hole = np.array([(2, 2), (2, 8), (8, 8), (8, 2)], dtype=np.int64)
    island = np.array([(4, 4), (6, 4), (6, 6), (4, 6)], dtype=np.int64)
    touching_a = np.array([(-4, -4), (-2, -4), (-2, -2), (-4, -2)], dtype=np.int64)
    touching_b = np.array([(-2, -2), (0, -2), (0, 0), (-2, 0)], dtype=np.int64)

    result = native.canonicalize_grid_regions([
        [shell, hole, island], [touching_a, touching_b],
    ])
    nested = [shapely.Polygon(outer, holes) for outer, holes in result[0]]
    touching = [shapely.Polygon(outer, holes) for outer, holes in result[1]]

    assert sum(polygon.area for polygon in nested) == 68
    assert sum(polygon.area for polygon in touching) == 8
    assert all(polygon.is_valid for polygon in nested + touching)
    assert len(touching) == 2


def test_integer_canonicalization_closes_nearly_parallel_point_touch_faces() -> None:
    n = 1_000_000_000_000
    origin = (0, 0)
    first = (n, n + 1)
    second = (n - 1, n)
    third = (n, n + 2)
    fourth = (-1, 0)
    ring = np.array(
        [origin, first, second, origin, third, fourth], dtype=np.int64,
    )

    result = native.canonicalize_grid_regions([[ring]])

    assert len(result) == 1
    assert len(result[0]) == 2
    actual = set()
    for shell, holes in result[0]:
        assert holes == []
        assert shell.shape == (4, 2)
        assert np.array_equal(shell[0], shell[-1])
        actual.add((frozenset(map(tuple, shell[:-1])), _exact_doubled_area(shell)))
    assert actual == {
        (frozenset((origin, first, second)), 1),
        (frozenset((origin, third, fourth)), n + 2),
    }


@pytest.mark.parametrize("outside_first", [False, True])
def test_integer_canonicalization_keeps_tiny_face_outside_large_shell(
    outside_first: bool,
) -> None:
    n = 500_000_000_000
    broad_triangle = [(0, 0), (2 * n - 2, 2 * n), (0, 2 * n)]
    small_triangle = [(0, 0), (-1, 0), (0, -1)]
    outside_triangle = [(n, n + 1), (n + 1, n + 1), (n + 1, n + 2)]
    shell = np.array(broad_triangle + small_triangle, dtype=np.int64)
    outside = np.array(outside_triangle, dtype=np.int64)
    rings = [outside, shell] if outside_first else [shell, outside]

    result = native.canonicalize_grid_regions([rings])

    assert len(result) == 1
    assert len(result[0]) == 3
    actual = set()
    for outer, holes in result[0]:
        assert holes == []
        assert outer.shape == (4, 2)
        assert np.array_equal(outer[0], outer[-1])
        actual.add((frozenset(map(tuple, outer[:-1])), _exact_doubled_area(outer)))
    assert actual == {
        (frozenset(broad_triangle), (2 * n - 2) * 2 * n),
        (frozenset(small_triangle), 1),
        (frozenset(outside_triangle), 1),
    }


def test_integer_canonicalization_rejects_non_grid_coordinates():
    triangle = np.array([(0, 0), (1.5, 0), (0, 1000)], dtype=np.float64)
    with pytest.raises(ValueError, match="exact integers"):
        native.canonicalize_grid_regions([[triangle]])

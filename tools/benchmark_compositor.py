"""Opt-in compositor measurements with the same correctness checks as CI."""

from __future__ import annotations

import argparse
import json
import platform
from pathlib import Path
from statistics import median
from time import perf_counter
from typing import Any

import lumina_svg_core


def _disconnected_rectangles(count: int) -> str:
    columns = 60
    rows = (count + columns - 1) // columns
    rectangles = "".join(
        (
            f'<rect x="{(index % columns) * 3}" '
            f'y="{(index // columns) * 3}" width="1" height="1" '
            f'fill="#{index % 256:02x}0000"/>'
        )
        for index in range(count)
    )
    return f'<svg viewBox="0 0 180 {rows * 3}">{rectangles}</svg>'


def check_disconnected(count: int) -> float:
    started = perf_counter()
    result = lumina_svg_core.analyze_svg_svg(
        _disconnected_rectangles(count),
        target_width_mm=180.0,
        tolerance_mm=0.02,
    )
    elapsed = perf_counter() - started
    assert result.status == "supported"
    assert result.visible.component_count == count
    return elapsed


def check_overlapping(count: int) -> float:
    # Keep every lower paint distinct so an exact-duplicate shortcut cannot
    # hide quadratic overlap enumeration.  The final rectangle covers every
    # earlier paint, so an incremental suffix coverage stays constant-sized.
    rectangles = "".join(
        (
            f'<rect x="{index / (count * 20):.9g}" '
            f'y="{index / (count * 20):.9g}" '
            f'width="{9 - index / (count * 10):.9g}" '
            f'height="{9 - index / (count * 10):.9g}" '
            f'fill="#{index % 251:02x}0000"/>'
        )
        for index in range(count - 1)
    )
    # Two final half-planes jointly cover the canvas.  Neither paint alone is
    # a rectangular occluder for the other lower paints, so an implementation
    # that rebuilds a union from every later paint still does quadratic work.
    rectangles += (
        '<rect width="5" height="10" fill="#0000ff"/>'
        '<rect x="5" width="5" height="10" fill="#00ff00"/>'
    )
    started = perf_counter()
    result = lumina_svg_core.analyze_svg_svg(
        f'<svg viewBox="0 0 10 10">{rectangles}</svg>',
        target_width_mm=10.0,
        tolerance_mm=0.02,
    )
    elapsed = perf_counter() - started
    assert result.status == "supported"
    assert result.visible.component_count == 2
    return elapsed


def check_distributed(count: int) -> float:
    extent = count * 4 + 2
    paints = "".join(
        (
            f'<path d="M{index * 2} {index * 2}h1v1h-1z '
            f'M{extent - index * 2} {extent - index * 2}h1v1h-1z" '
            f'fill="#{index % 251:02x}0000"/>'
        )
        for index in range(count)
    )
    started = perf_counter()
    result = lumina_svg_core.analyze_svg_svg(
        f'<svg viewBox="0 0 {extent + 2} {extent + 2}">{paints}</svg>',
        target_width_mm=100.0,
        tolerance_mm=0.02,
    )
    elapsed = perf_counter() - started
    assert result.status == "supported", result.reason_code
    assert result.visible.component_count == count * 2
    return elapsed


def check_duplicates(count: int) -> float:
    paints = "".join(
        f'<rect width="10" height="10" fill="#{index % 251:02x}0000"/>'
        for index in range(count)
    )
    started = perf_counter()
    result = lumina_svg_core.analyze_svg_svg(
        f'<svg viewBox="0 0 10 10">{paints}</svg>',
        target_width_mm=10.0,
        tolerance_mm=0.02,
    )
    elapsed = perf_counter() - started
    assert result.status == "supported"
    assert result.visible.component_count == 1
    assert result.visible.draw_indices.tolist() == [count - 1]
    assert result.visible.rgba.tolist() == [[(count - 1) % 251, 0, 0, 255]]
    return elapsed


def check_nested_viewport(nested: bool) -> float:
    curve = '<path d="M100 100C100 200 200 200 200 100Z" fill="black"/>'
    plain = f'<svg viewBox="0 0 6552 4612">{curve}</svg>'
    clipped = (
        '<svg viewBox="0 0 6552 4612">'
        '<svg width="6552" height="4612" viewBox="0 0 6552 4612">'
        f'{curve}</svg></svg>'
    )
    result = lumina_svg_core.analyze_svg_svg(
        clipped if nested else plain, 230.0, 0.02, allow_strokes=False,
    )
    assert result.status == "supported", result.reason_code
    assert result.raw.component_count == 1
    assert result.visible.component_count == 1
    return result.bounds_seconds


def benchmark(warm_runs: int = 7) -> dict[str, Any]:
    if warm_runs < 3:
        raise ValueError("warm_runs_must_be_at_least_three")
    cases = [
        ("disconnected", check_disconnected, 600, 2400),
        ("overlapping", check_overlapping, 200, 800),
        ("distributed", check_distributed, 150, 600),
        ("duplicates", check_duplicates, 600, 2400),
        ("nested_viewport", check_nested_viewport, False, True),
    ]
    measurements = []
    for name, measure, small_size, large_size in cases:
        measure(small_size)
        measure(large_size)
        small: list[float] = []
        large: list[float] = []
        # Interleave sizes so slow drift does not affect only the larger case.
        for _ in range(warm_runs):
            small.append(measure(small_size))
            large.append(measure(large_size))
        small_median, large_median = median(small), median(large)
        measurements.append({
            "name": name,
            "metric": "bounds_seconds" if name == "nested_viewport" else "analysis_seconds",
            "small": {"input": small_size, "seconds": small, "median_seconds": small_median},
            "large": {"input": large_size, "seconds": large, "median_seconds": large_median},
            "median_ratio": large_median / small_median if small_median else None,
        })
    return {
        "host": {
            "platform": platform.platform(),
            "machine": platform.machine(),
            "python": platform.python_version(),
        },
        "runtime": {
            "version": lumina_svg_core.__version__,
            "source_revision": lumina_svg_core.__source_revision__,
        },
        "warmup_runs": 1,
        "warm_runs": warm_runs,
        "cases": measurements,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--warm-runs", type=int, default=7)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    report = benchmark(args.warm_runs)
    payload = json.dumps(report, indent=2) + "\n"
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(payload, encoding="utf-8")
    print(payload, end="")


if __name__ == "__main__":
    main()

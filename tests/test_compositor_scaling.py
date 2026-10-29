"""Compositor correctness at both sample sizes, independent of host speed."""

from __future__ import annotations

import importlib.util
from pathlib import Path

import pytest


SPEC = importlib.util.spec_from_file_location(
    "benchmark_compositor",
    Path(__file__).resolve().parents[1] / "tools" / "benchmark_compositor.py",
)
assert SPEC is not None and SPEC.loader is not None
COMPOSITOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(COMPOSITOR)


@pytest.mark.parametrize("count", [600, 2400])
def test_disconnected_paints_preserve_all_components(count: int) -> None:
    COMPOSITOR.check_disconnected(count)


@pytest.mark.parametrize("count", [200, 800])
def test_fully_overlapping_paints_preserve_final_half_planes(count: int) -> None:
    COMPOSITOR.check_overlapping(count)


@pytest.mark.parametrize("count", [150, 600])
def test_distributed_paints_preserve_each_visible_component(count: int) -> None:
    COMPOSITOR.check_distributed(count)


@pytest.mark.parametrize("count", [600, 2400])
def test_exact_duplicate_paints_keep_last_owner_and_color(count: int) -> None:
    COMPOSITOR.check_duplicates(count)


@pytest.mark.parametrize("nested", [False, True])
def test_nested_viewport_preserves_the_curve(nested: bool) -> None:
    COMPOSITOR.check_nested_viewport(nested)

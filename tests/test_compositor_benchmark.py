"""The opt-in compositor benchmark reports measurements, not a speed gate."""

from __future__ import annotations

import importlib.util
import itertools
import json
from pathlib import Path
import statistics
import subprocess
import sys

import pytest


TOOL = Path(__file__).resolve().parents[1] / "tools" / "benchmark_compositor.py"


def test_compositor_benchmark_reports_warm_samples(tmp_path: Path) -> None:
    output = tmp_path / "compositor.json"
    completed = subprocess.run(
        [sys.executable, str(TOOL), "--warm-runs", "3", "--output", str(output)],
        capture_output=True,
        text=True,
        check=False,
    )
    assert completed.returncode == 0, completed.stderr
    report = json.loads(output.read_text(encoding="utf-8"))
    assert report["warmup_runs"] == 1
    assert report["warm_runs"] == 3
    assert report["host"]["machine"]
    assert report["runtime"]["version"]
    assert report["runtime"]["source_revision"]
    assert {case["name"] for case in report["cases"]} == {
        "disconnected", "overlapping", "distributed", "duplicates", "nested_viewport",
    }
    for case in report["cases"]:
        for size in ("small", "large"):
            samples = case[size]["seconds"]
            assert len(samples) == 3
            assert all(value >= 0 for value in samples)
            assert case[size]["median_seconds"] == statistics.median(samples)
        small = case["small"]["median_seconds"]
        expected = case["large"]["median_seconds"] / small if small else None
        assert case["median_ratio"] == expected


def test_slow_measurements_remain_diagnostic(monkeypatch: pytest.MonkeyPatch) -> None:
    spec = importlib.util.spec_from_file_location("benchmark_compositor", TOOL)
    assert spec is not None and spec.loader is not None
    tool = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(tool)
    # Only the wall clock is controlled; the native analysis and every geometry,
    # owner and color assertion still execute for the actual small/large inputs.
    clock = iter(itertools.chain.from_iterable(
        (0.0, duration) for duration in itertools.cycle((0.001, 1.0))
    ))
    monkeypatch.setattr(tool, "perf_counter", lambda: next(clock))
    report = tool.benchmark(warm_runs=3)
    for case in report["cases"]:
        if case["metric"] == "analysis_seconds":
            assert case["small"]["median_seconds"] == 0.001
            assert case["large"]["median_seconds"] == 1.0
            assert case["median_ratio"] == 1000.0

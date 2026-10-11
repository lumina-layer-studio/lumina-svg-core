"""Public Python package for the native SVG geometry engine."""

from __future__ import annotations

import json as _json
from pathlib import Path as _Path

from ._lumina_svg_core import (
    GeometryCollection,
    NativeResult,
    PaintCommand,
    PaintStrokeStyle,
    RECORDER_SCHEMA_VERSION,
    RESULT_SCHEMA_VERSION,
    RecorderResult,
    UnprintableFragmentCollection,
    analyze_svg,
    analyze_svg_svg,
    canonicalize_grid_regions,
    project_regions_to_grid,
    record_paint_commands,
    record_paint_commands_svg,
)

_build_info = _json.loads(
    (_Path(__file__).parent / "_build_info.json").read_text(encoding="utf-8")
)
__version__ = _build_info["version"]
__source_revision__ = _build_info["source_revision"]
__source_dirty__ = _build_info["source_dirty"]
del _build_info


__all__ = [
    "PaintCommand",
    "PaintStrokeStyle",
    "GeometryCollection",
    "NativeResult",
    "RecorderResult",
    "UnprintableFragmentCollection",
    "RECORDER_SCHEMA_VERSION",
    "RESULT_SCHEMA_VERSION",
    "__version__",
    "__source_revision__",
    "__source_dirty__",
    "analyze_svg",
    "analyze_svg_svg",
    "canonicalize_grid_regions",
    "project_regions_to_grid",
    "record_paint_commands",
    "record_paint_commands_svg",
]

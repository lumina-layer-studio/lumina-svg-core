"""Receipts bind the delivered files to package version and declared source."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess
import sys
from zipfile import ZipFile

import pytest


ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools/package_distribution.py"


def _identity() -> tuple[str, bool]:
    revision = subprocess.check_output(
        ["git", "-c", f"safe.directory={ROOT}", "rev-parse", "HEAD"],
        cwd=ROOT, text=True, encoding="utf-8",
    ).strip()
    status = subprocess.check_output(
        ["git", "-c", f"safe.directory={ROOT}", "status", "--porcelain"],
        cwd=ROOT, text=True, encoding="utf-8",
    )
    return revision, bool(status.strip())


def _wheel(directory: Path, *, version="0.3.1", revision=None) -> Path:
    actual_revision, dirty = _identity()
    directory.mkdir(exist_ok=True)
    wheel = directory / f"lumina_svg_core-{version}-cp314-cp314-win_amd64.whl"
    with ZipFile(wheel, "w") as archive:
        prefix = f"lumina_svg_core-{version}.dist-info"
        archive.writestr(
            f"{prefix}/METADATA",
            f"Metadata-Version: 2.4\nName: lumina-svg-core\nVersion: {version}\n"
            "License-Expression: AGPL-3.0-only\n",
        )
        archive.writestr(
            f"{prefix}/WHEEL", "Wheel-Version: 1.0\nTag: cp314-cp314-win_amd64\n",
        )
        archive.writestr("lumina_svg_core/_build_info.json", json.dumps({
            "version": version, "source_revision": revision or actual_revision,
            "source_dirty": dirty,
        }))
    return wheel


def _prepare(wheels: Path, output: Path, *extra: str):
    return subprocess.run(
        [sys.executable, str(TOOL), "--wheel-dir", str(wheels),
         "--output-dir", str(output), *extra],
        capture_output=True, text=True, encoding="utf-8",
    )


def test_receipt_preserves_file_bytes_tags_and_source_identity(tmp_path: Path) -> None:
    wheel = _wheel(tmp_path / "wheels")
    output = tmp_path / "prepared"
    result = _prepare(wheel.parent, output)
    assert result.returncode == 0, result.stderr
    assert (output / wheel.name).read_bytes() == wheel.read_bytes()
    receipt = json.loads((output / "distribution.json").read_text(encoding="utf-8"))
    revision, dirty = _identity()
    assert receipt["version"] == "0.3.1"
    assert receipt["source_revision"] == revision
    assert receipt["source_dirty"] is dirty
    artifact = receipt["artifacts"][0]
    assert artifact["filename"] == wheel.name
    assert artifact["tags"] == ["cp314-cp314-win_amd64"]
    assert artifact["sha256"] == hashlib.sha256(wheel.read_bytes()).hexdigest()


@pytest.mark.parametrize("variant", ["old_version", "different_source"])
def test_unrelated_wheel_is_rejected_without_preparing_files(
    tmp_path: Path, variant: str,
) -> None:
    options = {"version": "0.3.0"} if variant == "old_version" else {"revision": "b" * 40}
    wheel = _wheel(tmp_path / "wheels", **options)
    output = tmp_path / "prepared"
    result = _prepare(wheel.parent, output)
    assert result.returncode != 0
    assert "does not match" in result.stderr
    assert not (output / wheel.name).exists()


def test_empty_input_is_rejected_instead_of_producing_empty_receipt(tmp_path: Path) -> None:
    wheels = tmp_path / "empty"
    wheels.mkdir()
    output = tmp_path / "prepared"
    result = _prepare(wheels, output)
    assert result.returncode != 0
    assert "contains no" in result.stderr
    assert not (output / "distribution.json").exists()


def test_preparation_does_not_overwrite_different_existing_file(tmp_path: Path) -> None:
    wheel = _wheel(tmp_path / "wheels")
    output = tmp_path / "prepared"
    output.mkdir()
    original = b"already prepared different bytes"
    (output / wheel.name).write_bytes(original)
    result = _prepare(wheel.parent, output)
    assert result.returncode != 0
    assert "different bytes" in result.stderr
    assert (output / wheel.name).read_bytes() == original

"""Build identities consumed by installed users and distribution receipts."""

from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess

import pytest

import lumina_svg_core


ROOT = Path(__file__).resolve().parents[1]


def _resolve(tmp_path: Path, source: Path, declared: dict[str, str] | None = None):
    cmake = shutil.which("cmake")
    assert cmake is not None, "Provenance checks require CMake"
    output = tmp_path / "provenance.json"
    probe = tmp_path / "probe.cmake"
    probe.write_text(
        f'include("{(ROOT / "cmake/BuildProvenance.cmake").as_posix()}")\n'
        f'lumina_resolve_build_provenance("{source.as_posix()}")\n'
        f'file(WRITE "{output.as_posix()}" '
        '"{\\\"revision\\\":\\\"${LUMINA_SVG_SOURCE_REVISION}\\\",'
        '\\\"dirty\\\":${LUMINA_SVG_SOURCE_DIRTY}}")\n',
        encoding="utf-8",
    )
    environment = os.environ.copy()
    environment.pop("LUMINA_SVG_BUILD_REVISION", None)
    environment.pop("LUMINA_SVG_BUILD_DIRTY", None)
    environment.update(declared or {})
    result = subprocess.run(
        [cmake, "-P", str(probe)], env=environment,
        capture_output=True, text=True, encoding="utf-8",
    )
    return result, output


def test_installed_package_exposes_its_build_identity() -> None:
    assert lumina_svg_core.__version__ == "0.3.1"
    assert hasattr(lumina_svg_core, "__source_revision__")
    assert hasattr(lumina_svg_core, "__source_dirty__")
    revision = lumina_svg_core.__source_revision__
    if revision == "unknown":
        assert lumina_svg_core.__source_dirty__ is None
    else:
        assert len(revision) == 40
        int(revision, 16)
        assert isinstance(lumina_svg_core.__source_dirty__, bool)


def test_git_free_build_does_not_invent_provenance(tmp_path: Path) -> None:
    source = tmp_path / "archive"
    source.mkdir()
    result, output = _resolve(tmp_path, source)
    assert result.returncode == 0, result.stderr
    assert json.loads(output.read_text(encoding="utf-8")) == {
        "revision": "unknown", "dirty": None,
    }


def test_git_checkout_records_actual_commit_and_uncommitted_changes(tmp_path: Path) -> None:
    source = tmp_path / "checkout"
    source.mkdir()
    subprocess.run(["git", "init", str(source)], check=True, capture_output=True)
    tracked = source / "tracked.txt"
    tracked.write_text("reviewed source\n", encoding="utf-8")
    subprocess.run(["git", "add", "tracked.txt"], cwd=source, check=True)
    subprocess.run(
        ["git", "-c", "user.name=Provenance Test", "-c", "user.email=test@example.invalid",
         "commit", "-m", "test source"],
        cwd=source, check=True, capture_output=True,
    )
    revision = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=source, text=True, encoding="utf-8",
    ).strip()
    result, output = _resolve(tmp_path, source)
    assert result.returncode == 0, result.stderr
    assert json.loads(output.read_text(encoding="utf-8")) == {
        "revision": revision, "dirty": False,
    }
    tracked.write_text("uncommitted source change\n", encoding="utf-8")
    result, output = _resolve(tmp_path, source)
    assert result.returncode == 0, result.stderr
    assert json.loads(output.read_text(encoding="utf-8")) == {
        "revision": revision, "dirty": True,
    }


def test_archive_build_accepts_declared_clean_revision(tmp_path: Path) -> None:
    source = tmp_path / "archive"
    source.mkdir()
    revision = "0123456789abcdef0123456789abcdef01234567"
    result, output = _resolve(tmp_path, source, {
        "LUMINA_SVG_BUILD_REVISION": revision,
        "LUMINA_SVG_BUILD_DIRTY": "0",
    })
    assert result.returncode == 0, result.stderr
    assert json.loads(output.read_text(encoding="utf-8")) == {
        "revision": revision, "dirty": False,
    }


@pytest.mark.parametrize("revision,dirty", [("not-a-sha", "0"), ("a" * 40, "maybe")])
def test_invalid_declared_provenance_fails_before_build(
    tmp_path: Path, revision: str, dirty: str,
) -> None:
    result, output = _resolve(tmp_path, tmp_path, {
        "LUMINA_SVG_BUILD_REVISION": revision,
        "LUMINA_SVG_BUILD_DIRTY": dirty,
    })
    assert result.returncode != 0
    assert "LUMINA_SVG_BUILD_" in result.stderr
    assert not output.exists()

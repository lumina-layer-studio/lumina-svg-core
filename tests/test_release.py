"""Exercise release assembly against real Git checkouts and package archives."""

from __future__ import annotations

import io
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
from zipfile import ZipFile

import pytest


ROOT = Path(__file__).resolve().parents[1]
FIXTURE_VERSION = "7.8.9"
FIXTURE_TAG = f"v{FIXTURE_VERSION}"
TARGETS = [
    f"cp{python}-cp{python}-{platform}"
    for python in ("312", "313", "314")
    for platform in (
        "manylinux_2_28_x86_64", "win_amd64",
        f"macosx_{'14' if python == '314' else '12'}_0_arm64",
        f"macosx_{'14' if python == '314' else '12'}_0_x86_64",
    )
]


def _git(repo: Path, *arguments: str) -> str:
    return subprocess.check_output(
        ["git", "-c", "user.name=Release Test", "-c",
         "user.email=test@example.invalid", *arguments],
        cwd=repo, text=True, encoding="utf-8", stderr=subprocess.PIPE,
    ).strip()


@pytest.fixture
def release_repo(tmp_path: Path) -> Path:
    repo = tmp_path / "checkout"
    (repo / "tools").mkdir(parents=True)
    for name in ("package_distribution.py", "prepare_release.py"):
        source = ROOT / "tools" / name
        if source.exists():
            shutil.copy2(source, repo / "tools" / name)
    (repo / "pyproject.toml").write_text(
        '[project]\nname = "lumina-svg-core"\n'
        f'version = "{FIXTURE_VERSION}"\n'
        'requires-python = ">=3.12,<3.15"\nlicense = "AGPL-3.0-only"\n',
        encoding="utf-8",
    )
    (repo / "CMakeLists.txt").write_text(
        f"project(lumina_svg_core VERSION {FIXTURE_VERSION} LANGUAGES C CXX)\n",
        encoding="utf-8",
    )
    (repo / ".gitignore").write_text("__pycache__/\n", encoding="utf-8")
    _git(repo, "init")
    _git(repo, "add", ".")
    _git(repo, "commit", "-m", "Reviewed release source")
    _git(repo, "update-ref", "refs/remotes/origin/dev-refactor", "HEAD")
    _git(repo, "tag", FIXTURE_TAG)
    return repo


def _wheel(
    directory: Path, tag: str, revision: str, *, version: str = FIXTURE_VERSION,
    name: str = "lumina-svg-core", license: str = "AGPL-3.0-only",
    build: str = "", identity_version: str | None = None,
) -> Path:
    directory.mkdir(parents=True, exist_ok=True)
    build_part = f"{build}-" if build else ""
    wheel = directory / f"lumina_svg_core-{version}-{build_part}{tag}.whl"
    with ZipFile(wheel, "w") as archive:
        prefix = f"lumina_svg_core-{version}.dist-info"
        archive.writestr(
            f"{prefix}/METADATA",
            f"Metadata-Version: 2.4\nName: {name}\nVersion: {version}\n"
            f"License-Expression: {license}\n",
        )
        archive.writestr(f"{prefix}/WHEEL", f"Wheel-Version: 1.0\nTag: {tag}\n")
        archive.writestr("lumina_svg_core/_build_info.json", json.dumps({
            "version": identity_version or version,
            "source_revision": revision, "source_dirty": False,
        }))
    return wheel


def _sdist(directory: Path, *, version: str = FIXTURE_VERSION) -> Path:
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / f"lumina_svg_core-{version}.tar.gz"
    metadata = (
        f"Metadata-Version: 2.4\nName: lumina-svg-core\nVersion: {version}\n"
        "License-Expression: AGPL-3.0-only\n"
    ).encode("utf-8")
    with tarfile.open(path, "w:gz") as archive:
        entry = tarfile.TarInfo(f"lumina_svg_core-{version}/PKG-INFO")
        entry.size = len(metadata)
        archive.addfile(entry, io.BytesIO(metadata))
    return path


@pytest.fixture
def incoming(release_repo: Path, tmp_path: Path) -> Path:
    root = tmp_path / "incoming"
    revision = _git(release_repo, "rev-parse", "HEAD")
    for index in range(5):
        built = tmp_path / f"built-{index}"
        if index < 4:
            for target in TARGETS[index::4]:
                _wheel(built, target, revision)
            package_flag = "--wheel-dir"
        else:
            _sdist(built)
            package_flag = "--sdist-dir"
        subprocess.run(
            [sys.executable, str(release_repo / "tools/package_distribution.py"),
             package_flag, str(built), "--output-dir", str(root / str(index)),
             "--require-clean"],
            check=True, capture_output=True, text=True, encoding="utf-8",
        )
    return root


def _release(
    repo: Path, command: str, *arguments: str, tag: str = FIXTURE_TAG,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(repo / "tools/prepare_release.py"), command,
         "--tag", tag, *arguments],
        cwd=repo, capture_output=True, text=True, encoding="utf-8",
    )


def _assemble(
    repo: Path, incoming: Path, output: Path,
) -> subprocess.CompletedProcess[str]:
    return _release(repo, "assemble", "--incoming-dir", str(incoming),
                    "--output-dir", str(output))


def test_complete_release_preserves_thirteen_packages_and_hash_receipt(
    release_repo: Path, incoming: Path, tmp_path: Path,
) -> None:
    original = {path.name: path.read_bytes() for path in incoming.rglob("*")
                if path.suffix == ".whl" or path.name.endswith(".tar.gz")}
    output = tmp_path / "release"
    result = _assemble(release_repo, incoming, output)
    assert result.returncode == 0, result.stderr
    delivered = {path.name: path.read_bytes()
                 for path in (output / "packages").iterdir()}
    assert delivered == original
    receipt = json.loads((output / "distribution.json").read_text(encoding="utf-8"))
    assert receipt["version"] == FIXTURE_VERSION
    assert receipt["source_revision"] == _git(release_repo, "rev-parse", "HEAD")
    assert receipt["source_dirty"] is False
    assert len(receipt["artifacts"]) == 13
    for artifact in receipt["artifacts"]:
        data = delivered[artifact["filename"]]
        assert artifact["sha256"] == hashlib.sha256(data).hexdigest()
        assert artifact["size_bytes"] == len(data)
    assert {tag for item in receipt["artifacts"]
            for tag in item.get("tags", [])} == set(TARGETS)
    assert "distribution.json" not in delivered


def test_failed_release_copy_cleans_up_and_can_be_retried(
    release_repo: Path, incoming: Path, tmp_path: Path,
) -> None:
    output = tmp_path / "release"
    before = set(output.parent.iterdir())
    injection = """
import pathlib
import runpy
import sys

tool = pathlib.Path(sys.argv[1])
output_parent = pathlib.Path(sys.argv[2]).resolve()
sys.argv = [str(tool), *sys.argv[3:]]
sys.path.insert(0, str(tool.parent))
copied_wheels = 0

def fail_release_copy(event, arguments):
    global copied_wheels
    if event == "shutil.copyfile":
        destination = pathlib.Path(arguments[1]).resolve()
        if destination.is_relative_to(output_parent) and destination.suffix == ".whl":
            copied_wheels += 1
            if copied_wheels == 2:
                raise OSError("injected release-copy I/O failure")

sys.addaudithook(fail_release_copy)
runpy.run_path(str(tool), run_name="__main__")
"""
    failed = subprocess.run(
        [sys.executable, "-c", injection,
         str(release_repo / "tools/prepare_release.py"), str(output.parent),
         "assemble", "--tag", FIXTURE_TAG, "--incoming-dir", str(incoming),
         "--output-dir", str(output)],
        cwd=release_repo, capture_output=True, text=True, encoding="utf-8",
    )
    assert failed.returncode != 0
    assert "injected release-copy I/O failure" in failed.stderr
    assert not output.exists()
    assert set(output.parent.iterdir()) == before

    retried = _assemble(release_repo, incoming, output)
    assert retried.returncode == 0, retried.stderr
    assert len(list((output / "packages").iterdir())) == 13
    assert (output / "distribution.json").is_file()
    assert set(output.parent.iterdir()) == before | {output}


def test_existing_release_output_is_not_overwritten(
    release_repo: Path, incoming: Path, tmp_path: Path,
) -> None:
    output = tmp_path / "release"
    output.mkdir()
    sentinel = output / "keep.txt"
    sentinel.write_bytes(b"existing release output")
    before = set(output.parent.iterdir())
    result = _assemble(release_repo, incoming, output)
    assert result.returncode != 0
    assert "already exists" in result.stderr
    assert {path.name: path.read_bytes() for path in output.iterdir()} == {
        "keep.txt": b"existing release output",
    }
    assert set(output.parent.iterdir()) == before


def test_tag_check_reports_declared_version_and_actual_head(release_repo: Path) -> None:
    result = _release(release_repo, "check-tag")
    assert result.returncode == 0, result.stderr
    assert dict(line.split("=", 1) for line in result.stdout.splitlines()) == {
        "version": FIXTURE_VERSION,
        "source_revision": _git(release_repo, "rev-parse", "HEAD"),
    }


@pytest.mark.parametrize("case", [
    "wrong_tag", "missing_tag", "tag_not_head", "cmake_version", "dirty", "unmerged",
])
def test_tag_check_rejects_unreviewed_or_inconsistent_source(
    release_repo: Path, case: str,
) -> None:
    tag = FIXTURE_TAG
    if case == "wrong_tag":
        tag = "v0.0.1"
        _git(release_repo, "tag", tag)
    elif case == "missing_tag":
        _git(release_repo, "tag", "-d", tag)
    elif case in ("tag_not_head", "unmerged"):
        _git(release_repo, "commit", "--allow-empty", "-m", "Unmerged work")
        if case == "unmerged":
            _git(release_repo, "tag", "-f", tag)
    elif case == "cmake_version":
        (release_repo / "CMakeLists.txt").write_text(
            "project(lumina_svg_core VERSION 0.0.1 LANGUAGES C CXX)\n", encoding="utf-8",
        )
        _git(release_repo, "add", "CMakeLists.txt")
        _git(release_repo, "commit", "-m", "Mismatched CMake version")
        _git(release_repo, "tag", "-f", tag)
        _git(release_repo, "update-ref", "refs/remotes/origin/dev-refactor", "HEAD")
    else:
        (release_repo / "uncommitted.txt").write_text("dirty\n", encoding="utf-8")
    result = _release(release_repo, "check-tag", tag=tag)
    assert result.returncode != 0
    expected = {
        "wrong_tag": "declared version", "missing_tag": "does not exist",
        "tag_not_head": "HEAD", "cmake_version": "CMake", "dirty": "clean",
        "unmerged": "origin/dev-refactor",
    }
    assert expected[case] in result.stderr


@pytest.mark.parametrize("case", [
    "missing_wheel", "duplicate_target", "extra_target", "missing_sdist",
    "wrong_source", "wrong_version", "wrong_build_version", "wrong_package",
    "wrong_license", "receipt_source", "receipt_hash", "receipt_omission",
])
def test_assembly_rejects_incomplete_or_unrelated_packages_without_output(
    release_repo: Path, incoming: Path, tmp_path: Path, case: str,
) -> None:
    wheel = next((incoming / "0").glob("*.whl"))
    target = wheel.name.removeprefix(f"lumina_svg_core-{FIXTURE_VERSION}-").removesuffix(".whl")
    revision = _git(release_repo, "rev-parse", "HEAD")
    if case == "missing_wheel":
        wheel.unlink()
    elif case == "missing_sdist":
        next(incoming.rglob("*.tar.gz")).unlink()
    elif case in ("duplicate_target", "extra_target"):
        _wheel(wheel.parent, "cp311-cp311-win_amd64" if case == "extra_target" else target,
               revision, build="1")
    elif case.startswith("receipt_"):
        receipt_path = incoming / "4/distribution.json"
        receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
        if case == "receipt_source":
            receipt["source_revision"] = "b" * 40
        elif case == "receipt_hash":
            receipt["artifacts"][0]["sha256"] = "0" * 64
        else:
            receipt["artifacts"] = []
        receipt_path.write_text(json.dumps(receipt), encoding="utf-8")
    else:
        options = {
            "wrong_source": {"revision": "b" * 40},
            "wrong_version": {"version": "0.0.1"},
            "wrong_build_version": {"identity_version": "0.0.1"},
            "wrong_package": {"name": "another-package"},
            "wrong_license": {"license": "MIT"},
        }[case]
        wheel.unlink()
        _wheel(wheel.parent, target, options.pop("revision", revision), **options)
    output = tmp_path / "release"
    result = _assemble(release_repo, incoming, output)
    assert result.returncode != 0
    assert not output.exists()
    expected = {
        "missing_wheel": "missing", "duplicate_target": "duplicate", "extra_target": "target",
        "missing_sdist": "source distribution", "wrong_source": "build identity",
        "wrong_version": "version", "wrong_package": "version", "wrong_license": "license",
        "wrong_build_version": "build identity",
        "receipt_source": "receipt", "receipt_hash": "receipt", "receipt_omission": "receipt",
    }
    assert expected[case] in result.stderr


@pytest.mark.parametrize("inside", ["incoming", "output"])
def test_release_data_stays_outside_source_checkout(
    release_repo: Path, incoming: Path, tmp_path: Path, inside: str,
) -> None:
    output = tmp_path / "release"
    if inside == "incoming":
        shutil.copytree(incoming, release_repo / "incoming")
        incoming = release_repo / "incoming"
    else:
        output = release_repo / "release"
    result = _assemble(release_repo, incoming, output)
    assert result.returncode != 0
    assert "outside" in result.stderr
    assert not output.exists()

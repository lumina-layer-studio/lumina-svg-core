"""Prepare source/wheel distributions with source identities. 准备带来源的发行文件。"""

from __future__ import annotations

import argparse
from email.parser import Parser
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import tomllib
from typing import Any
from zipfile import BadZipFile, ZipFile


ROOT = Path(__file__).resolve().parents[1]


def _checkout_identity() -> tuple[str, bool]:
    command = ["git", "-c", f"safe.directory={ROOT}"]
    revision = subprocess.check_output(
        [*command, "rev-parse", "HEAD"], cwd=ROOT, text=True, encoding="utf-8",
    ).strip()
    status = subprocess.check_output(
        [*command, "status", "--porcelain"], cwd=ROOT, text=True, encoding="utf-8",
    )
    return revision, bool(status.strip())


def _wheel_description(path: Path, version: str, revision: str, dirty: bool) -> dict[str, Any]:
    with ZipFile(path) as archive:
        names = archive.namelist()
        metadata_name = next((name for name in names if name.endswith(".dist-info/METADATA")), None)
        tags_name = next((name for name in names if name.endswith(".dist-info/WHEEL")), None)
        identity_name = "lumina_svg_core/_build_info.json"
        if metadata_name is None or tags_name is None or identity_name not in names:
            raise ValueError(f"{path.name}: missing package metadata or build identity")
        metadata = Parser().parsestr(archive.read(metadata_name).decode("utf-8"))
        identity = json.loads(archive.read(identity_name).decode("utf-8"))
        if metadata["Name"] != "lumina-svg-core" or metadata["Version"] != version:
            raise ValueError(f"{path.name}: package version does not match this checkout")
        if metadata["License-Expression"] != "AGPL-3.0-only":
            raise ValueError(f"{path.name}: package license does not match AGPL-3.0-only")
        expected = {"version": version, "source_revision": revision, "source_dirty": dirty}
        if identity != expected:
            raise ValueError(f"{path.name}: embedded build identity does not match this checkout")
        tags = [line[5:] for line in archive.read(tags_name).decode("utf-8").splitlines()
                if line.startswith("Tag: ")]
        if not tags:
            raise ValueError(f"{path.name}: contains no wheel compatibility tags")
    return {"filename": path.name, "kind": "python-wheel", "tags": tags}


def _sdist_description(path: Path, version: str) -> dict[str, Any]:
    with tarfile.open(path) as archive:
        metadata_name = next((name for name in archive.getnames() if name.endswith("/PKG-INFO")), None)
        if metadata_name is None:
            raise ValueError(f"{path.name}: contains no source package metadata")
        stream = archive.extractfile(metadata_name)
        if stream is None:
            raise ValueError(f"{path.name}: source metadata is not a file")
        metadata = Parser().parsestr(stream.read().decode("utf-8"))
        if metadata["Name"] != "lumina-svg-core" or metadata["Version"] != version:
            raise ValueError(f"{path.name}: source package version does not match this checkout")
        if metadata["License-Expression"] != "AGPL-3.0-only":
            raise ValueError(f"{path.name}: source license does not match AGPL-3.0-only")
    return {"filename": path.name, "kind": "source-distribution"}


def prepare_distribution(
    output_dir: Path, *, wheel_dir: Path | None = None,
    sdist_dir: Path | None = None, require_clean: bool = False,
) -> Path:
    """Prepare checked files and a deterministic source/hash receipt.

    准备核验后的发行文件，并生成确定性的源码和哈希清单。

    Args:
        output_dir: Destination directory. 输出目录。
        wheel_dir: Built wheel directory. 已构建 wheel 的目录。
        sdist_dir: Built source archive directory. 已构建源码包的目录。
        require_clean: Reject dirty release sources. 是否拒绝未提交的发行源码。

    Returns:
        Path to distribution.json. distribution.json 的路径。

    Raises:
        ValueError: Inputs are empty, inconsistent, or would overwrite files.
            输入为空、来源不一致或将覆盖已有文件。
    """
    project = tomllib.loads((ROOT / "pyproject.toml").read_text(encoding="utf-8"))["project"]
    revision, dirty = _checkout_identity()
    if require_clean and dirty:
        raise ValueError("release preparation requires a clean source checkout")
    paths: list[Path] = []
    descriptions: list[dict[str, Any]] = []
    for directory, pattern in ((wheel_dir, "*.whl"), (sdist_dir, "*.tar.gz")):
        if directory is None:
            continue
        found = sorted(directory.glob(pattern))
        if not found:
            raise ValueError(f"{directory}: contains no {pattern} files")
        for path in found:
            description = (
                _wheel_description(path, project["version"], revision, dirty)
                if path.suffix == ".whl" else _sdist_description(path, project["version"])
            )
            with path.open("rb") as stream:
                description["sha256"] = hashlib.file_digest(stream, "sha256").hexdigest()
            description["size_bytes"] = path.stat().st_size
            destination = output_dir / path.name
            if destination.exists() and destination.read_bytes() != path.read_bytes():
                raise ValueError(f"{path.name}: destination already contains different bytes")
            paths.append(path)
            descriptions.append(description)
    if not paths:
        raise ValueError("provide a wheel directory or source distribution directory")
    output_dir.mkdir(parents=True, exist_ok=True)
    for path in paths:
        destination = output_dir / path.name
        if path.resolve() != destination.resolve():
            shutil.copy2(path, destination)
    receipt = {
        "schema_version": 1, "version": project["version"],
        "source_revision": revision, "source_dirty": dirty,
        "requires_python": project["requires-python"], "license": project["license"],
        "artifacts": descriptions,
    }
    receipt_path = output_dir / "distribution.json"
    receipt_path.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    return receipt_path


def main() -> None:
    """Prepare distribution files without publishing. 准备发行文件，不执行发布。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--wheel-dir", type=Path)
    parser.add_argument("--sdist-dir", type=Path)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--require-clean", action="store_true")
    args = parser.parse_args()
    try:
        receipt = prepare_distribution(
            args.output_dir, wheel_dir=args.wheel_dir, sdist_dir=args.sdist_dir,
            require_clean=args.require_clean,
        )
    except (ValueError, OSError, BadZipFile, tarfile.TarError) as error:
        parser.error(str(error))
    print(f"Prepared distribution receipt: {receipt}")


if __name__ == "__main__":
    main()

"""Check a merged version tag and assemble checked release packages. 核验并准备发行包。"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile
import tomllib
from typing import Any
from zipfile import BadZipFile

from package_distribution import ROOT, prepare_distribution


TARGETS = {
    f"cp{python}-cp{python}-{platform}"
    for python in ("312", "313", "314")
    for platform in (
        "manylinux_2_28_x86_64", "win_amd64",
        f"macosx_{'14' if python == '314' else '12'}_0_arm64",
        f"macosx_{'14' if python == '314' else '12'}_0_x86_64",
    )
}


def _git(*arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["git", "-c", f"safe.directory={ROOT}", *arguments], cwd=ROOT,
        capture_output=True, text=True, encoding="utf-8", check=False,
    )


def check_tag(tag: str) -> tuple[str, str]:
    """Require a clean version tag on HEAD, merged into origin/dev-refactor.

    要求版本标签指向干净 HEAD，且已合入 origin/dev-refactor。
    """
    project = tomllib.loads(
        (ROOT / "pyproject.toml").read_text(encoding="utf-8")
    )["project"]
    version = project["version"]
    if tag != f"v{version}":
        raise ValueError(f"release tag must be v{version}, matching the declared version")
    cmake_version = re.search(
        r"project\(\s*lumina_svg_core\s+VERSION\s+([^\s)]+)",
        (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"),
    )
    if cmake_version is None or cmake_version[1] != version:
        raise ValueError("CMake and project versions must match")
    head = _git("rev-parse", "HEAD")
    if head.returncode:
        raise ValueError("release preparation requires a Git checkout")
    revision = head.stdout.strip()
    tagged = _git("rev-parse", "--verify", f"refs/tags/{tag}^{{commit}}")
    if tagged.returncode:
        raise ValueError(f"release tag {tag} does not exist")
    if tagged.stdout.strip() != revision:
        raise ValueError(f"release tag {tag} must point to HEAD")
    status = _git("status", "--porcelain")
    if status.returncode or status.stdout.strip():
        raise ValueError("release preparation requires a clean source checkout")
    merged = _git(
        "merge-base", "--is-ancestor", revision, "refs/remotes/origin/dev-refactor",
    )
    if merged.returncode:
        raise ValueError("release source must be merged into origin/dev-refactor")
    return version, revision


def _check_targets(receipt: dict[str, Any], version: str) -> None:
    targets: set[str] = set()
    sources = []
    mismatched_names = []
    for artifact in receipt["artifacts"]:
        if artifact["kind"] == "source-distribution":
            sources.append(artifact["filename"])
            continue
        tags = artifact["tags"]
        if len(tags) != 1 or tags[0] not in TARGETS:
            raise ValueError(f"unexpected wheel target: {tags}")
        target = tags[0]
        if target in targets:
            raise ValueError(f"duplicate wheel target: {target}")
        targets.add(target)
        if artifact["filename"] != f"lumina_svg_core-{version}-{target}.whl":
            mismatched_names.append(artifact["filename"])
    if targets != TARGETS:
        raise ValueError(f"missing wheel targets: {sorted(TARGETS - targets)}")
    if sources != [f"lumina_svg_core-{version}.tar.gz"]:
        raise ValueError("release requires exactly one matching source distribution")
    if mismatched_names:
        raise ValueError(f"wheel filenames do not match package version/target: {mismatched_names}")


def _check_receipts(
    incoming: Path, paths: list[Path], prepared: dict[str, Any],
) -> None:
    receipts = sorted(incoming.rglob("distribution.json"))
    if len(receipts) != 5:
        raise ValueError("release requires five bundle receipts")
    artifacts = {item["filename"]: item for item in prepared["artifacts"]}
    listed: set[Path] = set()
    for path in receipts:
        receipt = json.loads(path.read_text(encoding="utf-8"))
        for field in ("schema_version", "version", "source_revision", "source_dirty",
                      "requires_python", "license"):
            if receipt.get(field) != prepared[field]:
                raise ValueError(f"{path}: receipt {field} does not match release source")
        for item in receipt.get("artifacts", []):
            filename = item.get("filename", "")
            package = path.parent / filename
            if (Path(filename).name != filename or package in listed
                    or item != artifacts.get(filename) or package not in paths):
                raise ValueError(f"{path}: receipt artifact does not match package bytes/metadata")
            listed.add(package)
    if listed != set(paths):
        raise ValueError("bundle receipts have missing or unlisted packages")


def assemble_release(tag: str, incoming: Path, output: Path) -> Path:
    """Assemble twelve wheels, one sdist and their receipt without changing bytes.

    核验并汇集十二个 wheel、一个源码包和来源清单，保留原始字节。
    """
    incoming, output = incoming.resolve(), output.resolve()
    if incoming.is_relative_to(ROOT) or output.is_relative_to(ROOT):
        raise ValueError("incoming bundles and release output must stay outside the checkout")
    version, _ = check_tag(tag)
    if output.exists():
        raise ValueError("release output already exists; use a new directory")
    wheels = sorted(incoming.rglob("*.whl"))
    sources = sorted(incoming.rglob("*.tar.gz"))
    if len(sources) != 1:
        raise ValueError("release requires exactly one source distribution")
    paths = wheels + sources
    if len({path.name for path in paths}) != len(paths):
        raise ValueError("duplicate package filenames in incoming bundles")
    with tempfile.TemporaryDirectory(prefix="lumina-svg-release-") as temporary:
        staging = Path(temporary) / "incoming"
        staging.mkdir()
        for path in paths:
            shutil.copy2(path, staging / path.name)
        checked = Path(temporary) / "release"
        receipt_path = prepare_distribution(
            checked / "packages", wheel_dir=staging, sdist_dir=staging, require_clean=True,
        )
        receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
        _check_targets(receipt, version)
        _check_receipts(incoming, paths, receipt)
        receipt_path.rename(checked / "distribution.json")
        output.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(
            prefix=f".{output.name}-", dir=output.parent,
        ) as sibling:
            complete = Path(sibling) / "release"
            shutil.copytree(checked, complete)
            if output.exists():
                raise ValueError("release output already exists; use a new directory")
            complete.rename(output)
    return output / "distribution.json"


def main() -> None:
    """Validate or assemble release files; never upload them. 核验和准备，不执行上传。"""
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    check = commands.add_parser("check-tag")
    check.add_argument("--tag", required=True)
    assemble = commands.add_parser("assemble")
    assemble.add_argument("--tag", required=True)
    assemble.add_argument("--incoming-dir", type=Path, required=True)
    assemble.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "check-tag":
            version, revision = check_tag(args.tag)
            print(f"version={version}\nsource_revision={revision}")
        else:
            print(assemble_release(args.tag, args.incoming_dir, args.output_dir))
    except (ValueError, OSError, KeyError, TypeError, BadZipFile, tarfile.TarError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()

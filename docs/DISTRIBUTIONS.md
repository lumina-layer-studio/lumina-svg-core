# Distribution preparation / 发行文件准备

The manual `Prepare distributions` workflow reuses the verified wheel/source
pipeline and retains files with source/hash receipts for 90 days. It produces
reviewable artifacts; it does not publish to GitHub Releases or PyPI. Regular
PR/default-branch/tag CI uses the same checks and retains artifacts for 30 days.
Public versioned downloads remain available through GitHub Releases.

手动工作流复用同一套构建和验证流程，将安装包、源码包及来源清单保存 90 天；
普通 CI 产物保存 30 天。工作流准备文件，不自动发布；公开版本通过 GitHub Releases 下载。

## Targets / 构建目标

| Platform | Python | Baseline |
| --- | --- | --- |
| Linux x86-64 | 3.12, 3.13, 3.14 | manylinux_2_28, glibc 2.28+ |
| Windows x86-64 | 3.12, 3.13, 3.14 | Matching 64-bit CPython |
| macOS arm64 and x86-64 | 3.12, 3.13 | macOS 12+ |
| macOS arm64 and x86-64 | 3.14 | macOS 14+ |

Each wheel is built, installed, and tested through its matching interpreter.
The source distribution is independently rebuilt and tested. Alpine/musl,
Windows ARM and free-threaded Python are outside this matrix. This workflow
does not produce a standalone C++ SDK.

每个 wheel 都使用匹配的 Python 安装并测试，源码包还会单独重新构建。
不覆盖 Alpine/musl、Windows ARM 或自由线程 Python，也不生成独立 C++ SDK。

## Build identity / 构建来源

```python
import lumina_svg_core as svg

print(svg.__version__)
print(svg.__source_revision__)
print(svg.__source_dirty__)
```

Git checkouts record the actual commit and dirty status. Git-free inputs report
`"unknown"` and `None` by default. Builders can explicitly declare a full SHA
and a `0`/`1` dirty flag with `LUMINA_SVG_BUILD_REVISION` and
`LUMINA_SVG_BUILD_DIRTY`; CI uses its checked-out revision. PR builds may identify
a temporary merge commit. These fields are build declarations, not a
cryptographic source attestation.

Git 构建记录提交 SHA 和改动状态；无 Git 信息时默认未知。CI 会明确传入检出的版本。
PR 构建可能记录临时合并提交。这些字段说明构建来源，不构成源码的密码学认证。

## Local preparation / 本地准备

For release preparation, start from a reviewed clean checkout:
`git status --porcelain` must produce no output. Build with a supported interpreter:

发行准备应从已审阅且没有未提交改动的源码开始；先确认 `git status --porcelain` 没有输出。

```bash
python -m pip install build
export LUMINA_SVG_BUILD_REVISION="$(git rev-parse HEAD)"
export LUMINA_SVG_BUILD_DIRTY=0
python -m build --outdir /tmp/lumina-svg-build
python tools/verify_wheel.py /tmp/lumina-svg-build/<actual-wheel-filename>.whl --contents-only
```

These commands use a POSIX shell; in PowerShell, set the corresponding `$env:`
variables. `python -m build` builds its wheel from a Git-free source archive,
so the explicit variables preserve the reviewed checkout identity. For local
development with uncommitted changes, declare `LUMINA_SVG_BUILD_DIRTY=1`.
Direct `python -m pip wheel .` builds can discover the checkout automatically.

以上使用 POSIX shell，PowerShell 请设置对应的 `$env:` 变量。`python -m build`
从不含 Git 的源码包构建 wheel，显式变量用于保留已审阅源码的来源。
本地开发若有未提交改动，应声明 `LUMINA_SVG_BUILD_DIRTY=1`；直接从检出目录
执行 `pip wheel .` 可自动读取 Git 信息。

Prepare only files from the same reviewed checkout:

```bash
python tools/package_distribution.py \
  --wheel-dir /tmp/lumina-svg-build \
  --sdist-dir /tmp/lumina-svg-build \
  --output-dir /tmp/lumina-svg-prepared \
  --require-clean
```

`distribution.json` records version, full source SHA, dirty status, Python
requirement, license, wheel tags, sizes and SHA-256 hashes. Wheel metadata and
embedded source identities are checked against the packaging checkout before
files are copied. Source archives are checked for version/license; their source
identity describes the packaging checkout. A conflicting destination file is
rejected instead of overwritten.

清单记录版本、完整源码 SHA、改动状态、Python 范围、许可证、wheel 标签、大小和哈希。
打包前核对 wheel 内嵌来源与当前源码；源码包核对版本和许可证，来源记录指打包检出目录。
已有同名且内容不同的文件会被拒绝，发行准备要求源码没有未提交改动。

Publish only reviewed clean artifacts, retain the receipt alongside downloads,
and use a new package version for changed files. Do not replace a published
artifact under the same filename/version. Installing a matching wheel requires
no compiler; source builds still require the documented toolchain.

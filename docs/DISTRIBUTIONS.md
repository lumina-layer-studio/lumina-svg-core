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

## Consumer installation / 使用者安装

The README's `--find-links` command selects the public versioned wheel without
a third-party checkout, manual patch, local wheelhouse, or C++ compiler.
Native-library source builds are a contributor workflow, not a prerequisite
for using the library from a Python application source checkout. This project
ships the native library; it does not ship a Lumina application installer.

README 中的 `--find-links` 命令会自动选择公开 wheel，使用者无需额外克隆第三方库、
手工打补丁、准备本地 wheelhouse 或编译 C++。原生库源码构建属于贡献者工作流，
不是从 Python 应用源码调用本库的前置要求；本项目也不分发 Lumina 整套软件安装器。

On 2026-10-15, public release `v0.3.1` provides all 12 wheels in the table above.
Binary-only resolution and download of both the library and NumPy succeeds
for all 12 target combinations. A fresh macOS arm64 / CPython 3.12 environment
installs that public wheel with build tools absent and exercises actual
alpha-composited polygon/hole output. This is a public-release installation
check, not a claim that an unmerged refactor is already released. Cross-target
downloads do not replace each platform's native CI execution.

2026-10-15 已核对公开的 `v0.3.1`：12 组目标均可解析依赖并下载完整二进制 wheel，
包括 NumPy。全新的 macOS arm64 / CPython 3.12 环境无需构建工具即可安装，
并实际解析透明叠色、多边形和孔洞。这验证的是已发布版本，不代表未合入重构已发布；
跨平台下载检查不能代替各平台 CI 的实际运行。

## PyPI readiness / PyPI 发布准备

GitHub Releases already provide public installation. PyPI is a separate
convenience endpoint; upstream acceptance of the recorder patch is not a
prerequisite for either distribution route.

GitHub Releases 已可满足公开安装；PyPI 是另一个便利入口，与上游是否接收补丁无关。

Before a first PyPI upload:

1. Select a new reviewed release version; never overwrite the published 0.3.1
   files with refactored binaries under the same names.
2. Complete the wheel matrix, source rebuild, installed runtime checks and
   `python -m twine check --strict <sdist> <wheels>` metadata validation.
3. Have the project owner establish PyPI ownership and an authorized publisher.
   A GitHub login alone does not establish PyPI publishing rights. A trusted
   publisher must match the chosen repository and actual publishing workflow;
   see [PyPI's publisher setup](https://docs.pypi.org/trusted-publishers/adding-a-publisher/).
4. Keep preparation separate from upload. The current `Prepare distributions`
   workflow deliberately prepares artifacts only; it is not a PyPI publisher.

首次上传前，应使用新版本完成各平台验证及 Twine 元数据检查，并由项目 owner
确认 PyPI 归属和发布身份。GitHub 登录不等于拥有 PyPI 发布权限；可信发布者需要
匹配实际的仓库和上传工作流。当前手动工作流只准备文件，不会自动上传。

The local source archive and rebuilt wheel pass strict Twine validation.
The public PyPI project endpoint returns 404 at this check; no PyPI upload or
publisher/account configuration is performed here. Do not describe build
artifacts or an unmerged branch as a published PyPI release.

本次源码包及重建 wheel 已通过严格 Twine 检查。核对时 PyPI 公开项目接口返回 404；
本轮没有上传 PyPI，也未替用户创建账号或配置发布身份。不要将构建产物或未合入分支
描述成已发布的 PyPI 版本。

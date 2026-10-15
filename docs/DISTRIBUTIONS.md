# Distribution preparation / 发行文件准备

The manual `Prepare distributions` workflow reuses the verified wheel/source
pipeline and retains files with source/hash receipts for 90 days. It produces
reviewable artifacts; it does not publish to GitHub Releases or PyPI. Regular
PR/default-branch CI uses the same checks and retains artifacts for 30 days.
The separate `Publish PyPI release` workflow reuses those builds with 90-day
retention after validating a merged release tag. Public versioned downloads
remain available through GitHub Releases.

手动工作流复用同一套构建和验证流程，将安装包、源码包及来源清单保存 90 天；
普通 CI 产物保存 30 天。手动准备工作流不发布；独立 PyPI 发布工作流先验证已合入的
发行标签，再复用构建并保存 90 天。既有公开版本仍可通过 GitHub Releases 下载。

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

## Complete release assembly / 完整发行组装

`package_distribution.py` can prepare individual platform bundles for review.
`prepare_release.py` coordinates an actual upload candidate and reuses that
metadata/license/source/hash validation with `require_clean=True`:

```bash
python tools/prepare_release.py check-tag --tag v0.3.2
python tools/prepare_release.py assemble \
  --tag v0.3.2 \
  --incoming-dir /tmp/lumina-svg-incoming \
  --output-dir /tmp/lumina-svg-release
```

The tag must exist on HEAD, equal the version declared in both `pyproject.toml`
and CMake, and be reachable from `origin/dev-refactor`. Fetch the current remote
branch and tags before a local release check. Incoming downloads and output
must be outside the source checkout. Keep the four wheel bundles and the sdist
bundle in separate subdirectories so their `distribution.json` receipts do not
overwrite each other.

Assembly requires exactly the 12 target wheels above and one matching sdist.
It checks each bundle's clean source SHA/version and the exact artifact list,
hashes, sizes and wheel tags against the packages. Missing, duplicate, extra or
unrelated targets and inconsistent receipts are rejected. The original package
bytes are copied to `packages/`; the combined `distribution.json` remains
beside that directory. An existing output directory is rejected. Only the
13 packages, not the JSON receipt, are uploaded to PyPI.

Source archive provenance is the clean checkout declared by its build-bundle
receipt; the archive's package version and license are checked as before.
This does not add a new embedded sdist identity format or a cryptographic source
attestation.

完整发行必须是 HEAD 上与 Python/CMake 版本一致、已合入 `origin/dev-refactor` 的标签。
五组下载目录分别保存清单，输入和输出都放在源码目录之外。组装核对 12 个 wheel、
1 个源码包、相同的干净源码 SHA、清单和实际文件字节；缺失、重复、多余或不一致即失败。
最终目录包含 `packages/` 和旁边的合并清单，上传仅使用 13 个包。源码包来源沿用构建
清单声明，不引入新的包格式或密码学认证。

Publish only reviewed clean artifacts, retain the receipt alongside downloads,
and use a new package version for changed files. Do not replace a published
artifact under the same filename/version. Installing a matching wheel requires
no compiler; source builds still require the documented toolchain.

## Consumer installation / 使用者安装

After 0.3.2 is available on PyPI, the standard command is:

```bash
python -m pip install lumina-svg-core==0.3.2
```

Until publication is confirmed by the
[PyPI version page](https://pypi.org/project/lumina-svg-core/0.3.2/) and
[publishing workflow](https://github.com/lumina-layer-studio/lumina-svg-core/actions/workflows/publish.yml),
the README's exact 0.3.1 GitHub `--find-links` command remains the public fallback.
Either matching prebuilt wheel installs without
a third-party checkout, manual patch, local wheelhouse, or C++ compiler.
Native-library source builds are a contributor workflow, not a prerequisite
for using the library from a Python application source checkout. This project
ships the native library; it does not ship a Lumina application installer.

0.3.2 正式出现在 PyPI 并通过发布验证后，可使用标准安装命令；在此之前使用 README
中保留的 0.3.1 GitHub 备用命令。匹配的预编译 wheel 无需额外克隆第三方库、
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

## PyPI publisher and release gate / PyPI 发布身份与门禁

GitHub Releases already provide public installation. PyPI is a separate
convenience endpoint; upstream acceptance of the recorder patch is not a
prerequisite for either distribution route.

GitHub Releases 已可满足公开安装；PyPI 是另一个便利入口，与上游是否接收补丁无关。

The project owner must authorize this exact PyPI Trusted Publisher; GitHub login
alone does not grant PyPI ownership. See
[PyPI's publisher setup](https://docs.pypi.org/trusted-publishers/adding-a-publisher/).

| Publisher field | Exact value |
| --- | --- |
| PyPI project | `lumina-svg-core` |
| GitHub owner | `lumina-layer-studio` |
| Repository | `lumina-svg-core` |
| Workflow filename | `publish.yml` |
| GitHub environment | `pypi` |

The pending publisher and GitHub environment are configured for this candidate.
That authorizes a future upload; it does not publish the PyPI project or version.
Confirm actual availability using the version page and completed workflow above.

The GitHub `pypi` environment accepts `v*` tags and uses the maintainer
`MOVIBALE` as required reviewer. Self-review is permitted for the sole maintainer.
This asks for one formal upload approval after the release packages are prepared;
ordinary builds and end-user installation need no publishing authorization.

1. A human merges recorder refactor PR #3, then the separate 0.3.2 release PR,
   after their checks pass. Neither workflow merges a PR automatically.
2. Create `v0.3.2` only on the resulting reviewed, clean release commit already
   merged into `dev-refactor`. Never tag an unmerged topic branch for upload.
3. A `v*` tag push starts `publish.yml`. Manual dispatch must also select the
   actual version tag: default/topic branch dispatch is rejected. The checked-out
   commit must equal `github.sha`; the tag, CMake and Python versions must agree.
4. Non-credentialed jobs build/rebuild/install/test the platform wheels and
   sdist, retain the five bundles for 90 days, assemble the complete checked set,
   and run `python -m twine check --strict` on its 13 packages.
5. The `pypi` environment gate authorizes the upload job. Only that job receives
   `id-token: write`; its pinned PyPI action downloads the checked set and uploads
   packages using Trusted Publishing. There is no stored PyPI token or
   `skip-existing` bypass. Never overwrite any existing PyPI/GitHub release file;
   changed bytes require a new version.
6. After upload, a fresh virtual environment installs the exact released version
   as binary wheels only from default public PyPI, without cache, local wheels or
   extra indexes. Its pip JSON report is retained for 90 days with download URLs.
   It checks the installed version and source SHA and executes real alpha/hole
   analysis, read-only int64 coordinates and float64 RGBA. An install or functional
   failure fails the job; no arbitrary retry masks it.

本候选已登记以上精确匹配的待用可信发布者，也已配置仅接受 `v*` 标签的 `pypi` 环境。
授权配置完成不代表项目或版本已经上传；实际可用状态仍以版本页面和工作流为准。
维护者 `MOVIBALE` 在完整制品通过检查后进行一次正式上传批准；普通构建和用户安装
无需额外发布授权。先人工合入 PR #3 和发行 PR，再对干净、已合入的提交创建新标签。
分支手动触发不能上传；只有最后上传任务获得 OIDC 权限，随后从 PyPI 的全新安装
必须通过实际二维几何烟测。不要把候选构建或未合入分支描述成已经公开的 PyPI 发行。

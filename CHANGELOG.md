# Changelog

## Unreleased

- Avoid excessive curve subdivision in clipped-document sizing when the
  recorded viewport already fixes the final dimensions. Preserve the union of
  viewport and conservative paint bounds, including transform roundoff.
- Keep final clipping, physical error budgets, tiny representable components,
  and cancelled/clipped-out paint diagnostics. Visible overflow and documents
  without a usable viewport retain the existing bounds path.

## 0.3.3 — Canonical-boundary fixes

- Preserve thin positive faces and holes with exact integer area, winding and
  edge-order decisions, including cancellation beyond signed 128-bit sums.
- Round each rational intersection consistently in either segment direction
  and account for canonical-boundary displacement in the existing physical
  error budget. Keep the public coordinate domain and result schema unchanged.
- Bound integer noding by the original arrangement rather than coordinate
  span. Rebuild only genuinely contacting computation sets from original
  segments; do not attract unrelated components through nearby grid cells.
- Rebuild exact multi-ring shell/hole ownership instead of trusting a
  simple-contact audit. Reuse this ownership for simple alpha components;
  preserve all original oriented contours in complex computational groups.
- Export already normalized, oriented coverage through that same exact rule
  without a redundant Union that could delete thin positive-area components
  or fill an existing empty core. Preserve required fill/clip/compositing
  operations. Deterministic component order or ring starts can differ.
- Add public synthetic regressions without redistributing private originals.
  Full-corpus, performance and distribution acceptance are tracked in the
  pull request associated with issue #5.

0.3.3 修正原生二维边界的整数精度、交点一致性及外壳/孔洞层级，
按原始真实接触关系有界拆分，不反复拆分已弯折的边，也不移动无关组件。
删除导出时重复的合并，保住仍可表达的小色块和空白孔，保留必要的填充、裁剪与合成。
保留现有坐标和结果协议，不涉及配色、网格或 3MF。公开回归只使用合成素材；
完整素材库、性能与跨平台包的验收记录在工单 #5 关联的 PR 中。

## 0.3.2 — PyPI publishing and recorder ownership

- Provide standard installation with `python -m pip install lumina-svg-core==0.3.2`
  from PyPI, with the complete 12-wheel matrix and source distribution.
  Existing release files are not replaced.
- Publish only a clean version tag on a commit merged into `dev-refactor`.
  Assemble the complete 12-wheel matrix and matching sdist, verify five source/hash
  receipts and strict Twine metadata, then use the protected `pypi` environment.
  A fresh environment installs the resulting public PyPI version and exercises
  alpha compositing, holes, read-only coordinates and float64 RGBA.
- Include the reviewed recorder ownership refactor: upstream additions decrease
  from 5,452 to 2,917 lines (46.5%), and the complete patch decreases from 7,049
  to 4,518 lines (35.9%). Keep native dependency pins, API, schema and runtime
  behavior unchanged; see [the ownership boundary](docs/recorder-boundary.md).
- Record the existing corpus limitation without treating failures as passes:
  at 220 mm / 0.02 mm with strokes enabled, both builds return 504 supported
  and four `native_geometry_error` results from 508 private SVGs, with zero
  output differences. All four record paints successfully; two also fail
  without strokes. These are existing 2D geometry errors, outside 3MF export.

0.3.2 已发布到 PyPI，提供 12 个平台/Python 组合的 wheel 和源码包，可使用标准安装命令。
发布流程核对已合入的标签、完整 13 个包及来源清单，再通过受保护环境上传；
原生接口、协议和依赖版本保持不变。既有 508 份样本对照中的 4 个二维几何失败
继续明确保留，不计入成功，也不归因于 3MF 导出。

## 0.3.1 — Python and distribution alignment

- Support CPython 3.12, 3.13 and 3.14 on all four existing platform targets.
- Build Linux wheels against the manylinux_2_28 / glibc 2.28 baseline.
- Expose installed version, source revision and dirty status from build metadata.
- Prepare checked wheels/source archives with `distribution.json` source/hash
  receipts and a manual workflow that retains review artifacts for 90 days.
- Preserve AGPL-3.0-only licensing and the geometry schema/API. Python 3.14
  macOS wheels target 14+; existing 3.12/3.13 targets retain macOS 12+.

## 0.3.0 — Initial standalone release

- Publish the SVG geometry engine as an independent repository under
  AGPL-3.0-only, retaining third-party licenses and attributions.
- Expose SVG file/string analysis, paint recording, integer-grid projection,
  and canonicalization through CPython 3.12 bindings.
- Preserve fills, strokes, clipping, paint order, holes, solid-paint alpha,
  group opacity, and ordered composite provenance in result schema 4.
- Include read-only NumPy result buffers, non-area fragment diagnostics,
  parallel replay, and the internal enclosed-hole selection extension.
- Provide platform wheels, source distributions, reproducible fixtures, and
  cross-platform build/install/regression checks.

This release is alpha. See [SVG support](docs/support.md) for unsupported
semantics and [installation](README.md#installation) for platform limits.

# Changelog

## 0.3.2 — Release candidate: PyPI publishing and recorder ownership

- Prepare standard installation with `python -m pip install lumina-svg-core==0.3.2`
  once the version is published on PyPI; retain the public 0.3.1 GitHub wheels
  as the fallback while publication is pending. Existing release files are not replaced.
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

0.3.2 为发布候选；只有 PyPI 出现该版本且发布工作流通过后，标准安装命令才可用。
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

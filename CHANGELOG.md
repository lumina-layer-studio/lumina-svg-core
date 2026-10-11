# Changelog

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

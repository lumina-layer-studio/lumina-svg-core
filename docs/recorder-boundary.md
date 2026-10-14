# Recorder ownership and upstream patch boundary

## Purpose

The recorder extension is still built against the pinned Neroued LunaSVG
revision. This refactor moves independently computable recorder algorithms out
of upstream implementation files and into this repository. It does not replace
LunaSVG's DOM, CSS cascade, layout, or traversal with a second implementation.

The comparison base is `50c1dd5b57c15b4cccb3c51c08134aea01338a42`
(`v0.3.1`). The Python signatures, schema 4, dependency revisions, package
version, and coordinate ownership are unchanged. No material matching, mesh
construction, 3MF export, printer-grid policy, or compensation algorithm changes
are included.

## Code ownership

The project-owned modules live under `include/lumina_svg_core/recorder/` and
`src/recorder/`:

| Module | Responsibility | Inputs at the upstream bridge |
| --- | --- | --- |
| `numeric` | Exact source numbers, absolute-unit conversion and length tokens | Source text, scalar layout values |
| `path_precision` / `source_area` | Path parsing work, rounding bounds and source-subpath area potential | Source text, scalar output fields |
| `transform` | Exact affine construction and transform error estimates | Source text, public paint transforms |
| `exact_path` | Exact paths, arcs, ellipses, rectangles and point lists | Source text, resolved primitive dimensions, public paint commands |
| `alpha` | Source decimal alpha and color-alpha classification | Source text, resolved color validity and alpha |
| `capability` | Attribute capability classification | Names, values and scalar element/property classifications |

These modules do not include LunaSVG's private `source/` headers or PlutoVG's
private headers. The small lexical compatibility helper `src/recorder/text.h`
is project-local, not an include into an upstream source tree. Adapted upstream
code retains the MIT attribution in
[THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).

The project CMake file explicitly adds the implementations to the existing
LunaSVG static target. Both the Python extension and optional raster oracle
therefore use the same implementations. There is no generated source, textual
`.inc` injection, runtime plugin, or extra shared library to distribute.

The patch is an internal input to this package's CMake build, not a standalone
LunaSVG distribution. A source distribution must carry the owned modules as
well as the patch; existing `include/**` and `src/**` inclusion rules cover
them. Installed-wheel users do not compile or apply the patch themselves.

## Measured source change

| Measurement | Baseline | Refactor |
| --- | ---: | ---: |
| Complete LunaSVG/PlutoVG patch lines, including diff context | 7,049 | 4,518 |
| Added upstream source lines | 5,452 | 2,917 |
| Removed upstream source lines | 213 | 213 |
| Touched upstream files | 16 | 16 |

Upstream additions decrease by 46.5%; the complete patch file decreases by
35.9%. The 14 project-owned recorder files contain 2,977 lines. The
implementation commit is a net increase of 490 repository lines, mostly from
explicit interfaces and the independent lexical compatibility helper. This is
a reduction in upstream ownership and coupling, not a claim that thousands of
lines of necessary algorithms were deleted or that the fork disappeared.

## What remains in the patch

- The versioned public paint-command data types and recorder entry point. These
  return copied data and do not expose private scene-tree objects.
- DOM property access, inherited layout values, viewport resolution, CSS
  selector matching/cascade, references and `<use>` expansion. Their authority
  remains the actual LunaSVG document being traversed.
- Paint-order, group/clip boundaries, source identity, exact-value attachment,
  and render traversal hooks. The bridge converts private layout objects to
  source text/scalars/public paint buffers at these sites.
- Existing upstream parser/semantic fixes and allocation-failure propagation.
  Moving those into a separate parser would create another interpretation of
  the document rather than reduce the recorder's private-object coupling.
- PlutoVG path allocation checks and failure state. They must run where the
  C path buffer is allocated; an external recorder helper cannot detect a
  failed internal reallocation after the fact.
- PlutoVG's existing `pi/8` arc subdivision. The owned exact-arc construction
  and fallback precision estimate assume the same lowering contract; marker
  origins still use the upstream float path. Removing this one-line change
  alone would change those paths or invalidate the recorded error estimate.

Real numeric bounds and allocation-failure handling are not arbitrary limits on
ordinary documents. This refactor does not introduce new document caps or a
production shadow-analysis pass. The repeated source-index limit branch after
an unconditional exception is removed because it was unreachable.

`cmake/Dependencies.cmake` still validates the pinned revisions and the exact
patched dependency diff. The patch must be regenerated against that revision;
changing the verification to accept an unknown dependency tree is not an
upgrade procedure.

## Validation

The before/after validation records are kept outside the repository. No private
SVG, source digest, or user filename is committed.

The validation compares the baseline and candidate installed wheels, not two
imports of one checkout. Public-entry snapshots include raw, visible and
silhouette buffers, exact RGBA, paint/source provenance, clip/group commands,
component/hole offsets, status/reason and unprintable-fragment data. Timing
fields are excluded from equality checks.

At implementation review, the unchanged baseline passes 613 Python tests and
the refactor passes 614, including a new inherited mixed-unit stroke-decimal
regression. That regression also passes on the baseline and fails under a
disposable float-rounding mutation. The restored implementation passes native
CTest and the full 614-test suite after an independent source-archive-to-wheel
rebuild and installation. Both independent specification and code-quality
reviews report no required fixes.

The PR validation record additionally compares 508 private SVGs at 220 mm and
all ten public fixtures at 111 mm / 220 mm, with strokes enabled and disabled.
Only after collecting the comparisons and platform checks is the PR ready.
Warm timing comparisons use the same compiler, Release configuration, warmups
and sample count; a bulk-verification worker count is not reported as a parser
speed improvement. Actual results belong in the PR's validation record, not
in a universal latency assertion.

## Maintenance after this change

When updating LunaSVG, first reapply only the remaining bridge/semantic patch
to the proposed pinned revision. Resolve changes to the DOM/layout/traversal
at their bridge sites; test the project-owned recorder algorithms normally.
Do not copy them back into upstream private files to make an update compile.
Run the complete semantic suite, native CTest, source-distribution rebuild and
platform wheel matrix before adopting the new revision.

This reduces the upstream diff; it does not make the project patch-free or
prove every possible SVG feature supported. The support policy remains in
[support.md](support.md). No new release is published automatically by this
source refactor.

## Remaining upstream work: classification, not new PRs

| Category | Current decision | Possible future upstream work |
| --- | --- | --- |
| Independent algorithms and capability policy | Owned modules in this repository; removed from upstream implementation files | None required for their maintenance |
| Copied paint-command API and DOM/layout/traversal hooks | Necessary recorder interface; keep versioned and pinned | Propose a recorder interface only with a separate, agreed upstream design |
| XML/entity/CSS correctness repairs | Keep the existing parser as the single authority | Extract minimal, renderer-reproducible fixes from the larger recorder patch |
| PlutoVG allocation failure handling | Keep at the allocation site; never emit a truncated path as success | Small general allocation/error-propagation fixes are candidates |
| PlutoVG arc lowering | Retain the existing error/marker contract | A configurable lowering-quality interface may be appropriate; do not change every renderer's default just for this consumer |
| Clipper2 over-limit SVG miter fallback | Retain the one-line bevel fallback patch | An explicit fallback option would be preferable to changing Clipper2's universal square default |

Selecting bevel joins for every stroke is not a replacement for the Clipper2
patch: SVG requires miters below the limit and bevels only above it. Likewise,
moving source-text algorithms does not eliminate the need to observe the real
layout/traversal. Upstream acceptance is independent of wheel distribution;
no upstream PR or maintainer contact is created by this refactor.

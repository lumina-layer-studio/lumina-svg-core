# SVG support and numerical limits

`lumina-svg-core` implements static, solid-paint SVG geometry. It is not a
general browser renderer. Unsupported document semantics produce one explicit
whole-document result, rather than a partial rendering.

## Supported operations

The engine handles static solid fills and strokes, fill rules, affine
transforms, vector clipping, supported CSS selectors, same-document `<use>`,
basic shapes, SVG path arcs, nested SVG viewports, root layout/transforms,
marker paints and marker viewport clipping. Stroke operations include caps,
joins, supported dash patterns, and non-scaling strokes.

Solid-paint alpha and isolated object/group opacity use normal source-over
compositing in premultiplied sRGB. The root canvas is transparent. Composites
retain ordered contributor identities and weights. Fully opaque documents use
the reverse-occlusion path.

## Unsupported operations

Examples include masks, gradients/patterns, text, images, external resources,
non-normal blend modes, non-sRGB color interpolation, conditional processing,
and unsupported CSS rules/properties. Other ambiguous semantics, including
duplicate nonempty XML IDs and empty clips whose meaning is unsupported, are
reported explicitly. These examples do not form an exhaustive allowlist;
always use the returned status and reason code.

Dash arrays containing a zero-length entry return
`unsupported_zero_length_dash`: exact cap and phase behavior is not established
for every open, closed, and degenerate path.

## Precision and error handling

Each analysis receives one positive physical tolerance. Resolved input
coordinates, transforms, stroke parameters, and curve/dash operations must fit
that error contract. Precision that cannot be established returns
`unsupported_input_precision`; the engine does not silently enlarge the
tolerance or simplify away small area-bearing features.

Malformed XML, file errors, invalid numeric state, actual allocation failures,
and recursion cycles still fail. Desktop input is not rejected merely because
of a configured byte, element, command, CSS-work, or elapsed-time quota. Integer
overflow checks remain in place. Callers processing untrusted input should
provide their own process isolation and operating-system resource policy.

Independent normalization may run concurrently; `LUMINA_SVG_REPLAY_THREADS`
selects a positive worker count, capped at 16. Workers commit results in
original paint order. If a parallel-only execution failure occurs, staged state
is discarded and one serial replay is attempted.

## Known corpus limitation

The recorder ownership refactor was compared against the previous implementation
using 508 private SVGs at a target width of 220 mm, tolerance 0.02 mm and strokes
enabled. Both builds return 504 `supported` results and four
`native_geometry_error` results, with zero output differences. The four failures
are not counted as passing cases. Paint recording succeeds for all four;
two also fail during fill-only analysis with strokes disabled.

These errors already exist in the native 2D geometry stage. This release does
not repair them, and they are unrelated to mesh or 3MF generation. Private
inputs are not redistributed. The existing comparison evidence is recorded
in [recorder refactor PR #3](https://github.com/lumina-layer-studio/lumina-svg-core/pull/3);
release coordination does not rerun or broaden that corpus/performance claim.

此前录制器重构对比了 508 份私有 SVG：目标宽度 220 mm、容差 0.02 mm、启用描边。
旧版和新版均为 504 份 `supported`、4 份 `native_geometry_error`，输出差异为零；
4 个失败不计作通过。它们都能完成绘制录制，其中 2 份关闭描边、仅分析填充时也失败。
这些是既有原生二维几何错误，本次发行不修复，也不涉及网格或 3MF 导出。
私有输入不随包发布，发行流程不重新扩大既有样本或性能验证结论。

## Scope of the output

The output contains polygon geometry, not meshes, slicer settings, printer
materials, backing layers, or 3MF archives. The parser does not perform
morphological closing, minimum-area filtering, printer-grid snapping,
triangulation, or mesh repair. Non-area fragments have explicit diagnostics.
Use [the coordinate and result contract](api.md) before passing the output to
a downstream print workflow.

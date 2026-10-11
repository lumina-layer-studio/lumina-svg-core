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

## Scope of the output

The output contains polygon geometry, not meshes, slicer settings, printer
materials, backing layers, or 3MF archives. The parser does not perform
morphological closing, minimum-area filtering, printer-grid snapping,
triangulation, or mesh repair. Non-area fragments have explicit diagnostics.
Use [the coordinate and result contract](api.md) before passing the output to
a downstream print workflow.

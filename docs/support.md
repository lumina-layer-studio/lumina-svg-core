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
tolerance or apply a minimum-area feature filter.

Malformed XML, file errors, invalid numeric state, actual allocation failures,
and recursion cycles still fail. Desktop input is not rejected merely because
of a configured byte, element, command, CSS-work, or elapsed-time quota. Integer
overflow checks remain in place. Callers processing untrusted input should
provide their own process isolation and operating-system resource policy.

Independent normalization may run concurrently; `LUMINA_SVG_REPLAY_THREADS`
selects a positive worker count, capped at 16. Workers commit results in
original paint order. If a parallel-only execution failure occurs, staged state
is discarded and one serial replay is attempted.

## Canonical-boundary fixes

The recorder ownership refactor was compared against the previous implementation
using 508 private SVGs at a target width of 220 mm, tolerance 0.02 mm and strokes
enabled. The pre-refactor build and published 0.3.2 returned 504 `supported` results and four
`native_geometry_error` results, with zero output differences. The four failures
are not counted as passing cases. Paint recording succeeds for all four;
two also fail during fill-only analysis with strokes disabled.

These errors originate in native 2D geometry, not mesh or 3MF generation.
The historical comparison remains recorded in
[recorder refactor PR #3](https://github.com/lumina-layer-studio/lumina-svg-core/pull/3).

Version 0.3.3 corrects the mechanisms in the same canonical
boundary module: exact area/winding/edge ordering, direction-independent
rational intersection rounding, and bounded noding of original integer
linework. Only exact-contact computation sets are routed through their owned
rounding cells; unrelated sets remain unchanged. Any newly meeting sets rebuild
from original segments, not previously bent output. Each operation incurs at
most one conservative grid-unit boundary displacement, regardless of rebuild
count. Independent raw/visible/silhouette exports share the maximum operation
bound; it is charged to the existing physical error budget, not a new grid or
tolerance. Point/line/empty fragments remain separate from area-bearing
polygons. No open face is silently accepted.

A simple-contact audit does not establish ring containment. Multi-ring exports
rebuild the exact shell/hole tree rather than trusting upstream ownership
metadata. Alpha decomposition shares this tree for simple linework; complex
linework keeps all original oriented contours of that paint together until
composition. Those computational groups are not output polygon components and
are never formed by distance.

Export consumes already normalized, oriented coverage directly. A second
Union is not required to resolve its SVG semantics and can erase a thin
positive-area component or fill a real empty core. Necessary fill-rule,
stroke, clip and compositing Boolean operations retain their existing role.
Exact canonical ownership replaces only the redundant export operation;
coordinate buffers can have different deterministic ring starts or component
ordering without changing their region or paint identity.

A boundary-distance bound is not an exact topology guarantee below the
numerical grid. Sub-grid connections can contract into shared vertices; an
empty region can remain empty while no longer being an owned hole ring.
Component/hole counts alone therefore do not establish source equivalence.
The existing fragment diagnostics are not an exhaustive detector of every
sub-grid topology change inside Boolean or rounding operations. No printer
resolution, minimum printable area or hole compensation is applied here.

Public synthetic regressions cover half-unit areas and holes, nearly parallel
point-touch components, reversal-independent intersections, coordinate-span
independent noding, locality, idempotence and certified displacement. Private
originals are not redistributed or matched by input identity. Whole-corpus
geometry and warm performance evidence
are reported in the pull request associated with
[issue #5](https://github.com/lumina-layer-studio/lumina-svg-core/issues/5).
PR CI artifacts are not the published PyPI files; use the versioned PyPI page
for installation.

此前录制器重构对比了 508 份私有 SVG：目标宽度 220 mm、容差 0.02 mm、启用描边。
旧版和新版均为 504 份 `supported`、4 份 `native_geometry_error`，输出差异为零；
4 个失败不计作通过。它们都能完成绘制录制，其中 2 份关闭描边、仅分析填充时也失败。
这些是既有原生二维几何错误，与网格或 3MF 导出无关。0.3.3 在同一
规范边界模块中修正精确面积、绕数和边排序，让同一个交点不因计算方向不同而舍入到
不同格点，并按原始真实交点分组做有界节点化，不移动无关组。新接触只会促使相关组
从原始线段重建，不反复拆分已经弯折的输出；每次操作的额外保守边界误差最多一格，
不随重建轮数累积。独立的 raw、visible、silhouette 导出取最大值，仍计入原有物理
误差预算，不增加坐标网格或放宽容差。不闭合的面不会被当成成功结果。
没有交叉不等于包含层级正确：多环输出重新建立精确的外壳、孔洞关系，不直接信任上游
层级。简单轮廓的透明分组复用同一棵树；复杂轮廓保留该次绘制的全部原始有向边界。
这些计算分组不是输出组件，也不按距离合组。
导出直接使用已有的规范有向区域，不再重复合并；这次多余的合并曾删除正面积小组件、
填掉真实空白核心。必要的填充规则、描边、裁剪及合成布尔运算保持不变。环起点或组件
排列可能确定性地变化，但不因此改变区域或绘制身份。
边界距离误差不保证数值网格以下的连接关系：极细连接可能收缩为接点；一块空白仍然
保留，也可能不再归属于单个外壳的孔环。因此不能只靠组件数或孔环数判断是否与源图
等价。既有片段诊断也不能穷尽布尔运算或舍入中的每一种亚格点拓扑变化。这里不使用
打印机精度、最小可打印面积或小洞补偿。
公开回归使用合成小例子验证面积、洞、独立组件、方向一致性、有界点数、局部性和误差。
原始私有素材不随包发布，不按素材身份匹配；完整素材库的几何及暖运行性能结果记录在
上述工单关联的 PR 中。PR 的 CI 构建文件不是 PyPI 正式发行文件。

## Scope of the output

The output contains polygon geometry, not meshes, slicer settings, printer
materials, backing layers, or 3MF archives. The parser does not perform
morphological closing, minimum-area filtering, printer-grid snapping,
triangulation, or mesh repair. Non-area fragments have explicit diagnostics.
Use [the coordinate and result contract](api.md) before passing the output to
a downstream print workflow.

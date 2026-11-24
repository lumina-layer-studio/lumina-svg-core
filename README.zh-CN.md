# lumina-svg-core

[![Build wheels](https://github.com/lumina-layer-studio/lumina-svg-core/actions/workflows/wheels.yml/badge.svg?branch=dev-refactor)](https://github.com/lumina-layer-studio/lumina-svg-core/actions/workflows/wheels.yml)
[![Python 3.12–3.14](https://img.shields.io/badge/python-3.12%E2%80%933.14-blue.svg)](https://www.python.org/downloads/)
[![License: AGPL v3](https://img.shields.io/badge/license-AGPL--3.0--only-blue.svg)](LICENSE)

[English](README.md) · [简体中文](README.zh-CN.md) · [安装](#安装) · [快速上手](#快速上手) · [API 文档](docs/api.md) · [支持范围](docs/support.md)

面向打印工作流的 C++17 与 Python 原生二维 SVG 几何分析引擎。

`lumina-svg-core` 解析 SVG 的填充与描边操作，将其转换为平面多边形，并以只读 NumPy 数组返回给 Python，保留遮挡顺序、孔洞、裁剪路径、纯色透明度叠加及颜色来源。

本项目最初为 [Lumina Studio](https://github.com/lumina-layer-studio/Lumina-Layers) 开发，现于本独立仓库维护，包含 C++ 几何内核、pybind11 绑定、测试集与 wheel 构建工具。

**当前版本：** `0.3.4`（Alpha） · **支持 Python：** CPython `3.12`、`3.13`、`3.14`

## 安装

### 安装预编译包

```bash
python -m pip install lumina-svg-core==0.3.4
```

预编译 wheel 已内置固定版本的 LunaSVG/PlutoVG 与 Clipper2（也可从 [GitHub Releases](https://github.com/lumina-layer-studio/lumina-svg-core/releases) 下载安装）：

| 系统 | 架构 | 最低环境要求 |
| --- | --- | --- |
| Windows | x86-64 | 64 位 CPython 3.12–3.14 |
| macOS | Apple Silicon（`arm64`）与 Intel（`x86_64`） | macOS 12+（Python 3.12/3.13），macOS 14+（Python 3.14） |
| Linux | x86-64 | `manylinux_2_28`（glibc 2.28+） |

### 从源码编译

需要 CPython 3.12–3.14、Git、网络连接（首次构建拉取固定版本的 LunaSVG 与 Clipper2）以及 C++17 编译器（MSVC、Apple Clang 或 GCC/Clang）：

```bash
git clone https://github.com/lumina-layer-studio/lumina-svg-core.git
cd lumina-svg-core
python3.12 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install .
```

Windows PowerShell 请在 Visual Studio 开发者终端中执行 `py -3.12 -m venv .venv` 与 `.venv\Scripts\Activate.ps1`。

## 快速上手

```python
import lumina_svg_core

svg = """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 20 10">
  <rect width="20" height="10" fill="#e63946"/>
  <circle cx="10" cy="5" r="3" fill="#457b9d" fill-opacity="0.5"/>
</svg>"""

result = lumina_svg_core.analyze_svg_svg(
    svg,
    target_width_mm=40.0,
    tolerance_mm=0.02,
)
if result.status != "supported":
    raise RuntimeError(f"SVG analysis failed: {result.status}: {result.reason_code}")

print(result.visible.geometry_count)
print(result.visible.rgba_f64)

# 将整数计算网格坐标换算为毫米：
coordinates_mm = (
    result.visible.coordinates.astype("float64")
    * result.grid_to_source_unit
    * result.scale_factor
)
```

- **分析文件：** `lumina_svg_core.analyze_svg("drawing.svg", target_width_mm=40.0, tolerance_mm=0.02)`（支持 `str` 或 `pathlib.Path`）。
- **仅提取填充：** 传入 `allow_strokes=False` 可忽略描边。
- **三层几何视图：** `result.raw` 为消隐前的绘制多边形，`result.visible` 为消隐与混色后的可见区域，`result.silhouette` 为整体外轮廓。

多边形环遍历、状态码、构建来源字段（`__version__`、`__source_revision__`、`__source_dirty__`）及底层接口见 [docs/api.md](docs/api.md) 与 [docs/DISTRIBUTIONS.md](docs/DISTRIBUTIONS.md)。

## 支持的 SVG 特性

| 类别 | 支持内容 |
| --- | --- |
| 纯色填充与描边 | 填充规则（`nonzero`、`evenodd`）、线帽、连接样式、虚线数组与 `non-scaling-stroke` |
| 基础图形与路径 | `<rect>`、`<circle>`、`<ellipse>`、`<polygon>`、`<polyline>`、`<line>` 以及 `<path>` 曲线与圆弧 |
| 视口与变换 | 仿射 `transform`、`viewBox` 原点偏移、嵌套 `<svg>` 视口与百分比长度 |
| 样式与引用 | 受支持的 CSS 选择器与同文档 `<use>` 引用 |
| 裁剪与标记 | `<clipPath>` 矢量裁剪、marker 绘制及视口裁剪 |
| 透明度合成 | 纯色透明度与对象/分组 `opacity`，在预乘 sRGB 空间合成 |

超出二维纯色子集的文档会返回 `status == "unsupported"` 及具体 `reason_code`；支持边界与数值范围见 [docs/support.md](docs/support.md)。

## 开发与本地验证

```bash
python -m pip install ".[test]" build
python -m pytest tests -q
python -m build
python tools/verify_wheel.py dist/<wheel-filename>.whl --contents-only
```

开发约定见 [CONTRIBUTING.md](CONTRIBUTING.md)，基准测试说明见 [benchmarks/README.md](benchmarks/README.md)。

## 许可证与致谢

采用 **AGPL-3.0-only** 许可证，见 [LICENSE](LICENSE)。

LunaSVG、PlutoVG、Clipper2、pybind11 与 ChromaPrint3D 的第三方声明和许可证全文见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) 和 [THIRD_PARTY_LICENSES/](THIRD_PARTY_LICENSES)。Bug 反馈请提交至 [GitHub Issues](https://github.com/lumina-layer-studio/lumina-svg-core/issues)，安全问题见 [SECURITY.md](SECURITY.md)。

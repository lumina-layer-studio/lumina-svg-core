# lumina-svg-core

[![Build wheels](https://github.com/lumina-layer-studio/lumina-svg-core/actions/workflows/wheels.yml/badge.svg?branch=dev-refactor)](https://github.com/lumina-layer-studio/lumina-svg-core/actions/workflows/wheels.yml)
[![Python 3.12–3.14](https://img.shields.io/badge/python-3.12%E2%80%933.14-blue.svg)](https://www.python.org/downloads/)
[![License: AGPL v3](https://img.shields.io/badge/license-AGPL--3.0--only-blue.svg)](LICENSE)

[English](README.md) · [简体中文](README.zh-CN.md) · [安装](#安装) · [快速上手](#快速上手) · [API 文档](docs/api.md) · [支持范围](docs/support.md)

面向打印工作流的 C++17 与 Python 原生二维 SVG 几何分析引擎。

`lumina-svg-core` 解析 SVG 的填充与描边操作，将其转换为平面多边形，并以只读 NumPy 数组返回给 Python，同时保留绘制遮挡顺序、孔洞、裁剪路径、纯色透明度叠加以及颜色来源信息。

本项目最初为 [Lumina Studio](https://github.com/lumina-layer-studio/Lumina-Layers) 开发，现于本独立仓库维护，包含完整的 C++ 几何内核、pybind11 绑定、测试集与跨平台 wheel 构建工具，任何 Python 项目均可直接安装调用。

**当前版本：** `0.3.4`（Alpha） · **支持 Python：** CPython `3.12`、`3.13`、`3.14` · **几何结果协议：** v4

## 安装

### 安装预编译包

从 PyPI 安装：

```bash
python -m pip install lumina-svg-core==0.3.4
```

也可以从 [GitHub Releases](https://github.com/lumina-layer-studio/lumina-svg-core/releases) 下载对应平台的 wheel 文件安装：

```bash
python -m pip install /path/to/lumina_svg_core-0.3.4-cp312-cp312-platform.whl
```

预编译 wheel 已内置固定版本的 LunaSVG/PlutoVG 与 Clipper2 原生代码，支持以下平台：

| 系统 | 架构 | 最低环境要求 |
| --- | --- | --- |
| Windows | x86-64 | 64 位 CPython 3.12–3.14 |
| macOS | Apple Silicon（`arm64`）与 Intel（`x86_64`） | macOS 12+（Python 3.12/3.13），macOS 14+（Python 3.14） |
| Linux | x86-64 | `manylinux_2_28`（glibc 2.28 及以上） |

已发布产物与构建记录也可在 [PyPI 版本页](https://pypi.org/project/lumina-svg-core/0.3.4/) 与[发布工作流](https://github.com/lumina-layer-studio/lumina-svg-core/actions/workflows/publish.yml)查看。

### 从源码编译

需要 CPython 3.12–3.14、Git、网络连接（首次编译会拉取固定提交的 LunaSVG 与 Clipper2 源码）以及支持 C++17 的编译器（Windows 用 MSVC，macOS 用 Apple Clang，Linux 用 GCC 或 Clang）：

```bash
git clone https://github.com/lumina-layer-studio/lumina-svg-core.git
cd lumina-svg-core
python3.12 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install .
```

在 Windows PowerShell 中，使用 `py -3.12 -m venv .venv` 创建虚拟环境，执行 `.venv\Scripts\Activate.ps1` 激活；若当前命令行未配置 MSVC，请在 Visual Studio 开发者终端中运行安装命令。

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

- **直接分析文件：** `lumina_svg_core.analyze_svg("drawing.svg", target_width_mm=40.0, tolerance_mm=0.02)`（支持 `str` 或 `pathlib.Path`）。
- **忽略描边：** 两个接口均可传入 `allow_strokes=False`，仅提取填充几何。
- **查看构建来源：** 可通过 `lumina_svg_core.__version__`、`lumina_svg_core.__source_revision__` 和 `lumina_svg_core.__source_dirty__` 查询当前安装包的提交与版本信息；构建清单规则见 [docs/DISTRIBUTIONS.md](docs/DISTRIBUTIONS.md)。

多边形环遍历、状态码、坐标单位及底层录制与网格接口说明见 [docs/api.md](docs/api.md)。

## 支持的 SVG 特性

| 类别 | 支持内容 |
| --- | --- |
| 纯色填充与描边 | 填充规则（`nonzero`、`evenodd`）、线帽、连接样式、正长度虚线数组与 `non-scaling-stroke` |
| 基础图形与路径 | `<rect>`（含圆角）、`<circle>`、`<ellipse>`、`<polygon>`、`<polyline>`、`<line>` 以及 `<path>` 曲线与圆弧 |
| 视口与变换 | 仿射 `transform`、`viewBox` 原点偏移、嵌套 `<svg>` 视口与百分比长度 |
| 样式与引用 | 受支持的 CSS 选择器与同文档 `<use>` 引用 |
| 裁剪与标记 | `<clipPath>` 矢量裁剪、marker 标记绘制及视口裁剪 |
| 透明度合成 | 纯色透明度与独立对象/分组 `opacity`，在预乘 sRGB 空间按标准 `source-over` 叠加 |

SVG 根画布默认为透明背景。全不透明文档按逆绘制顺序直接消隐；含半透明图元的文档会对重叠区域做布尔切分，并在预乘 sRGB 空间合成最终颜色。

遇到渐变、图案、遮罩、`<text>` 文本、内嵌位图、外部引用、特殊混合模式、非 sRGB 颜色插值或零长度虚线项等超出二维纯色子集的特性时，分析器会返回 `status == "unsupported"` 及具体的 `reason_code`，便于调用方明确处理。完整边界与数值范围说明见 [docs/support.md](docs/support.md)。

## 几何输出结构

- **三层几何视图：** `result.raw` 保存遮挡裁剪前的有面积绘制多边形，`result.visible` 保存消隐和混色后的最终可见无重叠区域，`result.silhouette` 保存整体外轮廓。
- **整数网格与颜色精度：** 顶点使用 `int64` 计算网格存储，并附带 `grid_to_source_unit` 与 `scale_factor` 换算系数；`rgba_f64` 提供 `[0.0, 1.0]` 直通 sRGB 浮点颜色，同时提供字节格式 `rgba` 与颜色来源索引。
- **退化几何单独汇总：** 无面积的点、开放线段、空路径与退化窄片记录在 `result.unprintable_fragments` 中。
- **多线程并行归约：** 设置环境变量 `LUMINA_SVG_REPLAY_THREADS` 为正整数（上限 `16`）即可并行执行独立多边形规范化，输出顺序与单线程完全一致。

## 开发与本地验证

```bash
python -m pip install ".[test]" build
python -m pytest tests -q
python -m build
python tools/verify_wheel.py dist/<wheel-filename>.whl --contents-only
```

CI 会在 Windows、macOS（arm64 与 x86-64）和 Linux 上针对 Python 3.12–3.14 构建并测试 wheel，检查二进制内容与随包许可证，同时验证源码包构建流程。开发约定见 [CONTRIBUTING.md](CONTRIBUTING.md)，基准测试说明见 [benchmarks/README.md](benchmarks/README.md)。

## 许可证与致谢

本项目采用 **AGPL-3.0-only** 许可证，完整条款见 [LICENSE](LICENSE)。

实现中使用了固定版本的 LunaSVG、PlutoVG、Clipper2 与 pybind11，并参考改编了 ChromaPrint3D 的小孔筛选判据。第三方项目的版权声明与许可证全文见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) 和 [THIRD_PARTY_LICENSES/](THIRD_PARTY_LICENSES)。

提交 Bug 请前往 [GitHub Issues](https://github.com/lumina-layer-studio/lumina-svg-core/issues)；安全漏洞报告请遵循 [SECURITY.md](SECURITY.md)。

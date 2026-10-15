# lumina-svg-core

[![Build wheels](https://github.com/lumina-layer-studio/lumina-svg-core/actions/workflows/wheels.yml/badge.svg?branch=dev-refactor)](https://github.com/lumina-layer-studio/lumina-svg-core/actions/workflows/wheels.yml)
[![Python 3.12–3.14](https://img.shields.io/badge/python-3.12%E2%80%933.14-blue.svg)](https://www.python.org/downloads/)
[![License: AGPL v3](https://img.shields.io/badge/license-AGPL--3.0--only-blue.svg)](LICENSE)

[English](README.md) | [简体中文](README.zh-CN.md)

面向打印工作流的原生 SVG 几何引擎。它将 SVG 绘制操作转换为多边形，通过只读 NumPy 数组交给 Python，保留绘制顺序、孔洞、裁剪、纯色透明叠加及颜色来源。

本项目最初用于 Lumina Studio，现作为独立仓库维护。运行时不依赖主程序、界面、打印机设置或应用服务。它负责二维几何分析，不生成网格或 3MF 文件。

**当前状态：** 0.3.1，Alpha 阶段；支持 CPython 3.12、3.13 和 3.14；几何结果协议版本为 4。

## 安装

### 预编译安装包

在支持的 Python 3.12～3.14 环境中，直接让 pip 自动选择公开 wheel：

```bash
python -m pip install --only-binary=lumina-svg-core --find-links https://github.com/lumina-layer-studio/lumina-svg-core/releases/expanded_assets/v0.3.1 "lumina-svg-core==0.3.1"
```

不需要安装 C++ 编译器、额外克隆第三方库、手工打补丁或准备本地 wheelhouse。命令安装的是已发布的 0.3.1，不是尚未合入的开发分支；LunaSVG/PlutoVG 和 Clipper2 已由维护者编译到扩展中。

也可以在 [GitHub Releases](https://github.com/lumina-layer-studio/lumina-svg-core/releases) 下载匹配的 wheel 后安装：

```bash
python -m pip install /path/to/lumina_svg_core-0.3.1-cp314-cp314-platform.whl
```

请将示例文件名替换为实际下载的文件。构建目标包括 Windows x86-64、Apple Silicon Mac、Intel Mac 和 Linux x86-64。Python 3.12/3.13 的 Mac 安装包要求 macOS 12+，Python 3.14 要求 macOS 14+。Linux 使用 manylinux_2_28 基线，要求 glibc 2.28+；不覆盖 Alpine/musl、Windows ARM 或自由线程 Python。本项目目前没有发布到 PyPI。

### 从源码构建原生库（贡献者）

需要 CPython 3.12～3.14、Git 和支持 C++17 的编译器：Windows 使用 MSVC，macOS 使用 Apple Clang，Linux 使用 GCC 或 Clang。首次构建需要联网下载固定版本的构建工具和 C++ 依赖。

这是原生库贡献者的开发流程，不是从 Python/TypeScript 源码运行 Lumina Studio 的前置要求。

```bash
git clone https://github.com/lumina-layer-studio/lumina-svg-core.git
cd lumina-svg-core
python3.12 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install .
```

Windows 中创建环境使用 `py -3.12 -m venv .venv`，PowerShell 激活命令为 `.venv\Scripts\Activate.ps1`。如果命令行没有 MSVC，请在 Visual Studio 开发者终端中构建。

## 使用示例

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

# 返回坐标使用整数计算格距，需要转换为毫米。
coordinates_mm = (
    result.visible.coordinates.astype("float64")
    * result.grid_to_source_unit
    * result.scale_factor
)
```

读取文件使用 `lumina_svg_core.analyze_svg("drawing.svg", target_width_mm=40.0, tolerance_mm=0.02)`，也可传入 `pathlib.Path`。两个分析接口都接受 `allow_strokes=False`，用于忽略描边、保留填充。

[API 文档](docs/api.md) 说明了坐标单位、结果状态、多边形遍历、颜色来源及底层接口。

使用 `lumina_svg_core.__version__`、`lumina_svg_core.__source_revision__` 和 `lumina_svg_core.__source_dirty__` 可以查看安装包版本、源码提交和构建时是否有未提交改动。无 Git 信息且未明确声明来源的源码构建会记录为未知。构建来源与文件校验清单见 [发行文件准备](docs/DISTRIBUTIONS.md)。

## 支持范围

| 能力 | 范围 |
| --- | --- |
| 纯色填充与描边 | 填充规则、端点、连接、受支持的虚线和不随缩放变化的描边 |
| 图形与路径 | 矩形、圆角矩形、圆、椭圆、多边形、路径曲线和圆弧 |
| 布局 | 仿射变换、viewBox 原点、嵌套视口和百分比长度 |
| 引用与样式 | 受支持的 CSS 选择器及文档内 `<use>` 引用 |
| 裁剪与标记 | 矢量裁剪、marker 绘制及视口裁剪 |
| 透明度 | 纯色透明度、对象及分组透明度，按正常 source-over 规则叠加 |

SVG 初始背景透明，引擎不会添加白底或打印材料底色。遮罩、渐变、图案、文本、嵌入图像、外部资源、特殊混合模式、非 sRGB 插值和零长度虚线项等暂不支持。出现不支持的内容时，整份文档返回 `unsupported`，不会静默漏掉元素。

无面积的点、线、空路径和坍缩片段通过 `unprintable_fragments` 单独报告。分析阶段不执行打印格距吸附、形态学闭合、最小面积过滤、三角化或网格修复。详细限制见 [支持范围与数值约束](docs/support.md)。

## 开发和验证

```bash
python -m pip install ".[test]" build
python -m pytest tests -q
python -m build
python tools/verify_wheel.py dist/<actual-wheel-filename>.whl --contents-only
```

CI 会为三种 Python 分别构建四个平台的 wheel，运行完整 Python 测试和二进制/许可证检查；另外验证从源码包构建，以及原生几何边界回归测试。

贡献流程见 [CONTRIBUTING.md](CONTRIBUTING.md)，性能测量方法见 [benchmarks/README.md](benchmarks/README.md)。性能结果与输入、编译器、CPU 和线程数有关，参考机器的测量值不代表通用延迟保证。

## 许可证与致谢

本项目采用 **GNU Affero General Public License 第 3 版，仅此版本（AGPL-3.0-only）**。完整条款见 [LICENSE](LICENSE)。

LunaSVG、PlutoVG、Clipper2、pybind11 及参考的 ChromaPrint3D 代码保留各自的许可证和版权声明，详见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) 与 [THIRD_PARTY_LICENSES/](THIRD_PARTY_LICENSES)。

问题反馈请使用 [GitHub Issues](https://github.com/lumina-layer-studio/lumina-svg-core/issues)。敏感安全问题请按 [SECURITY.md](SECURITY.md) 提交。

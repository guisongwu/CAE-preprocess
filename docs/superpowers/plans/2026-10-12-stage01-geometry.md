# Stage 01 Geometry Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.
>
> 此环境没有上述 superpowers 子技能。按用户已授权的分阶段教学方式在当前会话实施，使用可用 collaboration 工具做独立只读审查，保留相同的验收检查。

**Goal:** 用 C++ 生成带孔板并通过 STEP 往返的几何数值检查，交付可复现的第一阶段教程。

**Architecture:** `geometry.hpp/cpp` 管理 CAD 参数、创建、导入和验收；`main.cpp` 管理 CLI、Gmsh 生命周期与 JSON 输出。Python 集成测试调用编译产物，绘图工具从报告读取参数。

**Tech Stack:** C++17 / CMake 3.20+ / Gmsh 4.12.1 / Python 3 标准库；绘图额外使用已安装的 NumPy 和 Matplotlib。

## Global Constraints

- 所有工程参数和内部坐标统一为米；STEP 必须使用明确且可验证的单位策略。
- 默认宽 0.1 m、高 0.05 m、孔半径 0.01 m，孔心在板中心。
- 圆孔必须严格位于矩形内部，且与边界至少相距 1e-6 m；有限正参数才可接受。
- 面积和周长相对误差不超过 1e-8；包围盒坐标误差不超过 1e-6 m。
- 不在第一阶段生成网格、求解温度或启动图形窗口。
- 输出目录已存在时拒绝覆写。

## Task 1: Establish the contract and meaningful failing tests

**Files:** `tests/test_geometry.py`, `CMakeLists.txt`.

**Interfaces:** Tests consume the `geometry_demo` executable; it must accept the approved CLI and write `geometry_report.json` plus `plate.step`.

- [x] Add a subprocess-based test suite. Each valid case runs in an unused subdirectory of `tempfile.TemporaryDirectory`, loads the report, and compares BOTH measured snapshots against analytic area and perimeter. Invalid cases must return nonzero and leave the requested output directory absent.

```python
area = width * height - math.pi * radius ** 2
perimeter = 2 * (width + height) + 2 * math.pi * radius
for key in ('before_export', 'after_import'):
    snapshot = report[key]
    assert math.isclose(snapshot['area_m2'], area, rel_tol=1e-8)
    assert math.isclose(snapshot['perimeter_m'], perimeter, rel_tol=1e-8)
    assert snapshot['surface_count'] == 1
    assert snapshot['volume_count'] == 0
    assert snapshot['loop_count'] == 2
    assert snapshot['closed_loops'] is True
```

- [x] Run `python3 tests/test_geometry.py --executable build/geometry_demo`; expect failure because executable does not yet exist.

## Task 2: Implement CAD and independently validate STEP round trip

**Files:** `include/cae/geometry.hpp`, `src/geometry.cpp`, `src/main.cpp`, `CMakeLists.txt`, `tools/fetch_gmsh_header.py`, `DEPENDENCIES.md`.

**Interfaces:** `PlateParameters`, `GeometrySnapshot`, `validate_parameters`, `create_plate`, `import_step`, `inspect_geometry`, `validate_snapshot` are declared in `geometry.hpp`; `main.cpp` serializes their results.

- [x] Fetch pinned official header into ignored `.deps/gmsh-4.12.1/include/gmsh.h`; validate version and SHA256. Find existing system runtime rather than installing system packages.
- [x] Create and synchronize the CAD domain with the following core operation:

```cpp
const int rectangle = gmsh::model::occ::addRectangle(0, 0, 0, p.width, p.height);
const int disk = gmsh::model::occ::addDisk(p.cx, p.cy, 0, p.radius, p.radius);
gmsh::vectorpair result;
std::vector<gmsh::vectorpair> mapping;
gmsh::model::occ::cut({{2, rectangle}}, {{2, disk}}, result, mapping);
gmsh::model::occ::synchronize();
```

- [x] Inspect actual OCC entities and loops. For each loop, sample curve parameter endpoints, cluster within geometric tolerance, check endpoint degree two (self-loops count twice), and check connectivity. Compare sorted per-loop lengths to `2*pi*r` and `2*(W+H)`.
- [x] Export STEP with a verified unit strategy; clear Gmsh model, reimport with a known target unit, synchronize, and repeat ALL checks. Record runtime/header versions and tolerances.
- [x] Parse options strictly: reject unknown options, duplicate options, missing values, trailing nonnumeric content, nonfinite dimensions, small clearance, and existing output paths before CAD work.
- [x] Run `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`, `cmake --build build -j2`, then tests. Expected: three valid geometries plus invalid CLI and overwrite-protection cases pass.

## Task 3: Create an honest preview and teaching walkthrough

**Files:** `tools/plot_geometry.py`, `README.md`, `docs/01-geometry-and-step.md`, `docs/learning-progress.md`.

**Interfaces:** Plotter consumes report parameters only, writes equal-aspect geometry PNG/SVG; the tutorial links to exact source functions and explains each call.

- [x] Run `build/geometry_demo --output outputs/stage01`, inspect printed values and JSON. Default analytic area is `0.005 - pi*0.0001`, default total boundary length is `0.3 + 2*pi*0.01`.
- [x] Run `python3 tools/plot_geometry.py outputs/stage01/geometry_report.json`. Plot with an equal-aspect axis in mm, retain conversion rule 1 m = 1000 mm; label that this is geometry only.
- [x] Explain CAD entity `(dimension, tag)`, Boolean subtraction, synchronization, STEP exchange, analytic benchmarks, tolerances, reimport and validation. Include a user exercise changing radius/position and inspecting its effect.
- [x] Inspect the delivered PNG; check all labels, scales and the hole, and ensure generated files are ignored.
- [x] Run CTest and an independent review; address findings before committing the scoped project. Record the exact results in learning progress.

## Self-review

The executable contract is consistent across tasks. Tests check actual measured values, not only PASS flags. Each geometry snapshot is checked against the same mathematical model; loop tags are never assumed persistent. First-stage outputs do not imply a solved heat field. Later stages remain outside this implementation plan.

## 完成记录（2026-10-12）

G++ Debug 构建完成，无编译警告。CTest 的一个集成测试套件通过，包含六个 unittest 测试组（默认几何、偏心孔、不同尺寸、非法输入、覆写保护、help），其中非法输入检查包含 13 组子案例。已补强实测孔位置验证，并经独立只读代码审查；构造失败资源清理也已完成。

默认演示输出位于 `outputs/stage01`。面积为 0.004685840734641022 m²，总周长为 0.3628318530717959 m，导出前和导入后两者均与解析参考在本机 double 表示下相同。STEP 文件长度单位为 mm，内部与读回为 m。PNG/SVG 以实际读回 CAD 采样绘制，已肉眼检查图注、单位、比例和孔位置。

教学材料与依赖来源均已保存。当前轮次只交付第一阶段，下一阶段从语义边界标记和网格开始。

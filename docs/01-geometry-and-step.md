# 第一阶段：建立带孔板几何，并验证 STEP 往返

这一阶段要得到一个可以交给后续网格程序的 CAD 模型，而且能够证明导出、重新导入没有改变它的尺度和基本结构。你会运行 C++ 程序，再按照下面的顺序阅读代码、修改参数和解释结果。

本阶段只处理几何。图中的圆孔和板边界是 CAD 曲线的显示采样，**不是有限元网格**；我们尚未设置温度边界条件，也没有计算温度。

## 1. 先定义要建立的数学对象

板的宽为 \(W\)，高为 \(H\)，圆孔半径为 \(r\)，孔心为 \((c_x,c_y)\)。几何域可以简写为：

\[
\Omega=[0,W]\times[0,H]\setminus D_r(c_x,c_y),
\]

其中 \(D_r\) 表示圆盘；从后续偏微分方程的角度，我们在板的内部求解，并把外矩形边界和圆孔边界单独处理。CAD 模型保存的是带内边界的一个平面面。

默认参数：

| 参数 | 值 | 直观尺寸 |
|---|---:|---:|
| 宽 \(W\) | 0.1 m | 100 mm |
| 高 \(H\) | 0.05 m | 50 mm |
| 半径 \(r\) | 0.01 m | 10 mm |
| 孔心 \((c_x,c_y)\) | (0.05, 0.025) m | 板中心 |

代码、命令行输入和报告中的内部坐标统一用米。绘图坐标可以换成毫米，必须明确使用 \(1\,\mathrm m=1000\,\mathrm{mm}\)。

**先建立两个解析基准：**

\[
A_{\rm ref}=WH-\pi r^2,
\qquad
P_{\rm ref}=2(W+H)+2\pi r.
\]

这里的总周长包含孔壁；只计算矩形外边界会漏掉 \(2\pi r\)。默认面积约为 `0.004685840734641 m²`，总周长约为 `0.362831853072 m`。这些数由输入参数算出，稍后还要从实际 CAD 模型独立测量，再进行比较。

## 2. 运行一次完整流程

命令从项目根目录执行：

```bash
cd /home/wugs/Documents/ChatGPT/Daily-Chat/cae-preprocess-demo
python3 tools/fetch_gmsh_header.py
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j2
ctest --test-dir build --output-on-failure
./build/geometry_demo --output outputs/stage01
python3 tools/plot_geometry.py outputs/stage01/geometry_report.json
```

`fetch_gmsh_header.py` 为当前环境补齐匹配的 Gmsh 4.12.1 C++ 头文件，细节见 [依赖说明](../DEPENDENCIES.md)。它不是完整 Gmsh 安装器；本机还需要对应的运行库。

程序拒绝覆盖已有输出目录。如果 `outputs/stage01` 已存在，将最后两条命令改为：

```bash
./build/geometry_demo --output outputs/stage01-repeat
python3 tools/plot_geometry.py outputs/stage01-repeat/geometry_report.json
```

每次成功运行会产生：

| 文件 | 用途 |
|---|---|
| `plate.step` | 供其他 CAD/CAE 软件导入的几何交换文件 |
| `geometry_report.json` | 输入参数、版本、单位和导出前/导入后的实测检查结果 |
| `geometry_preview.png`、`.svg` | 绘图脚本根据报告里的实际 CAD 边界采样生成的几何预览 |

终端应报告一个二维面、两个闭合边界环、没有网格，以及 STEP 往返检查完成。`ctest` 的实际退出状态用于判断自动检查是否成功；不要仅凭看起来正确的图片作验收。

## 3. 理解项目的分工

先读 [geometry.hpp](../include/cae/geometry.hpp)，认识两类数据：

```cpp
struct PlateParameters {
  double width = 0.1;
  double height = 0.05;
  double radius = 0.01;
  double cx = 0.05;
  double cy = 0.025;
};
```

`PlateParameters` 是“我希望建立什么几何”。`GeometrySnapshot` 是“实际模型里检查到了什么”：面、体、曲线、网格节点和单元数量，面积、周长、包围盒以及边界环采样。

[geometry.cpp](../src/geometry.cpp) 负责创建、导入、测量和验收；[main.cpp](../src/main.cpp) 负责命令行、Gmsh 生命周期、输出目录和报告。这样分工以后，后续可以复用几何检查模块，而不必把检查逻辑复制到网格或求解代码里。

这里的 `node_count` 和 `element_count` 特指**网格**节点和单元。CAD 中有顶点、曲线和面，不等于已经存在离散网格。

## 4. 从参数生成一个带孔面

`validate_parameters()` 先检查：数值有限、宽高为正、半径至少为 `1e-6 m`，并且孔与四条外边界的最小间距至少为 `1e-6 m`。

孔与外边界的间距是：

\[
\delta=\min(c_x-r,\;W-c_x-r,\;c_y-r,\;H-c_y-r).
\]

要求 \(\delta\ge10^{-6}\,\mathrm m\)，能排除孔越界、相切或接近退化的输入。这个例子只接受严格位于板内部的圆孔；穿过外边界的缺口属于另一种几何问题。

创建函数的核心代码如下：

```cpp
const int rectangle = gmsh::model::occ::addRectangle(
    0, 0, 0, p.width, p.height);
const int disk = gmsh::model::occ::addDisk(
    p.cx, p.cy, 0, p.radius, p.radius);

gmsh::vectorpair result;
std::vector<gmsh::vectorpair> mapping;
gmsh::model::occ::cut({{2, rectangle}}, {{2, disk}}, result, mapping);
gmsh::model::occ::synchronize();
```

逐步理解这四件事：

1. `addRectangle` 在 `z=0` 平面创建矩形**面**，返回该面在当前模型中的 `tag`。
2. `addDisk` 创建椭圆盘面；两个半径相同，因此这里是圆盘。
3. `cut` 做布尔差：第一个实体集合减去第二个实体集合。`result` 返回运算后实体的 `(dim, tag)`，`mapping` 提供输入实体与结果实体的对应信息。当前代码检查结果确实是一个二维面，尚不使用 `mapping`。
4. `synchronize` 将 OpenCASCADE 几何内核的修改同步到 Gmsh 的模型接口。后面的 `model::getEntities` 等查询在同步后进行。

### `(dim, tag)` 为什么同时需要两个数？

Gmsh 用维数和编号共同确定一个实体：

| `dim` | 对象 |
|---:|---|
| 0 | 顶点 |
| 1 | 曲线 |
| 2 | 面 |
| 3 | 体 |

`{2, rectangle}` 表示二维矩形面。不同维度可能使用相同编号，因此只保存一个 `tag` 不够。

布尔运算和 STEP 导入都可能重新分配编号。不要写“面一定是 1，孔边一定是 5”这样的假设。当前创建函数检查 `result`，检查函数则重新查询实际面和边界环；后续边界标记也要根据几何位置、类型和关联关系识别。

## 5. 先检查实际 CAD，再导出

`inspect_geometry()` 使用 Gmsh 查询当前模型。我们要求：一个二维面，零三维体，两个闭合边界环。外环是矩形，内环是孔。

测量核心是：

```cpp
gmsh::model::occ::getMass(2, surface_tag, snapshot.area);
// 对每条边界曲线：
gmsh::model::occ::getMass(1, curve_tag, curve_length);
```

`getMass` 的名字沿用几何内核术语，**这里没有输入材料密度，也不是计算板的物理质量**。维数为 2 时得到面的面积，维数为 1 时得到曲线长度。我们把所有边界曲线长度相加，得到总周长。

`getCurveLoops()` 返回面的边界环及各环的曲线。代码检查每条曲线没有被重复归入多个环，且模型里没有游离于这个面边界之外的额外曲线，再把两个环按长度排序，与孔周长、外矩形周长分别比较。

面积与周长不能区分中心孔和偏心孔。因此验收还检查实测孔环的每个采样点是否满足 `hypot(x-cx, y-cy) ≈ radius` 且 `z ≈ 0`；外环采样点则必须位于矩形的四条边上。这个额外检查能发现“命令行报告写了新孔心，实际 CAD 仍用了旧孔心”的错误。

### 闭合不能只靠“最后一个点等于第一个点”

我们把曲线看成图的边，把曲线端点看成图的顶点。代码使用 `coordinate_tolerance_m` 合并足够接近的端点，然后检查两件事：

- 每个端点的度为 2，即有两个端点关联到它；度为 1 表示开口，度大于 2 表示分支。
- 从一条边出发，能把该环的每条边走一次并回到起点；这排除“两个互不相连的圆也都满足度为 2”的情况。

完整的周期圆可能由一条曲线表示，其参数起点和终点相同。它在图里是一条自环，同一顶点要计入**两次**端点关联。因此圆孔能够正确通过度为 2 的检查，而不会被误认成断开的单条边。

代码通过 `getParametrizationBounds()` 取得实际曲线参数区间，再用 `getValue()` 求曲线上的三维点。直线采样两个端点，其他曲线采样 65 个点；按连通方向排列这些点，用于闭合检查和报告可视化。

**面积和周长来自 CAD 内核测量，不是把这 65 个点连成折线后近似积分。** 图片中的圆是采样显示，不能据此判断几何精度或网格精度。这个端点图检查也是针对当前简单 CAD 面的验收，不能替代复杂工程模型的全部几何修复与自交检测。

### 容差分别是什么意思？

| 检查 | 容差 | 含义 |
|---|---|---|
| 端点合并、闭合、包围盒 | `1e-6 m` | 比较坐标时允许的绝对长度误差 |
| 面积、总周长、各环长度 | `1e-8` | 相对解析基准的允许误差 |

几何量的检查为：

\[
\frac{|Q_{\rm CAD}-Q_{\rm ref}|}{|Q_{\rm ref}|}\le 10^{-8}.
\]

坐标容差处理浮点误差与 CAD 内核可能带容差的包围盒；量值相对容差检查面积、周长是否一致。这些是**程序验收阈值**，不是实验测量不确定度，也不是有限元解的误差估计。后续网格和温度收敛要另做验证。

## 6. STEP 往返为什么要清空模型？

[main.cpp](../src/main.cpp) 的主流程可以读成：

```text
解析参数 → 验证参数 → 创建 CAD → 检查 CAD → 验收
         → 写 STEP → 清空模型 → 导入 STEP → 检查 CAD → 验收 → 写 JSON
```

关键片段：

```cpp
const auto before = cae::inspect_geometry();
cae::validate_snapshot(before, p);
gmsh::write(step.string());

gmsh::clear();
gmsh::option::setString("Geometry.OCCTargetUnit", "M");
cae::import_step(step);

const auto after = cae::inspect_geometry();
cae::validate_snapshot(after, p);
```

如果不 `clear()`，重新导入后的检查可能仍在查看原始面，或者模型同时保留两份几何。清空之后，我们检验的就是从文件恢复的模型。

`import_step()` 内部调用 `occ::importShapes()`，再 `occ::synchronize()`。STEP 的用途是交换 CAD 几何与拓扑；本项目的 STEP 文件不包含有限元网格、材料或温度条件。

### 内部用米，为什么 STEP 文件里会出现 100？

会话把 `Geometry.OCCTargetUnit` 设置为 `M`，内部按米建模、读回。本环境使用的 Gmsh/OpenCASCADE STEP 写出过程已实测：文件声明长度单位为毫米，宽高坐标相应写成 `100`、`50`；读入时根据文件单位转换到目标单位米，恢复为 `0.1`、`0.05`。

这是合法的单位转换。**不要在 `addRectangle` 前手动乘 1000**，否则你会把原本 0.1 m 的板改成 100 m。

代码从 STEP 文本读取明确的长度单位，写入报告的 `step_length_unit` 和 `step_length_unit_to_m`。测试还检查 STEP 中实际坐标与声明单位是否相符，并比较重新导入后的包围盒、面积和周长。只看到文件头声明“mm”还不足以证明坐标尺度正确。

这个简单解析器当前支持米和毫米的 SI 单位声明；它不是通用的任意 STEP 单位解析器。将来接收外部工程文件时，要扩展受支持单位并继续做尺度验证。

## 7. 两个 C++ 工程细节

### 为什么用 `GmshSession`？

```cpp
struct GmshSession {
  GmshSession() {
    gmsh::initialize(0, nullptr, false);
    try {
      gmsh::option::setString("Geometry.OCCTargetUnit", "M");
    } catch(...) {
      try { gmsh::finalize(); } catch(...) {}
      throw;
    }
  }
  ~GmshSession() {
    try { gmsh::finalize(); } catch(...) {}
  }
};
```

上面保留了生命周期相关的核心行。完整代码还关闭终端输出并禁止会话对象复制。

这运用了 RAII：对象构造时初始化库，离开作用域时自动清理。即使几何检查抛出异常，局部对象析构也会调用 `finalize()`，避免只在正常返回路径清理。还有一个细节：如果初始化之后、设置选项时构造函数抛出异常，对象还没有构造完成，析构函数不会调用。因此构造函数也有清理分支，再把原异常抛出。析构函数捕获清理异常，防止异常展开期间再抛出异常。`initialize` 的第三个参数为 `false`，表示不读个人 `.gmshrc`，让教程不受 GUI 设置影响。

### 头文件和运行库各负责什么？

[CMakeLists.txt](../CMakeLists.txt) 用 `find_path` 找到 `gmsh.h`，用 `find_library` 找到 Gmsh 库。头文件告诉编译器 API 的声明，库提供真正执行布尔运算、STEP 读写等功能的实现。只有一个同名头文件不能完成链接。

`Gmsh::Gmsh` 是 CMake 导入目标，统一携带头文件位置与库位置。`cae_geometry` 链接它，`geometry_demo` 再链接 `cae_geometry`，形成清晰的依赖链。`cxx_std_17` 启用当前代码所用的 C++17，例如 `std::filesystem`。

启动时程序比较 `GMSH_API_VERSION` 与运行库报告的版本，拒绝当前教程中的版本不匹配。第一阶段没有使用 Eigen；稀疏线性代数会在导热阶段加入。

## 8. 读报告，确认验收对象

打开 `geometry_report.json`，重点查看：

- `parameters`：你输入的米制几何参数。
- `reference`：通过数学公式得到的解析基准。
- `before_export` 和 `after_import`：两次独立查询实际 CAD 的结果。
- `area_m2`、`perimeter_m`、`loops[].length_m`：实际几何量；不能只看 `status`。
- `surface_count=1`、`volume_count=0`、`loop_count=2`、`closed_loops=true`：模型维度与边界结构。
- `node_count=0`、`element_count=0`：确认第一阶段没有误生成网格。
- `bounding_box_m`：依次为 `[xmin,ymin,zmin,xmax,ymax,zmax]`，应接近 `[0,0,0,W,H,0]`。

只有建模和重新导入都通过检查后，程序才写出 `status="PASS"` 报告。若导出后步骤失败，输出目录可能保留 STEP 文件，便于诊断；重新运行请用新目录，不要把残留目录当作成功结果。

[自动检查](../tests/test_geometry.py) 覆盖默认几何、偏心孔、不同尺寸、非法参数和命令行、重复输出保护及帮助命令；它把实测 CAD 与解析量比较，独立检查孔的位置，也检查 STEP 坐标尺度。检查代码的目的，是让你改参数或重构模块时及时发现退化和单位错误。

## 9. 你来做两个小练习

### 练习 A：半径从 10 mm 改成 7 mm

先预测：孔变小，剩余板面积增大；孔壁周长减小，因此总周长减小。再运行：

```bash
./build/geometry_demo --radius .007 --output outputs/stage01-radius7mm
python3 tools/plot_geometry.py outputs/stage01-radius7mm/geometry_report.json
```

解析值约为：

```text
A = 0.1 * 0.05 - pi * 0.007^2 = 0.004846061959974 m²
P = 2 * (0.1 + 0.05) + 2 * pi * 0.007 = 0.343982297150 m
```

比较默认报告与新报告的 `after_import`。解释面积为何增加、周长为何减少，并确认仍是一个面和两个闭合环。

### 练习 B：移动孔心

保留默认半径，只改变位置：

```bash
./build/geometry_demo --cx .035 --cy .018 --output outputs/stage01-offset
python3 tools/plot_geometry.py outputs/stage01-offset/geometry_report.json
```

预期面积、总周长和矩形包围盒都与默认模型一致，因为公式不含孔心坐标；图中的孔位置发生变化。把它看成对“几何位置变化”和“整体几何量变化”的一次区分。

完成后，你应能不用照读代码回答：

1. 为什么板是一个带内边界的二维面，而不是“矩形面加圆盘面”？
2. 为什么布尔运算后不能依赖原有 tag，为什么要 `synchronize()`？
3. 为什么 `getMass(2, ...)` 是面积，而不是材料质量？
4. 为什么度为 2 还必须配合连通检查，周期圆如何计数？
5. 为什么 STEP 里 `100 mm` 与内部 `0.1 m` 一致，怎样证明？
6. 怎样证明重新导入的模型与输入参数一致，而且尚未包含网格？

下一阶段将把这些 CAD 曲线识别为 `left/right/top/bottom/hole`，加入孔附近加密和网格质量检查。之后才把三角形网格交给 P1 导热有限元，并通过制造解验证收敛阶、残差和热流平衡。

## API 与单位资料

[Gmsh 4.12.1 API 头文件](https://raw.githubusercontent.com/gmsh-project/gmsh/gmsh_4_12_1/api/gmsh.h) 定义本文使用的 `addRectangle`、`addDisk`、`cut`、`synchronize`、`getMass`、`getCurveLoops` 与曲线求值接口；[官方手册](https://gmsh.info/doc/texinfo/gmsh.html) 解释 CAD 与网格数据模型。

本文的 STEP 单位处理结合 [Gmsh 4.12.1 STEP 实现](https://raw.githubusercontent.com/gmsh-project/gmsh/gmsh_4_12_1/src/geo/GModelIO_OCC.cpp) 与本机文件实测；具体依赖、精准头文件摘要和来源见 [DEPENDENCIES.md](../DEPENDENCIES.md)。

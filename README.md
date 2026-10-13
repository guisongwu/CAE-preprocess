# 带孔板 CAE 前处理与导热演示

用 C++17、Gmsh/OpenCASCADE 和逐阶段数值验收，建立从工程几何到有限元导热分析的可复现流程。

**当前完成范围：第一阶段，参数化 CAD 几何与 STEP 导出后重新导入。** 本阶段没有网格、温度场或导热求解器。

默认模型为宽 0.1 m、高 0.05 m、圆孔半径 0.01 m 的二维板。程序检查一个二维面、两个闭合边界环、面积、周长、包围盒及 STEP 单位，输出几何文件与 JSON 报告。

## 运行

在本机已有 Gmsh 4.12.1 运行库的前提下，获取匹配的 C++ 头文件后构建：

```bash
cd /home/wugs/Documents/ChatGPT/Daily-Chat/cae-preprocess-demo
python3 tools/fetch_gmsh_header.py
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j2
ctest --test-dir build --output-on-failure
./build/geometry_demo --output outputs/stage01
python3 tools/plot_geometry.py outputs/stage01/geometry_report.json
```

输出目录必须尚不存在；重复演示时改用 `outputs/stage01-repeat` 等新目录。生成物包括 `plate.step`、`geometry_report.json` 和 `geometry_preview.png/svg`。

从 [第一阶段教程](docs/01-geometry-and-step.md) 开始，逐步读代码、理解几何与验收原理，并完成改变孔半径及孔心的练习。依赖说明见 [DEPENDENCIES.md](DEPENDENCIES.md)。

## 后续阶段

1. 边界语义识别、孔边加密与网格质量报告。
2. P1 三角形有限元稳态导热：左侧高温、右侧低温，其余边界绝热。
3. 制造解收敛、求解残差与热流平衡验证，整理展示材料。

CAD 布尔运算及后续网格生成采用 Gmsh/OpenCASCADE；本项目重点是参数管理、几何与网格检查、求解器接口以及可复现的验证。

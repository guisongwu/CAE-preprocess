# 依赖与来源

第一阶段只使用 C++17、CMake、Gmsh 4.12.1 和 Python 3。Eigen 留待导热阶段使用；Python 绘图额外依赖 NumPy 和 Matplotlib。没有启动图形窗口的要求。

本机已存在 `/usr/lib/x86_64-linux-gnu/libgmsh.so.4.12.1`，但没有 C++ 头文件。项目通过以下命令只下载配套头文件到被 Git 忽略的 `.deps`，不修改系统安装：

```bash
python3 tools/fetch_gmsh_header.py
```

- 版本：4.12.1。
- 精准来源：[Gmsh 官方只读镜像的版本标签](https://raw.githubusercontent.com/gmsh-project/gmsh/gmsh_4_12_1/api/gmsh.h)。[镜像说明](https://github.com/gmsh-project/gmsh)。
- SHA256：`af694b54a7b166e93c26f4afa4485f9fe22d3bc60287a535f6ffe5387d1b9f94`。
- CMake 查找头文件和运行库；程序在运行时核对 `GMSH_API_VERSION` 和 `General.Version`。

其他电脑可以使用自身版本一致的 Gmsh 开发包或 [官方 SDK](https://gmsh.info/bin/Linux/)。配置时显式指定：

```bash
cmake -S . -B build -DGMSH_INCLUDE_DIR=/实际SDK/include -DGMSH_LIBRARY=/实际SDK/lib/libgmsh.so
```

不要混用不同版本的头文件和共享库。下载工具专门对应此项目当前验证的 4.12.1；其他版本需要成套指定并重新验证。下载失败时，已有源码和已验证依赖不会被覆盖。

## 几何与 STEP 来源

- [Gmsh 官方手册](https://gmsh.info/doc/texinfo/gmsh.html)：CAD 实体、API、OpenCASCADE。
- [4.12.1 的 STEP 实现](https://raw.githubusercontent.com/gmsh-project/gmsh/gmsh_4_12_1/src/geo/GModelIO_OCC.cpp)。
- [本机 OCCT 7.6.3 的 STEP 写出实现](https://raw.githubusercontent.com/Open-Cascade-SAS/OCCT/V7_6_3/src/STEPControl/STEPControl_Writer.cxx)。

本机实测：`Geometry.OCCTargetUnit="M"` 指定内核按米理解坐标；STEP 默认写出单位为毫米，0.1 m 对应文件坐标 100 mm；以 M 导入后恢复为 0.1 m。程序读取实际导出文件的长度单位，测试进一步核对文件坐标与声明单位。

Gmsh/OpenCASCADE 提供 CAD 布尔运算与几何内核；项目贡献是参数校验、几何与拓扑验收、STEP 往返验证、数据报告和教学流程。Gmsh 的版权和许可见 [官方许可说明](https://gmsh.info/doc/texinfo/gmsh.html#Copying-conditions)。

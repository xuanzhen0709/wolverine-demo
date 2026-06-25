> [📖 Docs](../README.md) › 指南 › 构建与安装

# 构建与安装

## 前置条件

- **CMake** >= 3.9
- **Conan** 2.x
- **Ninja** 构建系统
- **C++20** 编译器（GCC 11+ / Clang 14+）
- **Python** 3.8+（用于 Python demo 和分析工具）

## 启用 wlsim 运行时

terminal里运行以下命令启用wlsim运行时。

```bash
enable_wlsim_env
```

启用wlsim运行时后，会自动注入`WLSIM_ENV_KEY` 等环境变量。

## 构建所有 Demo

```bash
# 一键构建（Release 模式）
./run_build.sh

# Debug 模式
./run_build.sh debug

# RelWithDebInfo 模式
./run_build.sh relwithdebinfo

# 构建并安装
./run_build.sh -i

# 构建并打包
./run_build.sh -p
```

构建脚本内部流程：
1. 通过 Conan 安装依赖（根据 `WLSIM_ENV_KEY` 环境变量选择 profile）
2. 配置 CMake preset（`conan-<env_key>-<build_type>`）
3. 使用 Ninja 构建

### ⚠ 构建后必须安装

`wl-sim` 通过 `LD_LIBRARY_PATH`（指向 `$WLSIM_ENV_DIR/lib`，即
`~/.local/wlsim.<env_key>/lib`）加载信号库，**而不是从构建目录加载**。因此每次改完代码、
重新构建后，必须把 `.so` 安装到该目录，否则 `wl-sim` 会加载到旧的库：

```bash
# 构建并安装（等价于 ./run_build.sh -i）
./run_build.sh -i

# 或手动：构建后单独 install
cmake --build build/<preset>
cmake --install build/<preset>
```

> 症状排查：如果你改了 `main.cpp`、重新 `cmake --build` 成功，但 `wl-sim` 行为没变，
> 几乎可以肯定是忘了 `cmake --install`——构建产物在 `build/`，而 `wl-sim` 实际加载的是
> `$WLSIM_ENV_DIR/lib/` 下的旧副本。

## 构建系统详解

### CMake 结构

```
CMakeLists.txt                  # 顶层：find_package(wolverine), find_package(cfi_operators)
├── cmake/version.cmake         # 从 git tag 提取版本号
├── demos/CMakeLists.txt        # add_all_subdirectories() — 自动包含所有 demo
└── tools/CMakeLists.txt        # add_all_subdirectories() — 自动包含所有工具
```

### Conan 依赖

```ini
# conanfile.txt
[requires]
# 通过 conan install 时由 profile 解析

[generators]
CMakeDeps
CMakeToolchain
```

### 环境变量

| 变量 | 说明 |
|------|------|
| `WLSIM_ENV_KEY` | 环境标识（如 `manylinux-2_34`），决定 Conan profile 和预设路径 |

### Preset 选择

构建脚本根据 `WLSIM_ENV_KEY` 和 `build_type` 自动选择 CMake preset：
- 构建 profile: `wlsim/<WLSIM_ENV_KEY>/Release`
- 主机 profile: `wlsim/<WLSIM_ENV_KEY>/<build_type>`
- CMake preset: `conan-<WLSIM_ENV_KEY>-<build_type>`

## 运行 Demo

每个 demo 目录下包含 `wlsim.yml` 配置文件，通过 `wl-sim` 命令运行：

```bash
cd demos/checkpoint
wl-sim save.yml    # 运行 save 阶段
wl-sim load.yml    # 运行 load 阶段
```

## 版本管理

版本号由 `cmake/version.cmake` 从最近的 git tag 自动提取：

```bash
git tag v1.2.3   # 设置版本号
# CMake 自动解析为 PROJECT_VERSION = "1.2.3"
```

# wolverine-demo

wolverine 仿真平台（wlsim）的 demo 仓库，展示 signal、factor、strategy 的 C++ 与 Python 实现，覆盖横截面和时间序列两种行情数据模式。

📖 **完整文档：** [docs/](docs/README.md)

## 快速开始

```bash
# 启动运行时环境（决定 Conan profile / 平台包）
enable_wlsim_env

# 构建所有 demo（Conan 安装依赖 + CMake/Ninja 构建）
#   docs/guides/build-and-install.md
./run_build.sh
```

## 仓库布局

| 目录 | 用途 |
|------|------|
| `demos/` | Signal/factor/strategy 示例实现 |
| `tools/` | PnL、IC、Edge 等分析工具 |
| `scripts/` | 运维脚本（验证、转换、统计） |
| `docs/` | 完整文档 |

## 依赖要求

- **wolverine** >= 2.4.0
- **cfi_operators**（最新版）
- CMake >= 3.9、Conan 2.x、Ninja
- C++20 编译器

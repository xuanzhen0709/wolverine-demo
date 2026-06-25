> [📖 Docs](../README.md) › 路线图

# 路线图

待办事项和未来工作方向。

## 待补充

- [ ] 补充 `feature-factor` demo 中各角色（factor / feature_factor / ftreader / sigreader）的完整文档与配置矩阵
- [ ] 补充 `signal_reader` demo 的完整配置说明（`sigreader.yml` / `pystrat.yml` 的 9 个 factor）
- [ ] 增加更多算子（operator）类型的公式文档（除 `ts_sum/ts_mean/ts_std/ts_inner_product/sum_agg` 外的完整清单）
- [ ] 增加 Python Cython 加速的最佳实践指南（`cimport numpy`、`<intptr_t>` 强转等模式）
- [ ] 补充各工具的详细配置参数说明（exec_price、mode、stock_map 等）

## 已知问题（待处理）

（暂无）

## 上游问题（不修复）

以下位于 wlsim 运行时安装目录的头文件中，**不在本仓库内**，仅作记录、不予处理：

- `cfi_ops/dp_export.h`：`#d efine DP_EXPORT __declspec(dllexport)` 中 `#d efine` 是断词（仅影响 MSVC 构建路径，GCC 下不触发）。
- `cfi_ops/factornode.h`：`[[deprecated("Use Fearture instead.")]]` 拼写错误（"Fearture"）；这也是使用 `FactorNode::update` 时出现 `-Wdeprecated-declarations` 警告的来源，属预期行为。

## 未来工作

- [ ] 添加性能基准测试（benchmark）demo
- [ ] 增加更多资产类别的 demo（期权、外汇等）
- [ ] 集成 CI/CD 自动构建验证
- [ ] 添加单元测试覆盖
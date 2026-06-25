> [📖 Docs](../README.md) › 参考 › 脚本参考

# 脚本参考

`scripts/` 目录包含运维和数据分析脚本。

> 说明：wlsim 运行时（wolverine / wlmd / cfi_operators）的安装由 Conan 管理，
> 不通过本目录的脚本完成。参见 [构建与安装](../guides/build-and-install.md)。

## 脚本列表

| 脚本 | 功能 | 用法 |
|------|------|------|
| `validate_checkpoint.py` | 验证 checkpoint 正确性 | `python3 validate_checkpoint.py <config.yml> <date>` |
| `validate_signal_reader.py` | 验证 signal_reader 的 sigreader 回放与实时计算一致 | `python3 validate_signal_reader.py [-w demos/signal_reader]` |
| `run_pnl_calculator.py` | 运行 PnL 计算 | — |
| `run_edge_calculator.py` | 运行 Edge 计算 | — |
| `run_ic_calculator.py` | 运行 IC 计算 | — |
| `run_cs_pnl_calculator.py` | 运行横截面 PnL | — |
| `run_cs_edge_calculator.py` | 运行横截面 Edge | — |
| `run_cs_ic_calculator.py` | 运行横截面 IC | — |
| `run_pnl_stats.py` | PnL 统计分析 | — |
| `run_cs_pnl_stats.py` | 横截面 PnL 统计 | — |
| `run_sig_stats.py` | 信号统计分析 | — |
| `run_t0_stats.py` | T0 统计分析 | — |
| `run_edge_plot.py` | Edge 可视化 | — |
| `convert_ts_sig2cs_sig.py` | 时间序列 → 横截面信号格式转换 | — |
| `business_calendar.py` | 交易日历工具 | — |
| `collect_artifacts.py` | 收集构建产物 | — |

## validate_checkpoint.py

自动化验证 checkpoint 保存/加载的正确性。

```bash
python3 scripts/validate_checkpoint.py demos/checkpoint/save.yml 20230103
```

参数：
- `config` — 模板 yaml 文件路径
- `date` — 验证日期
- `-w, --workdir` — 工作目录（默认脚本所在目录）

**验证流程：**

1. **生成 save 配置** — 运行 `date` 当天，保存日末 checkpoint
2. **生成 load 配置** — 加载 `date` 的 checkpoint，运行 `date+1`
3. **生成 full 配置** — 不保存/加载，连续运行 `date` 到 `date+1`
4. **运行三个测试用例** — 每个用例通过 `wl-sim` 执行
5. 对比 load 和 full 的输出，确认 checkpoint 正确

## validate_signal_reader.py

验证 `signal_reader` demo 中 `system.sigreader` 回放的信号与实时计算一致。

```bash
python3 scripts/validate_signal_reader.py
# 或指定 demo 目录
python3 scripts/validate_signal_reader.py -w demos/signal_reader
```

参数：
- `-w, --workdir` — signal_reader demo 目录（默认 `demos/signal_reader`）
- `--pystrat` / `--sigreader` — 两份配置文件名（默认 `pystrat.yml` / `sigreader.yml`）

**验证流程：**

1. **pystrat.yml** — `fct1` 由 `cssnap_simple_factor` 实时计算（seed 1.234）并落盘；`fct2`–`fct9` 实时；strat 输出存为基准。
2. **sigreader.yml** — `fct1` 改由 `system.sigreader` 从磁盘回放；`fct2`–`fct9` 不变；strat 重新输出。
3. **对比** 两次的 `strat` 输出 CSV——逐字节一致即说明 `sigreader` 完整重现了实时因子。

> 运行前确保已 `cmake --install`（见 [构建与安装](../guides/build-and-install.md)），
> 否则 `wl-sim` 可能加载到旧的信号库。

## run_pnl_calculator.py / run_cs_pnl_calculator.py

PnL 计算入口脚本，内部调用 `tools/pnl_calculator/` 中的 PnLCalculator Signal。

## run_edge_calculator.py / run_cs_edge_calculator.py

Edge 收益计算入口脚本。

## run_ic_calculator.py / run_cs_ic_calculator.py

IC 信息系数计算入口脚本。

## convert_ts_sig2cs_sig.py

将时间序列信号格式转换为横截面信号格式。

## business_calendar.py

提供交易日历查询功能。依赖 CalendarMgr 中的交易日历数据。

## collect_artifacts.py

收集构建产物到指定目录，用于分发或部署。
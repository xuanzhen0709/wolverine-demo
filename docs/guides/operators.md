> [📖 Docs](../README.md) › 指南 › 使用 Operators

# 使用 Operators

`cfi_operators` 库提供高性能时序算子，支持在 C++ 和 Python 中声明式地定义时间序列计算。

## 核心概念

### Operators

`cfi::Operators` 是最基础的时序算子类，支持通过公式字符串定义计算逻辑：

```cpp
#include <cfi_ops/operators.h>

// 构造时初始化
cfi::Operators op1_{"ts_inner_product(@x,@y,2)", "op1"};

// 延迟初始化
cfi::Operators op2_;
void initialize() {
  op2_.initialize("ts_sum(@av1, 5)", "op2");
}
```

### FactorNode

`cfi::FactorNode` 扩展了 Operators，增加了横截面支持：

```cpp
#include <cfi_ops/factornode.h>

cfi::FactorNode node1_;
cfi::FactorNode node2_;

void initialize() {
  node1_.initialize("ts_inner_product(@bp1,@bp2,2)", "f1");
  node2_.initialize("ts_sum(@av1, 2)", "f2");
}
```

## 公式语法

公式使用类似函数调用的语法：

```
ts_inner_product(@x, @y, window)
ts_sum(@x, window)
```

| 函数 | 说明 |
|------|------|
| `ts_inner_product(@x, @y, N)` | x 和 y 的 N 期内积 |
| `ts_sum(@x, N)` | x 的 N 期求和 |
| `ts_mean(@x, N)` | x 的 N 期均值 |
| `ts_std(@x, N)` | x 的 N 期标准差 |

`@` 前缀表示占位符，在 `update()` 调用时按位置匹配实际值。

## 生命周期管理

### 日初/日末

```cpp
void Signal::on_sod(const SodEvent *ev)
{
  op1_.on_day_begin(ev->date);             // Operators：仅需 date
  node1_.on_day_begin(ev->date, ev->ins_nr); // FactorNode：还需 cs_len(ins_nr)
  av1_.resize(ev->ins_nr);                 // 横截面缓冲区必须 resize（见下）
}

void Signal::on_eod(const EodEvent *ev)
{
  op1_.on_day_end(ev->date);
  node1_.on_day_end(ev->date);
}
```

### Checkpoint

```cpp
void Signal::load_state(const std::string &dir)
{
  op1_.load_checkpoint(dir);
  node1_.load_checkpoint(dir);
}

void Signal::save_state(const std::string &dir)
{
  op1_.save_checkpoint(dir);
  node1_.save_checkpoint(dir);
}
```

## 使用示例

### 时间序列模式（Operators）

```cpp
// demos/operators/main.cpp
cfi::Operators op1_{"ts_inner_product(@x,@y,2)", "op1"};
cfi::Operators op2_;

void initialize() {
  size_t len = 10;
  std::string formula;
  fmt::format_to(std::back_inserter(formula), "ts_inner_product(@x,@y,{})", len);
  op2_.initialize(formula, "op2");
}

void on_snapshot(const SnapshotEvent *ev) {
  const auto ss = ev->snapshot;
  const auto ret1 = op1_.update(double(ss->bv[0]), ss->bp[0]);
  const auto ret2 = op2_.update(double(ss->av[0]), ss->ap[0]);
  sigval_[0] = ret1 + ret2;
  apis_.update_signal(apis_.token, ss->exchtime, ss->localtime, 1, sigval_.data());
}
```

### 横截面模式（FactorNode）

```cpp
// demos/csops/main.cpp
cfi::FactorNode node1_;
cfi::FactorNode node2_;

void initialize() {
  node1_.initialize("ts_inner_product(@bp1,@bp2,2)", "f1");
  node2_.initialize("ts_sum(@av1, 2)", "f2");
}

void on_cs_snapshot(const CsSnapshotEvent *ev) {
  using FldType = CsSnapshotEvent::FldType;
  auto bp = CsSnapshotUtils::get_fld<FldType::bp>(ev);
  const auto ret1 = node1_.update(bp[0], bp[1], ev->ins_nr);

  // av 是 int64_t 二维数组，先转成 double 一维数组再喂给算子
  // 注意：av1_ 必须先 resize（仅 reserve 会导致 copy_n 越界写）
  av1_.resize(ev->ins_nr);
  auto av = CsSnapshotUtils::get_fld<FldType::av>(ev);
  std::copy_n(av[0], ev->ins_nr, av1_.begin());
  const auto ret2 = node2_.update(av1_.data(), ev->ins_nr);

  for (size_t i = 0; i < ev->ins_nr; ++i) {
    ret_[i] = ret1[i] + ret2[i];
  }

  // 输出组合结果 ret1 + ret2
  apis_.update_signal(apis_.token, ev->exchtime, ev->localtime,
                       ev->ins_nr, ret_.data());
}
```

## 完整示例

参见 `demos/operators/` 和 `demos/csops/` 目录：

```
demos/operators/
├── main.cpp         # 时间序列 Operators 示例
├── full.yml         # 连续运行配置（20250106–07，输出 output/full）
├── save_state.yml   # 跑 20250106 并保存 checkpoint
├── load_state.yml   # auto_load: yesterday → 加载 20250106 ckpt，跑 20250107（输出 output/resume）
└── README.md

demos/csops/
├── main.cpp         # 横截面 FactorNode 示例
├── full.yml         # 连续运行 20241215–17
├── save_state.yml   # 跑 20241215–16，存 20241216 checkpoint
└── load_state.yml   # 加载 20241216 ckpt，跑 20241217
```

### save / load / full 三段式验证

`operators` 与 `csops` 都用同一套三段式配置验证 checkpoint 正确性（与 [Checkpoint 指南](checkpoint.md)
的思路一致）：

1. `save_state.yml` ——跑到某日并存该日末 checkpoint；
2. `load_state.yml` ——加载该 checkpoint 跑次日（`operators` 用 `auto_load: yesterday`）；
3. `full.yml` ——不存不加载，连续跑两日。

对比 `load_state` 与 `full` 在**次日**的输出，应当逐字节一致——说明 checkpoint 完整恢复了
算子内部状态。实测：`operators` 的 `resume` 与 `full` 在 20250107 的 CSV 完全一致 ✅。

> `csops` 的三段配置依赖 `stocksv2.CHN` 数据集；若 NAS 上缺该数据，可临时改用 `stocks.CHN`
> 验证代码逻辑（`FactorNode` 的 `on_day_begin`/`update`/checkpoint 均正常）。
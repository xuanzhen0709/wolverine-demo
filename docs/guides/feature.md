> [📖 Docs](../README.md) › 指南 › 使用 Feature

# 使用 Feature

Feature 是 `cfi_operators` 库提供的高级抽象，支持从 YAML 配置中动态构建计算图，自动解析数据依赖并适配行情字段。

## 核心概念

### Feature

`cfi::Feature` 通过公式字符串自动推断所需的数据字段，无需手动映射：

```cpp
#include <cfi_ops/feature.h>

cfi::Feature f1_;

void initialize(const Config *root) {
  auto formula = (*root)["formula"].as<std::string>();
  f1_.initialize(formula, "f1_");
  // 自动解析公式需要的字段名
  handle_data();
}
```

### 数据名自动映射

Feature 的 `get_datanames()` 返回公式所需的所有字段名，解析规则：

| 数据名模式 | 映射到 CsSnapshotEvent 字段 |
|-----------|---------------------------|
| `bid1`, `bid2`, ... | `bid_cnt[level-1]` |
| `ask1`, `ask2`, ... | `ask_cnt[level-1]` |
| `ap1`, `ap2`, ... | `ap[level-1]` |
| `bp1`, `bp2`, ... | `bp[level-1]` |
| `av1`, `av2`, ... | `av[level-1]` |
| `bv1`, `bv2`, ... | `bv[level-1]` |
| `last_price` | `last_price` |
| `volume` | `volume` |
| `turnover` | `turnover` |
| `open_interest` | `open_interest` |
| `exchtime` | `exchtime` |
| `localtime` | `localtime` |

### 自动构建数据管道

`handle_data()` 方法根据 `get_datanames()` 的结果，为每个数据名创建一个 lambda 函数，在行情到达时自动提取对应字段：

```cpp
void handle_data() {
  const auto &datanames = f1_.get_datanames();
  for (auto &name : datanames) {
    // 解析前缀和索引
    size_t num_pos = name.find_first_of("0123456789");
    std::string prefix = name.substr(0, num_pos);
    int index = std::stoi(name.substr(num_pos)) - 1; // 0-based

    if (prefix == "bid") {
      construct_data_.push_back([=, this](const CsSnapshotEvent *ev) {
        copy_data<FldType::bid_cnt, true>(ev, index);
      });
    } else if (prefix == "bp") {
      construct_data_.push_back([=, this](const CsSnapshotEvent *ev) {
        copy_data<FldType::bp, true>(ev, index);
      });
    }
    // ... 其他字段同理
  }
}
```

## 使用示例

### 完整实现

```cpp
// demos/ops_feature/main.cpp
#include <cfi_ops/feature.h>

cfi::Feature f1_;
std::vector<cfi::feature_variant> inputs_;
std::vector<std::function<void(const CsSnapshotEvent *)>> construct_data_;

void initialize(const Config *root) {
  auto formula = (*root)["formula"].as<std::string>();
  f1_.initialize(formula, "f1_");
  handle_data();  // 构建数据管道
}

void on_cs_snapshot(const CsSnapshotEvent *ev) {
  inputs_.clear();
  for (auto &f : construct_data_) {
    f(ev);  // 按顺序提取每个字段
  }
  const auto ret = f1_.update(inputs_, ev->ins_nr);
  apis_.update_signal(apis_.token, ev->exchtime, ev->localtime,
                       ev->ins_nr, ret);
}
```

### 生命周期管理

与 Operators 类似的 checkpoint 和日初/日末管理：

```cpp
void on_sod(const SodEvent *ev) {
  f1_.on_day_begin(ev->date, ev->ins_nr);
}

void on_eod(const EodEvent *ev) {
  f1_.on_day_end(ev->date);
}

void load_state(const std::string &indir) {
  f1_.load_checkpoint(indir);
}

void save_state(const std::string &outdir) {
  f1_.save_checkpoint(outdir);
}
```

### YAML 配置

```yaml
signal:
  name: test_feature
  module: ops.feature
  config:
    formula: "ts_sum(@bp1 + @bp2, 5) / ts_sum(@volume, 5)"
    targets:
      - stocks.CHN
```

## Feature 与 Factor 的组合

在 `feature-factor` demo 中，展示了 Feature 和 Factor 的嵌套使用：

```yaml
# factor_with_feature_reader.yml
features:
  - name: ft1
    module: system.ftreader
    config:
      format: parquet
      dir: output
      provides:
        - order_cnt
        - order_pxmean

marketdata:
  - module: cs-snapshot
    features:
      - ft1
    client:
      - test_ftreader

signal:
  name: test_ftreader
  module: nickchenyj.feature_factor
```

在 C++ 中访问 feature buffer：

```cpp
void on_sod(const SodEvent *ev) {
  if (!ft_order_cnt_) {
    ft_order_cnt_ = apis_.get_feature_buf(apis_.token, "order_cnt");
    ft_order_pxmean_ = apis_.get_feature_buf(apis_.token, "order_pxmean");
  }
}

void on_cs_snapshot(const CsSnapshotEvent *ev) {
  for (size_t i = 0; i < count; ++i) {
    sigs_[i] = (*ft_order_pxmean_)[i] * (*ft_order_cnt_)[i];
  }
}
```

## Mbo 算子

对于逐笔成交数据，使用 `cfi::Mbo` 算子：

```cpp
#include <cfi_ops/mbo.h>

cfi::Mbo mbo1_;
cfi::Mbo mbo2_;

void initialize() {
  mbo1_.initialize("root=sum_agg(@trade_price * @trade_qty * @trade_side)", "m1");
  mbo2_.initialize("root=sum_agg(@trade_price*@trade_qty - "
                    "@trade_price*@trade_qty*@trade_side)", "m2");
}

void on_cs_mbo(const CsMboEvent *ev) {
  // 逐标的处理
  std::vector<const double *> inputs{3, nullptr};
  for (uint16_t i = 0; i < ev->ins_nr; ++i) {
    inputs[0] = trade_price[i];
    inputs[1] = trade_qty[i];  // 需要展开为逐笔数组
    inputs[2] = trade_side[i];
    auto result = mbo1_.update(inputs, cnt, i);
    sigs.emplace_back(result);
  }
}
```

Mbo 公式支持：
- `sum_agg(@expr)` — 按标的分组聚合
- `@trade_price` — 逐笔成交价
- `@trade_qty` — 逐笔成交量
- `@trade_side` — 逐笔成交方向（0=买, 1=卖）

## 完整示例

参见 `demos/ops_feature/`、`demos/ops-mbo/` 和 `demos/feature-factor/` 目录。
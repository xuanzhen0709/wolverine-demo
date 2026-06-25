> [📖 Docs](../README.md) › 规范 › C++ 命名规范

# C++ 命名规范

本仓库 C++ demo 遵循的命名约定。核心目标：成员变量一眼可辨、与局部变量/参数区分清晰，
避免在回调密集的 Signal 代码里混淆。

## 成员变量：`snake_case_`（末尾下划线）

**类（`class`/`struct` 作 OOP 对象时）的私有成员变量用 `snake_case_` —— 末尾一个下划线。**

```cpp
class Signal {
private:
  SignalApis apis_ = {nullptr};          // 引擎 API 句柄
  size_t cnt_ = 0;                        // 计数
  cfi::Operators op1_;                    // 算子
  cfi::FactorNode node1_;
  std::vector<double> av1_;               // 横截面缓冲
  std::vector<double> ret_;
  const MdStatic *ms0_{nullptr};
};
```

- 末尾 `_` 即“私有成员”标记
- POD / 纯数据 `struct` 的成员可不带末尾下划线（与 `MdStatic`、`MdBar` 等框架数据结构一致）。
- 嵌套类型成员（如 `NeumaierAccumulator` 内部）同样遵循 `snake_case_`（`sum_`、`c_`）。

> 注：传给算子的字符串 id（如 `op1_.initialize(formula, "op1")` 里的 `"op1"`）只是内部标签，
> 不必套用成员命名；让它与成员名同名即可，不必带下划线。

## 其他命名

| 类别 | 约定 | 示例 |
|------|------|------|
| 类型 / class / struct（OOP） | `PascalCase` | `Signal`、`Factor`、`NeumaierAccumulator` |
| POD 数据 struct | `snake_case_t` | `tx_snapshot_t`、`crypto_trade_t`（与框架一致） |
| 函数 / 方法 | `snake_case` | `on_cs_snapshot`、`update_signal`、`get_fld` |
| 局部变量 / 参数 | `snake_case`（无下划线） | `ev`、`ins_nr`、`formula`、`ret1` |
| 命名空间 | `lowercase` | `nickchenyj::crossref`、`cfi::wolverine` |
| 宏 | `UPPER_SNAKE_CASE` | `C_DECLARATION_BEGIN`、`WOLVERINE_EXPORT`、`NAN_FILTER` |
| 模板参数 | 单大写 | `T`、`Fld`、`Args` |

## 示例：一个最小 Signal 的命名

```cpp
namespace nickchenyj {
namespace crossref {

class Signal {
public:
  Signal();
  void initialize(const Config *root);
  void set_apis(SignalApis);
  void on_sod(const SodEvent *ev);
  void on_eod(const EodEvent *ev);
  void on_cs_snapshot(const CsSnapshotEvent *ev);
private:
  SignalApis apis_ = {nullptr};   // 成员：末尾下划线
  size_t cnt_ = 0;
};

void Signal::on_cs_snapshot(const CsSnapshotEvent *ev) {
  using FldType = CsSnapshotEvent::FldType;
  const auto *bp = CsSnapshotUtils::get_fld<FldType::bp>(ev);  // 局部：无下划线
  ++cnt_;
  std::vector<double> sigs(ev->ins_nr, double(cnt_));
  apis_.update_signal(apis_.token, ev->exchtime, ev->localtime,
                      ev->ins_nr, sigs.data());
}

} // namespace crossref
} // namespace nickchenyj
```

## 格式化

- 使用仓库根的 `.clang-format`（已存在）自动格式化，不手动争论风格。
- `#pragma once` / include 顺序 / `using namespace` 规则参见全局 C++ 编码规范。

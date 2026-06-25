> [📖 Docs](../README.md) › 指南 › 使用 Checkpoint

# 使用 Checkpoint

Checkpoint 机制允许 Signal 在多次运行之间保存和恢复内部状态，支持跨日策略的连续性。

## 何时需要 Checkpoint

- 需要跨日维护状态（移动平均、累计统计等）
- 分阶段运行回测（先 save 再 load）
- 避免重跑历史数据，节省计算时间

## 配置

### 保存 Checkpoint

```yaml
# save.yml
start: 20230103
end: 20230103

checkpoint:
  dir: checkpoint
  save:
    - 20230103

signal:
  name: checkpoint_save
  module: nickchenyj.checkpoint
  ...
```

### 加载 Checkpoint

```yaml
# load.yml
start: 20230104
end: 20230104

checkpoint:
  dir: checkpoint
  auto_load: yesterday     # 自动加载前一日 checkpoint

signal:
  name: checkpoint_load
  module: nickchenyj.checkpoint
  ...
```

配置项说明：

| 配置项 | 类型 | 说明 |
|--------|------|------|
| `checkpoint.dir` | string | 存放 checkpoint 文件的目录 |
| `checkpoint.save` | list | 需要保存状态的日期列表 |
| `checkpoint.load` | string/int | 指定加载某日期的 checkpoint |
| `checkpoint.auto_load` | string | 自动加载策略（如 `yesterday`） |

## C++ 实现

### 序列化工具

使用 `zpp_helper.hpp` 提供的 `serialize`/`deserialize` 函数：

```cpp
#include "zpp_helper.hpp"

void Signal::load_state(const std::string &indir)
{
  const auto dir = std::filesystem::path(indir);
  deserialize(dir / "d2vec.bin", d2vec_);   // 恢复 std::vector
  deserialize(dir / "stats.bin", stats_);   // 恢复自定义 struct
}

void Signal::save_state(const std::string &outdir)
{
  const auto dir = std::filesystem::path(outdir);
  serialize(dir / "d2vec.bin", d2vec_);
  serialize(dir / "stats.bin", stats_);
}
```

支持序列化的类型：
- 标准容器：`std::vector`、`std::array`、`std::map`
- 算术类型：`int`、`double`、`size_t` 等
- 自定义 `struct`（成员需为上述类型）

### Operators / FactorNode 的 Checkpoint

使用 `cfi_operators` 时，算子自带 checkpoint 机制：

```cpp
void Signal::load_state(const std::string &dir)
{
  op1_.load_checkpoint(dir);    // 加载算子内部状态
  op2_.load_checkpoint(dir);
  node1_.load_checkpoint(dir);  // 加载 FactorNode 内部状态
}

void Signal::save_state(const std::string &dir)
{
  op1_.save_checkpoint(dir);
  op2_.save_checkpoint(dir);
  node1_.save_checkpoint(dir);
}
```

## Python 实现

```python
import pickle
from pathlib import Path

def load_state(self, path: str):
    indir: Path = Path(path)
    with open(indir / "state1.bin", "rb") as fin:
        val1 = pickle.load(fin)
        val2 = pickle.load(fin)

def save_state(self, path: str):
    outdir: Path = Path(path)
    with open(outdir.joinpath("state1.bin"), "wb") as fout:
        pickle.dump(self.cnt, fout)
        pickle.dump(self.cnt + 1, fout)
```

### checkpoint-py：与 C++ 版同构的 save/load 配置对

`demos/checkpoint-py/` 与 C++ 版的 `demos/checkpoint/` 结构一致，提供 `save.yml` /
`load.yml` / `run.sh`：

```bash
cd demos/checkpoint-py
./run.sh        # save(20230103) → copy ckpt → load(20230104) → diff
```

- **`save.yml`** ——运行 20230103–20230104，保存 20230103 的日末 checkpoint（`checkpoint/20230103/checkpoint_save/state1.bin`），并输出 CSV。
- **`load.yml`** ——`auto_load: yesterday`，加载 20230103 的 checkpoint，仅运行 20230104，输出到 `checkpoint_load` 名下。
- **`run.sh`** ——串起 save → `cp` 重建名为 `checkpoint_load` 的 ckpt 目录 → load → `diff` 对比 20230104 输出，应当一致。

## 验证 Checkpoint 正确性

`scripts/validate_checkpoint.py` 提供自动化验证：

```bash
python3 scripts/validate_checkpoint.py demos/checkpoint/save.yml 20230103
```

验证逻辑：
1. **save 阶段**：运行指定日期，保存日末 checkpoint
2. **load 阶段**：加载 checkpoint，运行次日
3. **full 阶段**：不保存/加载，连续运行两日
4. **对比**：load 阶段输出应与 full 阶段匹配

## 完整示例

参见 `demos/checkpoint/` 和 `demos/checkpoint-py/` 目录：

```
demos/checkpoint/
├── main.cpp         # Signal 实现（含 serialize/deserialize）
├── save.yml         # 运行 20230103–20230104，保存 20230103 的 checkpoint
├── load.yml         # 加载 20230103 的 checkpoint 运行 20230104
├── run.sh           # 一键 save→copy→load→diff 脚本
├── zpp_bits.h       # 序列化库
└── zpp_helper.hpp   # serialize/deserialize 封装

demos/checkpoint-py/
├── main.py          # Signal 实现（pickle 持久化）
├── save.yml         # 同上：运行 20230103–20230104，保存 20230103 的 checkpoint
├── load.yml         # 加载 20230103 的 checkpoint 运行 20230104
└── run.sh           # 同上：一键 save→copy→load→diff 脚本
```
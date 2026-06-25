> [📖 Docs](../README.md) › 指南 › 编写一个 Signal

# 编写一个 Signal

## C++ 实现

### 最小示例

```cpp
#include <wolverine/common.hpp>
#include <wolverine/config.hpp>
#include <wolverine/event.hpp>
#include <wolverine/logging.hpp>
#include <wolverine/marketdata.hpp>
#include <wolverine/signal.hpp>

using namespace cfi::wolverine;

namespace myns {

class Signal {
public:
  Signal();
  void initialize(const Config *root);
  void set_apis(SignalApis);
  void on_sod(const SodEvent *ev);
  void on_eod(const EodEvent *ev);
  void on_cs_snapshot(const CsSnapshotEvent *ev);

private:
  SignalApis apis_ = {nullptr};
  size_t cnt_ = 0;
};

Signal::Signal() {}

void Signal::initialize(const Config *root) {}

void Signal::set_apis(SignalApis apis) { apis_ = apis; }

void Signal::on_sod(const SodEvent *ev)
{
  cnt_ = 0;
  wllog_info("ins_nr={}\n", ev->ins_nr);
}

void Signal::on_eod(const EodEvent *ev)
{
  wllog_info("{} updates received\n", cnt_);
}

void Signal::on_cs_snapshot(const CsSnapshotEvent *ev)
{
  ++cnt_;
  std::vector<double> sigs(ev->ins_nr, double(cnt_));
  apis_.update_signal(apis_.token, ev->exchtime, ev->localtime,
                       ev->ins_nr, sigs.data());
}

} // namespace myns

C_DECLARATION_BEGIN;

void on_create(void **ptr, SignalOps *ops)
{
  using myns::Signal;
  *ptr = new Signal{};
  *ops = SignalOps{
    .initialize = [](void *hdl, const Config *root) -> void {
      auto *ptr = reinterpret_cast<Signal *>(hdl);
      ptr->initialize(root);
    },
    .set_apis = [](void *hdl, SignalApis apis) -> void {
      auto *ptr = reinterpret_cast<Signal *>(hdl);
      ptr->set_apis(apis);
    },
    .on_sod = [](void *hdl, const SodEvent *ev) -> void {
      auto *ptr = reinterpret_cast<Signal *>(hdl);
      ptr->on_sod(ev);
    },
    .on_eod = [](void *hdl, const EodEvent *ev) -> void {
      auto *ptr = reinterpret_cast<Signal *>(hdl);
      ptr->on_eod(ev);
    },
    .on_cs_snapshot = [](void *hdl, const CsSnapshotEvent *ev) -> void {
      auto *ptr = reinterpret_cast<Signal *>(hdl);
      ptr->on_cs_snapshot(ev);
    },
  };
}

C_DECLARATION_END;
```

### 关键要点

1. **`on_create` 必须是自由函数且带 `C_DECLARATION_BEGIN/END`** — 确保符号可见且不被 name mangling 破坏
2. **`SignalOps` 回调表** — 每个回调都通过 lambda 将 `void*` handle 转为具体类型，然后调用成员函数
3. **`update_signal` 输出信号** — 参数为 `(token, exchtime, localtime, ins_nr, sigs_ptr)`
4. **命名空间** — 使用唯一命名空间避免符号冲突（module 名通常在 yaml 中指定为 `namespace.classname`）

### 读取配置

```cpp
void Signal::initialize(const Config *root)
{
  auto seed = (*root)["seed"].as<double>();
  auto formula = (*root)["formula"].as<std::string>();
  wllog_info("using seed {}, formula {}\n", seed, formula);
}
```

### 访问行情数据

**横截面行情（CsSnapshotEvent）：**
```cpp
void Signal::on_cs_snapshot(const CsSnapshotEvent *ev)
{
  using FldType = CsSnapshotEvent::FldType;
  const auto *bp = CsSnapshotUtils::get_fld<FldType::bp>(ev);  // bp[level][ins]
  const auto *volume = CsSnapshotUtils::get_fld<FldType::volume>(ev); // volume[ins]
  const auto *last_price = CsSnapshotUtils::get_fld<FldType::last_price>(ev);
}
```

**横截面 MBO（CsMboEvent）：**
```cpp
void Signal::on_cs_mbo(const CsMboEvent *ev)
{
  using TradeFldType = CsMboEvent::TradeFldType;
  const auto *trade_cnt = CsMboUtils::get_fld<TradeFldType::Cnt>(ev);
  const auto *trade_price = CsMboUtils::get_fld<TradeFldType::Price>(ev);
  const auto *trade_qty = CsMboUtils::get_fld<TradeFldType::Qty>(ev);

  for (int ins = 0; ins < ev->ins_nr; ++ins) {
    for (int ti = 0; ti < trade_cnt[ins]; ++ti) {
      auto price = trade_price[ins][ti];
      auto qty = trade_qty[ins][ti];
    }
  }
}
```

**时间序列行情（SnapshotEvent）：**
```cpp
void Signal::on_snapshot(const SnapshotEvent *ev)
{
  const auto ss = ev->snapshot;
  wllog_info("last_price:{},level_nr:{}\n", ss->last_price, ss->level_nr);
  for (size_t i = 0; i < ss->level_nr; ++i) {
    const auto &lvl = ss->levels[i];
    // lvl.bp, lvl.bv, lvl.ap, lvl.av
  }
}
```

## Python 实现

### 最小示例

```python
import numpy as np
from cfi.wolverine.signal import *
from cfi.wolverine.event import *
from cfi.wolverine.marketdata import *

class MySig(SignalBase):
    def __init__(self):
        super().__init__()
        self.cnt: int = 0
        self.sigval: np.ndarray = np.full((1,), np.nan, dtype=np.float64)

    def initialize(self, cfg_str: str):
        cfg = yaml.safe_load(cfg_str)

    def on_sod(self, ev: SodEvent):
        self.cnt = 0
        print(f"on_sod:{ev.date},ins_nr:{ev.ins_nr}")

    def on_eod(self, ev: EodEvent):
        print(f"total tick cnt:{self.cnt}")

    def on_snapshot(self, ev: SnapshotEvent):
        self.cnt += 1
        ss: MdSnapshot = ev.snapshot.contents
        self.sigval[0] = self.cnt
        self.update_signal(ss.exchtime, ss.localtime, self.sigval)

def pysig_create():
    return MySig()
```

### 关键要点

1. **继承 `SignalBase`** — 获得引擎回调注册、`update_signal` 等能力
2. **`pysig_create()`** — 模块级工厂函数，引擎调用它创建实例
3. **`cfg_str: str`** — 配置以 YAML 字符串形式传入，需要 `yaml.safe_load` 解析
4. **`self.update_signal(exchtime, localtime, sigs)`** — 输出信号，sigs 为 numpy 数组

### 访问行情数据

**横截面行情（CsSnapshotEvent）：**
```python
def on_cs_snapshot(self, ev: CsSnapshotEvent):
    last_price_data = ev.data[CsSnapshotEvent.FldType.LAST_PRICE.value]
    # 必须复制以缓存数据
    data = np.ndarray.copy(last_price_data)
```

**时间序列行情（SnapshotEvent）：**
```python
def on_snapshot(self, ev: SnapshotEvent):
    ss: MdSnapshot = ev.snapshot.contents
    ap = ss.get_arr("ap")  # 所有档位 ask price
    bp = ss.get_arr("bp")  # 所有档位 bid price
```

### Cython 加速（`.pyx`）

Python demo 可以使用 `.pyx` 文件利用 Cython 编译获得 C 原生类型性能：

```python
import cython
from libc.stdint cimport *

cimport numpy as np
np.import_array()

class MySig(SignalBase):
    def on_cs_mbo(self, ev: CsMboEvent):
        cdef int ins_nr = ev.ins_nr
        cdef uint32_t* cnts = <uint32_t*><intptr_t>(ev.get_trades_cnt_ptr())
        cdef double* price_arr
        for ii in range(ins_nr):
            ins_cnt = cnts[ii]
            ...
```

## YAML 配置

Signal 必须通过 `wlsim.yml` 注册：

```yaml
signal:
  name: my_signal          # 唯一标识符
  module: myns.mysig       # 命名空间.类名（C++）/ 模块名（Python）
  is_python: false         # true 为 Python
  output:
    module: csv            # csv / npy / parquet
    config:
      output_dir: output
  config:                  # 传递给 initialize() 的配置
    targets:
      - dynamic:stocks.CHN
    seed: 1.234
```

## 目录结构约定

```
demos/<demo-name>/
├── CMakeLists.txt         # 构建配置
├── main.cpp / main.py(x)  # Signal 实现
├── wlsim.yml              # 主配置文件
├── save.yml               # （可选）checkpoint 保存配置
├── load.yml               # （可选）checkpoint 加载配置
└── output/                # 输出目录
```
> [📖 Docs](../README.md) › 指南 › 因子市值 / 行业中性化

# 因子市值 / 行业中性化

在算 IC / ICIR 前，把因子在每个截面上**剔除市值、行业带来的暴露**：把因子对
`[1, z(log 流通市值), 中证一级行业哑元]` 做 OLS，取**残差**再和 label 算相关。
这样得到的 IC 反映的是「因子本身」的选股能力，而不是搭了市值 / 行业的顺风车。

- **实现**：[factor_neutralize.py](../../factor_neutralize.py)（暴露构建 + 截面残差化）
  + [factor_ic_yearly.ipynb](../../factor_ic_yearly.ipynb) 里的 `NEUTRALIZE` 开关。
- **触发方式**：设 `NEUTRALIZE="size" / "industry" / "both"`，其余全自动
  （统一暴露缓存 `results/exposures_<行业口径>.pkl`：首次触库读盘建好，之后**所有因子批次一直复用**；
  日期超出缓存则只**增量**补缺的交易日）。

---

## 数据源与口径

| 暴露 | 来源 | 口径 | 覆盖范围 |
|------|------|------|---------|
| 行业 | JYDB `LC_CSIIndustry.FstIndustryCSI` | 中证**一级**（≈11 类；二级=`SndIndustryCSI`） | 逐日快照，**覆盖至今** |
| 流通股本 | JYDB `LC_ShareStru.NonResiSharesJY` | 无限售流通股 | 快照，**冻结于 2025-04** |
| 收盘价 | NAS `refdata_v3.csv` 的 `Close` | 当日**原始**收盘（非复权） | **覆盖至今** |

**流通市值 = `NonResiSharesJY`（按最近 `EndDate ≤ 当日` 的快照 carry-forward）× 当日原始 `Close`。**

### 为什么这么算（关键约束 + 交叉验证）

JYDB 里现成的流通市值 `LC_DIndicesForValuation.NegotiableMV` 和股本表 `LC_ShareStru`
在 public_data 快照里**都冻结于 2025-04**，无法覆盖研究区间的后半段；而
`LC_CSIIndustry` 与 NAS `Close` 覆盖至今。于是用「**冻结股本 carry-forward × 当日 Close**」
把市值补全到全区间。

抽样日（2020 / 2022 / 2024）对比「补全值」vs JYDB 原生 `NegotiableMV`：

| 指标 | 结果 |
|------|------|
| `corr(logA, logB)` | ≈ **0.9997** |
| rank corr | ≈ 0.9995 |
| `\|A/B − 1\| < 5%` 覆盖 | 97 ~ 100% |
| 中位比值 | 恰为 1.0000 |

结论：截面 size 排序对慢变的股本口径不敏感，补全值用于中性化**足够**。

> ⚠️ 必须用 `NonResiSharesJY`（或近乎等价的 `NonRestrictedShares`）。
> `AFloatListed` 口径差很多（corr 0.993 / 仅 71% 落在 5% 内），**不要用**。

---

## 数据流

```
              JYDB (mssql, 只读 public_data)            NAS refdata_v3.csv
        ┌───────────────────────────────┐          (当日原始 Close)
        │ LC_CSIIndustry.FstIndustryCSI  │ 行业(至今)        │
        │ LC_ShareStru.NonResiSharesJY   │ 股本(冻结2025-04) │
        └───────────────┬───────────────┘                  │
                        └──────────┬───────────────────────┘
                                   │ 阶段1 ensure_cache → build/增量（主进程一次：触库 + 读 NAS）
                                   │   · 行业: 去重连续快照 → asof carry-forward
                                   │   · 股本: asof carry-forward
                                   │   · 市值 = 股本 × 当日 close  ← 在这里算好并入缓存
                                   ▼
                 results/exposures_<行业口径>.pkl  （统一缓存，与因子无关）
                     { IND:{date:{sym:ind}}, MV:{date:{sym:市值}}, ... }
                                   │  (worker fork 继承；运行期不再碰 JYDB / NAS)
                                   │ 阶段2 date_exposures（每因子每日，纯查缓存）
                                   ▼
                     exp = DataFrame[sym → logmv, ind]   logmv = log(MV)
                                   │ 阶段3 residualize（每截面）
                                   │   y ~ 1 + z(logmv) + 行业哑元(drop_first) → 残差
                                   ▼
                     阶段4  残差因子 vs label → Pearson / Spearman IC / ICIR
```

---

## 全流程

### 阶段 0 · 口径确定（已完成，见上）

行业用中证一级，市值用 `NonResiSharesJY × Close`，交叉验证 corr≈0.9997。此结论已固化进代码默认值，无需每次重跑。

### 阶段 1 · `ensure_cache(dates, cache_path, industry_col)` —— 统一缓存入口

暴露只取决于交易日范围、**与因子无关**，所以所有因子批次共享同一份
`results/exposures_<industry_col>.pkl`（中证一级 / 二级各一份，互不覆盖）。`ensure_cache` 三种情形：

- 缓存不存在 / 口径变了 / 旧格式(无 `MV`) → 全量 `build_exposure_cache`（触库 + 读 NAS）。
- 缓存已覆盖本次日期 → 直接**复用**，0 触库 / 0 读 NAS。
- 缓存只覆盖一部分 → 只对**缺失交易日**拉库 + 读 NAS，merge 进缓存后写回（历史不重算）。

`build_exposure_cache` 主进程**触库 + 读 NAS**：把行业、股本按 asof carry-forward 摊平，再读一遍 NAS
把市值算好，一起 pickle 到 `cache_path`。要点：

- **行业**：向前多取约 1 年做缓冲（`ind_lo`），先按 sym 去掉「连续相同行业」的快照以压体积，
  再对每个交易日用 `searchsorted(side="right")-1` 取 ≤ 它的最近快照。对**假日 / 缺快照稳健**
  （曾在端午节当天因用「精确日匹配」而全 NaN，改 asof 后修复）。
- **股本**：同样按 sym asof carry-forward（冻结快照也能一路 carry 到全区间）。
- **市值**：对每个交易日读当日 NAS `close`，算 `MV = carry-forward 股本 × close` 存进缓存
  （缓存存 `MV` 而非 `SHR`）—— 这样运行期就不必再读 NAS；缺 refdata 的日期市值留空。
- 定宽 `YYYYMMDD` 字符串**字典序 = 时间序**，可直接 `np.searchsorted`。

### 阶段 2 · `date_exposures(date, syms, cache)`

对某交易日、给定 `syms`，产出 `DataFrame(index=syms, columns=[logmv, ind])`：

- `logmv = log(缓存里预算好的市值 MV)`，缺失记 NaN；
- **纯查缓存**，运行期不读 NAS（close 已在阶段 1 并入 `MV`）。

### 阶段 3 · `residualize(values, exp, mode, min_count)`

单截面 OLS 取残差，`mode ∈ {"size", "industry", "both"}`：

1. 掩码 `y`、`logmv` 有限且 `ind` 非空的样本；有效数 `< min_count`（默认 30）→ 整列 NaN。
2. 构造设计矩阵 `X`：截距 + `z(logmv)`（size/both）+ 行业哑元 `get_dummies(drop_first=True)`（industry/both，避开哑元陷阱）。
3. `np.linalg.lstsq(X, yy)` → 残差 `yy − X @ beta` 写回原位置。

残差与 logmv 正交（验证 corr≈1e-17）。

### 阶段 4 · IC 集成（notebook `NEUTRALIZE` 开关）

`factor_ic_yearly.ipynb` 已内建：

- 配置 cell：`NEUTRALIZE` / `INDUSTRY_COL` / `EXPO_CACHE`。
- 计算 cell：跑 `Pool` **之前**建 / 复用 `EXPO` 缓存（缓存不覆盖任务日期或 `industry_col` 变了 → 重建），
  多进程 fork 让各 worker 继承 `EXPO` 全局。
- `process_date`：每因子每日读一次 `close`，对每个截面先 `residualize` 再 `_corr_pair`。

---

## 代码结构

### factor_neutralize.py

| 函数 | 阶段 | 作用 |
|------|------|------|
| `ensure_cache(dates, cache_path, industry_col)` | 1 | **统一缓存入口**：覆盖则复用 / 缺则增量补 / 口径变则重建 |
| `default_cache_path(industry_col)` | 1 | 统一缓存路径 `results/exposures_<col>.pkl`（与因子无关） |
| `build_exposure_cache(dates, cache_path, industry_col, refdata_root)` | 1 | 全量构建：触库拉行业+股本 + 读 NAS 算市值，asof 摊平，pickle |
| `load_cache(path)` / `cache_covers(cache, dates)` | 1 | 读缓存 / 判断是否覆盖给定交易日 |
| `load_close(date, refdata_root)` | 1 | 读当日 NAS `refdata_v3.csv` 原始 Close（**仅建缓存时用**） |
| `date_exposures(date, syms, cache)` | 2 | 单日暴露 `DataFrame[sym → logmv, ind]`（纯查缓存，不读 NAS） |
| `residualize(values, exp, mode, min_count)` | 3 | 单截面回归取残差 |
| 常量 `JYDB_URL` / `REFDATA_ROOT` | — | 连接串（只读 public_data）/ NAS refdata 根目录 |

### notebook 集成点

| cell | 改动 |
|------|------|
| 配置 | `NEUTRALIZE` / `INDUSTRY_COL="FstIndustryCSI"` / `EXPO_CACHE="results/exposures_<INDUSTRY_COL>.pkl"`（**与因子无关**） |
| `process_date` | `import factor_neutralize as fn`；建 `exp`、`residualize` 后再算相关（不再读 close） |
| 计算 | Pool 前 `EXPO = fn.ensure_cache(task_dates, EXPO_CACHE, INDUSTRY_COL)`（覆盖复用 / 缺则增量 / 口径变重建） |
| 聚合 | `results` 里附带 `neutralize` / `industry_col` |

---

## 使用方法

```bash
# 环境（默认 python3 是无依赖 uv 版，会 ModuleNotFoundError）
PY=/home/zxuan/.local/wlsim.rocky9/bin/python3

# notebook 里改配置 cell，或用环境变量驱动：
IC_PREFIX=obi IC_NEUTRALIZE=both  $PY -m jupyter nbconvert --execute factor_ic_yearly.ipynb
```

- `NEUTRALIZE`：`None`=不做 / `"size"`=只市值 / `"industry"`=只行业 / `"both"`=两者。
- `INDUSTRY_COL`：`"FstIndustryCSI"`（一级，默认）或 `"SndIndustryCSI"`（二级）。
- **统一缓存**：`results/exposures_<INDUSTRY_COL>.pkl` 与因子无关、跨批次共享。本次日期超出缓存 →
  只**增量**补缺的交易日（历史不重算）；改 `INDUSTRY_COL` → 自动切到 / 另建一份该口径的缓存；
  手动强制重建则删掉对应 `EXPO_CACHE` 文件即可。

---

## 数据量

只有 `EXPO_CACHE` 这一个 pickle 落盘；**JYDB 与 NAS 都只在建缓存时读一次**，之后跑 IC
只 load 这个本地 pickle（`/localdata` NVMe，秒级），运行期 0 次 JYDB / NAS。

| 区间 | 交易日 | 缓存磁盘 | 加载 RAM |
|------|-------|---------|---------|
| 59 天 | 59 | ~11 MB | ~76 MB |
| 2024–2025 | 485 | ~90 MB | ~629 MB |
| 全区间(至今) | 2073 | ~344 MB | ~2.7 GB |

- **建缓存（一次性）**：JYDB 行业约数百万行 + 股本约 26.7 万行；NAS 每天读一个
  `refdata_v3.csv`（约 5.5 MB，单日实测 ~175 ms），全区间 2073 天约 6 分钟。
- **运行期**：市值 `MV` 已并入缓存，跑 IC 不再读 NAS —— 省掉每次约 6 分钟的 NAS 读盘。
- 存 `MV` 与原先存股本 `SHR` 体积相同（每 date-sym 一个 float），缓存不变大。
- 可选优化：缓存只存「变更点」而非逐日 `{date: {...}}`，磁盘 / RAM 可再降约 10×。

---

## 注意事项

- **连接串仅只读 public_data**：`mssql+pymssql://public_data:public_data@dbs.cfi/JYDB`。
- **SecuMain join 取 A 股**：`SecuCategory=1 AND SecuMarket IN (83=SH, 90=SZ)`，
  `sym = SecuCode + '.' + {83:SH, 90:SZ}`。行业按 `InnerCode` join，股本按 `CompanyCode` join。
- **多进程**：`EXPO` 等全局须在建 `Pool` **之前**定义，靠 Linux fork 继承；worker 里不要用
  自定义 globals 的 `exec`（会破坏 `_run` 的 pickle 查找）。
- **点位对齐**：因子每日 9 行（含 15:00），label 每日 8 行；按 `exchtime` 取交集自动丢 15:00 行。
- **缓存与历史 close 绑定**：市值已并入 `MV`，若 NAS 历史 close 被回补 / 修正，删对应 `exposures_<col>.pkl` 重建即可；
  日期超出缓存会**增量补**，改 `INDUSTRY_COL` 或缓存是旧格式（仅 `SHR`）会**自动重建**。

#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""市值 & 行业中性化的 exposures 构建与截面残差化。

暴露口径
    行业   : LC_CSIIndustry 的 **中证一级** FstIndustryCSI（逐日快照，覆盖至今）。
    市值   : 无限售流通市值 = NonResiSharesJY(无限售流通股, LC_ShareStru) × NAS refdata 原始 Close。
             股本按最后一个 EndDate ≤ 交易日的快照 **carry-forward**；close 取当日 NAS 收盘。
             市值在 build_exposure_cache 时算好并缓存，运行期（跑 IC）不再读 NAS。

为什么这么算（交叉验证结论）
    JYDB 的 LC_DIndicesForValuation.NegotiableMV(流通市值) 与
    NonResiSharesJY × NAS_close 在 2020/2022/2024 抽样日  corr(logA,logB)≈0.9997、
    rankcorr≈0.9995、|A/B-1|<5% 覆盖 97~100%（中位比值恰为 1.0000）。
    但 NegotiableMV 与 LC_ShareStru 在 public_data 快照里都 **冻结于 2025-04**；
    而 LC_CSIIndustry 与 NAS Close 覆盖至今 —— 故用「冻结股本 carry-forward × 当日 close」
    把市值 **补全** 到全区间，用于中性化足够（截面 size 排序对慢变股本不敏感）。

用法
    dates = ["20240102", ...]                        # 交易日 YYYYMMDD
    EXPO = ensure_cache(dates)                        # 统一缓存：覆盖则复用 / 缺则增量补（主进程一次）
    exp  = date_exposures("20240102", syms, EXPO)    # -> DataFrame[sym -> logmv, ind]
    resid = residualize(factor_vec, exp, mode="both")# 截面回归取残差

    统一缓存 results/exposures_<industry_col>.pkl 与因子无关、跨批次一直复用（见 default_cache_path）。
"""
from __future__ import annotations

import os
import pickle

import numpy as np
import pandas as pd

JYDB_URL = "mssql+pymssql://public_data:public_data@dbs.cfi/JYDB"
REFDATA_ROOT = "/mnt/zxuan/nas-3/ProcessedData/reference_data"
EXPO_DIR = "results"                                  # 统一暴露缓存所在目录


def default_cache_path(industry_col: str = "FstIndustryCSI") -> str:
    """统一暴露缓存路径：**与因子无关**（暴露只取决于交易日范围），仅按行业口径隔离。

    所有因子批次共享同一个 results/exposures_<industry_col>.pkl，跨批次、跨时间一直复用；
    中证一级 / 二级各存一份，互不覆盖。
    """
    return os.path.join(EXPO_DIR, f"exposures_{industry_col}.pkl")


# ----------------------------- 工具 -----------------------------

def _sym(df: pd.DataFrame) -> pd.Series:
    """SecuCode + 交易所后缀 → '000001.SZ'（SecuMarket 83=SH, 90=SZ）。"""
    return df["SecuCode"] + "." + np.where(df["SecuMarket"] == 83, "SH", "SZ")


def _dint_to_sql(d: str) -> str:
    """'20240102' → '2024-01-02'。"""
    return f"{d[:4]}-{d[4:6]}-{d[6:8]}"


# ----------------------------- 缓存构建（触库，主进程一次） -----------------------------

def _compute_exposures(dates, industry_col: str = "FstIndustryCSI",
                       refdata_root: str = REFDATA_ROOT) -> dict:
    """拉行业(逐日快照)+股本(carry-forward)+读 NAS 算市值，返回 payload（**不写盘**）。

    dates       : list[str] 交易日 YYYYMMDD
    industry_col: 中证一级 FstIndustryCSI（默认）/ 二级 SndIndustryCSI
    refdata_root: NAS refdata 根目录（建缓存时读 close 算市值；运行期不再用）
    返回 {IND:{date:{sym:ind}}, MV:{date:{sym:市值}}, dates, industry_col}
    """
    from sqlalchemy import create_engine, text          # 延迟导入，worker 不需要

    dates = sorted(set(dates))
    lo, hi = _dint_to_sql(dates[0]), _dint_to_sql(dates[-1])
    # 行业快照按 asof carry-forward（对假日/缺快照稳健）；向前多取 ~1 年做缓冲，
    # 保证最早的交易日也能取到不晚于它的行业快照。
    ind_lo = f"{int(dates[0][:4]) - 1}-01-01"
    eng = create_engine(JYDB_URL)
    with eng.connect() as c:
        ind = pd.read_sql(text(
            f"""SELECT s.SecuCode, s.SecuMarket, i.EndDate, i.{industry_col} AS ind
                FROM LC_CSIIndustry i JOIN SecuMain s ON i.InnerCode = s.InnerCode
                WHERE i.EndDate >= :lo AND i.EndDate < DATEADD(day, 1, :hi)
                  AND s.SecuCategory = 1 AND s.SecuMarket IN (83, 90)"""),
            c, params={"lo": ind_lo, "hi": hi})
        sh = pd.read_sql(text(
            """SELECT s.SecuCode, s.SecuMarket, e.EndDate, e.NonResiSharesJY AS shares
               FROM LC_ShareStru e JOIN SecuMain s ON e.CompanyCode = s.CompanyCode
               WHERE s.SecuCategory = 1 AND s.SecuMarket IN (83, 90)
                 AND e.EndDate <= DATEADD(day, 1, :hi)
                 AND e.NonResiSharesJY IS NOT NULL"""),
            c, params={"hi": hi})

    tdates = np.array(dates)                              # 定宽 YYYYMMDD → 字典序=时间序

    # 行业：按 sym asof carry-forward（先按 ind 去重连续快照以压体积），再对每个交易日取 ≤ 它的最近快照
    ind["sym"] = _sym(ind)
    ind["d"] = pd.to_datetime(ind["EndDate"]).dt.strftime("%Y%m%d")
    ind = ind.dropna(subset=["ind"]).sort_values(["sym", "d"])
    keep = ind.groupby("sym", sort=False)["ind"].transform(lambda s: s.ne(s.shift()))
    ind = ind[keep.to_numpy(dtype=bool)]                 # 只留行业变更点
    IND = {d: {} for d in dates}
    for s, g in ind.groupby("sym", sort=False):
        ed = g["d"].to_numpy()
        iv = g["ind"].to_numpy()
        pos = np.searchsorted(ed, tdates, side="right") - 1
        for i, d in enumerate(dates):
            if pos[i] >= 0:
                IND[d][s] = iv[pos[i]]

    # 股本：每个 sym 的 (排序 EndDate, shares)，对每个交易日 carry-forward（asof）
    sh["sym"] = _sym(sh)
    sh["shares"] = pd.to_numeric(sh["shares"], errors="coerce")
    sh = sh.dropna(subset=["shares"]).sort_values("EndDate")
    SHR = {d: {} for d in dates}
    for s, g in sh.groupby("sym", sort=False):
        ed = pd.to_datetime(g["EndDate"]).dt.strftime("%Y%m%d").to_numpy()
        sv = g["shares"].to_numpy()
        pos = np.searchsorted(ed, tdates, side="right") - 1   # 最近的 ed <= 每个交易日
        for i, d in enumerate(dates):
            if pos[i] >= 0:
                SHR[d][s] = sv[pos[i]]

    # 市值 = carry-forward 流通股本 × 当日 NAS 原始 Close。
    # 建缓存时读一遍 NAS 把市值算好缓存，运行期（跑 IC）就不再碰 NAS。
    MV = {d: {} for d in dates}
    for i, d in enumerate(dates):
        try:
            cd = load_close(d, refdata_root).to_dict()
        except FileNotFoundError:
            continue                                          # 该日无 refdata → 市值留空
        for s, sh_v in SHR[d].items():
            px = cd.get(s)
            if px is not None and px > 0 and sh_v > 0:
                MV[d][s] = sh_v * px
        if (i + 1) % 200 == 0 or i + 1 == len(dates):
            print(f"\r  [exposures] 读 NAS close {i + 1}/{len(dates)} 天",
                  end="", flush=True)
    print()

    payload = {"IND": IND, "MV": MV, "dates": dates, "industry_col": industry_col}
    return payload


def _save_cache(payload: dict, cache_path: str) -> None:
    """把 payload 落盘并打印摘要。"""
    os.makedirs(os.path.dirname(cache_path) or ".", exist_ok=True)
    with open(cache_path, "wb") as f:
        pickle.dump(payload, f, protocol=pickle.HIGHEST_PROTOCOL)
    dates = payload["dates"]
    n_ind = sum(len(v) for v in payload["IND"].values())
    n_mv = sum(len(v) for v in payload["MV"].values())
    print(f"[exposures] 缓存 → {cache_path}  行业 col={payload['industry_col']}  "
          f"日期 {dates[0]}~{dates[-1]} ({len(dates)}天)  "
          f"行业条目 {n_ind}  市值条目 {n_mv}  (已含 NAS close，运行期不再读盘)")


def build_exposure_cache(dates, cache_path: str, industry_col: str = "FstIndustryCSI",
                         refdata_root: str = REFDATA_ROOT) -> dict:
    """全量构建暴露缓存：触库拉行业+股本、读 NAS 算市值，pickle 到 cache_path。返回 payload。"""
    payload = _compute_exposures(sorted(set(dates)), industry_col, refdata_root)
    _save_cache(payload, cache_path)
    return payload


def ensure_cache(dates, cache_path: str = None, industry_col: str = "FstIndustryCSI",
                 refdata_root: str = REFDATA_ROOT) -> dict:
    """确保统一暴露缓存覆盖 dates；缺哪些交易日就**增量**补哪些，返回加载好的 cache。

    这是「与因子无关」的统一入口：暴露只取决于交易日范围，所有因子批次共享同一个
    results/exposures_<industry_col>.pkl，跨批次、跨时间一直复用。

    - 缓存不存在 / 行业口径不符 / 旧格式(无 MV) → 全量 build。
    - 缓存已覆盖 dates                          → 直接复用（0 触库 / 0 读 NAS）。
    - 缓存只覆盖一部分                           → 只对缺失交易日拉库+读 NAS，merge 后写回。
    """
    if cache_path is None:
        cache_path = default_cache_path(industry_col)
    want = sorted(set(dates))

    cache = load_cache(cache_path) if os.path.exists(cache_path) else None
    if cache is not None and (cache.get("industry_col") != industry_col
                              or "MV" not in cache):
        cache = None                                     # 口径变了 / 旧格式 → 全量重建

    if cache is None:
        return build_exposure_cache(want, cache_path, industry_col, refdata_root)

    missing = sorted(set(want) - set(cache["dates"]))
    if not missing:
        print(f"[exposures] 复用统一缓存 {cache_path}"
              f"（{len(cache['dates'])}天，已覆盖本次 {len(want)} 天，0 触库 / 0 读 NAS）")
        return cache

    # 增量：只对缺失交易日拉库 + 读 NAS，merge 进现有缓存后写回（历史暴露不重算）。
    print(f"[exposures] 统一缓存缺 {len(missing)} 天 "
          f"({missing[0]}~{missing[-1]}) → 增量触库 + 读 NAS…", flush=True)
    add = _compute_exposures(missing, industry_col, refdata_root)
    cache["IND"].update(add["IND"])
    cache["MV"].update(add["MV"])
    cache["dates"] = sorted(set(cache["dates"]) | set(add["dates"]))
    _save_cache(cache, cache_path)
    return cache


def load_cache(cache_path: str) -> dict:
    with open(cache_path, "rb") as f:
        return pickle.load(f)


def cache_covers(cache: dict, dates) -> bool:
    """缓存是否覆盖给定交易日集合。"""
    return set(dates) <= set(cache.get("dates", []))


# ----------------------------- 运行期：单日暴露 + 截面残差 -----------------------------

def load_close(date: str, refdata_root: str = REFDATA_ROOT) -> pd.Series:
    """某交易日 NAS refdata 的原始 Close，index='000001.SZ' 形式（沪深股/指数都含）。"""
    rd = pd.read_csv(os.path.join(refdata_root, date, "refdata_v3.csv"), dtype=str)
    rd = rd[rd["Exchange"].isin(["SH", "SZ"])].copy()
    rd["sym"] = rd["Ticker"] + "." + rd["Exchange"]
    return pd.to_numeric(rd.set_index("sym")["Close"], errors="coerce")


def date_exposures(date: str, syms, cache: dict) -> pd.DataFrame:
    """某交易日、给定 syms 的暴露 → DataFrame(index=syms, columns=[logmv, ind])。

    logmv = log(缓存里预算好的流通市值)；市值 = carry-forward 无限售流通股 × 当日
    NAS 原始 Close，已在 build_exposure_cache 时算好，运行期不再读 NAS。缺失记 NaN。
    """
    mv = cache.get("MV", {}).get(date, {})
    ind = cache["IND"].get(date, {})
    vals = pd.Series({s: mv.get(s, np.nan) for s in syms}, dtype=float)
    out = pd.DataFrame(index=list(syms))
    out["logmv"] = np.log(vals.reindex(out.index))
    out["ind"] = [ind.get(s) for s in out.index]
    return out


def residualize(values, exp: pd.DataFrame, mode: str = "both",
                min_count: int = 30) -> np.ndarray:
    """截面回归取残差：y ~ 1 [+ z(logmv)] [+ 行业哑元]。

    values : 1d，按 exp.index 顺序对齐的因子截面值
    mode   : "size" | "industry" | "both"
    返回残差 ndarray（被剔除/样本不足处为 NaN），与 values 等长同序。
    """
    y = np.asarray(values, dtype=float)
    logmv = exp["logmv"].to_numpy(dtype=float)
    ind = exp["ind"].to_numpy(dtype=object)
    m = np.isfinite(y) & np.isfinite(logmv) & pd.notna(ind)
    out = np.full(y.shape, np.nan)
    if int(m.sum()) < min_count:
        return out

    yy = y[m]
    cols = [np.ones(m.sum())]                            # 截距
    if mode in ("size", "both"):
        z = logmv[m]
        sd = z.std()
        cols.append((z - z.mean()) / (sd if sd > 0 else 1.0))
    if mode in ("industry", "both"):
        du = pd.get_dummies(pd.Categorical(ind[m]), drop_first=True)
        for cc in du.columns:                            # drop_first 避开哑元陷阱
            cols.append(du[cc].to_numpy(dtype=float))
    X = np.column_stack(cols)
    beta, *_ = np.linalg.lstsq(X, yy, rcond=None)
    out[np.flatnonzero(m)] = yy - X @ beta
    return out

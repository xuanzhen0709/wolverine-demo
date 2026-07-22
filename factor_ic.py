#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
因子 × label 截面相关性（IC）计算。

输入
    factor_dir : 因子结果目录。两种布局都支持：
        - 直接含日期子目录  <factor_dir>/<date>/<name>-<date>.csv          → 单个因子序列
        - 含多个 stat 子目录 <factor_dir>/<stat>/<date>/<stat>-<date>.csv   → 多个序列(逐个算)
      例：output/factors/obi （含 mean/skew/twa/last 四个序列）
    --label-dir: label 目录，含 <date>/label-<date>.csv（默认 label/1d）

两边都是「每天 8 行」的宽表：exchtime,localtime,<symbol...>；缺失写 "nan"。
因子行与 label 行**按 exchtime 对齐**（两者 8 个时点完全一致），
列**按 symbol 名取交集**，再对同时有限的样本算截面相关。

计算
    对因子每一行(每个时点的截面)算：
        Pearson  corr(factor_row, label_row)
        Spearman corr = 秩后的 Pearson（scipy.rankdata 处理并列）
    再**逐年**统计：IC = 年内所有截面相关的均值；ICIR = 均值/标准差(ddof=1)。

输出
    - 逐序列打印 markdown 表格（Year | Days | Slices | Pearson IC/ICIR | Spearman IC/ICIR）
    - 逐序列保存 JSON 到 results/ic_<group>_<series>.json

多进程：按「日期」并行（每个 worker 读 1 个 label 文件 + 各序列 1 个因子文件）。

用法
    python3 factor_ic.py output/factors/obi
    python3 factor_ic.py output/factors/obi --label-dir label/1d --jobs 32
    python3 factor_ic.py output/factors/obi/mean --start 20230101 --end 20251231
"""
from __future__ import annotations

import argparse
import json
import os
import sys
from collections import defaultdict
from functools import partial
from multiprocessing import Pool

import numpy as np
import pandas as pd
from scipy.stats import rankdata


# ----------------------------- 目录 / 文件发现 -----------------------------

def is_date(name: str) -> bool:
    return len(name) == 8 and name.isdigit()


def date_dirs(path: str) -> list[str]:
    """返回 path 下的日期子目录名（已排序）。"""
    if not os.path.isdir(path):
        return []
    return sorted(d for d in os.listdir(path)
                  if is_date(d) and os.path.isdir(os.path.join(path, d)))


def csv_in(date_dir: str) -> str | None:
    """取日期目录里的那个 csv（每个日期目录恰好一个）。"""
    for f in sorted(os.listdir(date_dir)):
        if f.endswith(".csv"):
            return os.path.join(date_dir, f)
    return None


def discover_series(factor_dir: str) -> tuple[str, dict[str, str]]:
    """
    返回 (group_name, {series_name: series_path})。
    - factor_dir 直接含日期目录 → 单序列，series_name = basename，group = 父目录名
    - 否则把每个「含日期目录」的子目录当作一个序列，group = basename(factor_dir)
    """
    factor_dir = factor_dir.rstrip("/")
    base = os.path.basename(factor_dir)
    if date_dirs(factor_dir):  # 单序列
        group = os.path.basename(os.path.dirname(factor_dir)) or base
        return group, {base: factor_dir}
    series: dict[str, str] = {}
    for e in sorted(os.listdir(factor_dir)):
        p = os.path.join(factor_dir, e)
        if os.path.isdir(p) and date_dirs(p):
            series[e] = p
    return base, series


# ----------------------------- 单文件读取 / 相关 -----------------------------

def _load(path: str):
    """读宽表 → (exchtime[int64], symbols[list], matrix[rows×nsym, float])。"""
    df = pd.read_csv(path)
    ex = df["exchtime"].to_numpy(dtype=np.int64)
    syms = list(df.columns[2:])                     # 跳过 exchtime,localtime
    mat = df[syms].to_numpy(dtype=float)            # 'nan' 被 pandas 解析为 NaN
    return ex, syms, mat


def _corr_pair(f: np.ndarray, l: np.ndarray, min_count: int):
    """单个截面：返回 (pearson, spearman, n_valid)。样本不足或零方差 → NaN。"""
    mask = np.isfinite(f) & np.isfinite(l)
    n = int(mask.sum())
    if n < min_count:
        return np.nan, np.nan, n
    fm, lm = f[mask], l[mask]
    pear = (np.nan if fm.std() == 0 or lm.std() == 0
            else float(np.corrcoef(fm, lm)[0, 1]))
    fr, lr = rankdata(fm), rankdata(lm)             # 平均秩，处理并列
    spear = (np.nan if fr.std() == 0 or lr.std() == 0
             else float(np.corrcoef(fr, lr)[0, 1]))
    return pear, spear, n


def process_date(task, min_count: int):
    """
    一个交易日：读 label 一次 + 各序列因子一次，对齐后逐行算相关。
    task = (date, label_file, {series: factor_file})
    返回 {series: [(date, exchtime, pearson, spearman, n_valid), ...]}
    """
    date, label_file, series_files = task
    lex, lsyms, lmat = _load(label_file)
    lrow = {int(e): i for i, e in enumerate(lex)}   # exchtime → label 行号
    lpos = {s: i for i, s in enumerate(lsyms)}      # symbol   → label 列号

    out: dict[str, list] = {}
    for series, ffile in series_files.items():
        fex, fsyms, fmat = _load(ffile)
        common = [s for s in fsyms if s in lpos]    # 按名取交集（保持因子列序）
        rows = []
        if common:
            fcol = np.fromiter((fsyms.index(s) for s in common), dtype=np.int64)
            lcol = np.fromiter((lpos[s] for s in common), dtype=np.int64)
            # 预先按因子的 symbol 顺序抽出 label 列
            for r in range(len(fex)):
                e = int(fex[r])
                if e not in lrow:
                    continue
                pear, spear, n = _corr_pair(fmat[r, fcol], lmat[lrow[e], lcol],
                                            min_count)
                rows.append((date, e, pear, spear, n))
        out[series] = rows
    return out


# ----------------------------- 逐年聚合 -----------------------------

def _stats(pear: np.ndarray, spear: np.ndarray) -> dict:
    """给定一组截面相关值，算 IC(均值)/std/ICIR(均值/std)。"""
    def agg(a):
        a = a[np.isfinite(a)]
        if a.size == 0:
            return dict(ic=None, std=None, icir=None, n=0)
        m = float(a.mean())
        s = float(a.std(ddof=1)) if a.size > 1 else 0.0
        return dict(ic=m, std=s, icir=(m / s if s > 0 else None), n=int(a.size))
    return {"pearson": agg(pear), "spearman": agg(spear)}


def aggregate_by_year(rows: list) -> dict:
    """
    rows = [(date, exchtime, pearson, spearman, n_valid), ...]（单序列全量）
    → {year: {...}, "ALL": {...}}，每个含 n_days/n_slices/pearson/spearman。
    """
    by_year_p: dict[str, list] = defaultdict(list)
    by_year_s: dict[str, list] = defaultdict(list)
    days: dict[str, set] = defaultdict(set)
    for date, _e, pear, spear, _n in rows:
        y = date[:4]
        by_year_p[y].append(pear)
        by_year_s[y].append(spear)
        days[y].add(date)

    result: dict[str, dict] = {}
    all_p, all_s, all_days = [], [], set()
    for y in sorted(by_year_p):
        p = np.array(by_year_p[y], float)
        s = np.array(by_year_s[y], float)
        st = _stats(p, s)
        result[y] = {"n_days": len(days[y]), "n_slices": len(p),
                     "pearson": st["pearson"], "spearman": st["spearman"]}
        all_p.append(p); all_s.append(s); all_days |= days[y]
    if all_p:
        p = np.concatenate(all_p); s = np.concatenate(all_s)
        st = _stats(p, s)
        result["ALL"] = {"n_days": len(all_days), "n_slices": len(p),
                         "pearson": st["pearson"], "spearman": st["spearman"]}
    return result


# ----------------------------- 打印 / 保存 -----------------------------

def _fmt(v, nd):
    return "nan" if v is None or (isinstance(v, float) and not np.isfinite(v)) \
        else f"{v:+.{nd}f}"


def to_markdown(group: str, series: str, label_dir: str, by_year: dict) -> str:
    lines = [f"### {group} / {series}   (label = {label_dir})",
             "",
             "| Year | Days | Slices | Pearson IC | Pearson ICIR | "
             "Spearman IC | Spearman ICIR |",
             "|------|-----:|-------:|-----------:|-------------:|"
             "------------:|--------------:|"]
    order = [k for k in by_year if k != "ALL"]
    if "ALL" in by_year:
        order.append("ALL")
    for y in order:
        r = by_year[y]
        p, s = r["pearson"], r["spearman"]
        label = "**ALL**" if y == "ALL" else y
        lines.append(f"| {label} | {r['n_days']} | {r['n_slices']} "
                     f"| {_fmt(p['ic'],4)} | {_fmt(p['icir'],3)} "
                     f"| {_fmt(s['ic'],4)} | {_fmt(s['icir'],3)} |")
    return "\n".join(lines)


# ----------------------------- 主流程 -----------------------------

def build_tasks(series: dict[str, str], label_dir: str,
                start: str | None, end: str | None):
    """构造 [(date, label_file, {series: factor_file})]，只保留 label 与因子都有的日期。"""
    label_dates = set(date_dirs(label_dir))
    lo, hi = (start or "00000000"), (end or "99999999")

    # 每个序列可用日期 → 文件
    ser_files: dict[str, dict[str, str]] = {}
    for name, path in series.items():
        ser_files[name] = {d: csv_in(os.path.join(path, d)) for d in date_dirs(path)}

    all_dates = sorted({d for m in ser_files.values() for d in m} & label_dates)
    tasks = []
    for d in all_dates:
        if not (lo <= d <= hi):
            continue
        lf = csv_in(os.path.join(label_dir, d))
        if not lf:
            continue
        sf = {name: ser_files[name][d] for name in series
              if ser_files[name].get(d)}
        if sf:
            tasks.append((d, lf, sf))
    return tasks


def main():
    ap = argparse.ArgumentParser(
        description="因子 × label 截面相关性 (Pearson/Spearman)，逐年统计",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    ap.add_argument("factor_dir", help="因子目录 (如 output/factors/obi 或其下某个 stat)")
    ap.add_argument("--label-dir", default="label/1d", help="label 目录")
    ap.add_argument("--out-dir", default="results", help="JSON 输出目录")
    ap.add_argument("--jobs", "-j", type=int, default=min(32, os.cpu_count() or 8),
                    help="并行进程数")
    ap.add_argument("--start", default=None, help="起始日期 YYYYMMDD (含)")
    ap.add_argument("--end", default=None, help="结束日期 YYYYMMDD (含)")
    ap.add_argument("--min-count", type=int, default=30,
                    help="单截面最少有效样本数，不足记 NaN")
    args = ap.parse_args()

    if not os.path.isdir(args.factor_dir):
        sys.exit(f"因子目录不存在: {args.factor_dir}")
    if not os.path.isdir(args.label_dir):
        sys.exit(f"label 目录不存在: {args.label_dir}")

    group, series = discover_series(args.factor_dir)
    if not series:
        sys.exit(f"在 {args.factor_dir} 下未发现任何含日期子目录的因子序列")

    tasks = build_tasks(series, args.label_dir, args.start, args.end)
    if not tasks:
        sys.exit("没有可用的（因子 ∩ label）交易日")

    print(f"factor group : {group}")
    print(f"series       : {', '.join(series)}")
    print(f"label        : {args.label_dir}")
    print(f"dates        : {tasks[0][0]} ~ {tasks[-1][0]}  ({len(tasks)} 天)")
    print(f"jobs         : {args.jobs}   min_count={args.min_count}")
    print("-" * 64)

    # 并行按日期计算，再按序列汇总
    per_series: dict[str, list] = {name: [] for name in series}
    worker = partial(process_date, min_count=args.min_count)
    done = 0
    with Pool(args.jobs) as pool:
        for res in pool.imap_unordered(worker, tasks, chunksize=8):
            for name, rows in res.items():
                per_series[name].extend(rows)
            done += 1
            if done % 200 == 0 or done == len(tasks):
                print(f"\r  progress {done}/{len(tasks)} 天", end="", flush=True)
    print()
    print("-" * 64)

    os.makedirs(args.out_dir, exist_ok=True)
    for name in series:
        by_year = aggregate_by_year(sorted(per_series[name]))
        # 打印 markdown
        print()
        print(to_markdown(group, name, args.label_dir, by_year))
        # 保存 JSON
        out = {"factor_group": group, "series": name,
               "factor_dir": os.path.abspath(os.path.join(series[name])),
               "label_dir": os.path.abspath(args.label_dir),
               "min_count": args.min_count,
               "date_range": [tasks[0][0], tasks[-1][0]],
               "by_year": by_year}
        fn = os.path.join(args.out_dir, f"ic_{group}_{name}.json")
        with open(fn, "w") as f:
            json.dump(out, f, ensure_ascii=False, indent=2)
        print(f"\n  saved → {fn}")
    print("\n完成。")


if __name__ == "__main__":
    main()

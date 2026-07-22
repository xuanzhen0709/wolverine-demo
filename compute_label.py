#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
计算跨日 label。

输入目录结构（与 output/stats/mid 一致）:
    <in_dir>/<date>/mid-<date>.csv
每个 csv 为宽表:
    exchtime, localtime, <symbol_1>, <symbol_2>, ...
共 8 个数据行（8 个时间点: 9:30/10:00/10:30/11:00/13:00/13:30/14:00/14:30），
所有日期的这 8 个时间点完全一致，因此可以按行依次对应。

逻辑:
    对每个日期 d, 找到 *下一个有效日期* d_next（排序后紧邻的下一个日期，不要求自然日相邻）。
    对 8 个时间点逐行、逐 symbol 计算跨日 return:
        label(d, t, s) = mid(d_next, t, s)·Adj(d_next, s) / (mid(d, t, s)·Adj(d, s)) - 1
    即 d 的 9:30 的 target 是 d_next 的 9:30，10:00 对 10:00，依此类推。

    ★ 除权除息修正（复权）:
      跨日收益必须用复权价，否则除权除息日的机械跳空（送转/分红导致价格缩水）会被
      当成假收益污染 label（实测分红季单日约 2% 的票受影响，最坏 -30%）。
      Adj(d, s) = 该 symbol 在 d 的后复权因子 AdjFactor，来自
        <refdata_dir>/<date>/refdata_v3.csv 的 AdjFactor 列（按 Ticker.Exchange 对齐）。
      AdjFactor 日内恒定，8 个时点共用同一因子。
      某 symbol 在 d 或 d_next 缺 AdjFactor（新股/退市/停牌无数据）→ 该位置 label = NaN。
      --refdata-dir 传空字符串则退化为不复权（raw）计算。

    比较两日时先按 symbol 对齐: 输出列 = 当日 d 的 symbol 列（顺序与 d 相同）。
    若某 symbol 在 d_next 中不存在，则该位置输出 NaN。
    最后一个日期没有 d_next，整份 label 全为 NaN（仍生成文件，保持与输入 1:1 对应）。

输出目录结构（与输入一致，前缀由 mid 改为 label）:
    <out_dir>/<date>/label-<date>.csv
NaN 以 "nan" 文本写出，与输入文件保持一致。
"""

import argparse
import os
import sys

import numpy as np
import pandas as pd

# 复权因子默认来源（每票每日后复权因子）。
DEFAULT_REFDATA_DIR = "/mnt/zxuan/nas-3/ProcessedData/reference_data"


def list_dates(in_dir):
    """返回排序后的 (date_str, csv_path) 列表。仅保留存在 mid-<date>.csv 的日期目录。"""
    dates = []
    for name in sorted(os.listdir(in_dir)):
        d = os.path.join(in_dir, name)
        if not os.path.isdir(d):
            continue
        csv = os.path.join(d, f"mid-{name}.csv")
        if os.path.isfile(csv):
            dates.append((name, csv))
    return dates


def read_day(csv_path):
    """读取单日 csv。exchtime/localtime 保持整数, symbol 列为 float, 'nan' 解析为 NaN。"""
    df = pd.read_csv(csv_path)
    return df


def load_adjfactor(refdata_dir, date):
    """
    读取某日的复权因子, 返回 dict[symbol -> AdjFactor]。
    symbol 形如 "600000.SH" = Ticker.Exchange, 与 mid 列名一致。
    文件缺失 / 无 AdjFactor 列 → 返回 None（调用方据此对该日不复权→全 NaN）。
    只取 AdjFactor 有限且 >0 的行; 同名 symbol 保留第一条。
    """
    path = os.path.join(refdata_dir, date, "refdata_v3.csv")
    if not os.path.isfile(path):
        return None
    df = pd.read_csv(path, usecols=["Ticker", "Exchange", "AdjFactor"],
                     dtype={"Ticker": str, "Exchange": str}, low_memory=False)
    df = df.dropna(subset=["Ticker", "Exchange", "AdjFactor"])
    df = df[np.isfinite(df["AdjFactor"]) & (df["AdjFactor"] > 0)]
    sym = df["Ticker"] + "." + df["Exchange"]
    return dict(zip(sym, df["AdjFactor"].astype(float)))


def _adj_vec(sym_cols, adj):
    """把 dict[symbol->AdjFactor] 投影成与 sym_cols 对齐的 (N,) 向量; 缺失记 NaN。"""
    if adj is None:
        return np.full(len(sym_cols), np.nan)
    return np.array([adj.get(s, np.nan) for s in sym_cols], dtype=float)


def compute_label(cur, nxt, adj_cur=None, adj_nxt=None, use_adj=True):
    """
    根据当日 cur 与下一日 nxt 计算 label DataFrame。
    输出列与 cur 完全一致（exchtime, localtime, cur 的 symbol 列，且顺序不变）。
    nxt 为 None 时（最后一日）返回全 NaN 的 label。

    use_adj=True 时用复权价算收益:
        label = (mid_nxt·Adj_nxt) / (mid_cur·Adj_cur) - 1
      adj_cur/adj_nxt = dict[symbol->AdjFactor]（load_adjfactor 结果）。
      任一侧缺该 symbol 的因子 → 该 symbol 该日 label = NaN（乘以 NaN 自然传播）。
    use_adj=False 时退化为原始（raw）收益: mid_nxt / mid_cur - 1。
    """
    sym_cols = list(cur.columns[2:])                 # 当日的 symbol 列（决定输出列与顺序）
    cur_vals = cur[sym_cols].to_numpy(dtype=float)   # shape (8, N)，按行=时间点

    if nxt is None:
        ret = np.full(cur_vals.shape, np.nan, dtype=float)
    else:
        # 按 symbol 名对齐: 取 nxt 中同名列; nxt 缺失的 symbol -> 全 NaN 列。
        # 两日均为 8 行且时间点顺序一致(已校验), 用 numpy 保证按行位置对应。
        nxt_vals = nxt.reindex(columns=sym_cols).to_numpy(dtype=float)  # shape (8, N)
        if use_adj:
            # AdjFactor 日内恒定 → (N,) 向量广播到 8 行; 缺失因子 → NaN 传播到该列。
            a_cur = _adj_vec(sym_cols, adj_cur)      # (N,)
            a_nxt = _adj_vec(sym_cols, adj_nxt)      # (N,)
            cur_vals = cur_vals * a_cur
            nxt_vals = nxt_vals * a_nxt
        with np.errstate(divide="ignore", invalid="ignore"):
            ret = nxt_vals / cur_vals - 1.0

    out = pd.DataFrame(ret, columns=sym_cols)
    # exchtime / localtime 直接沿用当日的值（保持整数纳秒时间戳）。
    out.insert(0, "localtime", cur["localtime"].to_numpy())
    out.insert(0, "exchtime", cur["exchtime"].to_numpy())
    return out



def main():
    ap = argparse.ArgumentParser(description="计算跨日 label（默认复权）")
    ap.add_argument("--in-dir", default="output/stats/mid",
                    help="输入目录 (默认: output/stats/mid)")
    ap.add_argument("--out-dir", default="output/stats/label",
                    help="输出目录 (默认: output/stats/label)")
    ap.add_argument("--prefix", default="label",
                    help="输出文件名前缀 (默认: label -> label-<date>.csv)")
    ap.add_argument("--refdata-dir", default=DEFAULT_REFDATA_DIR,
                    help="复权因子目录 (含 <date>/refdata_v3.csv 的 AdjFactor 列); "
                         "传空字符串 '' 则不复权(raw)")
    args = ap.parse_args()

    in_dir = args.in_dir
    out_dir = args.out_dir
    if not os.path.isdir(in_dir):
        sys.exit(f"输入目录不存在: {in_dir}")

    use_adj = bool(args.refdata_dir)
    if use_adj and not os.path.isdir(args.refdata_dir):
        sys.exit(f"复权因子目录不存在: {args.refdata_dir}（如需不复权请传 --refdata-dir ''）")

    dates = list_dates(in_dir)
    if not dates:
        sys.exit(f"在 {in_dir} 下未找到任何 mid-<date>.csv")
    mode = "复权(adjusted)" if use_adj else "不复权(raw)"
    print(f"共 {len(dates)} 个日期, 从 {dates[0][0]} 到 {dates[-1][0]}；模式: {mode}")

    os.makedirs(out_dir, exist_ok=True)

    # 滚动读取: 每个文件/复权因子只读一次。计算日期 i 需要 df[i] 与 df[i+1]。
    cur = read_day(dates[0][1])
    adj_cur = load_adjfactor(args.refdata_dir, dates[0][0]) if use_adj else None
    n_no_adj = 0
    for i, (date, _) in enumerate(dates):
        if i + 1 < len(dates):
            nxt = read_day(dates[i + 1][1])
            adj_nxt = (load_adjfactor(args.refdata_dir, dates[i + 1][0])
                       if use_adj else None)
        else:
            nxt, adj_nxt = None, None

        if use_adj and nxt is not None and (adj_cur is None or adj_nxt is None):
            n_no_adj += 1  # 该跨日缺复权数据 → 全 NaN（见 compute_label）

        label = compute_label(cur, nxt, adj_cur, adj_nxt, use_adj=use_adj)

        day_out = os.path.join(out_dir, date)
        os.makedirs(day_out, exist_ok=True)
        out_path = os.path.join(day_out, f"{args.prefix}-{date}.csv")
        # na_rep='nan' 与输入文件的缺失表示保持一致; index=False 与输入格式一致。
        label.to_csv(out_path, index=False, na_rep="nan")

        if i == len(dates) - 1:
            print(f"[{i + 1}/{len(dates)}] {date}: 无下一日, 全部 NaN -> {out_path}")

        cur = nxt          # 复用, 避免重复读取
        adj_cur = adj_nxt

    if use_adj and n_no_adj:
        print(f"警告: {n_no_adj} 个跨日因缺复权数据整份记 NaN")
    print(f"完成, 输出目录: {out_dir}")


if __name__ == "__main__":
    main()

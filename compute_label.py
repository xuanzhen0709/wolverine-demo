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
        label(d, t, s) = mid(d_next, t, s) / mid(d, t, s) - 1
    即 d 的 9:30 的 target 是 d_next 的 9:30，10:00 对 10:00，依此类推。

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


def compute_label(cur, nxt):
    """
    根据当日 cur 与下一日 nxt 计算 label DataFrame。
    输出列与 cur 完全一致（exchtime, localtime, cur 的 symbol 列，且顺序不变）。
    nxt 为 None 时（最后一日）返回全 NaN 的 label。
    """
    sym_cols = list(cur.columns[2:])                 # 当日的 symbol 列（决定输出列与顺序）
    cur_vals = cur[sym_cols].to_numpy(dtype=float)   # shape (8, N)，按行=时间点

    if nxt is None:
        ret = np.full(cur_vals.shape, np.nan, dtype=float)
    else:
        # 按 symbol 名对齐: 取 nxt 中同名列; nxt 缺失的 symbol -> 全 NaN 列。
        # 两日均为 8 行且时间点顺序一致(已校验), 用 numpy 保证按行位置对应。
        nxt_vals = nxt.reindex(columns=sym_cols).to_numpy(dtype=float)  # shape (8, N)
        with np.errstate(divide="ignore", invalid="ignore"):
            ret = nxt_vals / cur_vals - 1.0

    out = pd.DataFrame(ret, columns=sym_cols)
    # exchtime / localtime 直接沿用当日的值（保持整数纳秒时间戳）。
    out.insert(0, "localtime", cur["localtime"].to_numpy())
    out.insert(0, "exchtime", cur["exchtime"].to_numpy())
    return out


def main():
    ap = argparse.ArgumentParser(description="计算跨日 label")
    ap.add_argument("--in-dir", default="output/stats/mid",
                    help="输入目录 (默认: output/stats/mid)")
    ap.add_argument("--out-dir", default="output/stats/label",
                    help="输出目录 (默认: output/stats/label)")
    ap.add_argument("--prefix", default="label",
                    help="输出文件名前缀 (默认: label -> label-<date>.csv)")
    args = ap.parse_args()

    in_dir = args.in_dir
    out_dir = args.out_dir
    if not os.path.isdir(in_dir):
        sys.exit(f"输入目录不存在: {in_dir}")

    dates = list_dates(in_dir)
    if not dates:
        sys.exit(f"在 {in_dir} 下未找到任何 mid-<date>.csv")
    print(f"共 {len(dates)} 个日期, 从 {dates[0][0]} 到 {dates[-1][0]}")

    os.makedirs(out_dir, exist_ok=True)

    # 滚动读取: 每个文件只读一次。计算日期 i 需要 df[i] 与 df[i+1]。
    cur = read_day(dates[0][1])
    for i, (date, _) in enumerate(dates):
        nxt = read_day(dates[i + 1][1]) if i + 1 < len(dates) else None

        label = compute_label(cur, nxt)

        day_out = os.path.join(out_dir, date)
        os.makedirs(day_out, exist_ok=True)
        out_path = os.path.join(day_out, f"{args.prefix}-{date}.csv")
        # na_rep='nan' 与输入文件的缺失表示保持一致; index=False 与输入格式一致。
        label.to_csv(out_path, index=False, na_rep="nan")

        if i == len(dates) - 1:
            print(f"[{i + 1}/{len(dates)}] {date}: 无下一日, 全部 NaN -> {out_path}")

        cur = nxt  # 复用, 避免重复读取

    print(f"完成, 输出目录: {out_dir}")


if __name__ == "__main__":
    main()

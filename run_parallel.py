#!/usr/bin/env python3
"""
按机器核数把一段日期拆成多个区间，并行跑 wl-sim。

做法：
  1. 从 dates.txt 取出 [start, end] 内的真实交易日；
  2. 按 --jobs（默认 = CPU 核数）把交易日「等分成若干连续区间」；
  3. 在 tmp 目录里，依据传入的 wlsim.yml 逐区间生成辅助 yml
     （只改顶层 start/end，其余原样保留）；
  4. 每个区间起一个 `wl-sim <辅助yml>` 进程并行跑；
  5. 全部成功后删除 tmp（失败则保留日志）。

各区间的 start/end 都落在真实交易日上、且相邻区间首尾不重叠，
输出按日期写入各自子目录（output/<signal>/<date>/...），互不冲突。

用法：
  # 从仓库根目录运行（保证 yml 里相对 output_dir 落到 ./output）
  # 省略 start/end 时，默认跑 dates.txt 内的全部交易日
  python3 run_parallel.py wlsim.yml
  python3 run_parallel.py wlsim.yml 20250102 20250630
  python3 run_parallel.py wlsim.yml --jobs 16
  python3 run_parallel.py wlsim.yml 20250102 20250630 --dry-run

前置：先 ./run_build.sh -i 把因子 .so 安装到运行时目录。
"""
from __future__ import annotations

import argparse
import os
import re
import shutil
import signal as signal_mod
import subprocess
import sys
import time
from pathlib import Path


def read_trading_days(dates_path: str, start: str | None, end: str | None) -> list[str]:
    """读取 dates_path 内的交易日；start/end 为 None 时不设该侧边界（即取全部）。"""
    if not os.path.isfile(dates_path):
        sys.exit(f"找不到交易日历: {dates_path}")
    lo = start or "00000000"
    hi = end or "99999999"
    days = []
    with open(dates_path) as f:
        for line in f:
            d = line.strip()
            if len(d) == 8 and d.isdigit() and lo <= d <= hi:
                days.append(d)
    days.sort()
    return days


def split_contiguous(lst: list[str], k: int) -> list[list[str]]:
    """把列表等分成 k 个连续块（前 rem 块多 1 个）。"""
    n = len(lst)
    k = max(1, min(k, n))
    base, rem = divmod(n, k)
    chunks, idx = [], 0
    for i in range(k):
        size = base + (1 if i < rem else 0)
        chunks.append(lst[idx:idx + size])
        idx += size
    return chunks


def make_chunk_yml(base_text: str, c0: str, c1: str) -> str:
    """复制原 yml，仅替换顶层 start/end 两行，其余（含注释）原样保留。"""
    text = re.sub(r"(?m)^start:.*$", f"start: {c0}", base_text)
    text = re.sub(r"(?m)^end:.*$", f"end: {c1}", text)
    return text


def parse_signal_output(base_text: str) -> tuple[str | None, str]:
    """从 yml 文本解析 signal.name 与 output.config.output_dir（环境无 PyYAML，用正则近似）。

    只在顶层 `signal:` 块内取 name / output_dir，避免误匹配 marketdata / features。
    解析不到 name 时返回 (None, ...)，调用方据此退化为按区间粒度估计进度。
    """
    m = re.search(r"(?ms)^signal:\s*$(.*?)(?=^\S|\Z)", base_text)
    block = m.group(1) if m else base_text
    m_name = re.search(r"(?m)^\s+name:\s*([^\s#]+)", block)
    m_out = re.search(r"(?m)^\s+output_dir:\s*([^\s#]+)", block)
    return (m_name.group(1) if m_name else None,
            m_out.group(1) if m_out else "output")


def count_done_days(out_base: Path | None, days: list[str], since: float) -> int | None:
    """统计本次运行已产出输出的交易日数（自动跨所有 worker 汇总）。

    依据 output/<signal>/<date>/ 布局：某日目录存在、且 mtime 晚于本次启动时刻，
    即视为该日已落盘。各 worker 的区间互不重叠、合起来正好是全部交易日，
    因此“有输出的日期数”天然就是所有 worker 的完成天数之和。
    out_base 为 None（未能解析 signal.name）时返回 None，表示无法按天统计。
    """
    if out_base is None:
        return None
    if not out_base.is_dir():
        return 0
    n = 0
    for d in days:
        p = out_base / d
        try:
            if p.is_dir() and p.stat().st_mtime >= since - 1:
                n += 1
        except OSError:
            pass
    return n


def draw_bar(done_days: int, total_days: int, done_chunks: int, total_chunks: int,
             failed: int, elapsed: float, width: int = 30):
    """单行原地刷新的进度条，主进度按“已完成交易日数”显示，区间数作辅助信息。"""
    frac = done_days / total_days if total_days else 1.0
    fill = int(width * frac)
    bar = "█" * fill + "░" * (width - fill)
    fail = f", {failed} 失败" if failed else ""
    sys.stdout.write(
        f"\r  [{bar}] {done_days}/{total_days} 天"
        f"  ({done_chunks}/{total_chunks} 区间{fail})  {elapsed:5.1f}s")
    sys.stdout.flush()


def main():
    ap = argparse.ArgumentParser(
        description="按核数拆分日期区间并行跑 wl-sim",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    ap.add_argument("config", help="因子的 wlsim.yml 路径")
    ap.add_argument("start", nargs="?", default=None,
                    help="起始日期 YYYYMMDD (含)，缺省=dates 文件最早日期")
    ap.add_argument("end", nargs="?", default=None,
                    help="结束日期 YYYYMMDD (含)，缺省=dates 文件最晚日期")
    ap.add_argument("--jobs", "-j", type=int, default=os.cpu_count(),
                    help="并行进程数 / 区间数，默认 = CPU 核数")
    ap.add_argument("--dates", default="dates.txt", help="交易日历文件")
    ap.add_argument("--workdir", default=".",
                    help="wl-sim 的工作目录（相对 output_dir 以此为基准）")
    ap.add_argument("--tmp", default="tmp", help="辅助 yml / 日志的临时目录")
    ap.add_argument("--wl-sim", default="wl-sim", help="wl-sim 可执行文件")
    ap.add_argument("--keep-tmp", action="store_true", help="结束后保留 tmp 目录")
    ap.add_argument("--dry-run", action="store_true",
                    help="只打印区间划分与将生成的 yml，不实际运行")
    args = ap.parse_args()

    if not os.path.isfile(args.config):
        sys.exit(f"找不到配置文件: {args.config}")

    days = read_trading_days(args.dates, args.start, args.end)
    if not days:
        rng = f"[{args.start or '最早'}, {args.end or '最晚'}]"
        sys.exit(f"{rng} 内在 {args.dates} 里没有交易日")

    chunks = split_contiguous(days, args.jobs)
    label = os.path.basename(os.path.dirname(os.path.abspath(args.config))) or "job"

    with open(args.config) as f:
        base_text = f.read()

    signal_name, output_dir = parse_signal_output(base_text)
    if signal_name:
        out_base = Path(args.workdir) / output_dir / signal_name
        progress_src = f"{out_base}/<date>/"
    else:
        out_base = None
        progress_src = "未能解析 signal.name → 进度按区间粒度估计"

    print(f"配置       : {args.config}")
    print(f"区间       : {days[0]} ~ {days[-1]}  共 {len(days)} 个交易日")
    print(f"并行度     : {len(chunks)} 个区间 (jobs={args.jobs}, cores={os.cpu_count()})")
    print(f"工作目录   : {os.path.abspath(args.workdir)}")
    print(f"进度依据   : {progress_src}")
    print("-" * 60)

    # 生成辅助 yml（静默，不逐区间打印）
    os.makedirs(args.tmp, exist_ok=True)
    jobs = []
    for i, ch in enumerate(chunks):
        c0, c1 = ch[0], ch[-1]
        yml_path = os.path.join(args.tmp, f"{label}_{i:03d}_{c0}_{c1}.yml")
        with open(yml_path, "w") as f:
            f.write(make_chunk_yml(base_text, c0, c1))
        log_path = os.path.join(args.tmp, f"{label}_{i:03d}_{c0}_{c1}.log")
        jobs.append({"i": i, "c0": c0, "c1": c1, "n": len(ch),
                     "yml": yml_path, "log": log_path})

    if args.dry_run:
        # dry-run 专门用来检查划分，这里才逐区间列出
        for j in jobs:
            print(f"  区间 {j['i']:>3}: {j['c0']} ~ {j['c1']}  "
                  f"({j['n']:>3} 天)  -> {j['yml']}")
        print("\n[dry-run] 未运行；tmp 内的辅助 yml 已生成，可自行检查。")
        return

    t0 = time.time()
    running = []  # {job, proc, fh}

    def terminate_all():
        for r in running:
            if r["proc"].poll() is None:
                r["proc"].terminate()

    results = []
    total_chunks = len(jobs)
    total_days = len(days)

    def compute_done_days() -> int:
        c = count_done_days(out_base, days, t0)
        if c is None:  # 无法定位输出目录：退化为按已完成区间累计天数
            return sum(j["n"] for j, _ in results)
        return c

    try:
        pending = list(jobs)
        done_days = 0
        last_poll = 0.0
        draw_bar(0, total_days, 0, total_chunks, 0, 0.0)
        while pending or running:
            while pending and len(running) < args.jobs:
                job = pending.pop(0)
                fh = open(job["log"], "w")
                proc = subprocess.Popen(
                    [args.wl_sim, os.path.abspath(job["yml"])],
                    cwd=args.workdir, stdout=fh, stderr=subprocess.STDOUT)
                running.append({"job": job, "proc": proc, "fh": fh})
            done_changed = False
            for r in running[:]:
                rc = r["proc"].poll()
                if rc is not None:
                    r["fh"].close()
                    running.remove(r)
                    results.append((r["job"], rc))
                    done_changed = True
            failed_so_far = sum(1 for _, rc in results if rc != 0)
            now = time.time()
            # 每 ~1s（或有区间结束时）重扫输出目录，刷新“已完成天数”；每 0.2s 重绘计时
            if done_changed or now - last_poll >= 1.0:
                done_days = compute_done_days()
                last_poll = now
            draw_bar(done_days, total_days, len(results), total_chunks,
                     failed_so_far, now - t0)
            time.sleep(0.2)
    except KeyboardInterrupt:
        print("\n中断，正在终止子进程...")
        terminate_all()
        for r in running:
            r["fh"].close()
        sys.exit(130)

    elapsed = time.time() - t0
    failed = [j for j, rc in results if rc != 0]
    # 收尾：成功区间的天数是确定值，精确累计（不受输出探测的 ±1 天误差影响）
    done_days_final = sum(j["n"] for j, rc in results if rc == 0)
    draw_bar(done_days_final, total_days, len(results), total_chunks,
             len(failed), elapsed)
    print()  # 收尾换行
    print("-" * 60)
    print(f"用时 {elapsed:.1f}s，"
          f"{len(results) - len(failed)}/{len(results)} 个区间成功、"
          f"{done_days_final}/{total_days} 个交易日完成")

    if failed:
        print(f"失败区间（日志保留在 {args.tmp}/）:")
        for j in failed:
            print(f"  区间{j['i']:>3} [{j['c0']}~{j['c1']}]  日志: {j['log']}")
        sys.exit(1)

    if args.keep_tmp:
        print(f"全部成功；--keep-tmp 已保留 {args.tmp}/")
    else:
        shutil.rmtree(args.tmp, ignore_errors=True)
        print(f"全部成功；已清除 {args.tmp}/")


if __name__ == "__main__":
    main()

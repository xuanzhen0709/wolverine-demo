import argparse
from pathlib import Path
from typing import List, Dict
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import math
import numpy as np

FORMAT_STR = "{:.3f}"
plt.style.use("seaborn-v0_8-darkgrid")


def str2ns(val: str) -> int:
    if val.endswith("ns"):
        val_ns = float(val[:-2])
    elif val.endswith("s"):
        val_ns = float(val[:-1]) * int(1e9)
    elif val.endswith("m"):
        val_ns = float(val[:-1]) * int(1e9) * 60
    elif val.endswith("h"):
        val_ns = float(val[:-1]) * int(1e9) * 3600
    else:
        raise RuntimeError(f"unknown unit {val}")
    return int(val_ns)


def check_data(edge_folder: Path) -> Dict:
    print("start check data")
    if not edge_folder.exists():
        raise RuntimeError(f"folder {edge_folder} doesn't exist!")
    tokens = edge_folder.name.split(".")
    if len(tokens) < 6:
        raise RuntimeError(f"invalid edge result path {edge_folder}")
    edge_name = tokens[0]
    signal_name = ".".join(tokens[1:-4])
    start_date = tokens[-4]
    end_date = tokens[-3]
    quantile = f"{tokens[-2]}.{tokens[-1]}"
    edge_file_list = list(edge_folder.rglob("*.csv"))
    if 0 == len(edge_file_list):
        raise RuntimeError(f"edge file doesn't exist!")
    PnL_df = pd.read_csv(edge_file_list[0], low_memory=False)
    print("end check data")
    edge_info = {
        "signal": signal_name,
        "quantile": quantile,
        "start": start_date,
        "end": end_date,
        "df": PnL_df,
    }
    return edge_info


def edge_plot(edge_info: Dict, output: Path, locator: str = '5m'):
    print("start draw figure")
    output.mkdir(parents=True, exist_ok=True)
    plt.figure()
    plt.title(
        f"""{edge_info['start']}-{edge_info['end']} {edge_info['signal']} {edge_info['quantile']} edge plot"""
    )
    plt.xlabel("futret_bias(min)")
    plt.ylabel("avg_return(%)")
    edge_df = edge_info["df"]
    tick_num = len(edge_df)
    x = range(tick_num)
    plt.plot(
        x,
        (edge_df["max_edge"] - 1) * 100,
        label="Top",
        linestyle="solid",
        marker="D" if (tick_num <= 1) else None,
    )
    plt.plot(
        x,
        (edge_df["min_edge"] - 1) * 100,
        label="Bottom",
        linestyle="solid",
        marker="D" if (tick_num <= 1) else None,
    )
    plt.plot(
        x,
        (edge_df["all_avg"] - 1) * 100,
        label="Mean",
        linestyle="dotted",
        marker="D" if (tick_num <= 1) else None,
    )
    plt.plot(
        x,
        (edge_df["max_edge"] - edge_df["all_avg"]) * 100,
        label="DeMeanTop",
        linestyle="dashdot",
        marker="D" if (tick_num <= 1) else None,
    )
    plt.plot(
        x,
        (edge_df["min_edge"] - edge_df["all_avg"]) * 100,
        label="DeMeanBottom",
        linestyle="dashdot",
        marker="D" if (tick_num <= 1) else None,
    )
    plt.xticks(ticks=x, labels=(edge_df["futret_bias(ns)"] / 6e10).astype(np.int64))
    
    if tick_num > 1:
        locator_ns = str2ns(locator)
        step = edge_df["futret_bias(ns)"][1] - edge_df["futret_bias(ns)"][0]
        interval = math.ceil(float(locator_ns) / step)
        plt.gca().xaxis.set_major_locator(ticker.MultipleLocator(interval))
    
    plt.tight_layout()
    plt.legend()
    plt.savefig(f"{output}/{edge_info['signal']}_edge_plot.png")
    print("done")


def main():
    parser = argparse.ArgumentParser("edge plot")
    parser.add_argument(
        "-edge",
        "--edge-folder",
        type=Path,
        help="the folder where the edge files to be analyzed is saved",
    )
    parser.add_argument("-o", "--output", type=Path, required=True, help="output dir")
    args = parser.parse_args()

    edge_info = check_data(args.edge_folder)
    edge_plot(edge_info, args.output)


if __name__ == "__main__":
    main()

import argparse
from pathlib import Path
from typing import List, Dict
import pandas as pd
from business_calendar import Calendar
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np
import math

plt.style.use("seaborn-v0_8-darkgrid")

FORMAT_STR = "{:.4f}"
EXCH_9_30 = 9.5 * 60 * 60
EXCH_11_30 = 11.5 * 60 * 60
EXCH_13_00 = 13 * 60 * 60
START_SHIFT = EXCH_13_00 - EXCH_9_30
SHIFT_TIME = EXCH_13_00 - EXCH_11_30 - 1


def exchtime2index(exchtime: int) -> int:
    exchtime /= int(1e9)
    exchtime -= EXCH_9_30  # 9:30
    if exchtime >= START_SHIFT:  # 13:00  46800-34200
        exchtime = exchtime - SHIFT_TIME  # 13:00 - 11:30 + 1
    return int(exchtime)


def check_data(ic_folder_list: List, date_list: List) -> List[Dict]:
    ic_info_list: List = []
    for folder_path in ic_folder_list:
        if not folder_path.exists():
            raise RuntimeError(f"folder {folder_path} doesn't exit!")
        ic_file_list = list(folder_path.rglob("*.csv"))
        if 0 == len(ic_file_list):
            raise RuntimeError(f"ic file doesn't exit!")
        for ic_file in ic_file_list:
            tokens = folder_path.name.split(".")
            if len(tokens) < 5:
                raise RuntimeError(f"invalid ic result path {folder_path}")
            ic_type = tokens[0]
            signal_name = ".".join(tokens[1:-3])
            start_date = tokens[-3]
            end_date = tokens[-2]
            future_bias = tokens[-1]

            if ic_type != "cs_ic":
                raise RuntimeError(f"only cs_ic are supported")
            if not ic_file.is_file():
                raise RuntimeError(f"ic file {ic_file} doesn't exit!")
            if ic_file.stem != signal_name:
                raise RuntimeError(
                    f"ic file {ic_file} & folder {folder_path} don't match!"
                )

            ic_df = pd.read_csv(ic_file, low_memory=False)
            df_list = []
            for date in date_list:
                selected = ic_df[ic_df["date"] == date]
                if 0 == len(selected):
                    raise RuntimeError(
                        f"The file {ic_file} does not exist for the {date}'s IC data"
                    )
                df_list.append(selected)
            ic_df = pd.concat(df_list)
            ic_df["date"] = ic_df["date"].astype(int)
            ic_df["exchtime"] = ic_df["exchtime"].astype(np.int64)
            ic_df["exchtime"] = ic_df["exchtime"].apply(exchtime2index)
            ic_info_list.append(
                {"sig": signal_name, "data": ic_df, "future_bias": future_bias}
            )

    return ic_info_list


def ic_daily(
    output: Path, ic_info_list: List, date_list: List, rolling_window: int = 5
):
    daily_average_ic: Dict = {}
    continuous_average_ic: Dict = {}
    rolling_average_ic: Dict = {}

    date_num = len(date_list)
    for ic_info in ic_info_list:
        ic_df = ic_info["data"]
        sig_name = ic_info["sig"]
        continuous_average_ic[sig_name] = ic_df["ic"].mean()
        daily_average_ic[sig_name] = ic_df.groupby("date").ic.mean()

        rolling_ic: Dict = {}
        for i in range(rolling_window - 1, date_num):
            selected = ic_df[
                (ic_df["date"] >= date_list[i - rolling_window + 1])
                & (ic_df["date"] <= date_list[i])
            ]
            rolling_ic[date_list[i]] = selected["ic"].mean()

        rolling_average_ic[sig_name] = rolling_ic

    plt.figure()
    plt.title(f"{date_list[0]}-{date_list[-1]} daily ic (pearson)")
    plt.xlabel("date")
    plt.ylabel("IC")

    x = range(date_num)
    rolling_x = range(rolling_window - 1, date_num)

    for sig_name, date2ic in daily_average_ic.items():
        plt.plot(
            x,
            date2ic.values,
            linestyle="-",
            label=f"{sig_name}({ FORMAT_STR.format(continuous_average_ic[sig_name]) })",
            marker="D" if (date_num <= 1) else None,
        )

    for sig_name, date2ic in rolling_average_ic.items():
        plt.plot(
            rolling_x,
            date2ic.values(),
            label=f"{sig_name}_rollingmean_{rolling_window}d({ FORMAT_STR.format(continuous_average_ic[sig_name])})",
            marker="D" if (len(rolling_x) <= 1) else None,
        )

    interval = math.ceil(date_num / 7)
    plt.xticks(ticks=x, labels=date_list)
    plt.gca().xaxis.set_major_locator(ticker.MultipleLocator(interval))
    plt.text(
        date_num - 1, (plt.yticks()[0][-1] + plt.yticks()[0][0]) / 2, date_list[-1]
    )
    plt.axvline(x=date_num - 1, color="white", linestyle="dotted")
    # plt.gca().xaxis.set_minor_locator(ticker.FixedLocator([len(date_list)-1]))
    # plt.gca().xaxis.set_minor_formatter(
    #     ticker.FixedFormatter([str(date_list[-1])[-4:]]))

    plt.tight_layout()
    plt.legend()
    plt.savefig(f"{output}/ic_daily")


def ic_in_cycle(output: Path, ic_info_list: List, date_list: List, cycle: str = "1d"):
    cycle_sec = 0
    if cycle.endswith("1d"):
        pass
    elif cycle.endswith("s"):
        cycle_sec = int(cycle[:-1])
    elif cycle.endswith("m"):
        cycle_sec = int(cycle[:-1]) * 60
    elif cycle.endswith("h"):
        cycle_sec = int(cycle[:-1]) * 3600
    else:
        raise RuntimeError(f"unknown unit {cycle}")
    second_average_ic: Dict = {}
    average_ic: Dict = {}

    for ic_info in ic_info_list:
        ic_df = ic_info["data"].copy()
        if not cycle.endswith("1d"):
            ic_df["exchtime"] = ic_df["exchtime"].apply(lambda x: x % cycle_sec)
        sig_name = ic_info["sig"]
        average_ic[sig_name] = ic_df["ic"].mean()
        second_average_ic[sig_name] = ic_df.groupby("exchtime").ic.mean()

    if cycle.endswith("1d"):
        plt.figure(figsize=(15, 10))
        plt.title(f"{date_list[0]}-{date_list[-1]} intraDay ic (pearson)")
        plt.gca().xaxis.set_major_locator(ticker.MultipleLocator(2000))
    else:
        plt.figure()
        plt.title(f"{date_list[0]}-{date_list[-1]} {cycle} ic (pearson)")
        plt.gca().xaxis.set_major_locator(ticker.MultipleLocator(50))

    plt.xlabel("second")
    plt.ylabel("IC")

    for sig_name, second2ic in second_average_ic.items():
        plt.plot(
            second2ic.index,
            second2ic.values,
            linestyle="-",
            label=f"{sig_name}({ FORMAT_STR.format(average_ic[sig_name]) })",
            marker="D" if (len(second2ic) <= 1) else None,
        )

    plt.tight_layout()
    plt.legend()
    if cycle.endswith("1d"):
        plt.savefig(f"{output}/ic_intraDay")
    else:
        plt.savefig(f"{output}/ic_{cycle}")


def correlation_daily(output: Path, ic_info_list: List, date_list: List):
    sig_num = len(ic_info_list)
    factor_corr: Dict = {}
    for i in range(sig_num):
        for j in range(i + 1, sig_num):
            factor_pair = f"{ic_info_list[i]['sig']}--{ic_info_list[j]['sig']}"
            cur_factor_corr: Dict = {}
            df1 = ic_info_list[i]["data"]
            df2 = ic_info_list[j]["data"]
            for date in date_list:
                selected1 = df1[df1["date"] == date]
                selected2 = df2[df2["date"] == date]
                ic = np.ma.corrcoef(
                    np.ma.masked_invalid(selected1["ic"]),
                    np.ma.masked_invalid(selected2["ic"]),
                )[0][1]
                if ic:
                    cur_factor_corr[date] = ic
            factor_corr[factor_pair] = cur_factor_corr

    plt.figure()
    plt.title(f"{date_list[0]}-{date_list[-1]} daily factor correlation (pearson)")
    plt.xlabel("date")
    plt.ylabel("IC")

    date_num = len(date_list)
    x = range(date_num)

    for factor_pair, date2ic in factor_corr.items():
        plt.plot(
            x,
            date2ic.values(),
            linestyle="-",
            label=f"{factor_pair}({ FORMAT_STR.format(np.mean(list(date2ic.values()))) })",
            marker="D" if (date_num <= 1) else None,
        )

    plt.xticks(ticks=x, labels=date_list)
    interval = math.ceil(date_num / 7)
    plt.gca().xaxis.set_major_locator(ticker.MultipleLocator(interval))
    plt.text(
        date_num - 1, (plt.yticks()[0][-1] + plt.yticks()[0][0]) / 2, date_list[-1]
    )
    plt.axvline(x=date_num - 1, color="white", linestyle="dotted")
    plt.tight_layout()
    plt.legend()
    plt.savefig(f"{output}/correlation_daily")


def correlation_intraday(output: Path, ic_info_list: List, date_list: List):
    sig_num = len(ic_info_list)
    factor_corr: Dict = {}
    for i in range(sig_num):
        for j in range(i + 1, sig_num):
            factor_pair = f"{ic_info_list[i]['sig']}--{ic_info_list[j]['sig']}"
            cur_factor_corr: Dict = {}
            exchtime = np.append(
                ic_info_list[i]["data"]["exchtime"].unique(),
                ic_info_list[j]["data"]["exchtime"].unique(),
            )
            exchtime = np.unique(exchtime)
            df1 = ic_info_list[i]["data"]
            df2 = ic_info_list[j]["data"]
            for et in exchtime:
                selected1 = df1[df1["exchtime"] == et]
                selected2 = df2[df2["exchtime"] == et]
                ic = np.ma.corrcoef(
                    np.ma.masked_invalid(selected1["ic"]),
                    np.ma.masked_invalid(selected2["ic"]),
                )[0][1]
                if ic:
                    cur_factor_corr[et] = ic
            factor_corr[factor_pair] = cur_factor_corr

    plt.figure(figsize=(15, 10))
    plt.title(f"{date_list[0]}-{date_list[-1]} intraDay factor correlation (pearson)")
    plt.xlabel("second")
    plt.ylabel("IC")

    for factor_pair, second2ic in factor_corr.items():
        plt.plot(
            second2ic.keys(),
            second2ic.values(),
            linestyle="-",
            label=f"{factor_pair}({  FORMAT_STR.format(np.mean(list(second2ic.values()))) })",
            marker="D" if (len(second2ic) <= 1) else None,
        )

    plt.gca().xaxis.set_major_locator(ticker.MultipleLocator(2000))
    plt.tight_layout()
    plt.legend()
    plt.savefig(f"{output}/correlation_intraDay")


def main():
    parser = argparse.ArgumentParser("t0 stats calculator")
    parser.add_argument(
        "-ic",
        "--ic-folder",
        type=Path,
        action="append",
        help="the folder where the ic files to be analyzed is saved",
    )
    parser.add_argument("-o", "--output", type=Path, required=True, help="output dir")
    parser.add_argument("-s", "--start", type=int, required=True, help="start date")
    parser.add_argument("-e", "--end", type=int, required=True, help="end date")
    args = parser.parse_args()

    business_days: List = Calendar.get_instance().get_business_days(
        args.start, args.end
    )

    if len(business_days) <= 0:
        raise RuntimeError(
            f"There are no trading days between {args.start} and {args.end}"
        )
    ic_info_list = check_data(args.ic_folder, business_days)
    args.output.mkdir(parents=True, exist_ok=True)

    ic_daily(args.output, ic_info_list, business_days)
    for c in ["1d", "1m", "5m"]:
        ic_in_cycle(args.output, ic_info_list, business_days, c)

    if len(ic_info_list) > 1:
        correlation_daily(args.output, ic_info_list, business_days)
        correlation_intraday(args.output, ic_info_list, business_days)


if __name__ == "__main__":
    main()

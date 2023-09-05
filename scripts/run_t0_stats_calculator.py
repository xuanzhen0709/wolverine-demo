import argparse
from pathlib import Path
from typing import List, Dict
import pandas as pd
from business_calendar import Calendar
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np
from statistics import mean

plt.style.use('seaborn-v0_8-darkgrid')

FORMAT_STR = '{:.4f}'
EXCH_9_30 = 9.5 * 60 * 60
EXCH_11_30 = 11.5 * 60 * 60
EXCH_13_00 = 13 * 60 * 60
START_SHIFT = EXCH_13_00 - EXCH_9_30
SHIFT_TIME = EXCH_13_00 - EXCH_11_30 - 1


def check_data(ic_file_list: List,
               date_list: List):
    for file_path in ic_file_list:
        if not file_path.is_file():
            raise RuntimeError(f"ic file {file_path} doesn't exit!")

        csv_data = pd.read_csv(file_path, low_memory=False)
        csv_df = pd.DataFrame(csv_data)
        for date in date_list:
            selected = csv_df[csv_df['date'] == date]
            if 0 == len(selected):
                raise RuntimeError(
                    f"The file {file_path} does not exist for the {date}'s IC data")


def daily_average_ic(output: Path, ic_file_list: List,
                     date_list: List, future_bias: str, rolling_window: int = 5):
    daily_average_ic: Dict = {}
    continuous_average_ic: Dict = {}
    rolling_average_ic: Dict = {}

    for file_path in ic_file_list:
        csv_data = pd.read_csv(file_path, low_memory=False)
        csv_df = pd.DataFrame(csv_data)
        csv_df['date'] = csv_df['date'].astype(int)
        sig_name = file_path.stem
        continuous_average_ic[sig_name] = csv_df['ic'].mean()
        daily_average_ic[sig_name] = csv_df.groupby('date').ic.mean()

        rolling_ic: Dict = {}
        for i in range(rolling_window - 1, len(date_list)):
            selected = csv_df[(csv_df['date'] >= date_list[i -
                                                           rolling_window +
                                                           1]) & (csv_df['date'] <= date_list[i])]
            rolling_ic[date_list[i]] = selected['ic'].mean()

        rolling_average_ic[sig_name] = rolling_ic

    plt.figure()
    plt.title(
        f"{date_list[0]}-{date_list[-1]} IC decay (pearson)({future_bias} future ret)")
    plt.xlabel('date')
    plt.ylabel('IC')

    title = ""
    for sig_name, date2ic in daily_average_ic.items():
        plt.plot(date2ic.index, date2ic.values, linestyle='-',
                 label=f"{sig_name}({ FORMAT_STR.format(continuous_average_ic[sig_name]) })")
        plt.scatter(date2ic.index, date2ic.values)
        title += f"{sig_name}-"

    for sig_name, date2ic in rolling_average_ic.items():
        plt.plot(
            date2ic.keys(),
            date2ic.values(),
            linestyle='-.',
            label=f"{sig_name}_rollingmean_{rolling_window}d({ FORMAT_STR.format(continuous_average_ic[sig_name])})")
        plt.scatter(date2ic.keys(), date2ic.values(), marker='x')

    plt.xticks(ticks=date_list, labels=date_list)
    plt.tight_layout()
    plt.legend()
    title += f"rolling{rolling_window}d-" + "daily_IC"
    plt.savefig(f"{output}/{title}")


def exchtime2index(exchtime: int) -> int:
    exchtime /= int(1e9)
    exchtime -= EXCH_9_30       # 9:30
    if exchtime >= START_SHIFT:   # 13:00  46800-34200
        exchtime = exchtime - SHIFT_TIME   # 13:00 - 11:30 + 1
    return int(exchtime)


def secondly_average_ic(output: Path, ic_file_list: List,
                        date_list: List, future_bias: str):
    second_average_ic: Dict = {}
    average_ic: Dict = {}

    for file_path in ic_file_list:
        csv_data = pd.read_csv(file_path, low_memory=False)
        csv_df = pd.DataFrame(csv_data)
        csv_df['exchtime'] = csv_df['exchtime'].astype(int)
        csv_df['exchtime'] = csv_df['exchtime'].apply(exchtime2index)
        sig_name = file_path.stem
        average_ic[sig_name] = csv_df['ic'].mean()
        second_average_ic[sig_name] = csv_df.groupby('exchtime').ic.mean()

    plt.figure(figsize=(15, 10))
    plt.title(
        f"{date_list[0]}-{date_list[-1]} IC decay (pearson)({future_bias} future ret)")
    plt.xlabel('second')
    plt.ylabel('IC')

    title = ""
    for sig_name, second2ic in second_average_ic.items():
        plt.plot(second2ic.index, second2ic.values, linestyle='-',
                 label=f"{sig_name}({ FORMAT_STR.format(average_ic[sig_name])})")
        title += f"{sig_name}-"

    plt.gca().xaxis.set_major_locator(ticker.MultipleLocator(2000))
    plt.tight_layout()
    plt.legend()
    title += "second_IC"
    plt.savefig(f"{output}/{title}")


def average_ic_in_x_min(output: Path, ic_file_list: List,
                        date_list: List, future_bias: str, cycle: int = 5):
    second_average_ic: Dict = {}
    average_ic: Dict = {}

    cycle_sec = cycle * 60
    for file_path in ic_file_list:
        csv_data = pd.read_csv(file_path, low_memory=False)
        csv_df = pd.DataFrame(csv_data)
        csv_df['exchtime'] = csv_df['exchtime'].astype(int)
        csv_df['exchtime'] = csv_df['exchtime'].apply(exchtime2index)
        csv_df['exchtime'] = csv_df['exchtime'].apply(lambda x: x % cycle_sec)
        sig_name = file_path.stem
        average_ic[sig_name] = csv_df['ic'].mean()
        second_average_ic[sig_name] = csv_df.groupby('exchtime').ic.mean()

    plt.figure()
    plt.title(
        f"{date_list[0]}-{date_list[-1]} IC decay (pearson)({future_bias} future ret)")
    plt.xlabel('second')
    plt.ylabel('IC')

    title = ""
    for sig_name, second2ic in second_average_ic.items():
        plt.plot(second2ic.index, second2ic.values, linestyle='-',
                 label=f"{sig_name}({ FORMAT_STR.format(average_ic[sig_name]) })")
        title += f"{sig_name}-"

    plt.gca().xaxis.set_major_locator(ticker.MultipleLocator(50))
    plt.tight_layout()
    plt.legend()
    title += f"{cycle}min_IC"
    plt.savefig(f"{output}/{title}")


def daily_factor_corr(output: Path, ic_file_list: List,
                      date_list: List, future_bias: str):
    ic_df: List = []
    for file_path in ic_file_list:
        csv_data = pd.read_csv(file_path, low_memory=False)
        csv_df = pd.DataFrame(csv_data)
        csv_df['date'] = csv_df['date'].astype(int)
        sig_name = file_path.stem
        ic_df.append({
            "sig": sig_name,
            "ic_df": csv_df
        })

    sig_num = len(ic_df)
    factor_corr: Dict = {}
    for i in range(sig_num):
        for j in range(i + 1, sig_num):
            factor_pair = f"{ic_df[i]['sig']}--{ic_df[j]['sig']}"
            cur_factor_corr: Dict = {}
            for date in date_list:
                df1 = ic_df[i]["ic_df"]
                selected1 = df1[csv_df['date'] == date]
                df2 = ic_df[j]["ic_df"]
                selected2 = df2[csv_df['date'] == date]
                ic = np.ma.corrcoef(
                    np.ma.masked_invalid(selected1['ic']),
                    np.ma.masked_invalid(selected2['ic'])
                )[0][1]
                if ic:
                    cur_factor_corr[date] = ic
            factor_corr[factor_pair] = cur_factor_corr

    plt.figure()
    plt.title(
        f"{date_list[0]}-{date_list[-1]} FactorCorr (pearson)({future_bias} future ret)")
    plt.xlabel('date')
    plt.ylabel('IC')

    title = "daily_FactorCorr"
    for factor_pair, date2ic in factor_corr.items():
        plt.plot(date2ic.keys(), date2ic.values(), linestyle='-',
                 label=f"{factor_pair}({ FORMAT_STR.format(mean(date2ic.values())) })")
        plt.scatter(date2ic.keys(), date2ic.values())

    plt.xticks(ticks=date_list, labels=date_list)
    plt.tight_layout()
    plt.legend()
    plt.savefig(f"{output}/{title}")


def secondly_factor_corr(output: Path, ic_file_list: List,
                         date_list: List, future_bias: str):
    ic_df: List = []
    for file_path in ic_file_list:
        csv_data = pd.read_csv(file_path, low_memory=False)
        csv_df = pd.DataFrame(csv_data)
        csv_df['exchtime'] = csv_df['exchtime'].astype(int)
        csv_df['exchtime'] = csv_df['exchtime'].apply(exchtime2index)
        csv_df['localtime'] = csv_df['localtime'].astype(int)
        csv_df = csv_df.sort_values(
            by=['exchtime', 'localtime'], ascending=[True, True])
        sig_name = file_path.stem
        ic_df.append({
            "sig": sig_name,
            "ic_df": csv_df
        })

    sig_num = len(ic_df)
    factor_corr: Dict = {}
    for i in range(sig_num):
        for j in range(i + 1, sig_num):
            factor_pair = f"{ic_df[i]['sig']}--{ic_df[j]['sig']}"
            cur_factor_corr: Dict = {}
            exchtime = np.append(
                ic_df[i]["ic_df"]['exchtime'].unique(),
                ic_df[j]["ic_df"]['exchtime'].unique())
            exchtime = np.unique(exchtime)
            for et in exchtime:
                df1 = ic_df[i]["ic_df"]
                selected1 = df1[csv_df['exchtime'] == et]
                df2 = ic_df[j]["ic_df"]
                selected2 = df2[csv_df['exchtime'] == et]
                ic = np.ma.corrcoef(
                    np.ma.masked_invalid(selected1['ic']),
                    np.ma.masked_invalid(selected2['ic'])
                )[0][1]
                if ic:
                    cur_factor_corr[et] = ic
            factor_corr[factor_pair] = cur_factor_corr

    plt.figure(figsize=(15, 10))
    plt.title(
        f"{date_list[0]}-{date_list[-1]} FactorCorr (pearson)({future_bias} future ret)")
    plt.xlabel('second')
    plt.ylabel('IC')

    title = "secondly_FactorCorr"
    for factor_pair, second2ic in factor_corr.items():
        plt.plot(second2ic.keys(), second2ic.values(), linestyle='-',
                 label=f"{factor_pair}({  FORMAT_STR.format(mean(second2ic.values())) })")

    plt.gca().xaxis.set_major_locator(ticker.MultipleLocator(2000))
    plt.tight_layout()
    plt.legend()
    plt.savefig(f"{output}/{title}")


def main():
    parser = argparse.ArgumentParser("t0 stats calculator")
    parser.add_argument(
        "-ic",
        "--ic-file",
        type=Path,
        action="append",
        help="ic files to be analyzed")
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        required=True,
        help="output dir")
    parser.add_argument(
        "-fb",
        "--future-bias",
        type=str,
        required=True,
        help="comma separated future biases, postfixes such as 's' 'm' and 'h' are supported")
    parser.add_argument(
        "-s",
        "--start",
        type=int,
        required=True,
        help="start date")
    parser.add_argument(
        "-e",
        "--end",
        type=int,
        required=True,
        help="end date")
    args = parser.parse_args()

    business_days: List = Calendar.get_instance(
    ).get_business_days(args.start, args.end)

    check_data(
        args.ic_file,
        business_days)

    args.output.mkdir(parents=True, exist_ok=True)

    daily_average_ic(
        args.output,
        args.ic_file,
        business_days,
        args.future_bias)
    secondly_average_ic(
        args.output,
        args.ic_file,
        business_days,
        args.future_bias)
    average_ic_in_x_min(
        args.output,
        args.ic_file,
        business_days,
        args.future_bias,
        5)
    daily_factor_corr(
        args.output,
        args.ic_file,
        business_days,
        args.future_bias)
    secondly_factor_corr(
        args.output,
        args.ic_file,
        business_days,
        args.future_bias)


# python scripts/run_t0_stats_calculator.py  -ic
# output/cs_ic.csstock-py1.20230701.20230709.5m/csstock-py1.csv  -ic
# output/cs_ic.csstock-py.20230701.20230709.5m/csstock-py.csv -o output/
# --future-bias 5m -s 20230701 -e 20230709
if __name__ == "__main__":
    main()

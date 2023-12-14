import argparse
from pathlib import Path
from typing import List, Dict, Sequence, Tuple
import pandas as pd
from business_calendar import Calendar
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np
import math
from functools import reduce
import bottleneck as bn

plt.style.use("seaborn-v0_8-darkgrid")

TRADING_DAYS_PER_YREAR = 242


def localtime2str(localtime: np.uint64, format_str: str):
    t = (
        pd.to_datetime(localtime, utc=True)
        .tz_convert("Asia/Shanghai")
        .tz_localize(None)
    )
    t_str = t.strftime(format_str)
    return t_str


def hhmmss_to_exchtime(val: str) -> int:
    hh: int = int(val[:2])
    mm: int = int(val[2:4])
    ss: int = int(val[4:])
    time: int = hh * 3600 * int(1e9) + mm * 60 * int(1e9) + ss * int(1e9)
    if hh >= 18:
        time -= 24 * 3600 * int(1e9)
    return time


def get_sessions(business_days: List, instruments: List) -> Dict:
    str_sessions: Dict = {}
    for date in business_days:
        reference_file = f"/mnt/nas-3/ProcessedData/reference_data/{date}/refdata.csv"
        info = pd.read_csv(reference_file)
        info["tk_exch"] = info["Ticker"] + "." + info["Exchange"]
        info = info.set_index("tk_exch")
        for _i in instruments:
            if _i not in str_sessions:
                str_sessions[_i] = set()
            try:
                s = info["Session"][_i].split(";")
                str_sessions[_i] |= set(s)
            except Exception as e:
                print(e)

    exch_session: Dict = {}
    for _i, session in str_sessions.items():
        exch_session[_i] = []
        for s in session:
            exch_session[_i].append([hhmmss_to_exchtime(_j) for _j in s.split("-")])
        exch_session[_i].sort(key=lambda x: x[0])

    return exch_session


def check_data(pnl_folder: Path, start: int, end: int) -> (Dict, str, List, List):
    print("start check data")
    PnL_info: Dict = {}

    if not pnl_folder.exists():
        raise RuntimeError(f"folder {pnl_folder} doesn't exist!")

    tokens = pnl_folder.name.split(".")
    if len(tokens) < 5:
        raise RuntimeError(f"invalid pnl result path {pnl_folder}")
    PnL_name = tokens[0]
    signal_name = ".".join(tokens[1:-3])
    start_date = start if start else int(tokens[-3])
    end_date = end if end else int(tokens[-2])
    exec_price = tokens[-1]
    exec_price_list = exec_price.split("-")
    business_days: List = Calendar.get_instance().get_business_days(
        start_date, end_date
    )
    if len(business_days) <= 0:
        raise RuntimeError(
            f"There are no trading days between {start_date} and {end_date}"
        )
    if "pnl" != PnL_name:
        raise RuntimeError(f"only type pnl is support")
    pnl_file_list = list(pnl_folder.rglob("*.csv"))
    if 0 == len(pnl_file_list):
        raise RuntimeError(f"pnl file doesn't exist!")
    for pnl_file in pnl_file_list:
        if not pnl_file.is_file():
            raise RuntimeError(f"PnL file {pnl_file} doesn't exist!")
        instrument = pnl_file.stem
        PnL_df = pd.read_csv(pnl_file, low_memory=False)
        if 0 == len(PnL_df):
            print(f"no data for {instrument}, skip")
            continue
        PnL_df["date"] = PnL_df["date"].astype(int)
        PnL_df["exchtime"] = PnL_df["exchtime"].astype(np.int64)
        PnL_df["localtime"] = PnL_df["localtime"].astype(np.uint64)
        PnL_df[exec_price_list] = PnL_df[exec_price_list].astype(np.float64)
        PnL_info[instrument] = PnL_df

    print("end check data")
    return PnL_info, signal_name, exec_price_list, business_days


def process_data(business_days: List, PnL_info: Dict, exec_price_list: List):

    instrument_session = get_sessions(business_days, PnL_info.keys())
    for _i, PnL_df in PnL_info.items():
        df_list = []
        for date in business_days:
            for s in instrument_session[_i]:
                selected = PnL_df[
                    (PnL_df["date"] == date)
                    & (PnL_df["exchtime"] >= s[0])
                    & (PnL_df["exchtime"] <= s[1])
                ]
                if 0 == len(selected):
                    print(f"Warning: {_i} does not exist for the {date}'s PnL data")
                df_list.append(selected)
        PnL_df = pd.concat(df_list)
        PnL_df[exec_price_list] = PnL_df[exec_price_list] * 100
        PnL_df[exec_price_list] = np.nancumsum(PnL_df[exec_price_list], axis=0)
        PnL_df.reset_index(drop=True, inplace=True)
        PnL_info[_i] = PnL_df

    return PnL_info


def PnL_trend_by_instrument(
    pnl_info: Dict, signal_name: str, exec_price: List, output: Path
):
    print("start draw figure by instrument")
    output = output.joinpath("byInstrument")
    output.mkdir(parents=True, exist_ok=True)

    for intrument, pnl_df in pnl_info.items():
        plt.figure()
        plt.title(
            f"""{pnl_df['date'].iloc[0]}-{pnl_df['date'].iloc[-1]} {signal_name}-{intrument} PnL Trend"""
        )
        plt.xlabel("date")
        plt.ylabel("PnL(%)")
        tick_num = len(pnl_df)
        x = range(tick_num)
        for price in exec_price:
            plt.plot(
                x,
                pnl_df[price],
                label=price,
            )
        interval = math.ceil(tick_num / 7)
        plt.xticks(ticks=x, labels=pnl_df["date"])
        plt.gca().xaxis.set_major_locator(ticker.MultipleLocator(interval))

        last_localtime_str = localtime2str(
            pnl_df["localtime"].iloc[-1], "%Y-%m-%d\n   %H:%M:%S"
        )
        plt.text(
            tick_num - 1,
            (plt.yticks()[0][-1] + plt.yticks()[0][0]) / 2,
            f"""{last_localtime_str}""",
        )
        plt.axvline(x=tick_num - 1, color="white", linestyle="dotted")
        plt.tight_layout()
        plt.legend()

        plt.savefig(f"{output}/{intrument}.png")
        print(f"{intrument} done")
    print("end draw figure by instrument")


def PnL_trend_by_exec_price(
    pnl_info: Dict, signal_name: str, exec_price: List, output: Path
):
    print("start draw figure by exec price")
    output = output.joinpath("byExecPrice")
    output.mkdir(parents=True, exist_ok=True)

    localtime_list = []
    for df in pnl_info.values():
        localtime_list.append(df["localtime"].values)
    localtime_array = reduce(np.union1d, localtime_list)
    # localtime_array.sort()
    localtime_idx: Dict = {}
    tick_num = len(localtime_array)
    date_array: List = []
    for i, lt in enumerate(localtime_array):
        localtime_idx[lt] = i
        date_array.append(localtime2str(lt, "%Y%m%d"))

    last_localtime_str = localtime2str(localtime_array[-1], "%Y-%m-%d\n   %H:%M:%S")

    ticks = range(tick_num)
    for instrument in pnl_info:
        pnl_info[instrument]["idx"] = pnl_info[instrument]["localtime"].map(
            localtime_idx
        )
    for price in exec_price:
        plt.figure()
        plt.title(
            f"""{date_array[0]}-{date_array[-1]} {signal_name}-{price} PnL Trend"""
        )
        plt.xlabel("date")
        plt.ylabel("PnL(%)")
        for instrument, df in pnl_info.items():
            plt.plot(
                df["idx"],
                df[price],
                label=instrument,
            )
        interval = math.ceil(tick_num / 7)
        plt.xticks(ticks=ticks, labels=date_array)
        plt.gca().xaxis.set_major_locator(ticker.MultipleLocator(interval))
        plt.text(
            tick_num - 1,
            (plt.yticks()[0][-1] + plt.yticks()[0][0]) / 2,
            last_localtime_str,
        )
        plt.axvline(x=tick_num - 1, color="white", linestyle="dotted")
        plt.tight_layout()
        plt.legend()
        plt.savefig(f"{output}/{price}.png")
        print(f"{price} done")
    print("end draw figure by exec price")


def max_drawdown(returns: pd.Series) -> Tuple[int, int, np.float64]:
    cum_max = np.fmax.accumulate(returns)
    drawdown = cum_max - returns
    i = bn.nanargmax(drawdown)
    if i == 0:
        return 0, 0, 0
    j = np.argmax(returns[:i])
    return j, i, returns[j] - returns[i]


def sharpe_ratio(daily_returns: pd.Series, trading_days_per_year: int):
    std = bn.nanstd(daily_returns, ddof=1)
    if np.isclose(std, 0):
        return np.nan
    return (bn.nanmean(daily_returns) / std) * np.sqrt(trading_days_per_year)


def run_pnl_stats(pnl_folder: Path, start: int, end: int, output: Path):

    PnL_info, signal_name, exec_price, business_days = check_data(
        pnl_folder, start, end
    )
    PnL_info = process_data(business_days, PnL_info, exec_price)

    output = output.resolve().joinpath(f"{signal_name}_pnl_stats")
    fig_output = output / "figure"
    fig_output.mkdir(parents=True, exist_ok=True)
    PnL_trend_by_exec_price(PnL_info, signal_name, exec_price, fig_output)
    PnL_trend_by_instrument(PnL_info, signal_name, exec_price, fig_output)

    with pd.ExcelWriter(f"{output}/{signal_name}_stats.xlsx") as writer:
        for _i, PnL_df in PnL_info.items():
            day_nr = PnL_df["date"].nunique()
            daily_end_pnl = PnL_df.groupby("date")[exec_price].last()
            daily_pnl = np.vstack(
                [daily_end_pnl.iloc[0], np.diff(daily_end_pnl, axis=0)]
            )
            max_dd: Dict = {}
            rtn: Dict = {}
            ann_rtn: Dict = {}
            calmar: Dict = {}
            sharp: Dict = {}
            dd_start: Dict = {}
            dd_end: Dict = {}
            max_dd: Dict = {}
            for _j, price in enumerate(exec_price):
                dd_start_idx, dd_end_idx, dd = max_drawdown(PnL_df[price])
                if dd == 0:
                    dd_start_ts = None
                    dd_end_ts = None
                else:
                    dd_start_ts = localtime2str(
                        PnL_df["localtime"][dd_start_idx], "%Y-%m-%d %H:%M:%S"
                    )
                    dd_end_ts = localtime2str(
                        PnL_df["localtime"][dd_end_idx], "%Y-%m-%d %H:%M:%S"
                    )
                dd_start[price] = dd_start_ts
                dd_end[price] = dd_end_ts
                max_dd[price] = dd
                rtn[price] = PnL_df[price].iloc[-1]
                ann_rtn[price] = rtn[price] / (day_nr / TRADING_DAYS_PER_YREAR)
                calmar[price] = ann_rtn[price] / dd
                sharp[price] = sharpe_ratio(daily_pnl[:, _j], TRADING_DAYS_PER_YREAR)

            _df = pd.DataFrame(
                {
                    "max drawdown(%)": max_dd,
                    "dd start": dd_start,
                    "dd end": dd_end,
                    "return(%)": rtn,
                    "ann return(%)": ann_rtn,
                    "calmar": calmar,
                    "sharp": sharp,
                }
            )

            _df.to_excel(writer, sheet_name=_i)


def main():
    parser = argparse.ArgumentParser("pnl stats calculator")
    parser.add_argument(
        "-pnl",
        "--pnl-folder",
        type=Path,
        help="the folder where the ic files to be analyzed is saved",
    )
    parser.add_argument("-o", "--output", type=Path, required=True, help="output dir")
    parser.add_argument("-s", "--start", type=int, required=False, help="start date")
    parser.add_argument("-e", "--end", type=int, required=False, help="end date")
    args = parser.parse_args()

    run_pnl_stats(args.pnl_folder, args.start, args.end, args.output)


if __name__ == "__main__":
    main()

import argparse
from pathlib import Path
from typing import List, Dict, Tuple
import pandas as pd
from business_calendar import Calendar
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np
import math
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


def check_data(pnl_folder: Path, start: int, end: int) -> (Dict, str, List, List):
    print("start check data")
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
    if "cs_pnl" != PnL_name:
        raise RuntimeError(f"only type pnl is support")
    pnl_list: List = []
    for _dt in business_days:
        sub_dir = pnl_folder / signal_name / str(_dt)
        localtime = None
        price2dict = {}
        for _price in exec_price_list:
            _file = sub_dir / f"{_price}.csv"
            _df = pd.read_csv(_file, low_memory=False)
            if localtime is None:
                localtime = _df["localtime"].astype(np.uint64)
            else:
                if not (_df["localtime"].astype(np.uint64) == localtime).all():
                    raise
            _targets = [
                _tgt for _tgt in _df.columns if _tgt not in ["exchtime", "localtime"]
            ]
            pnl_values = bn.nanmean(_df[_targets].values, axis=1)
            price2dict[_price] = pnl_values

        final_dict = {"date": _dt, "localtime": localtime}
        final_dict.update(price2dict)
        pnl_list.append(pd.DataFrame(final_dict))

    pnl_info = pd.concat(pnl_list)
    pnl_info.reset_index(drop=True, inplace=True)
    pnl_info[exec_price_list] = pnl_info[exec_price_list] * 100
    pnl_info[exec_price_list] = np.nancumsum(pnl_info[exec_price_list], axis=0)

    print("end check data")
    return pnl_info, signal_name, exec_price_list


def draw_pnl_trend(
    pnl_df: pd.DataFrame, signal_name: str, exec_price: List, output: Path
):
    print("start draw figure")
    output.mkdir(parents=True, exist_ok=True)
    plt.figure()
    plt.title(
        f"""{pnl_df['date'].iloc[0]}-{pnl_df['date'].iloc[-1]} {signal_name} PnL Trend"""
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

    plt.savefig(f"{output}/{signal_name}.png")
    print(f"{signal_name} done")
    print("end draw figure")


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

    pnl_df, signal_name, exec_price = check_data(pnl_folder, start, end)

    output = output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    draw_pnl_trend(pnl_df, signal_name, exec_price, output)

    with pd.ExcelWriter(f"{output}/{signal_name}_stats.xlsx") as writer:
        day_nr = pnl_df["date"].nunique()
        daily_end_pnl = pnl_df.groupby("date")[exec_price].last()
        daily_pnl = np.vstack([daily_end_pnl.iloc[0], np.diff(daily_end_pnl, axis=0)])
        max_dd: Dict = {}
        rtn: Dict = {}
        ann_rtn: Dict = {}
        calmar: Dict = {}
        sharp: Dict = {}
        dd_start: Dict = {}
        dd_end: Dict = {}
        max_dd: Dict = {}
        for _j, price in enumerate(exec_price):
            dd_start_idx, dd_end_idx, dd = max_drawdown(pnl_df[price])
            if dd == 0:
                dd_start_ts = None
                dd_end_ts = None
            else:
                dd_start_ts = localtime2str(
                    pnl_df["localtime"][dd_start_idx], "%Y-%m-%d %H:%M:%S"
                )
                dd_end_ts = localtime2str(
                    pnl_df["localtime"][dd_end_idx], "%Y-%m-%d %H:%M:%S"
                )
            dd_start[price] = dd_start_ts
            dd_end[price] = dd_end_ts
            max_dd[price] = dd
            rtn[price] = pnl_df[price].iloc[-1]
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

        _df.to_excel(writer)


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

import argparse
from pathlib import Path
from typing import List
import yaml
import pandas as pd
import numpy as np
from enum import IntEnum
from business_calendar import Calendar
import bottleneck as bn
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import math

plt.style.use("seaborn-v0_8-darkgrid")

FORMAT_STR = "{:.4f}"


class SigFileType(IntEnum):
    csv = 0
    npy = 1


def calculate_turnover(sig_df: pd.DataFrame, pre_pos: pd.DataFrame, targets: List):
    if pre_pos is not None:
        sig_df = pd.concat([pre_pos, sig_df])
        sigs = np.nan_to_num(sig_df[targets].values)
        sigs_diff = np.diff(sigs, axis=0)
    else:
        sigs = np.nan_to_num(sig_df[targets].values)
        sigs_diff = np.vstack([sigs[0], np.diff(sigs, axis=0)])

    sigs_diff = np.fabs(sigs_diff)
    turnover = bn.nansum(sigs_diff)
    return turnover


def calculate_exposure(sig_df: pd.DataFrame, targets: List):
    sigs = sig_df[targets].values
    return bn.nansum(sigs)


class SingalCfg:
    def __init__(self, infile: Path, start: int, end: int, rel_sig_dir: Path):
        print(f"loading {infile}")
        with open(infile) as fin:
            self.main_cfg = yaml.safe_load(fin)
        self.name: str = self.main_cfg["signal"]["name"]
        self.start: int = start if start else int(self.main_cfg["start"])
        self.end: int = end if end else int(self.main_cfg["end"])
        sigout_cfg = self.main_cfg["signal"]["output"]
        sigout_dir: Path = Path(sigout_cfg["config"]["output_dir"])
        self.sig_dir: Path = (
            sigout_dir if sigout_dir.is_absolute() else rel_sig_dir.joinpath(sigout_dir)
        )
        self.file_type: SigFileType = SigFileType[str(sigout_cfg["module"])]
        self.cal = Calendar.get_instance()

    def load_signal(self, date) -> pd.DataFrame:
        def __load_csv(sigdir: Path, signame: str, date: int) -> pd.DataFrame:
            sig_file: Path = sigdir.joinpath(
                signame, str(date), f"{signame}-{date}.csv"
            )
            df: pd.DataFrame = pd.read_csv(
                sig_file,
                header=0,
                index_col=None,
                dtype={"exchtime": str, "localtime": np.uint64},
            )
            return df

        def __load_npy(sigdir: Path, signame: str, date: int) -> pd.DataFrame:
            date_dir: Path = sigdir.joinpath(signame, str(date))

            uv = np.memmap(
                date_dir.joinpath(f"{signame}-{date}.uv.npy"), dtype="S16", mode="r"
            )
            targets: List[str] = [x.decode("utf8") for x in uv]

            exchtime = np.memmap(
                date_dir.joinpath(f"{signame}-{date}.ts.npy"), dtype=np.int64, mode="r"
            )
            localtime = np.memmap(
                date_dir.joinpath(f"{signame}-{date}.localts.npy"),
                dtype=np.uint64,
                mode="r",
            )
            sigs = np.memmap(
                date_dir.joinpath(f"{signame}-{date}.data.npy"),
                dtype=np.float64,
                mode="r",
                shape=(exchtime.shape[0], len(targets)),
            )

            df: pd.DataFrame = pd.DataFrame(sigs, columns=targets)
            df["exchtime"] = exchtime
            df["localtime"] = localtime
            df = df[["exchtime", "localtime"] + targets]
            return df

        if self.file_type == SigFileType.csv:
            sig_df = __load_csv(self.sig_dir, self.name, date)
        else:
            sig_df = __load_npy(self.sig_dir, self.name, date)

        return sig_df

    def run_stats(
        self, output_root: Path, ignore_missing: bool, rolling_window: int = 5
    ):
        print("start run signal stats")
        output_root = output_root.resolve()
        all_date_list: List[int] = self.cal.get_business_days(self.start, self.end)
        daily_turnover: List = []
        daily_exposure: List = []
        date_list: List = []
        pre_pos = None
        for date in all_date_list:
            try:
                sig_df = self.load_signal(date)
            except:
                if ignore_missing:
                    print(f"[WARNING]:can't load signal data for {date}")
                    continue
                else:
                    raise
            if len(sig_df):
                targets = [
                    col
                    for col in sig_df.columns
                    if col not in ["localtime", "exchtime"]
                ]
                exps = calculate_exposure(sig_df, targets)
                daily_exposure.append(exps)
                trnv = calculate_turnover(sig_df, pre_pos, targets)
                daily_turnover.append(trnv)
                pre_pos = pd.DataFrame(columns=targets, data=sig_df.iloc[-1])
                date_list.append(date)

        date_num = len(date_list)
        interval = math.ceil(date_num / 7)
        x_tick = range(date_num)
        table_name = ["turnover", "exposure"]
        for _i, _matrice in enumerate([daily_turnover, daily_exposure]):
            plt.subplot(2, 1, _i + 1)
            plt.plot(
                x_tick,
                _matrice,
                label=f"{table_name[_i]}({FORMAT_STR.format(bn.nanmean(_matrice))})",
                marker="D" if (date_num <= 1) else None,
            )
            plt.legend()

            if date_num >= rolling_window:
                rolling_matrice = (
                    pd.Series(_matrice).rolling(window=rolling_window).mean()
                )
                plt.plot(
                    x_tick,
                    rolling_matrice,
                    label=f"{table_name[_i]}_rollingmean_{rolling_window}d({FORMAT_STR.format(bn.nanmean(rolling_matrice))})",
                    marker="D" if (date_num <= 1) else None,
                )
            plt.title(table_name[_i])
            plt.xticks(ticks=x_tick, labels=date_list)
            plt.gca().xaxis.set_major_locator(ticker.MultipleLocator(interval))
            plt.legend()
            plt.text(
                date_num - 1,
                (plt.yticks()[0][-1] + plt.yticks()[0][0]) / 2,
                date_list[-1],
            )
            plt.axvline(x=date_num - 1, color="white", linestyle="dotted")
        plt.suptitle(f"{self.name} signal stats")
        output_root.mkdir(exist_ok=True, parents=True)
        plt.savefig(f"{output_root}/{self.name}_sig_stats")
        print("end")


def main():
    parser = argparse.ArgumentParser("signal stats calculator")
    parser.add_argument(
        "signal_config",
        type=Path,
        help="configuration file of the signal to be analyzed",
    )
    parser.add_argument("-s", "--start", type=int, required=False, help="start date")
    parser.add_argument("-e", "--end", type=int, required=False, help="end date")
    parser.add_argument("-o", "--output", type=Path, required=True, help="output dir")
    parser.add_argument(
        "--ignore-missing",
        action="store_true",
        help="whether or not to skip missing data",
    )
    parser.add_argument(
        "--rel-sig-dir",
        type=Path,
        help="the directory at which the wl-sim command is executed when the signal is generated, used to extend the output_dir in the form of a relative address",
    )
    args = parser.parse_args()
    cfg = SingalCfg(args.signal_config, args.start, args.end, args.rel_sig_dir)
    cfg.run_stats(args.output, args.ignore_missing)


if __name__ == "__main__":
    main()

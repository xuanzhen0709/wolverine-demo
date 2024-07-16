import argparse
from pathlib import Path
from typing import List
import yaml
import pandas as pd

from cfi.wolverine.misc.sigreader import SignalReader
from cfi.wolverine.misc.calendar_utils import CalendarMgr


class SingalCfg:
    def __init__(self, infile: Path, instrument: str):
        print(f"loading {infile}")
        self.infile: Path = infile
        with open(infile) as fin:
            self.main_cfg = yaml.safe_load(fin)
        self.name: str = self.main_cfg["signal"]["name"]
        self.start: int = int(self.main_cfg["start"])
        self.end: int = int(self.main_cfg["end"])
        self.sigcfg = self.main_cfg["signal"]["config"]
        sigout_cfg = self.main_cfg["signal"]["output"]
        self.sigout_dir: Path = Path(sigout_cfg["config"]["output_dir"])
        self.instrument: str = instrument
        self.cal = CalendarMgr.get(instrument.partition(".")[2])

    def load_signal(self, date) -> pd.DataFrame:

        print(f"loading signal {date}")
        data_path: Path = (
            self.sigout_dir / self.name / str(date) / f"{self.name}-{date}.data.npy"
        )
        reader = SignalReader(data_path, instrument=self.instrument)
        sig_df = reader.read()

        return sig_df

    def make_cs_sigs(self, map_file: Path, output_root: Path, ignore_missing: bool):
        if len(self.sigcfg["targets"]) != 1:
            raise RuntimeError("multi target!")
        target = self.sigcfg["targets"][0]
        map_info: pd.DataFrame = pd.read_excel(map_file, header=None, sheet_name=target)
        date_list: List[int] = self.cal.get_days(self.start, self.end)
        for date in date_list:
            print(f"start gen date {date}")
            try:
                origin_sig_df = self.load_signal(date)
            except Exception as e:
                if ignore_missing:
                    print(f"[WARNING]:can't load signal data for {date}")
                    continue
                else:
                    raise FileNotFoundError(e)
            stock_file = f"/mnt/nas-3/ProcessedData/reference_data/{date}/uv.stocks.csv"
            stock_df = pd.read_csv(stock_file)
            total_stocks = stock_df["symbol"].tolist()
            new_sig_df = pd.DataFrame(
                columns=[["exchtime", "localtime"] + total_stocks]
            )
            new_sig_df["exchtime"] = origin_sig_df["exchtime"]
            new_sig_df["localtime"] = origin_sig_df["localtime"]
            for st in map_info[0]:
                if st in new_sig_df.columns:
                    new_sig_df[st] = origin_sig_df[target]

            output_dir = output_root.joinpath(f"cs_sig.{self.name}", str(date))
            output_dir.mkdir(parents=True, exist_ok=True)

            new_sig_df.to_csv(
                output_dir.joinpath(f"cs_sig.{self.name}-{date}.csv"), index=False
            )


def main():
    parser = argparse.ArgumentParser("signal converter")
    parser.add_argument(
        "signal_config",
        type=Path,
        help="configuration file of the signal to be analyzed",
    )
    parser.add_argument("-o", "--output", type=Path, required=True, help="output dir")
    parser.add_argument("-m", "--map-file", type=Path, required=True, help="output dir")
    parser.add_argument(
        "-i",
        "--instrument",
        type=str,
        default="stocks.CHN",
        help="instrument, such as stocks.CHN/cbonds.CHN/xxx.BIANUM/xxx.BIANCM/etc, used to determine trading calendar",
    )

    parser.add_argument(
        "--ignore-missing",
        action="store_true",
        help="whether or not to skip missing data",
    )

    args = parser.parse_args()
    cfg = SingalCfg(args.signal_config, args.instrument)
    output_root = args.output.resolve()
    cfg.make_cs_sigs(args.map_file, output_root, args.ignore_missing)


if __name__ == "__main__":
    main()

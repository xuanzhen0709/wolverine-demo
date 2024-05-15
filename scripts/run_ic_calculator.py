import argparse
import copy
from pathlib import Path
from typing import Dict, List
import platform
import subprocess
import yaml


class SignalCfg:
    REQUIRED_FIELDS: List[str] = ["ap", "bp"]

    def __init__(self, infile: Path, start: str, end: str):
        print(f"loading {infile}")
        self.infile: Path = infile
        with open(infile) as fin:
            self.main_cfg = yaml.safe_load(fin)
        self.name: str = self.main_cfg["signal"]["name"]
        self.start: int = int(start) if start else int(self.main_cfg["start"])
        self.end: int = int(end) if end else int(self.main_cfg["end"])
        self.sigcfg = self.main_cfg["signal"]["config"]
        sigout_cfg = self.main_cfg["signal"]["output"]
        self.sigout_dir: Path = Path(sigout_cfg["config"]["output_dir"])
        self.file_type: str = str(sigout_cfg["module"])

    def run(
        self,
        outdir_root: Path,
        future_biases: List[str],
        mode: str,
        map_file: Path,
        rel_sig_dir: Path,
    ):
        def __set_misc(cfg: Dict):
            cfg.pop("checkpoint", None)
            cfg.pop("worker", None)
            cfg["start"] = self.start
            cfg["end"] = self.end
            py_version = platform.python_version_tuple()
            cfg["env"][
                "python_runtime"
            ] = f"libpython{py_version[0]}.{py_version[1]}.so"

        def __set_calendar(cfg: Dict):
            if "calendar" not in cfg:
                cfg["calendar"] = "/mnt/nas-3/CTA/Data/ChinaTradingDates.txt"

        def __set_refdata(cfg: Dict):
            if "refdata" not in cfg:
                cfg["refdata"] = {}

        def __set_signal(cfg: Dict):
            sig_dir: Path = (
                self.sigout_dir
                if self.sigout_dir.is_absolute()
                else rel_sig_dir.joinpath(self.sigout_dir)
            )
            sigcfg = {
                "signame": self.name,
                "sigdir": str(sig_dir),
                "file_type": self.file_type,
                "output_dir": str(outdir),
                "futret_bias": future_biases,
                "mode": mode,
            }

            if None is map_file:
                sigcfg["targets"] = copy.deepcopy(self.sigcfg["targets"])
                cfg["marketdata"] = [
                    {
                        "module": "snapshot",
                        "symbols": copy.deepcopy(self.sigcfg["targets"]),
                        "config": {},
                        "client": [self.name],
                    }
                ]
            else:
                sigcfg["targets"] = ["dynamic:stocks"]
                sigcfg["marketdata"] = [
                    {
                        "module": "cs-snapshot",
                        "symbols": ["stocks"],
                        "config": {
                            # "data_dir": "/mnt/nas-3.old/ProcessedData/stock_snapshot_bin/binary_tick",
                            "fields": list(SignalCfg.REQUIRED_FIELDS),
                            "levels": 1,
                        },
                        "client": [self.name],
                    }
                ]
                sigcfg["stock_map"] = str(map_file)

            cfg["signal"] = {
                "name": "ic",
                "module": "nickchenyj.ic_calculator",
                "is_python": True,
                "config": sigcfg,
            }

        outdir: Path = (
            outdir_root.resolve()
            / f"""ic.{self.name}.{self.start}.{self.end}.{"-".join(future_biases)}"""
        )
        outdir.mkdir(parents=True, exist_ok=True)

        outcfg_file: Path = (
            outdir.resolve()
            / f"""ic.{self.name}.{self.start}.{self.end}.{"-".join(future_biases)}.yml"""
        )

        cfg = copy.deepcopy(self.main_cfg)

        __set_misc(cfg)
        __set_calendar(cfg)
        __set_refdata(cfg)
        __set_signal(cfg)

        with open(outcfg_file, "wt") as fout:
            print(f"dumping cfg file {outcfg_file}")
            yaml.safe_dump(cfg, fout, sort_keys=False)

        subprocess.run(["wl-sim", outcfg_file], check=True)


def main():
    parser = argparse.ArgumentParser("ic calculator")
    parser.add_argument(
        "signal_config",
        type=Path,
        help="configuration file of the signal to be analyzed",
    )
    parser.add_argument("-o", "--output", type=Path, required=True, help="output dir")
    parser.add_argument("--start", "-s", type=str, help="start date")
    parser.add_argument("--end", "-e", type=str, help="end date")
    parser.add_argument(
        "--future-bias",
        type=str,
        action="append",
        required=True,
        help="comma separated future biases, postfixes such as 's' 'm' and 'h' are supported",
    )
    parser.add_argument(
        "--mode",
        type=str,
        choices=["daily", "continuous"],
        default="continuous",
        help="calculation mode",
    )
    parser.add_argument(
        "--stock-map",
        type=Path,
        required=False,
        help="mapping files for futures signals and tickers",
    )
    parser.add_argument(
        "--rel-sig-dir",
        type=Path,
        required=False,
        help="the directory at which the wl-sim command is executed when the signal is generated, used to extend the output_dir in the form of a relative address",
    )
    args = parser.parse_args()

    if args.stock_map:
        if len(args.future_bias) > 1:
            raise RuntimeError("fut2stock mode only support one futret_bias")
        if args.mode != "daily":
            raise RuntimeError("fut2stock mode only support daily")

    cfg = SignalCfg(args.signal_config, args.start, args.end)
    cfg.run(args.output, args.future_bias, args.mode, args.stock_map, args.rel_sig_dir)


if __name__ == "__main__":
    main()

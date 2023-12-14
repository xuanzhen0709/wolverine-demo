import argparse
import copy
from pathlib import Path
from typing import List, Dict
import platform
import subprocess
import yaml
from enum import IntEnum
from run_cs_pnl_stats import run_pnl_stats
from run_sig_stats import SingalCfg as StatsSingalCfg


class ExecPriceType(IntEnum):
    mid = 0
    close = 1
    vwap = 2


class SingalCfg:
    REQUIRED_FIELDS: List[str] = ["last_price"]
    MID_REQUIRED_FIELDS: List[str] = ["ap", "bp"]
    VWAP_REQUIRED_FIELDS: List[str] = ["turnover", "volume"]

    def __init__(
        self, infile: Path, out_name: str, start: str, end: str, use_system_uv: bool
    ):
        print(f"loading {infile}")
        self.infile: Path = infile
        with open(infile) as fin:
            self.main_cfg = yaml.safe_load(fin)
        self.in_name: str = self.main_cfg["signal"]["name"]
        self.out_name: str = str(out_name) if out_name else self.in_name
        self.start: int = int(start) if start else int(self.main_cfg["start"])
        self.end: int = int(end) if end else int(self.main_cfg["end"])
        self.sigcfg = self.main_cfg["signal"]["config"]
        py_version = platform.python_version_tuple()
        self.pylib: str = f"libpython{py_version[0]}.{py_version[1]}.so"
        module: str = self.main_cfg["signal"]["module"]
        if module == "py":
            self.pylib = self.sigcfg["pylib"]
            self.sigcfg = self.sigcfg["config"]

        sigout_cfg = self.main_cfg["signal"]["output"]
        self.sig_dir: Path = Path(sigout_cfg["config"]["output_dir"])
        self.file_type: str = str(sigout_cfg["module"])
        self.use_system_uv: bool = use_system_uv

    def run(
        self,
        outdir_root: Path,
        exec_price: List[str],
        mode: str,
        rel_sig_dir: Path,
    ):
        def __set_misc(cfg: Dict):
            cfg.pop("checkpoint", None)
            cfg.pop("worker", None)
            cfg["start"] = self.start
            cfg["end"] = self.end

        def __set_calendar(cfg: Dict):
            if "calendar" not in cfg:
                cfg["calendar"] = "/mnt/nas-3/CTA/Data/ChinaTradingDates.txt"

        def __set_refdata(cfg: Dict):
            if "refdata" not in cfg:
                cfg["refdata"] = {}

        def __set_signal(cfg: Dict):
            sig_dir: Path = (
                self.sig_dir
                if self.sig_dir.is_absolute()
                else rel_sig_dir.joinpath(self.sig_dir)
            )

            price_type = set()
            for price_str in exec_price:
                type_str = price_str.split("_")[0]
                try:
                    pt = ExecPriceType[type_str]
                except BaseException:
                    raise RuntimeError(
                        f"unknown PnL type {type_str}, only support:{[i.name for i in ExecPriceType]}"
                    )
                price_type.add(pt)
            fields = SingalCfg.REQUIRED_FIELDS
            if ExecPriceType.mid in price_type:
                fields.extend(SingalCfg.MID_REQUIRED_FIELDS)
            if ExecPriceType.vwap in price_type:
                fields.extend(SingalCfg.VWAP_REQUIRED_FIELDS)
            sigcfg = {
                "mode": mode,
                "signame": self.out_name,
                "sigdir": str(sig_dir),
                "file_type": self.file_type,
                "output_dir": str(self.outdir),
                "exec_price": exec_price,
            }
            sigcfg["targets"] = ["dynamic:stocks"]
            sigcfg["marketdata"] = [
                {
                    "module": "cs-snapshot",
                    "symbols": ["stocks"],
                    "config": {
                        "data_dir": "/mnt/nas-3.old/ProcessedData/stock_snapshot_bin/binary_tick",
                        "fields": fields,
                        "levels": 1,
                    },
                }
            ]

            cfg["signal"] = {
                "name": "pnl",
                "module": "py",
                "config": {
                    "module": "nickchenyj.cs_pnl_calculator",
                    "pylib": self.pylib,
                    "config": sigcfg,
                },
            }

        self.outdir = (
            outdir_root.resolve()
            / f"""cs_pnl.{self.out_name}.{self.start}.{self.end}.{"-".join(exec_price)}"""
        )
        self.outdir.mkdir(parents=True, exist_ok=True)

        outcfg_file: Path = (
            self.outdir.resolve()
            / f"""cs_pnl.{self.out_name}.{self.start}.{self.end}.{"-".join(exec_price)}.yml"""
        )

        cfg = copy.deepcopy(self.main_cfg)
        __set_misc(cfg)
        __set_calendar(cfg)
        __set_refdata(cfg)
        __set_signal(cfg)

        with open(outcfg_file, "wt") as fout:
            print(f"dumping cfg file {outcfg_file}")
            yaml.safe_dump(cfg, fout, sort_keys=False)

        return subprocess.run(["wl-sim", outcfg_file], check=True)

    def run_stats(self, infile: Path, start: int, end: int, rel_sig_dir: Path):
        output = self.outdir.joinpath(f"{self.out_name}_stats")
        run_pnl_stats(self.outdir, None, None, output)
        stats_cfg = StatsSingalCfg(infile, start, end, rel_sig_dir)
        stats_cfg.run_stats(self.outdir, True)


def main():
    parser = argparse.ArgumentParser("cs pnl calculator")
    parser.add_argument(
        "signal_config",
        type=Path,
        help="configuration file of the signal to be analyzed",
    )
    parser.add_argument("--name", "-n", type=str, help="name override")
    parser.add_argument(
        "--use-system-uv", action="store_true", help="use system uv instead of input uv"
    )
    parser.add_argument("-o", "--output", type=Path, required=True, help="output dir")
    parser.add_argument(
        "-p",
        "--exec-price",
        type=str,
        action="append",
        required=True,
        help="exercise price, the format is {type}_{delay}. Support {len(ExecPriceType)} types: {[i.name for i in ExecPriceType]}; delay is suffixed with 'ns' 's' 'm' and 'h'.",
    )
    parser.add_argument("-s", "--start", type=str, help="start date")
    parser.add_argument("-e", "--end", type=str, help="end date")
    parser.add_argument(
        "--mode",
        type=str,
        choices=["daily", "continuous"],
        default="daily",
        help="calculation mode",
    )
    parser.add_argument(
        "--rel-sig-dir",
        type=Path,
        help="the directory at which the wl-sim command is executed when the signal is generated, used to extend the output_dir in the form of a relative address",
    )

    parser.add_argument(
        "--run-stats",
        action="store_true",
        help="whether to display statistics on signals and PnL",
    )

    args = parser.parse_args()
    cfg = SingalCfg(
        args.signal_config, args.name, args.start, args.end, args.use_system_uv
    )
    wlsim_ret = cfg.run(args.output, args.exec_price, args.mode, args.rel_sig_dir)
    if 0 == wlsim_ret.returncode and args.run_stats:
        cfg.run_stats(args.signal_config, args.start, args.end, args.rel_sig_dir)


if __name__ == "__main__":
    main()

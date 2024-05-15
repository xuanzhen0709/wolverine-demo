import argparse
import copy
from pathlib import Path
from typing import Dict, List
import platform
import subprocess
import yaml


class SignalCfg:
    REQUIRED_FIELDS: List[str] = ["ap", "bp"]

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
        sigout_cfg = self.main_cfg["signal"]["output"]
        self.sig_dir: Path = Path(sigout_cfg["config"]["output_dir"])
        self.file_type: str = str(sigout_cfg["module"])
        self.use_system_uv: bool = use_system_uv

    def run(
        self,
        outdir_root: Path,
        future_bias: List[str],
        ffill_interval: str,
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
                self.sig_dir
                if self.sig_dir.is_absolute()
                else rel_sig_dir.joinpath(self.sig_dir)
            )

            sigcfg = {
                "targets": copy.deepcopy(self.sigcfg["targets"]),
                "signame": self.in_name,
                "sigdir": str(sig_dir),
                "file_type": self.file_type,
                "output_dir": str(outdir),
                "outname": self.out_name,
                "futret_bias": future_bias,
                "use_system_uv": self.use_system_uv,
            }
            if ffill_interval:
                sigcfg["ffill_interval"] = ffill_interval
            cfg["signal"] = {
                "name": self.out_name,
                "module": "nickchenyj.cs_ic_calculator",
                "is_python": True,
                "config": sigcfg,
            }

        def __set_marketdata(cfg: Dict):
            mktdata = [x for x in cfg["marketdata"] if x["module"] == "cs-snapshot"]
            cfg["marketdata"] = mktdata
            for x in cfg["marketdata"]:
                x["config"]["fields"] = list(SignalCfg.REQUIRED_FIELDS)
                x["config"]["levels"] = 1
                x["client"] = [self.out_name]

        outdir: Path = (
            outdir_root.resolve()
            / f"""cs_ic.{self.out_name}.{self.start}.{self.end}.{"_".join(future_bias)}"""
        )
        outdir.mkdir(parents=True, exist_ok=True)

        outcfg_file: Path = (
            outdir.resolve()
            / f"""cs_ic.{self.out_name}.{self.start}.{self.end}.{"_".join(future_bias)}.yml"""
        )

        cfg = copy.deepcopy(self.main_cfg)

        __set_misc(cfg)
        __set_calendar(cfg)
        __set_refdata(cfg)
        __set_signal(cfg)
        __set_marketdata(cfg)

        with open(outcfg_file, "wt") as fout:
            print(f"dumping cfg file {outcfg_file}")
            yaml.safe_dump(cfg, fout, sort_keys=False)

        subprocess.run(["wl-sim", outcfg_file], check=True)


def main():
    parser = argparse.ArgumentParser("cross-sectional ic calculator")
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
        "--ffill-interval",
        type=str,
        required=False,
        help="signal forward fill interval, postfixes such as 's' 'm' and 'h' are supported",
    )
    parser.add_argument(
        "--rel-sig-dir",
        type=Path,
        required=False,
        help="the directory at which the wl-sim command is executed when the signal is generated, used to extend the output_dir in the form of a relative address",
    )

    args = parser.parse_args()
    cfg = SignalCfg(
        args.signal_config, args.name, args.start, args.end, args.use_system_uv
    )
    cfg.run(args.output, args.future_bias, args.ffill_interval, args.rel_sig_dir)


if __name__ == "__main__":
    main()

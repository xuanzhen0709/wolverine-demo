import argparse
import copy
from pathlib import Path
from typing import List, Dict
import subprocess
import yaml
from run_edge_plot import *


class SignalCfg:
    REQUIRED_FIELDS: List[str] = ["ap", "bp"]

    def __init__(self, infile: Path, out_name: str, start: str, end: str):
        print(f"loading {infile}")
        self.infile: Path = infile
        with open(infile) as fin:
            self.main_cfg = yaml.safe_load(fin)
        self.in_name: str = self.main_cfg["signal"]["name"]
        self.out_name: str = out_name if out_name else self.in_name
        self.start: int = int(start) if start else int(self.main_cfg["start"])
        self.end: int = int(end) if end else int(self.main_cfg["end"])
        self.sigcfg = self.main_cfg["signal"]["config"]
        sigout_cfg = self.main_cfg["signal"]["output"]
        self.sig_dir: Path = Path(sigout_cfg["config"]["output_dir"])
        self.sigout_dir: Path = Path(sigout_cfg["config"]["output_dir"])
        self.file_type: str = str(sigout_cfg["module"])

    def run(
        self,
        outdir_root: Path,
        quantile: float,
        future_bias: str,
        rel_sig_dir: Path,
    ) -> subprocess.CompletedProcess:
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

            fb_start, fb_end, fb_step = future_bias.split(":")
            sigcfg = {
                "targets": copy.deepcopy(self.sigcfg["targets"]),
                "sigdir": str(sig_dir),
                "signame": self.in_name,
                "file_type": self.file_type,
                "output_dir": str(self.outdir),
                "outname": self.out_name,
                "quantile": quantile,
                "futret_bias_start": fb_start,
                "futret_bias_end": fb_end,
                "futret_bias_step": fb_step,
            }

            cfg["signal"] = {
                "name": self.out_name,
                "module": "tools.edge_calculator",
                "is_python": True,
                "config": sigcfg,
            }

        def __set_marketdata(cfg: Dict):
            mktdata = [x for x in cfg["marketdata"] if x["module"] == "snapshot"]
            cfg["marketdata"] = mktdata
            for x in cfg["marketdata"]:
                x["config"]["fields"] = list(SignalCfg.REQUIRED_FIELDS)
                x["config"]["levels"] = 1
                x["client"] = [self.out_name]

        quantile = quantile if quantile else 0.1
        self.outdir: Path = (
            outdir_root.resolve()
            / f"edge.{self.out_name}.{self.start}.{self.end}.{quantile*100}%"
        )
        self.outdir.mkdir(parents=True, exist_ok=True)

        outcfg_file: Path = (
            self.outdir.resolve()
            / f"edge.{self.out_name}.{self.start}.{self.end}.{quantile*100}%.yml"
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

        return subprocess.run(["wl-sim", outcfg_file], check=True)

    def draw_figure(self):
        edge_info = check_data(self.outdir)
        edge_plot(edge_info, self.outdir)


def main():
    parser = argparse.ArgumentParser("edge calculator")
    parser.add_argument(
        "signal_config",
        type=Path,
        help="configuration file of the signal to be analyzed",
    )
    parser.add_argument("--name", "-n", type=str, help="name override")
    parser.add_argument("-o", "--output", type=Path, required=True, help="output dir")
    parser.add_argument("--start", "-s", type=str, help="start date")
    parser.add_argument("--end", "-e", type=str, help="end date")
    parser.add_argument(
        "--rel-sig-dir",
        type=Path,
        required=False,
        help="the directory at which the wl-sim command is executed when the signal is generated, used to extend the output_dir in the form of a relative address",
    )
    parser.add_argument(
        "--quantile",
        type=float,
        help="proportion of extreme values, the range is: (0,1], default: 0.1",
    )
    parser.add_argument(
        "--future-bias",
        type=str,
        required=True,
        help="future bias in the form of start:end:step, each of the segment may have postfixes such as 's' 'm' and 'h' are supported",
    )
    parser.add_argument(
        "--draw-figure", action="store_true", help="whether or not draw figure"
    )

    args = parser.parse_args()

    cfg = SignalCfg(args.signal_config, args.name, args.start, args.end)
    wlsim_ret = cfg.run(
        args.output,
        args.quantile,
        args.future_bias,
        args.rel_sig_dir,
    )
    if 0 == wlsim_ret.returncode and args.draw_figure:
        cfg.draw_figure()


if __name__ == "__main__":
    main()

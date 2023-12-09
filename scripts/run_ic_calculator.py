import argparse
import copy
from pathlib import Path
from typing import List
import platform
import subprocess
import yaml


class SignalCfg:
    REQUIRED_FIELDS: List[str] = ["ap", "bp"]

    def __init__(self, infile: Path):
        print(f"loading {infile}")
        self.infile: Path = infile
        with open(infile) as fin:
            self.main_cfg = yaml.safe_load(fin)
        self.name: str = self.main_cfg["signal"]["name"]
        self.start: int = int(self.main_cfg["start"])
        self.end: int = int(self.main_cfg["end"])
        self.sigcfg = self.main_cfg["signal"]["config"]
        py_version = platform.python_version_tuple()
        self.python_runtime: str = f"libpython{py_version[0]}.{py_version[1]}.so"
        module: str = self.main_cfg["signal"]["module"]
        if module == "py":
            self.python_runtime = self.main_cfg["env"]["python_runtime"]
            self.sigcfg = self.sigcfg["config"]

        sigout_cfg = self.main_cfg["signal"]["output"]
        self.sigout_dir: Path = Path(sigout_cfg["config"]["output_dir"])
        self.file_type: str = str(sigout_cfg["module"])

    def run(self, outdir_root: Path,
            future_biases: List[str], mode: str, map_file: Path):
        fut_bias_str: str = "-".join(future_biases)
        outdir: Path = outdir_root.resolve(
        ) / f"ic.{self.name}.{self.start}.{self.end}.{fut_bias_str}"
        outdir.mkdir(parents=True, exist_ok=True)

        outcfg_file: Path = outdir_root.resolve(
        ) / f"ic.{self.name}.{self.start}.{self.end}.{fut_bias_str}.yml"

        cfg = copy.deepcopy(self.main_cfg)
        cfg.pop("checkpoint", None)
        cfg.pop("worker", None)
        cfg["refdata"]["config"] = {
            # "data_dir": "/mnt/nas-3.old/ProcessedData/reference_data"
        }


        sigcfg = {
            "signame": self.name,
            "sigdir": str(self.sigout_dir),
            "file_type": self.file_type,
            "output_dir": str(outdir),
            "futret_bias": future_biases,
            "mode": mode
        }

        if None is map_file:
            sigcfg["targets"] = copy.deepcopy(self.sigcfg["targets"])
            sigcfg["marketdata"] = [{
                "module": "snapshot",
                "symbols": copy.deepcopy(self.sigcfg["targets"]),
                "config": {
                    # "data_dir": "/mnt/nas-3.old/ProcessedData/snapshot_bin",
                    "fields": list(SignalCfg.REQUIRED_FIELDS),
                    "levels": 1
                }
            }]
        else:
            sigcfg["targets"] = ["dynamic:stocks"]
            sigcfg["marketdata"] = [{
                "module": "cs-snapshot",
                "symbols": ["stocks"],
                "config": {
                    # "data_dir": "/mnt/nas-3.old/ProcessedData/stock_snapshot_bin/binary_tick",
                    "fields": list(SignalCfg.REQUIRED_FIELDS),
                    "levels": 1
                }
            }]
            sigcfg["stock_map"] = str(map_file)

        cfg["env"]["python_runtime"] = self.python_runtime
        cfg["signal"] = {
            "name": "ic",
            "module": "py",
            "config": {
                "module": "nickchenyj.ic_calculator",
                "config": sigcfg,
            },
        }
        with open(outcfg_file, "wt") as fout:
            print(f"dumping cfg file {outcfg_file}")
            yaml.safe_dump(cfg, fout, sort_keys=False)

        subprocess.run(["wl-sim", outcfg_file], check=True)


def main():
    parser = argparse.ArgumentParser("ic calculator")
    parser.add_argument("signal_config",
                        type=Path,
                        help="configuration file of the signal to be analyzed")
    parser.add_argument("-o",
                        "--output",
                        type=Path,
                        required=True,
                        help="output dir")
    parser.add_argument(
        "--future-bias",
        type=str,
        action="append",
        required=True,
        help="comma separated future biases, postfixes such as 's' 'm' and 'h' are supported"
    )
    parser.add_argument("--mode",
                        type=str,
                        choices=["daily", "continuous"],
                        default="continuous",
                        help="calculation mode")

    parser.add_argument("--stock-map",
                        type=Path,
                        required=False,
                        help="mapping files for futures signals and tickers")

    args = parser.parse_args()

    if args.stock_map:
        if len(args.future_bias) > 1:
            raise RuntimeError("fut2stock mode only support one futret_bias")
        if args.mode != 'daily':
            raise RuntimeError("fut2stock mode only support daily")

    cfg = SignalCfg(args.signal_config)
    cfg.run(args.output, args.future_bias, args.mode, args.stock_map)


if __name__ == "__main__":
    main()

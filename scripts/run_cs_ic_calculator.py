import argparse
import copy
from pathlib import Path
from typing import List
import platform
import subprocess
import yaml


class SingalCfg:
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
        self.pylib: str = f"libpython{py_version[0]}.{py_version[1]}.so"
        module: str = self.main_cfg["signal"]["module"]
        if module == "py":
            self.pylib = self.sigcfg["pylib"]
            self.sigcfg = self.sigcfg["config"]

        sigout_cfg = self.main_cfg["signal"]["output"]
        self.sigout_dir: Path = Path(sigout_cfg["config"]["output_dir"])
        self.file_type: str = str(sigout_cfg["module"])

    def run(self, outdir_root: Path, future_bias: str, ffill_interval: str):
        outdir: Path = outdir_root.resolve(
        ) / f"cs_ic.{self.name}.{self.start}.{self.end}.{future_bias}"
        outdir.mkdir(parents=True, exist_ok=True)

        outcfg_file: Path = outdir_root.resolve(
        ) / f"cs_ic.{self.name}.{self.start}.{self.end}.{future_bias}.yml"

        cfg = copy.deepcopy(self.main_cfg)
        cfg.pop("checkpoint", None)

        sigcfg = {
            "targets": copy.deepcopy(self.sigcfg["targets"]),
            "marketdata": copy.deepcopy(self.sigcfg["marketdata"]),
            "signame": self.name,
            "sigdir": str(self.sigout_dir),
            "file_type": self.file_type,
            "output_dir": str(outdir),
            "futret_bias": future_bias,
        }
        if ffill_interval:
            sigcfg["ffill_interval"] = ffill_interval

        cfg["signal"] = {
            "name": "ic",
            "module": "py",
            "config": {
                "module": "nickchenyj.cs_ic_calculator",
                "pylib": self.pylib,
                "config": sigcfg,
            },
        }

        for x in cfg["signal"]["config"]["config"]["marketdata"]:
            if x["module"] != "cs-snapshot":
                continue
            x["config"]["fields"] = list(SingalCfg.REQUIRED_FIELDS)
            x["config"]["levels"] = 1

        with open(outcfg_file, "wt") as fout:
            print(f"dumping cfg file {outcfg_file}")
            yaml.safe_dump(cfg, fout, sort_keys=False)

        subprocess.run(["wl-sim", outcfg_file], check=True)


def main():
    parser = argparse.ArgumentParser("ic calculator")
    parser.add_argument("signal_config", type=Path,
                        help="configuration file of the signal to be analyzed")
    parser.add_argument("-o", "--output", type=Path,
                        required=True, help="output dir")
    parser.add_argument("--future-bias", type=str, required=True,
                        help="comma separated future biases, postfixes such as 's' 'm' and 'h' are supported")
    parser.add_argument("--ffill-interval", type=str, required=False,
                        help="signal forward fill interval, postfixes such as 's' 'm' and 'h' are supported")

    args = parser.parse_args()
    cfg = SingalCfg(args.signal_config)
    cfg.run(args.output, args.future_bias, args.ffill_interval)


if __name__ == "__main__":
    main()

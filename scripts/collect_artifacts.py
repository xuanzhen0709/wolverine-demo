import argparse
import shutil
import glob
import re
from pathlib import Path
from typing import Dict, List
import yaml


class Module:

    def __init__(self, cfg):
        self.is_py: bool = cfg["module"] == "py"
        if self.is_py:
            cfg = cfg["config"]
        self.name: str = cfg["module"]

    def canonical_name(self) -> str:
        if self.is_py:
            return f"wlpysig.{self.name}"
        return f"wlsig.{self.name}"

    def regex(self) -> re.Pattern:
        if self.is_py:
            filename: str = self.canonical_name().replace(".", "_")
            return re.compile(f"{filename}-.*.whl")
        filename: str = self.canonical_name().replace(".", "-")
        return re.compile(f"{filename}.so")

    def __repr__(self) -> str:
        return self.canonical_name()


class Collector:

    def __init__(self, config_file: Path):
        with open(config_file) as fin:
            cfg = yaml.safe_load(fin)

        signal = cfg["signal"]
        factors_cfg = signal.get("factors", [])

        self.main: Module = Module(signal)
        factors: List[Module] = [Module(_x) for _x in factors_cfg]
        factors_dict: Dict[str, Module] = {
            _x.canonical_name(): _x
            for _x in factors
        }
        self.factors: List = list(factors_dict.values())
        print(self.main)
        print(self.factors)
        print(f"found signal {self.main} and {len(self.factors)} factors")

    def collect(self, outdir: Path, build_dir: Path):
        outdir.mkdir(parents=True, exist_ok=True)
        c_modules = [_x for _x in self.factors if not _x.is_py]
        py_modules = [_x for _x in self.factors if _x.is_py]
        if self.main.is_py:
            py_modules.append(self.main)
        else:
            c_modules.append(self.main)

        search_params = [{
            "modules": c_modules,
            "pattern": "**/*.so",
        }, {
            "modules": py_modules,
            "pattern": "**/*.whl",
        }]

        count: int = 0
        for params in search_params:
            modules = params["modules"]
            if modules:
                regexes = {_x: _x.regex() for _x in modules}
                for _fpath in glob.glob(str(build_dir / params["pattern"]),
                                        recursive=True):
                    _fpath = Path(_fpath)
                    for _mod, _regex in regexes.items():
                        if _regex.match(_fpath.name):
                            print(f"{_mod.name}: found artifact {_fpath}")
                            self.__collect_item(outdir, _mod, _fpath)
                            regexes.pop(_mod)
                            count += 1
                            break
                if regexes:
                    raise RuntimeError(
                        f"missing modules {list(regexes.keys())}")
        print(f"collected {count} artifacts")

    def __collect_item(self, outdir: Path, module: Module, modfile: Path):
        if module == self.main:
            mod_dir: Path = outdir / "signals" / module.name
        else:
            mod_dir: Path = outdir / "factors" / module.name
        mod_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy2(modfile, mod_dir)


def main():
    parser = argparse.ArgumentParser("collect factors")
    parser.add_argument("-b",
                        "--build-dir",
                        type=Path,
                        required=True,
                        help="build directories")
    parser.add_argument("-c",
                        "--config",
                        type=Path,
                        required=True,
                        help="strategy/signal main config file")
    parser.add_argument("-o",
                        "--outdir",
                        type=Path,
                        required=True,
                        help="output dir")
    args = parser.parse_args()
    collector = Collector(args.config)
    collector.collect(args.outdir, args.build_dir)


if __name__ == "__main__":
    main()

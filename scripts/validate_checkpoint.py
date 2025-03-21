import argparse
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List

from cfi.wolverine.misc.calendar_utils import CalendarMgr
from cfi.wolverine.misc.wlsim_cfg import WlsimCfg


@dataclass
class TestCase:
    name: str
    start: int
    end: int
    checkpoint: Dict
    cfgpath: Path = Path("")
    logpath: Path = Path("")


class CheckpointValidator:

    def __init__(self, cfgfile: Path, date: str, cwd: Path):
        self.cfg: WlsimCfg = WlsimCfg(cfgfile)
        cal = CalendarMgr.get()
        self.date: int = cal.shift(date, 0)
        self.tmr: int = cal.shift(self.date, 1)
        self.cwd: Path = Path(cwd).resolve()
        self.name: str = self.cfg.get("/signal/name")[0].unwrapped_node
        self.cfg_dir: Path = cwd / "cfg"
        self.chkpnt_dir: Path = cwd / "checkpoint"
        self.out_dir: Path = cwd / "output"
        self.log_dir: Path = cwd / "log"

        self.cfg_dir.mkdir(parents=True, exist_ok=True)
        self.log_dir.mkdir(parents=True, exist_ok=True)

        self.test_cases: List = [
            TestCase(
                "save",
                self.date,
                self.date,
                {
                    "dir": str(self.chkpnt_dir),
                    "save": [
                        self.date,
                    ],
                },
            ),
            TestCase(
                "load",
                self.tmr,
                self.tmr,
                {
                    "dir": str(self.chkpnt_dir),
                    "load": [
                        self.date,
                    ],
                },
            ),
            TestCase(
                "full",
                self.date,
                self.tmr,
                {},
            ),
        ]

    def run(self):
        for _case in self.test_cases:
            self.generate_test_cfg(_case)

        for _case in self.test_cases:
            self.run_test(_case)

    def generate_test_cfg(self, case: TestCase):
        outdir: Path = self.out_dir / case.name
        savecfg: Path = self.cfg_dir / f"{case.name}.yml"
        print(f"generating test cfg for {case.name},{savecfg}")

        self.cfg.set("/start", case.start, mustexist=True)
        self.cfg.set("/end", case.end, mustexist=True)
        self.cfg.set("/env/print_exec_time", True, mustexist=False)
        self.cfg.delete("/checkpoint", mustexist=False)
        if case.checkpoint:
            self.cfg.set("/checkpoint", case.checkpoint, mustexist=False)

        self.cfg.set("/signal/output/config/output_dir", str(outdir), mustexist=False)
        self.cfg.set("/signal/output/module", "npy", mustexist=True)

        self.cfg.dump(savecfg)
        case.cfgpath = savecfg
        case.logpath = self.log_dir / f"{case.name}.log"
        return savecfg

    def run_test(self, case: TestCase):
        print(f"running test for {case.name},{case.cfgpath}")
        subprocess.run(
            f"wl-sim {case.cfgpath} 2>&1 | tee {case.logpath}",
            shell=True,
            check=True,
        )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("config", type=Path, help="template yaml file")
    parser.add_argument("date", type=str, help="date")
    parser.add_argument(
        "-w",
        "--workdir",
        type=Path,
        default=Path(__file__).parent,
        help="working directory",
    )

    args = parser.parse_args()

    validator = CheckpointValidator(args.config, args.date, args.workdir)
    validator.run()


if __name__ == "__main__":
    main()
